#!/usr/bin/env python3
"""Regression contracts for commands received before NimBLE host sync."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "components/cyberdeck/src/features/bluetooth/ble_mgr.cpp"


def body(source: str, signature: str) -> str:
    marker = 0
    while True:
        marker = source.find(signature, marker)
        if marker < 0:
            raise AssertionError(f"missing {signature}")
        opening = source.find("{", marker)
        semicolon = source.find(";", marker)
        if opening >= 0 and (semicolon < 0 or opening < semicolon):
            break
        marker += len(signature)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated {signature}")


def between(source: str, start: str, end: str) -> str:
    first = source.index(start)
    return source[first:source.index(end, first)]


def main() -> int:
    source = SOURCE.read_text(encoding="utf-8")
    task = body(source, "static void ble_mgr_task(")
    sync = body(source, "ble_hs_cfg.sync_cb = []()")
    reject = body(source, "static void reject_pre_sync_command(")

    gate = task.index("if (!s_host_synced && cmd.kind != BLE_MGR_CMD_STOP)")
    switch = task.index("switch (cmd.kind)")
    assert gate < switch
    pre_sync_path = task[gate:switch]
    assert "ble_gap_" not in pre_sync_path
    assert "reject_pre_sync_command(cmd)" in pre_sync_path

    # Pre-sync has no generic queue or deferred GAP work.  Every radio command
    # is rejected terminally and the STOP lifecycle command remains executable.
    assert "queue_pre_sync_command" not in source
    assert "s_pre_sync_command_count" not in source
    assert "does not touch NimBLE" in reject
    assert "drain_dispatch_events();" in reject
    assert "ble_gap_" not in reject
    for command in ("BLE_MGR_CMD_SCAN_START", "BLE_MGR_CMD_SCAN_CANCEL",
                    "BLE_MGR_CMD_PAIR", "BLE_MGR_CMD_PAIR_CANCEL",
                    "BLE_MGR_CMD_PASSKEY_REPLY", "BLE_MGR_CMD_CONNECT",
                    "BLE_MGR_CMD_RECONNECT", "BLE_MGR_CMD_DISCONNECT"):
        assert command in reject
    assert "cmd.kind != BLE_MGR_CMD_STOP" in task[gate:switch]
    assert "s_host_synced" in task[gate:switch]
    assert "s_gap_event_queue" in source
    assert "s_gap_terminal_queue" in source

    # Sync establishes identity before releasing the radio gate.  No GAP call
    # is made here; ensure/infer are local identity setup only.
    assert source.count("ble_hs_util_ensure_addr(0)") == 1
    assert source.count("ble_hs_id_infer_auto(0, &s_own_addr_type)") == 1
    assert "ble_hs_util_ensure_addr(0)" in sync
    assert sync.index("ble_hs_id_infer_auto") < sync.index("s_host_synced = true")
    assert "load_bonds_from_nvs();" in sync
    assert "ble_gap_" not in sync
    assert task.count("s_scan_start_pending && s_scan_next_generation_ready") == 2
    assert "s_scan_start_pending = false" in task
    assert "start_scan_command(next_scan)" in task

    scan_cancel = between(task, "case BLE_MGR_CMD_SCAN_CANCEL:", "case BLE_MGR_CMD_PAIR:")
    pair_cancel = between(task, "case BLE_MGR_CMD_PAIR_CANCEL:", "case BLE_MGR_CMD_CONNECT:")
    connect = between(task, "case BLE_MGR_CMD_CONNECT:", "case BLE_MGR_CMD_DISCONNECT:")
    disconnect = between(task, "case BLE_MGR_CMD_DISCONNECT:", "case BLE_MGR_CMD_RECONNECT:")
    reconnect = task[task.index("case BLE_MGR_CMD_RECONNECT:"):]
    assert "cmd.token != s_scan_token" in scan_cancel
    assert "cmd.token != s_dispatch.active_scan_token()" in scan_cancel
    assert "cmd.token != s_pair_token" in pair_cancel
    assert "cmd.token != s_dispatch.active_pair_token()" in pair_cancel
    assert "cmd.token <= s_connection_token" in connect
    assert "cmd.token <= s_connection_token" in reconnect
    assert "s_pair_conn != BLE_HS_CONN_HANDLE_NONE" in disconnect
    assert "s_connection_conn != BLE_HS_CONN_HANDLE_NONE" in disconnect
    assert "ble_gap_conn_cancel()" in pair_cancel

    # PASSKEY_REPLY and all cancel/connect/disconnect commands are covered by
    # the same gate rather than maintaining a second pre-sync path.
    assert "BLE_MGR_CMD_PASSKEY_REPLY" in reject
    assert "BLE_MGR_CMD_DISCONNECT" in reject

    gap = body(source, "static int ble_gap_event_cb(")
    assert "s_dispatch_mutex" not in gap
    assert "xQueueOverwrite(queue, &snapshot)" in gap
    complete = body(source, "static void process_gap_event(")
    complete = complete[complete.index("case BLE_GAP_EVENT_DISC_COMPLETE:"):]
    assert "token != s_scan_token" in complete
    assert "token != s_dispatch.active_scan_token()" in complete
    assert "s_scan_cancel_token != 0" in complete
    assert "s_scan_next_generation_ready = true" in complete
    assert "s_dispatch_mutex" not in gap
    finish = body(source, "static void handle_scan_finished(")
    assert finish.index("publish_scan_finished") < finish.index("drain_dispatch_events()")
    assert "s_dispatch.pending()" in finish

    print("PASS: BLE pre-sync command gating/bounded terminal contract")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        sys.exit(1)
