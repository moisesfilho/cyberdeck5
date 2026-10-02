#!/usr/bin/env python3
"""Host-only contract for the BLE transient UI rendering seam.

The LVGL UI is not linkable on the host.  This contract therefore inspects the
real UI source for ownership/consolidation ordering and pairs with the pure
device-list tests for selection and the 18-entry boundary.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
SESSION = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp"
STATE = ROOT / "components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_state_machine.cpp"
EVENTS = ROOT / "components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_event_dispatch.cpp"


def function_body(source: str, signature: str) -> str:
    start = 0
    while True:
        start = source.find(signature, start)
        if start < 0:
            raise AssertionError(f"missing {signature}")
        opening = source.find("{", start)
        semicolon = source.find(";", start)
        if opening >= 0 and (semicolon < 0 or opening < semicolon):
            break
        start += len(signature)
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
    source = UI.read_text(encoding="utf-8")
    # Key dispatch moved to the extracted session; the LVGL render/timer seams
    # (list visibility, transient consolidation, event pump) stay in the UI.
    session = SESSION.read_text(encoding="utf-8")
    state_source = STATE.read_text(encoding="utf-8")
    events_source = EVENTS.read_text(encoding="utf-8")
    visible = function_body(source, "bool ble_list_is_visible()")
    sync = function_body(source, "void sync_ble_transient_block()")
    rendered = function_body(source, "std::string get_rendered_output(")
    terminal = function_body(source, "void render_terminal() {")
    key = function_body(session, "void session::handle_key(")
    process = function_body(source, "void process_ble_events(lv_timer_t *)")

    assert "current == cyberdeck_ble::screen::results" in visible
    assert "current == cyberdeck_ble::screen::paired" in visible
    assert "s_ble_model.owns_input()" in visible

    # Notices are appended once per text change; time still advances before
    # event processing, so navigation and deadlines remain active.
    assert "const std::string notice = s_ble_model.notice_text();" in process
    assert "notice != s_ble_last_notice" in process
    assert process.count("append_line(notice + \"\\n\")") == 1
    assert process.index("s_ble_model.advance_time(100)") < process.index("const std::string notice")
    assert "pressed == key::up" in key and "pressed == key::down" in key

    # While owned, the list is transient.  On the first release only, its last
    # model-backed representation is appended to history; repainting is not a
    # second history append.
    assert sync.index("s_ble_transient_active = true") < sync.index("return;")
    assert "if (!s_ble_transient_active) return;" in sync
    assert "if (s_ble_transient_committed) return;" in sync
    assert sync.count("append_output(rendered.data(), rendered.size())") == 1
    assert "append_line(s_ble_model.devices().render())" not in process

    # Every repaint reads the current model list, rather than a stale string or
    # the scan staging list.  device_list::render owns the selected marker.
    assert "s_ble_model.devices().render()" in rendered
    assert "get_rendered_output(view)" in terminal
    assert "s_ble_scan_devices.render()" not in source

    # Pairing/connecting/connected status is a repaint-only transient.  It is
    # deliberately added to the rendered snapshot, never to s_output/history;
    # otherwise every timer repaint would duplicate the line in the terminal.
    for screen in ("pairing", "connecting", "connected"):
        assert f"ble_screen == cyberdeck_ble::screen::{screen}" in rendered
    assert "output += s_ble_model.status_line();" in rendered
    assert rendered.index("output += s_ble_model.status_line();") < rendered.index(
        'output += "\\n";'
    )
    assert "append_line(s_ble_model.status_line()" not in source
    assert "s_output += s_ble_model.status_line()" not in source
    assert "s_history.add(s_ble_model.status_line()" not in source
    notice_body = state_source[state_source.index("std::string state_machine::notice_text()"):
                               state_source.index("std::string state_machine::status_line()")]
    assert "format_passkey" not in notice_body
    auth_log = events_source[events_source.rindex("case ble_event_kind::auth_request:"):
                            events_source.rindex("case ble_event_kind::pair_finished:")]
    assert "mask_passkey(event.passkey)" in auth_log
    assert "std::to_string(event.passkey)" not in auth_log

    # Only auth INPUT gets an editable passkey presentation, and it is rendered
    # as a temporary mask.  The buffer is zeroed whenever auth ownership ends;
    # no auth request is routed through notice/history/output persistence.
    auth_block = rendered[rendered.index("if (ble_screen == cyberdeck_ble::screen::auth"):]
    assert 'output += "Passkey input: ";' in auth_block
    assert "output.append(s_shell_app.ble_auth_input_size(), '*');" in auth_block
    assert "ble_screen == cyberdeck_ble::screen::auth &&" in auth_block
    assert "s_ble_model.pending_auth_action() == cyberdeck_ble::auth_io_action::input" in auth_block
    assert "clear_ble_auth_input()" in process
    assert "s_ble_auth_input" not in source, "the passkey buffer must belong to the session"
    assert "append_line(notice + \"\\n\")" in process
    auth_event = process[process.index("case BLE_MGR_EVT_AUTH_REQUEST:"):process.index("case BLE_MGR_EVT_PAIR_FINISHED:")]
    assert "append_line" not in auth_event
    assert "s_history.add" not in auth_event

    # Authentication INPUT has legitimate BACKSPACE/ENTER early returns before
    # the generic BLE key dispatch.  Its detailed contract is kept in
    # test_ble_auth_contract.py; this contract checks that auth routing remains
    # present, but scopes ordering assertions to the results/paired path.
    assert "current_screen() == cyberdeck_ble::screen::auth" in key
    assert "pending_auth_action() == cyberdeck_ble::auth_io_action::input" in key

    generic_ble_path = key[key.index("cyberdeck_ble::key ble_key;"):]
    # ENTER/UP/DOWN are routed to the state machine only while it owns input;
    # the model consequently resolves ENTER from its current selected item.
    assert "pressed == key::enter" in key
    assert "ble.press(ble_key)" in generic_ble_path
    assert generic_ble_path.index("ble.press(ble_key)") < generic_ble_path.index("return;")
    assert "ble.selected()" not in key

    # Ownership release must synchronize before the repaint, allowing the
    # local marker to return without disturbing Wi-Fi/shell/SSH routing.
    assert "host_.sync_ble_transient()" in key
    # Prompt composition belongs to the shell application, so the surface state
    # that suppresses the local marker is resolved next to the repaint.
    compose = function_body(source, "cyberdeck_shell_console::line_view compose_console_line()")
    assert "s_ble_model.owns_input()" in compose
    assert "surface.input_owned_elsewhere" in compose
    assert "compose_console_line()" in terminal
    assert "s_shell_app.compose_line(surface)" in compose
    assert "s_wifi_ui_state" in compose
    assert "service_ports::ssh_state" in compose
    assert "execute_line()" in key

    print("PASS: BLE transient UI rendering/ownership contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
