#!/usr/bin/env python3
"""Structural regressions for the host-side BLE scan adapter contract.

The ESP-IDF adapter is not linkable in the host suite.  These checks therefore
pin the ordering and bounded diagnostic surface without opening hardware or a
simulator.
"""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
BLE_MGR = ROOT / "components/cyberdeck/src/features/bluetooth/ble_mgr.cpp"
EXPECTED_TYPES = (
    "BLE_HCI_ADV_RPT_EVTYPE_ADV_IND",
    "BLE_HCI_ADV_RPT_EVTYPE_DIR_IND",
    "BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND",
    "BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND",
    "BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP",
)


def function_body(source: str, signature: str) -> str:
    marker = 0
    while True:
        marker = source.find(signature, marker)
        if marker < 0:
            raise AssertionError(f"missing function {signature!r}")
        opening = source.find("{", marker)
        semicolon = source.find(";", marker)
        if semicolon < 0 or opening < semicolon:
            break
        marker = semicolon + 1
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated function {signature!r}")


def scan_case_body(source: str) -> str:
    body = function_body(source, "static void ble_mgr_task(")
    marker = body.index("case BLE_MGR_CMD_SCAN_START:")
    end = body.index("case BLE_MGR_CMD_SCAN_CANCEL:", marker)
    return body[marker:end]


def log_lines(source: str):
    return [line.strip() for line in source.splitlines()
            if "ESP_LOG" in line and "(" in line]


def main() -> int:
    source = BLE_MGR.read_text(encoding="utf-8")
    callback = function_body(source, "static int ble_gap_event_cb(")
    discovery = callback[callback.index("switch (event->disc.event_type)"):]
    switch_end = discovery.index("        }\n        break;")
    report_switch = discovery[:switch_end]
    for adv_type in EXPECTED_TYPES:
        assert report_switch.count(f"case {adv_type}:") == 1, adv_type
    assert report_switch.count("scan_report_adv(&event->disc);") == len(EXPECTED_TYPES)
    assert "default:" in report_switch
    assert "Ignore non-advertising GAP reports" in report_switch

    # Regression: infer the local address type in the scan command itself and
    # pass that result to GAP.  A fixed PUBLIC address is not portable across
    # NimBLE configurations and must not become the scan argument.
    scan = scan_case_body(source)
    infer = scan.index("ble_hs_id_infer_auto(0, &own_addr_type)")
    gap_disc = scan.index("ble_gap_disc(")
    assert infer < gap_disc, "address type must be inferred before scan starts"
    assert "ble_gap_disc(own_addr_type," in scan
    assert "ble_gap_disc(BLE_OWN_ADDR_PUBLIC," not in scan
    assert "if (rc != 0)" in scan[infer:gap_disc]
    assert "handle_scan_finished(rc)" in scan[infer:gap_disc]

    # The scan diagnostic is a fixed, bounded summary: counters and status
    # only.  It must not print advertisement buffers, names, addresses, or
    # authentication material (including through a future format expansion).
    stats = source[source.index("struct ble_scan_stats {"):source.index("};", source.index("struct ble_scan_stats {"))]
    fields = ("gap_disc", "accepted_adv", "accepted_dir", "accepted_scan",
              "accepted_nonconn", "accepted_rsp", "ignored", "token_dropped",
              "mutex_dropped", "published", "dispatch_dropped", "malformed")
    for field in fields:
        assert f"uint32_t {field};" in stats, field
    assert "s_scan_stats = {};" in scan
    finish = function_body(source, "static void handle_scan_finished(")
    assert "BLE scan finish token=" in finish
    assert "s_scan_stats." in finish
    forbidden_diagnostic_tokens = (
        "payload", "data", "name", "address", "passkey", "link_key",
        "irk", "ltk", "csrk", "pin",
    )
    finish_log = next(line for line in log_lines(finish)
                      if "BLE scan finish" in line)
    lowered = finish_log.lower()
    for token in forbidden_diagnostic_tokens:
        assert token not in lowered, token

    # The report path is bounded and supports unnamed advertisements without
    # ever logging the raw AD payload.  The pure device contract owns the exact
    # placeholder and sanitizer; this adapter must seed/use that bounded path.
    report = function_body(source, "static void scan_report_adv(")
    assert "sanitize_name" in report
    assert "peer.record.name.clear()" in report
    assert "disc->length_data != 0 && disc->data == nullptr" in report
    assert "reinterpret_cast<const char *>(fields.name)" in report
    assert not any("ESP_LOG" in line for line in report.splitlines())
    print("PASS: BLE advertisement report types contract")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (AssertionError, OSError, UnicodeError, ValueError) as error:
        print(f"FAIL: {error}")
        sys.exit(1)
