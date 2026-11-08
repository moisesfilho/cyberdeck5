#!/usr/bin/env python3
"""Structural contract for the ID3v2 1 MiB streaming envelope.

REQ-MUSIC-META-06 / AC-MUSIC-META-23..30 (TEST-MUSIC-META-ID3-CONTRACT).

Behavioral coverage lives in `test_music_model.cpp`, `test_music_metadata_io.cpp`,
`test_music_player.cpp` and `test_music_app.cpp`. Those tests derive their
expectations from the production constants, so a structural regression (a
smaller ceiling, a proportional allocation, a re-opened descriptor, a missing
Makefile prerequisite) can keep them green. This contract locks the invariants
that the tests cannot derive from themselves:

- the ceiling, header size and read window are single exported constants and the
  ceiling is exactly 1 MiB counted as header + payload;
- the reader walks the tag with `pread` at explicit offsets and never opens,
  duplicates, closes, seeks or does a sequential `read`;
- the only allocation is the fixed read window (no allocation proportional to
  the tag or to a frame), target payloads are read up to the window and
  non-target frames are skipped by the declared size;
- a zero frame size is rejected, the frame pointer advances by
  header + payload in both paths and a payload that crosses the tag end rejects
  the tag;
- the approved header/frame flag policy is present;
- the Makefile wires the focused I/O target through BINS, a `.PHONY` wrapper,
  the aggregate `test` and its wrapped-descriptor link flags, and wires this
  contract into the aggregate too (properties derived from the file, never a
  literal count);
- the project docs describe the current 1 MiB envelope and keep the old 64 KiB
  limit only as explicit history (fixture names are exempt).

Textual contract: it complements, and does not replace, the behavioral tests.
"""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
MODEL_HEADER = ROOT / "components/cyberdeck/include/apps/music/cyberdeck_music_model.h"
METADATA_HEADER = ROOT / "components/cyberdeck/include/apps/music/cyberdeck_music_metadata.h"
METADATA_SOURCE = ROOT / "components/cyberdeck/src/apps/music/cyberdeck_music_metadata.cpp"
MODEL_SOURCE = ROOT / "components/cyberdeck/src/apps/music/cyberdeck_music_model.cpp"
MAKEFILE = ROOT / "tests/host/keymap/Makefile"
PLAN = ROOT / "docs/03-audio-player-plan.pt-BR.md"
MANUAL = ROOT / "tests/manual/music-player-validation.pt-BR.md"

MIB = 1024 * 1024
IO_TARGET = "test_music_metadata_io"
CONTRACT_TARGET = "test_music_id3_contract"


def strip_cpp_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", " ", source, flags=re.S)
    return re.sub(r"//[^\n]*", " ", source)


def flat(source: str) -> str:
    return re.sub(r"\s+", " ", source)


def evaluate(expression: str, replacements: dict[str, int]) -> int:
    for name, value in replacements.items():
        expression = expression.replace(name, str(value))
    if not re.fullmatch(r"[\d\s*+()\-]+", expression):
        raise AssertionError(f"unsupported constant expression: {expression!r}")
    return int(eval(expression, {"__builtins__": {}}, {}))  # noqa: S307 - validated charset


def constant(source: str, name: str) -> int:
    matches = re.findall(
        rf"inline\s+constexpr\s+std::size_t\s+{name}\s*=\s*([^;]+);", source)
    if len(matches) != 1:
        raise AssertionError(f"{name} must have exactly one exported definition, found {len(matches)}")
    return evaluate(matches[0], {})


def require(condition: bool, failures: list[str], message: str) -> None:
    if not condition:
        failures.append(message)


def lines_with(text: str, pattern: str) -> list[str]:
    return [line for line in text.splitlines() if re.search(pattern, line)]


def rule_block(text: str, header_pattern: str) -> tuple[str, list[str]]:
    """Return (header with prerequisite continuations, recipe lines) of the rule."""
    lines = text.splitlines()
    for index, line in enumerate(lines):
        if not re.match(header_pattern, line):
            continue
        header = [line]
        cursor = index
        while header[-1].rstrip().endswith("\\"):
            cursor += 1
            if cursor >= len(lines):
                break
            header.append(lines[cursor])
        recipe = []
        for follower in lines[cursor + 1:]:
            if not follower.startswith("\t"):
                break
            recipe.append(follower)
        return " ".join(part.rstrip().rstrip("\\") for part in header), recipe
    return "", []


