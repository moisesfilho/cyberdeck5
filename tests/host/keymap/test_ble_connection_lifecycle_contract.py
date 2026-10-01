#!/usr/bin/env python3
"""Host-only regression contract for BLE connection lifecycle ownership.

The state machine is UI-only: a successful connection releases its transient
screen, while ble_mgr retains the authenticated connection identity and
generation for real/stale disconnect filtering and independent HID discovery.
The UI must then expose the local prompt and keep shell/Wi-Fi/SSH routing.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
STATE = ROOT / "components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_state_machine.cpp"
MGR = ROOT / "components/cyberdeck/src/apps/bluetooth/ble_mgr.cpp"
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
SESSION = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp"


def body(source: str, signature: str) -> str:
    start = -1
    while True:
        start = source.find(signature, start + 1)
        if start < 0:
            raise AssertionError(f"missing {signature}")
        opening = source.find("{", start)
        semicolon = source.find(";", start)
        if opening >= 0 and (semicolon < 0 or opening < semicolon):
            break
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
    state = STATE.read_text(encoding="utf-8")
    manager = MGR.read_text(encoding="utf-8")
    ui = UI.read_text(encoding="utf-8")
    session = SESSION.read_text(encoding="utf-8")

    success = body(state, "void state_machine::connection_finished")
    assert "state_->current = screen::idle;" in success
    assert "state_->connection_established = true;" in success
    assert "emit_action(action_kind::disconnect" not in success
    assert "state_->connect_token" in success

    # The state machine must not own or clear the manager's link context.  The
    # adapter retains address/type/connection handle and validates generations.
    connected = body(manager, "static void handle_connection_result")
    assert "s_dispatch.publish_connected(token)" in connected
    assert "s_connection_address" in connected
    assert "s_connection_addr_type" in connected
    assert "start_hid_discovery(s_connection_conn, token)" in connected
    assert "s_connection_address.clear" not in connected
    assert "s_connection_addr_type =" not in connected

    disconnect = manager[manager.index("case BLE_GAP_EVENT_DISCONNECT:"):]
    assert "event->disconnect.conn.conn_handle == s_connection_conn" in disconnect
    assert "token == s_connection_gap_token || token == s_connection_token" in disconnect
    assert "s_connection_token == s_dispatch.active_connection_token()" in disconnect
    assert "s_dispatch.publish_disconnected(s_connection_token)" in disconnect

    # Once BLE releases ownership, rendering returns to the local prompt and
    # the ordinary local/Wi-Fi/SSH paths remain in the same UI.
    rendered = body(ui, "std::string get_rendered_output")
    assert 's_local_shell.cwd() + "$ "' in rendered
    assert "s_ble_model.owns_input()" in rendered
    terminal = body(ui, "void render_terminal()")
    assert "s_wifi_ui_state" in terminal
    assert "ssh_client_get_state" in terminal
    execute = body(session, "void session::execute_line(bool line_already_sent)")
    assert "CYBERDECK_CMD_WIFI" in execute
    assert "host_.ssh_send_data" in execute
    assert "local_shell().execute" in execute

    print("PASS: BLE connection lifecycle/token identity and UI availability contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
