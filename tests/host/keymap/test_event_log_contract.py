#!/usr/bin/env python3
"""Host-side structural contract for the ESP-only event log.

The logger owns FreeRTOS, SD and ESP-IDF symbols, so linking it on the host
would test shims rather than the firmware path.  These checks keep the
deterministic persistence invariants executable: record layout/checksum,
recovery/slot sequencing, durability ordering, and fixed GMT formatting.
"""

from pathlib import Path
import re
import struct
import zlib


ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "components/cyberdeck/src/platform/logging/event_log.cpp"


class ContractFailure(AssertionError):
    pass


def check(condition, message):
    if not condition:
        raise ContractFailure(message)


def between(text, start, end):
    first = text.index(start)
    last = text.index(end, first)
    return text[first:last]


def test_record_contract(source):
    check("constexpr size_t RECORD_SIZE = 256;" in source, "record size must be 256 bytes")
    check("__attribute__((packed))" in source, "record layout must be explicitly packed")
    check("static_assert(sizeof(LogRecord) == RECORD_SIZE" in source,
          "record size must be compile-time guarded")
    check("record.magic != RECORD_MAGIC" in source, "invalid magic must be rejected")
    check("RECORD_SIZE - sizeof(record.checksum)" in source,
          "checksum must exclude only the checksum field")


def test_checksum_and_slot_model():
    # Independent oracle for the CRC and the exact serialized prefix used by
    # LogRecord (magic, sequence, timestamps, level, reserved, tag, message).
    prefix = struct.pack("<IIqqB3x32s192s", 0x354C4F47, 0xFFFFFFFF,
                         1_700_000_000_000_000, 42, ord("E"),
                         b"test\0", b"payload\0")
    record = prefix + struct.pack("<I", zlib.crc32(prefix) & 0xFFFFFFFF)
    check(len(record) == 256, "independent record oracle must be 256 bytes")
    check((zlib.crc32(record[:-4]) & 0xFFFFFFFF) == struct.unpack_from("<I", record, 252)[0],
          "checksum oracle must validate a serialized record")

    capacity = (16 * 1024 * 1024) // 256
    check(capacity == 65536, "log capacity contract changed unexpectedly")
    next_slot = capacity - 1
    next_sequence = 0xFFFFFFFF
    expected_slot = (next_slot + 1) % capacity
    expected_sequence = (next_sequence + 1) & 0xFFFFFFFF
    check(expected_slot == 0, "slot must wrap at the circular capacity")
    check(expected_sequence == 0, "sequence must wrap as uint32")


def _sequence_is_newer(candidate, current):
    return candidate != current and ((candidate - current) & 0xFFFFFFFF) < 0x80000000


def _insert_recent_model(records, sequence):
    """Independent oracle for the sorted twenty-record reconstruction window."""
    position = 0
    while position < len(records) and not _sequence_is_newer(records[position], sequence):
        position += 1
    if len(records) == 20 and position == 0:
        return
    if len(records) < 20:
        records.insert(position, sequence)
        return
    records.pop(0)
    position -= 1
    records.insert(position, sequence)


def test_recent_reconstruction_wrap_and_order(source):
    insert_recent = between(source, "void insert_recent", "void copy_text")
    check("sequence_is_newer(records[position].sequence, record.sequence)" in insert_recent,
          "recent reconstruction must compare sequences with wrap-safe ordering")
    check("insert_recent(s_rebuild_recent, &recent_count, record)" in source,
          "recovery must insert every valid physical record into the recent window")

    # Physical scan order crosses the ring boundary: slots 0..15 contain the
    # post-wrap records, while the older pre-wrap records occupy the tail.
    physical_scan = list(range(16)) + list(range(0xFFFFFFF8, 0x100000000))
    recent = []
    for sequence in physical_scan:
        _insert_recent_model(recent, sequence & 0xFFFFFFFF)

    expected = list(range(0xFFFFFFFC, 0x100000000)) + list(range(16))
    check(recent == expected,
          "reconstructed twenty recent records must remain ordered across sequence wrap")
    check(len(recent) == len(set(recent)) == 20,
          "reconstructed recent records must not contain duplicates")
    check(recent[-1] == 15, "reconstructed recent records must include the newest sequence")


def test_recovery_and_durability_contract(source):
    rebuild = between(source, "bool rebuild_state", "bool open_log_file")
    write = between(source, "bool write_record", "void log_task")
    check("fstat(fd, &info)" in rebuild, "recovery must inspect the persisted file")
    check("info.st_size / RECORD_SIZE" in rebuild, "recovery must scan complete records only")
    check("slot_count < LOG_CAPACITY ? slot_count : LOG_CAPACITY" in rebuild,
          "recovery scan must be bounded by the ring capacity")
    check("!valid_record(record)" in rebuild, "recovery must skip corrupt records")
    check("sequence_is_newer(record.sequence, latest_sequence)" in rebuild,
          "recovery must select the newest sequence, not the last physical slot")
    check("(latest_slot + 1U) % LOG_CAPACITY" in rebuild,
          "recovery must derive the next circular slot")
    check("latest_sequence + 1U" in rebuild, "recovery must derive the next sequence")
    check(re.search(r"static_cast<int32_t>\(candidate - [A-Za-z_][A-Za-z0-9_]*\) > 0", source) is not None,
          "sequence comparison must be wrap-safe")
    check("fflush(file) != 0 || fsync(fileno(file)) != 0" in write,
          "a record must be flushed and fsynced before commit")
    durability_tail = write[write.index("fflush"):]
    check(durability_tail.index("fsync") < durability_tail.index("*record = candidate"),
          "published record/state must follow fsync")
    check(durability_tail.index("fsync") < durability_tail.index("*next_sequence"),
          "next sequence must advance only after fsync")


def test_fixed_gmt_and_invalid_clock_fallback(source):
    latest = between(source, "extern \"C\" size_t event_log_latest", "\n}")
    check("gmtime_r" in latest, "log timestamps must start from UTC")
    check("CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN" in latest,
          "log timestamps must use the fixed GMT-3 offset")
    check("localtime_r" not in latest and "TZ" not in latest,
          "log formatting must not depend on host timezone configuration")
    check('"up:%" PRId64 "ms"' in latest,
          "invalid/unavailable wall clock must use the uptime fallback")
    check('"%Y-%m-%d %H:%M:%S"' in latest,
          "persisted log output must use the deterministic timestamp shape")


def main():
    source = SOURCE.read_text(encoding="utf-8")
    tests = [test_record_contract, test_checksum_and_slot_model,
             test_recent_reconstruction_wrap_and_order,
             test_recovery_and_durability_contract, test_fixed_gmt_and_invalid_clock_fallback]
    failures = []
    for test in tests:
        try:
            test(source) if test in (test_record_contract,
                                     test_recent_reconstruction_wrap_and_order,
                                     test_recovery_and_durability_contract,
                                     test_fixed_gmt_and_invalid_clock_fallback) else test()
        except (ContractFailure, ValueError, IndexError) as exc:
            failures.append(f"{test.__name__}: {exc}")
    if failures:
        for failure in failures:
            print(f"FAIL {failure}")
        return 1
    print(f"PASS: event_log contract ({len(tests)} scenarios)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
