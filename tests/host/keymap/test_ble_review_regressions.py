#!/usr/bin/env python3
"""Structural regressions for the post-review BLE adapter fixes.

The ESP-IDF/NimBLE adapter is intentionally not host-linkable.  These checks
inspect only the production seams and never open hardware, a simulator, or the
Serial Automation Bridge.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
MGR = ROOT / "components/cyberdeck/src/features/bluetooth/ble_mgr.cpp"
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"


def function_body(source: str, signature: str) -> str:
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
    if opening < 0:
        raise AssertionError(f"missing body for {signature}")
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
    mgr = MGR.read_text(encoding="utf-8")
    ui = UI.read_text(encoding="utf-8")

    task = function_body(mgr, "static void ble_mgr_task(")
    connects = re.findall(r"ble_gap_connect\((.*?);\s", task, re.S)
    assert len(connects) == 3, "pair/connect/reconnect must retain three GAP calls"
    for arguments in connects:
        assert re.match(r"\s*s_own_addr_type\s*,", arguments), arguments
        assert "BLE_OWN_ADDR_PUBLIC" not in arguments, arguments
    # The safe implementation infers the local type during host sync and
    # reuses that state for scan and all three connection paths.
    sync = function_body(mgr, "ble_hs_cfg.sync_cb = []()")
    assert "ble_hs_id_infer_auto(0, &s_own_addr_type)" in sync
    assert sync.index("ble_hs_id_infer_auto") < sync.index("s_host_synced = true")
    assert "ble_gap_" not in sync
    scan_start = function_body(mgr, "static void start_scan_command(")
    assert "const uint8_t own_addr_type = s_own_addr_type" in scan_start

    # A full UI queue may lose a visual scan result, but never its terminal
    # outcome.  The adapter also drains after publishing the terminal event.
    event_callback = function_body(mgr if False else ui, "void on_ble_event(")
    assert "xQueueSend(s_ble_event_queue, event, 0)" in event_callback
    assert "event->kind == BLE_MGR_EVT_SCAN_FINISHED" in event_callback
    assert "queued.kind == BLE_MGR_EVT_SCAN_RESULT" in event_callback
    assert "evicted" in event_callback
    assert "xQueueSend(s_ble_event_queue, event, 0)" in event_callback
    finish = function_body(mgr, "static void handle_scan_finished(")
    assert "s_dispatch.publish_scan_finished(token, outcome)" in finish
    assert "drain_dispatch_events()" in finish

    # Teardown is cooperative: the public stop path signals and joins, but
    # never deletes the manager task handle from outside its own task.
    stop = function_body(mgr, "esp_err_t ble_mgr_stop(")
    assert "BLE_MGR_CMD_STOP" in stop
    assert "xQueueSend(s_ble_queue" in stop
    assert "xSemaphoreTake(s_stop_done" in stop
    assert "vTaskDelete(s_ble_task)" not in stop

    # The UI lookup key is the complete BLE identity, and each union member is
    # populated explicitly after value-initialization (no type-punning alias).
    submit = function_body(ui, "void ble_submit_actions(")
    assert "ble_mgr_cmd_t cmd{}" in submit
    assert submit.count("find(action.address, action.addr_type)") == 2
    assert "reinterpret_cast" not in submit
    assert "cmd.pair.addr_type = addr_type" in submit
    assert "cmd.connect.addr_type = addr_type" in submit
    assert "cmd.passkey.addr_type = addr_type" in submit

    # Versioned storage policy: v2 is preferred; only the legacy four-field
    # blob is erased and requires a new pairing.
    load = function_body(mgr, "static void load_bonds_from_nvs(")
    assert 'const char *key = BLE_MGR_NVS_KEY_V2' in load
    assert 'key = BLE_MGR_NVS_KEY' in load
    assert 'cyberdeck_ble::is_legacy_bond_blob' in load
    assert 'nvs_erase_key(erase_handle, key)' in load
    assert 's_store.clear()' in load
    assert 'new pairing is required' in load
    assert load.index("const char *key = BLE_MGR_NVS_KEY_V2") < load.index(
        "key = BLE_MGR_NVS_KEY;")
    assert "s_store.deserialize(buffer.data(), required_size)" in load

    print("PASS: BLE review regressions (overflow/identity/storage/teardown/UI)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        sys.exit(1)
