#!/usr/bin/env python3
"""Host-only structural contract for the asynchronous Phase 2 HID discovery.

The ESP-IDF/NimBLE adapter is intentionally not linkable on the host.  These
checks inspect the production seam while test_ble_event_dispatch.cpp exercises
the pure bounded publication path.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
MGR = (ROOT / "components/cyberdeck/src/features/bluetooth/ble_mgr.cpp").read_text()
HDR = (ROOT / "components/cyberdeck/include/features/bluetooth/ble_mgr.h").read_text()
EVENTS = (ROOT / "components/cyberdeck/src/features/bluetooth/cyberdeck_ble_event_dispatch.cpp").read_text()


def body(source: str, signature: str) -> str:
    marker = -1
    while True:
        marker = source.index(signature, marker + 1)
        opening = source.find("{", marker)
        semicolon = source.find(";", marker)
        if semicolon < 0 or opening < semicolon:
            break
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated {signature}")


def switch_case(source: str, label: str, next_label: str) -> str:
    """Return one case from the intended switch, not an earlier helper case."""
    start = source.index(f"case {label}:")
    end = source.index(f"case {next_label}:", start)
    return source[start:end]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    # HID service and relevant characteristics are discovered, not interpreted.
    require("k_hid_service_uuid = 0x1812" in MGR, "HID service UUID changed")
    for uuid in ("0x2a4b", "0x2a4e", "0x2a22", "0x2a4d"):
        require(uuid in MGR, f"missing HID characteristic UUID {uuid}")
    require("ble_uuid_u16(&service->uuid.u) == k_hid_service_uuid" in MGR,
            "HID service is not selected by UUID")

    # Arrays and both counters are bounded at the collection boundary.
    require("k_hid_max_characteristics = 8" in MGR and
            "k_hid_max_cccd = 4" in MGR, "HID bounds changed")
    require("hid_characteristic characteristics[k_hid_max_characteristics]" in MGR,
            "characteristics are not stored in a fixed bounded array")
    characteristic = body(MGR, "static int hid_characteristic_cb(")
    require("s_hid.characteristic_count < k_hid_max_characteristics" in characteristic,
            "characteristic overflow is not rejected")
    require("s_hid.cccd_target_count < k_hid_max_cccd" in characteristic,
            "CCCD target overflow is not rejected")
    descriptor = body(MGR, "static int hid_descriptor_cb(")
    require("s_hid.cccd_count < s_hid.cccd_target_count" in descriptor,
            "CCCD callback is not bounded")

    # Every callback carries and validates both the connection and generation.
    current = body(MGR, "static bool hid_callback_is_current(")
    require("s_hid.active" in current and "generation == s_hid.generation" in current and
            "conn_handle == s_hid.conn_handle" in current,
            "stale HID callback guard lost conn_handle/token validation")
    start = body(MGR, "static void start_hid_discovery(")
    require("s_hid.conn_handle = conn_handle" in start and
            "s_hid.generation = token" in start and
            "s_hid.token = token" in start and
            "static_cast<uintptr_t>(s_hid.generation)" in start,
            "HID discovery context does not bind handle and generation")
    for callback in ("hid_service_cb", "hid_characteristic_cb", "hid_descriptor_cb"):
        require(callback in start or callback in MGR, f"missing {callback}")

    # CONNECTED is published before the independent asynchronous HID phase.
    connection = body(MGR, "static void handle_connection_result(")
    require(connection.index("s_dispatch.publish_connected(token)") <
            connection.index("start_hid_discovery("),
            "CONNECTED is gated by HID discovery")
    require("BLE_MGR_EVT_HID_DISCOVERY" in HDR and
            "hid_discovery" in HDR, "HID event is absent from the observer ABI")
    require("publish_hid_discovery" in EVENTS and
            "ble_event_kind::hid_discovery" in EVENTS,
            "HID publication is not an additional dispatch event")

    # Disconnect and STOP invalidate the context; old callbacks cannot finish it.
    task = body(MGR, "static void ble_mgr_task(")
    stop = switch_case(task, "BLE_MGR_CMD_STOP", "BLE_MGR_CMD_SCAN_START")
    disconnect = MGR[MGR.index("case BLE_GAP_EVENT_DISCONNECT:"):MGR.index("case BLE_GAP_EVENT_ENC_CHANGE:")]
    require("s_hid.active = false" in stop and "++s_hid.generation" in stop,
            "STOP does not invalidate HID discovery")
    require("s_hid.active = false" in disconnect and "++s_hid.generation" in disconnect,
            "disconnect does not invalidate HID discovery")

    # Phase boundary: no report-map parser, notification subscription, or key injection.
    require("ble_gattc_read" not in MGR and "ble_gattc_notify" not in MGR and
            "ble_gattc_write_flat" not in MGR,
            "HID discovery grew reads/notifications/writes")
    require("ble_sm_inject_io" not in body(MGR, "static void finish_hid_discovery("),
            "HID discovery can inject authentication input")

    # Existing auth/address-type contracts remain present in the same adapter.
    gate = task.index("if (!s_host_synced && cmd.kind != BLE_MGR_CMD_STOP)")
    switch = task.index("switch (cmd.kind)")
    require(gate < switch and "reject_pre_sync_command(cmd)" in task[gate:switch],
            "PASSKEY_REPLY is not covered by the central host_synced gate")
    passkey = switch_case(task, "BLE_MGR_CMD_PASSKEY_REPLY", "BLE_MGR_CMD_PAIR_CANCEL")
    require("ble_sm_inject_io(auth_conn, &pkey)" in MGR and
            "cmd.passkey.addr_type" in MGR,
            "passkey/address-type path was lost")
    reject_start = MGR.index("static void reject_pre_sync_command(const ble_mgr_cmd_t &cmd)\n{")
    reject_end = MGR.index("static void start_scan_command(", reject_start)
    require("BLE_MGR_CMD_PASSKEY_REPLY" in MGR[reject_start:reject_end],
            "pre-sync rejection lost PASSKEY_REPLY")
    print("PASS: BLE HID discovery bounded lifecycle/publication contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError, ValueError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
