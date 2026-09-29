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


def scan_command_body(source: str) -> str:
    """Return the implementation that owns the GAP discovery start."""
    return function_body(source, "static void start_scan_command(")


def manager_task_body(source: str) -> str:
    """Return the central command dispatcher, not a GAP-specific helper."""
    return function_body(source, "static void ble_mgr_task(")


def log_lines(source: str):
    return [line.strip() for line in source.splitlines()
            if "ESP_LOG" in line and "(" in line]


def main() -> int:
    source = BLE_MGR.read_text(encoding="utf-8")
    callback = function_body(source, "static int ble_gap_event_cb(")
    # GAP callbacks only copy bounded snapshots.  They must not contend for
    # the dispatcher mutex or call GAP/dispatch code from the NimBLE task.
    assert "s_dispatch_mutex" not in callback
    assert "s_dispatch." not in callback
    assert "ble_gap_" not in callback
    assert "gap_event_snapshot snapshot = {}" in callback
    assert "snapshot.event = *event" in callback
    assert "s_gap_terminal_queue" in callback
    assert "xQueueOverwrite(queue, &snapshot)" in callback
    assert "xQueueSend(queue, &snapshot, 0)" in callback

    # Reports and the terminal discovery snapshot are processed by the real
    # manager-side queues, with DISC_COMPLETE reserved in its own slot.
    report = function_body(source, "static void process_gap_event(")
    report_switch = report[report.index("switch (event->disc.event_type)"):]
    for adv_type in EXPECTED_TYPES:
        assert report_switch.count(f"case {adv_type}:") == 1, adv_type
    assert report_switch.count("scan_report_adv(&event->disc);") == len(EXPECTED_TYPES)
    assert "default:" in report_switch
    assert "Ignore non-advertising GAP reports" in report_switch
    queues = source[source.index("#define BLE_MGR_GAP_EVENT_QUEUE_SIZE"):source.index("struct gap_event_snapshot")]
    assert "BLE_MGR_GAP_EVENT_QUEUE_SIZE 16" in queues
    assert "BLE_MGR_GAP_TERMINAL_QUEUE_SIZE 1" in queues
    gap_pump = function_body(source, "static void process_gap_events(")
    assert gap_pump.index("s_gap_event_queue") < gap_pump.index("s_gap_terminal_queue")
    assert "DISC_COMPLETE remains the terminal event" in gap_pump

    # Regression: infer the local address type during host sync and retain it
    # for scan. A fixed PUBLIC address is not portable across NimBLE
    # configurations and must not become the scan argument.
    sync = function_body(source, "ble_hs_cfg.sync_cb = []()")
    infer = sync.index("ble_hs_id_infer_auto(0, &s_own_addr_type)")
    assert infer < sync.index("s_host_synced = true")
    assert "ble_gap_" not in sync
    scan = scan_command_body(source)
    gap_disc = scan.index("ble_gap_disc(")
    assert "const uint8_t own_addr_type = s_own_addr_type" in scan
    assert "ble_gap_disc(own_addr_type," in scan
    assert "ble_gap_disc(BLE_OWN_ADDR_PUBLIC," not in scan

    # The radio-ready gate belongs to the central command dispatcher. Keep
    # this contract independent from the scan-specific helper.
    task = manager_task_body(source)
    gate = task.index("if (!s_host_synced && cmd.kind != BLE_MGR_CMD_STOP)")
    switch = task.index("switch (cmd.kind)", gate)
    assert task.index("reject_pre_sync_command(cmd);", gate, switch) < switch
    assert task.index("continue;", gate, switch) < switch
    for command in (
        "SCAN_START", "SCAN_CANCEL", "PAIR", "PASSKEY_REPLY",
        "PAIR_CANCEL", "CONNECT", "DISCONNECT", "RECONNECT",
    ):
        assert f"case BLE_MGR_CMD_{command}:" in task[switch:], command
    assert "xSemaphoreTake(s_dispatch_mutex" not in task
    assert "xSemaphoreGive(s_dispatch_mutex)" not in task
    assert "ble_gap_disc_active()" in task

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