def main() -> int:
    failures: list[str] = []

    model_header = MODEL_HEADER.read_text(encoding="utf-8")
    metadata_header = METADATA_HEADER.read_text(encoding="utf-8")
    metadata_source = strip_cpp_comments(METADATA_SOURCE.read_text(encoding="utf-8"))
    model_source = strip_cpp_comments(MODEL_SOURCE.read_text(encoding="utf-8"))
    makefile = MAKEFILE.read_text(encoding="utf-8")
    plan = PLAN.read_text(encoding="utf-8")
    manual = MANUAL.read_text(encoding="utf-8")

    # --- constants: single, exported, approved ceiling ----------------------
    try:
        title_cap = constant(model_header, "k_max_title_bytes")
        header_bytes = constant(metadata_header, "k_id3v2_header_bytes")
        ceiling = constant(metadata_header, "k_id3_max_tag_bytes")
        window = evaluate(
            re.search(r"inline\s+constexpr\s+std::size_t\s+k_id3_read_window_bytes\s*=\s*([^;]+);",
                      metadata_header).group(1),
            {"k_max_title_bytes": title_cap})
    except (AssertionError, AttributeError) as error:
        failures.append(str(error))
        title_cap = header_bytes = ceiling = window = 0

    require(header_bytes == 10, failures,
            f"k_id3v2_header_bytes must be the 10-byte ID3v2 header, found {header_bytes}")
    require(ceiling == MIB, failures,
            f"k_id3_max_tag_bytes must be exactly 1 MiB (header + payload), found {ceiling}")
    require(window >= 2 * title_cap + 3, failures,
            f"k_id3_read_window_bytes ({window}) must hold an encoding byte, a UTF-16 BOM "
            f"and k_max_title_bytes ({title_cap}) two-byte units")

    for name in ("k_id3v2_header_bytes", "k_id3_max_tag_bytes", "k_id3_read_window_bytes"):
        for path, source in ((METADATA_SOURCE, metadata_source), (MODEL_SOURCE, model_source)):
            require(not re.search(rf"constexpr\s+[^;]*\b{name}\b\s*=", source),
                    failures, f"{name} must be defined only in the metadata header, not in {path.name}")

    # The ceiling counts the header plus the declared payload.
    require(re.search(r"payload_bytes \+ k_id3v2_header_bytes > k_id3_max_tag_bytes", flat(model_source)),
            failures, "parse_id3v2_header must reject when header + payload exceeds the ceiling")
    require(re.search(r"tag_bytes = k_id3v2_header_bytes \+ tag\.payload_bytes", flat(metadata_source)),
            failures, "the streaming reader must count the ceiling as header + payload")
    require(re.search(r"tag_bytes > size", flat(metadata_source)),
            failures, "the streaming reader must reject a tag larger than the verified file size")

    # --- reader: same descriptor, explicit offsets, no reopen/seek ----------
    require(re.search(r"pread\(fd, dst, length, static_cast<off_t>\(offset\)\)", flat(metadata_source)),
            failures, "read_exact must use pread at the explicit offset")
    for forbidden in ("open", "openat", "dup", "dup2", "fopen", "close", "lseek", "mmap"):
        require(not re.search(rf"\b{forbidden}\s*\(", metadata_source),
                failures, f"the metadata reader must not call {forbidden}()")
    require(not re.search(r"\bread\s*\(", metadata_source),
            failures, "the metadata reader must not use sequential read()")
    require(not re.search(r"\bstd::string\b(?!_view)", metadata_source),
            failures, "the metadata reader must not build a std::string from the tag")
    require(not re.search(r"\bstd::vector\b", metadata_source),
            failures, "the metadata reader must not build a std::vector from the tag")

    # --- allocation: one fixed window, never proportional to tag/frame ------
    allocations = re.findall(r"new \(std::nothrow\) unsigned char\[([^\]]*)\]", flat(metadata_source))
    require(len(allocations) == 1, failures,
            f"the reader must have exactly one heap allocation, found {len(allocations)}")
    if len(allocations) == 1:
        require(allocations[0].strip() == "k_id3_read_window_bytes", failures,
                "the only allocation must be k_id3_read_window_bytes, "
                f"not a tag/frame-sized expression ({allocations[0].strip()})")
    for forbidden in ("malloc", "calloc", "realloc"):
        require(not re.search(rf"\b{forbidden}\s*\(", metadata_source),
                failures, f"the metadata reader must not call {forbidden}()")
    require(re.search(
        r"const std::size_t want = std::min<std::size_t>\(frame\.payload_bytes, "
        r"k_id3_read_window_bytes\);", flat(metadata_source)),
        failures, "a target frame must be read up to min(payload, read window)")

    # --- frame walk: skip by size, advance, reject zero/cross-end -----------
    advance = "offset += frame.header_bytes + frame.payload_bytes;"
    for path, source in ((METADATA_SOURCE, metadata_source), (MODEL_SOURCE, model_source)):
        require(flat(source).count(advance) == 1,
                failures, f"{path.name} must advance the frame offset by header + payload exactly once")
        require(not re.search(r"offset \+= 1\s*;", flat(source)),
                failures, f"{path.name} must not walk into a payload byte by byte")
    require(re.search(r"frame\.payload_bytes > tag_bytes - offset - frame\.header_bytes", flat(metadata_source)),
            failures, "the reader must reject a frame payload that crosses the tag end")
    require(re.search(r"frame\.payload_bytes > end - offset - frame\.header_bytes", flat(model_source)),
            failures, "the parser must reject a frame payload that crosses the tag end")
    require(re.search(r"if \(size == 0\) return false;", flat(model_source)),
            failures, "parse_id3v2_frame_header must reject a zero frame size")
    require(re.search(r"\(bytes\[4\] \| bytes\[5\] \| bytes\[6\] \| bytes\[7\]\) & 0x80", flat(model_source)),
            failures, "ID3v2.4 syncsafe frame sizes with the high bit set must be rejected")

    # --- flag policy (AC-MUSIC-META-28) -------------------------------------
    require(re.search(r"reserved = version == 2 \? 0x3f : version == 3 \? 0x1f : 0x0f",
                      flat(model_source)),
            failures, "the header flag mask must keep the approved per-version reserved bits")
    require(re.search(r"version == 2 \? flags != 0 : \(flags & \(0x80 \| 0x40 \| reserved\)\) != 0",
                      flat(model_source)),
            failures, "unsync (0x80), extended header (0x40) and reserved bits must reject the tag")
    require(re.search(r"out\.flags_clear = version == 2 \|\| \(bytes\[8\] == 0 && bytes\[9\] == 0\)",
                      flat(model_source)),
            failures, "frame flags must be detected for v2.3/v2.4")
    require(re.search(r"if \(!frame\.flags_clear\) return;", flat(metadata_source)),
            failures, "the reader must reject a target frame that carries flags")
    require(re.search(r"if \(!frame\.flags_clear\) return false;", flat(model_source)),
            failures, "the parser must reject a target frame that carries flags")

    # --- Makefile wiring for the focused I/O target -------------------------
    bins_lines = [line for line in makefile.splitlines() if re.match(r"^BINS\b", line)]
    require(sum(line.count(f"{IO_TARGET}_bin") for line in bins_lines) == 1,
            failures, f"{IO_TARGET}_bin must be added to BINS exactly once")
    require(len([line for line in makefile.splitlines()
                 if line.startswith(".PHONY:") and IO_TARGET in line]) == 1,
            failures, f"{IO_TARGET} must have exactly one .PHONY declaration")
    io_rule, io_recipe = rule_block(makefile, rf"^{IO_TARGET}:\s")
    require(io_rule != "", failures, f"{IO_TARGET} wrapper target is missing")
    require(any(f"./$(MUSIC_METADATA_IO_BIN)" in line for line in io_recipe),
            failures, f"{IO_TARGET} must run the built binary")
    io_bin_rule, io_bin_recipe = rule_block(makefile, r"^\$\(MUSIC_METADATA_IO_BIN\):\s")
    require(io_bin_rule != "", failures, "$(MUSIC_METADATA_IO_BIN) rule is missing")
    for prerequisite in ("$(MUSIC_METADATA_IO_TEST)", "$(MUSIC_MODEL_SRC)", "$(MUSIC_METADATA_SRC)",
                         "$(MUSIC_MODEL_HDR)", "$(MUSIC_METADATA_HDR)"):
        require(prerequisite in io_bin_rule, failures,
                f"$(MUSIC_METADATA_IO_BIN) must depend on {prerequisite}")
    joined_recipe = " ".join(io_bin_recipe)
    for wrapped in ("pread", "open", "openat", "dup", "close"):
        require(f"-Wl,--wrap={wrapped}" in joined_recipe, failures,
                f"$(MUSIC_METADATA_IO_BIN) must link with -Wl,--wrap={wrapped}")
    aggregate = lines_with(makefile, r"--no-print-directory\s+" + IO_TARGET + r"\s*$")
    require(len(aggregate) == 1, failures,
            f"the aggregate `test` target must invoke {IO_TARGET} exactly once")

    # --- this contract's own wiring -----------------------------------------
    contract_rule, contract_recipe = rule_block(makefile, rf"^{CONTRACT_TARGET}:\s")
    require(contract_rule != "", failures, f"{CONTRACT_TARGET} target is missing")
    for prerequisite in ("test_music_id3_contract.py",
                         "../../../components/cyberdeck/include/apps/music/cyberdeck_music_metadata.h",
                         "../../../components/cyberdeck/include/apps/music/cyberdeck_music_model.h",
                         "../../../components/cyberdeck/src/apps/music/cyberdeck_music_metadata.cpp",
                         "../../../components/cyberdeck/src/apps/music/cyberdeck_music_model.cpp",
                         "../../../docs/03-audio-player-plan.pt-BR.md",
                         "../../../tests/manual/music-player-validation.pt-BR.md",
                         "Makefile"):
        require(prerequisite in contract_rule, failures,
                f"{CONTRACT_TARGET} must depend on {prerequisite}")
    require(any("$(PYTHON) test_music_id3_contract.py" in line for line in contract_recipe),
            failures, f"{CONTRACT_TARGET} must run the contract script")
    require(len(lines_with(makefile, r"--no-print-directory\s+" + CONTRACT_TARGET + r"\s*$")) == 1,
            failures, f"the aggregate `test` target must invoke {CONTRACT_TARGET} exactly once")

    # --- docs: current 1 MiB, old 64 KiB only as explicit history -----------
    require("1 MiB" in plan, failures, "the audio plan must document the current 1 MiB envelope")
    require("k_id3_max_tag_bytes" in plan, failures,
            "the audio plan must name the single ceiling constant")
    require("header + payload" in plan, failures,
            "the audio plan must state that the ceiling counts header + payload")
    require("1 MiB" in manual, failures,
            "the manual validation steps must document the current 1 MiB envelope")
    require("header + payload" in manual, failures,
            "the manual validation steps must state that the ceiling counts header + payload")

    history = re.compile(r"antig|anterior|antes|históric|historic|superad|legacy|previous|prior|old|rejeit|exced",
                         re.I)
    for document, name in ((plan, "docs/03-audio-player-plan.pt-BR.md"),
                           (manual, "tests/manual/music-player-validation.pt-BR.md")):
        document_lines = document.splitlines()
        for index, line in enumerate(document_lines):
            # Fixture identifiers such as `compact-64KiB` are names, not limits.
            without_identifiers = re.sub(r"`[^`]*`", "", line)
            if not re.search(r"64\s*KiB|64KiB|65536|64 \* 1024", without_identifiers):
                continue
            # The historical marker may wrap to a neighbouring line in the
            # paragraph, so the judgement window is the line and its neighbours.
            window = " ".join(document_lines[max(0, index - 2):index + 3])
            require(bool(history.search(window)), failures,
                    f"{name} mentions the old 64 KiB limit without a historical marker: {line.strip()}")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("PASS: 1 MiB ceiling (header + payload) and window are single exported constants; "
          "the reader uses pread offsets on the same descriptor with one fixed allocation; "
          "frames advance by header + payload, zero-size/cross-end/flags are rejected; "
          f"the Makefile wires {IO_TARGET} and {CONTRACT_TARGET} into BINS/.PHONY/aggregate; "
          "the docs document 1 MiB and keep 64 KiB only as explicit history")
    return 0


if __name__ == "__main__":
    sys.exit(main())
