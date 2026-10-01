#!/usr/bin/env python3
"""Host-only contracts for the non-linkable NimBLE scan adapter."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "components/cyberdeck/src/apps/bluetooth/ble_mgr.cpp"


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
    start_helper = body(source, "static void start_scan_command(")
    sync = body(source, "ble_hs_cfg.sync_cb = []()")
    scan = task[task.index("case BLE_MGR_CMD_SCAN_START:"):]
    scan = scan[:scan.index("case BLE_MGR_CMD_SCAN_CANCEL:")]

    # BLE requirement: continuous, duplicate-free scan with a five-second window.
    assert re.search(r"\.itvl\s*=\s*0", start_helper), "itvl"
    assert re.search(r"\.window\s*=\s*0", start_helper), "window"
    assert re.search(r"\.filter_duplicates\s*=\s*1", start_helper), "duplicates"
    assert re.search(r"ble_gap_disc\(own_addr_type,\s*5000,\s*&params", start_helper), "duration"
    assert "ble_hs_id_infer_auto(0, &s_own_addr_type)" in sync
    assert sync.index("ble_hs_id_infer_auto") < sync.index("s_host_synced = true"), "infer before release"
    assert "ble_gap_" not in sync, "sync callback must not call GAP"
    assert "const uint8_t own_addr_type = s_own_addr_type" in start_helper

    # GAP calls are owned by the manager task.  The GAP callback only copies a
    # bounded snapshot and enqueues it; it must never wait for the dispatch
    # mutex or dispatch synchronously from the NimBLE host task.
    assert "xSemaphoreTake(s_dispatch_mutex" not in start_helper
    assert "s_dispatch_mutex" not in task, "GAP task calls must stay outside dispatch mutex"
    gap_callback = body(source, "static int ble_gap_event_cb(")
    assert "s_dispatch_mutex" not in gap_callback
    assert "xQueueSend(queue, &snapshot, 0)" in gap_callback
    assert "xQueueOverwrite(queue, &snapshot)" in gap_callback
    assert "ble_gap_" not in gap_callback

    # Reports and terminal events are copied into bounded, independent queues;
    # DISC_COMPLETE cannot be discarded by an advertising-report burst.
    assert "BLE_MGR_GAP_EVENT_QUEUE_SIZE 16" in source
    assert "BLE_MGR_GAP_TERMINAL_QUEUE_SIZE 1" in source
    assert "uint8_t adv_data[BLE_HS_ADV_MAX_SZ]" in source
    assert "xQueueCreate(BLE_MGR_GAP_EVENT_QUEUE_SIZE" in source
    assert "xQueueCreate(BLE_MGR_GAP_TERMINAL_QUEUE_SIZE" in source
    assert "event->type == BLE_GAP_EVENT_DISC_COMPLETE" in gap_callback
    assert "? s_gap_terminal_queue : s_gap_event_queue" in gap_callback
    assert "event->type == BLE_GAP_EVENT_DISC_COMPLETE" in gap_callback[gap_callback.index("QueueHandle_t queue") :]
    assert gap_callback.index("xQueueOverwrite(queue, &snapshot)") < gap_callback.index("xQueueSend(queue, &snapshot, 0)")
    assert "xQueueReceive(s_gap_event_queue" in body(source, "static void process_gap_events(")
    assert "xQueueReceive(s_gap_terminal_queue" in body(source, "static void process_gap_events(")

    # Preemption is serialized: cancellation is requested first, the GAP
    # terminal callback publishes DISC_COMPLETE, and only then can the next
    # generation enter start_scan_command().
    assert "s_pending_scan_start = cmd" in scan
    assert "s_scan_start_pending = true" in scan
    assert "s_scan_cancel_token = s_scan_token" in scan
    assert "s_scan_cancel_pending = true" in scan
    assert scan.index("if (ble_gap_disc_active())") < scan.index("start_scan_command(cmd)")
    assert "s_scan_start_pending && s_scan_next_generation_ready" in task
    assert "!s_scan_cancel_pending && !ble_gap_disc_active()" in task
    assert "s_scan_cancel_pending ||" in start_helper
    assert "s_scan_next_generation_ready = false" in start_helper
    # Readiness is enforced once, before the command switch.  Pre-sync is a
    # terminal reject path, not a deferred scan queue (covered in detail by
    # test_ble_pre_sync_contract.py).
    gate = task.index("if (!s_host_synced && cmd.kind != BLE_MGR_CMD_STOP)")
    switch = task.index("switch (cmd.kind)")
    assert gate < switch
    assert "reject_pre_sync_command(cmd)" in task[gate:switch]
    assert "queue_pre_sync_command" not in source
    assert "s_pre_sync_command_count" not in source
    assert task.count("s_scan_start_pending && s_scan_next_generation_ready") == 2
    for condition_at in [
        index for index in range(len(task))
        if task.startswith("s_scan_start_pending && s_scan_next_generation_ready", index)
    ]:
        release = task[condition_at:]
        assert release.index("s_scan_start_pending = false") < release.index("start_scan_command(next_scan)")

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

    # Interactive scans consume both advertisements and their terminal event;
    # a report cannot be accepted under an old token/generation.
    process = body(source, "static void process_gap_event(")
    assert process.count("scan_report_adv(&event->disc)") == 5
    complete = process[process.index("case BLE_GAP_EVENT_DISC_COMPLETE:"):]
    assert "token != s_scan_token" in complete
    assert "token != s_dispatch.active_scan_token()" in complete
    assert "handle_scan_finished(token, event->disc_complete.reason)" in complete
    assert complete.index("handle_scan_finished") < complete.index("s_scan_next_generation_ready = true")

    cancel = task[task.index("case BLE_MGR_CMD_SCAN_CANCEL:"):]
    cancel = cancel[:cancel.index("case BLE_MGR_CMD_PAIR:")]
    assert "const bool scan_active = ble_gap_disc_active()" in cancel
    assert "scan_active ? ble_gap_disc_cancel() : BLE_HS_EALREADY" in cancel
    assert "if (rc == BLE_HS_EALREADY && !scan_active)" in cancel
    assert "if (rc == BLE_HS_EALREADY && scan_active)" not in cancel

    # Local identity setup belongs exclusively to sync; scan and every connect
    # path reuse the inferred value rather than inferring/ensuring ad hoc.
    assert source.count("ble_hs_util_ensure_addr(0)") == 1
    assert source.count("ble_hs_id_infer_auto(0, &s_own_addr_type)") == 1
    assert "ble_hs_util_ensure_addr(0)" in sync
    assert "ble_hs_id_infer_auto(0, &s_own_addr_type)" in sync
    assert start_helper.count("s_own_addr_type") >= 1
    assert task.count("ble_gap_connect(s_own_addr_type") == 3
    print("PASS: BLE scan parameters/parser/identity/aggregation contract")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        sys.exit(1)
