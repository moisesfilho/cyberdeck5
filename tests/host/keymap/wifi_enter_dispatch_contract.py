#!/usr/bin/env python3
"""Regression contract for Wi-Fi menu Enter dispatch."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
SESSION = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def function_body(source: str, signature: str) -> str:
    start = 0
    while True:
        start = source.find(signature, start)
        require(start >= 0, f"function not found: {signature}")
        opening = source.find("{", start)
        semi = source.find(";", start)
        if semi < 0 or semi > opening:
            break
        start = semi + 1
    require(opening >= 0, f"opening brace not found: {signature}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unclosed function: {signature}")


def main() -> int:
    source = UI.read_text(encoding="utf-8")
    session = SESSION.read_text(encoding="utf-8")
    async_consumer = function_body(source, "void on_keyboard_event(")
    terminal_changed = function_body(source, "void terminal_changed(")
    # Menu selection now lives in the extracted session key handler; the UI
    # facade only translates LVGL key codes and delegates.
    local_key = function_body(session, "void session::handle_key(")

    # Enter must reach the stateful Wi-Fi menu handler instead of executing an
    # empty shell line. Password entry remains handled by execute_line().
    require("local_key(special_key)" in async_consumer,
            "physical special keys must reach local_key")
    require("if (event->special_key == LV_KEY_ENTER) execute_line(false);" not in async_consumer,
            "physical Enter must not bypass local_key")
    require("local_key(LV_KEY_ENTER)" in terminal_changed,
            "virtual Enter must reach local_key")

    # The session key handler is the single owner of menu selection, and the
    # UI facade must delegate to it.
    require("s_shell_session.handle_key(translate_session_key(key))" in source,
            "UI local_key facade must delegate to the extracted session handler")
    require("wifi_search_menu().selected_item()" in local_key,
            "search Enter must inspect the selected AP")
    require("wifi_saved_menu().selected_ssid()" in local_key,
            "saved-menu Enter must inspect the selected SSID")
    require("wifi_state == wifi_ui_state_t::SEARCH_PASSWORD" in local_key,
            "password state must retain its cancellation path")

    # Connection start must keep the two tokens distinct. The manager callback
    # is matched against the token reported by wifi_mgr, while the model token
    # identifies the attempt; conflating them strands the UI in CONNECTING
    # because later status callbacks no longer match.
    connect = function_body(session, "bool session::begin_wifi_connection(")
    require("wifi_model_connection_token_ = model_token;" in connect,
            "the model connection token must be the attempt token")
    require("wifi_connection_token_ = host_.wifi_current_token();" in connect,
            "the connection token must come from the manager, not the model token")
    pump = function_body(source, "void process_wifi_state(")
    require("status->connection_token == s_shell_session.wifi_connection_token()" in pump,
            "manager status must be matched against the manager-reported token")
    require("s_wifi_model.active_connection_token() == s_shell_session.wifi_model_connection_token()" in pump,
            "the model token must be matched against the session attempt token")
    require("invalidate_wifi_connection()" in connect,
            "a refused connection must clear both tokens")

    print("PASS: Wi-Fi Enter dispatch contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, ValueError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
