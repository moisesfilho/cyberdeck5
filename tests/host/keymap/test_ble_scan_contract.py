#!/usr/bin/env python3
"""Host-only contracts for the non-linkable NimBLE scan adapter."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "components/cyberdeck/src/features/bluetooth/ble_mgr.cpp"


def body(source: str, signature: str) -> str:
    start = 0
    while True:
        marker = source.find(signature, start)
        if marker < 0:
            raise AssertionError(f"missing {signature}")
        opening = source.find("{", marker)
        semicolon = source.find(";", marker)
        if semicolon < 0 or opening < semicolon:
            break
        start = semicolon + 1
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated {signature}")


def main() -> int:
    source = SOURCE.read_text(encoding="utf-8")
    task = body(source, "static void ble_mgr_task(")
    scan = task[task.index("case BLE_MGR_CMD_SCAN_START:"):]
    scan = scan[:scan.index("case BLE_MGR_CMD_SCAN_CANCEL:")]

    # Tab5 parity: continuous, duplicate-free scan for exactly five seconds.
    assert re.search(r"\.itvl\s*=\s*0", scan), "itvl"
    assert re.search(r"\.window\s*=\s*0", scan), "window"
    assert re.search(r"\.filter_duplicates\s*=\s*0", scan), "duplicates"
    assert re.search(r"ble_gap_disc\(own_addr_type,\s*5000,\s*&params", scan), "duration"
    assert scan.index("ble_hs_id_infer_auto") < scan.index("ble_gap_disc("), "infer order"

    report = body(source, "static void scan_report_adv(")
    parse = report.index("ble_hs_adv_parse_fields")
    assert "BLE_HS_ADV_MAX_SZ" in report[:parse], "bounded size"
    assert "disc->length_data != 0 && disc->data == nullptr" in report[:parse], "null payload"
    assert "fields" in report[parse:], "fields parse"

    # Identity is address + address type, and all four NimBLE peer classes are
    # preserved before conversion to the random/public stack type.
    key = report[report.index("for (std::size_t i"):report.index("if (peer_index ==")]
    assert "record.address == addr_str" in key
    assert "record.addr_type == addr_type" in key
    peer_type = body(source, "static cyberdeck_ble::address_type peer_address_type(")
    for token in ("BLE_ADDR_PUBLIC", "BLE_ADDR_IS_STATIC", "BLE_ADDR_IS_RPA",
                  "BLE_ADDR_IS_NRPA"):
        assert token in peer_type
    stack_type = body(source, "static uint8_t stack_address_type(")
    assert "BLE_ADDR_PUBLIC" in stack_type and "BLE_ADDR_RANDOM" in stack_type

    primary = report[report.index("const bool primary"):]
    assert "disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP" in primary
    assert "if (primary)" in primary
    assert "peer.record.connectable = disc->event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND" in primary
    rsp = primary[primary.index("if (!primary"):]
    assert "connectable" not in rsp
    assert "!peer.primary_seen" in rsp

    # Every connection path must derive the peer type from the command before
    # calling GAP, including pair, manual connect and reconnect.
    task_connects = [line for line in task.splitlines() if "ble_gap_connect(" in line]
    assert len(task_connects) == 3
    assert task.count("stack_address_type(static_cast<cyberdeck_ble::address_type>(cmd.") >= 3
    assert task.count("peer_addr = {.type = stack_address_type") >= 3
    assert source.count("out.connection.addr_type = static_cast<uint8_t>(in.addr_type)") == 2

    # Stale callbacks and malformed reports are bounded/fail-closed.
    assert "uint64_t token = s_scan_token" in report
    assert "s_dispatch.publish_scan_result(token" in report
    assert "++s_scan_stats.malformed" in report
    assert "disc->length_data > BLE_HS_ADV_MAX_SZ" in report
    assert "BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP" in source
    print("PASS: BLE scan parameters/parser/identity/aggregation contract")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        sys.exit(1)
