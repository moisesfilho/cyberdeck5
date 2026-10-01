#!/usr/bin/env python3
"""Host-side contract and regression tests for Wi-Fi Enter dispatch and lifecycle.

Validates that:
1. Physical special key dispatch routes through local_key without bypassing Enter.
2. Virtual Enter in terminal_changed routes through local_key.
3. The extracted session key handler contains proper handling for SEARCH_SELECT
   (open, saved, protected networks), SEARCH_PASSWORD, CONNECTING, SAVED_SELECT,
   and SAVED_CONFIRM.
4. Memory sanitization (zeroing secrets) is enforced across connection and
   cancellation paths.
"""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
SESSION = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp"


def function_body(source: str, signature: str) -> str:
    start = 0
    while True:
        start = source.find(signature, start)
        if start < 0:
            raise AssertionError(f"function not found: {signature}")
        opening = source.find("{", start)
        semi = source.find(";", start)
        if semi < 0 or semi > opening:
            break
        start = semi + 1
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unclosed function: {signature}")


def state_body(function: str, state: str) -> str:
    marker = f"wifi_state == wifi_ui_state_t::{state}"
    condition = function.find(marker)
    require(condition >= 0, f"missing state branch: {state}")
    opening = function.find("{", condition)
    require(opening >= 0, f"missing state body: {state}")
    depth = 0
    for index in range(opening, len(function)):
        if function[index] == "{":
            depth += 1
        elif function[index] == "}":
            depth -= 1
            if depth == 0:
                return function[opening + 1:index]
    raise AssertionError(f"unclosed state branch: {state}")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def test_dispatch_routing(source: str) -> None:
    consumer = function_body(source, "void on_keyboard_event(")
    consumer_clean = re.sub(r"/\*.*?\*/|//[^\n]*", "", consumer, flags=re.S)

    # Physical dispatch must route all special keys through local_key
    require("local_key(special_key)" in consumer_clean,
            "physical async consumer must route special keys to local_key")
    require("if (event->special_key == LV_KEY_ENTER) execute_line" not in consumer_clean,
            "physical async consumer must not bypass local_key for LV_KEY_ENTER")

    # Virtual Enter dispatch in terminal_changed must also route through local_key
    term_changed = function_body(source, "void terminal_changed(")
    require("local_key(LV_KEY_ENTER)" in term_changed,
            "terminal_changed must route virtual Enter to local_key(LV_KEY_ENTER)")


def test_wifi_search_select_contract(session: str) -> None:
    handle_key = function_body(session, "void session::handle_key(")

    # SEARCH_SELECT navigation & actions
    search_body = state_body(handle_key, "SEARCH_SELECT")

    # Up/Down navigation
    require("wifi_search_menu().move_up()" in search_body, "SEARCH_SELECT must handle UP key")
    require("wifi_search_menu().move_down()" in search_body, "SEARCH_SELECT must handle DOWN key")

    # ESC cancels search
    require("pressed == key::esc" in search_body and
            "wifi_state = wifi_ui_state_t::IDLE" in search_body,
            "SEARCH_SELECT must handle ESC to cancel and return to IDLE")

    # Enter selection
    require("pressed == key::enter" in search_body, "SEARCH_SELECT must handle Enter")
    require("wifi_search_menu().selected_item()" in search_body,
            "SEARCH_SELECT Enter must inspect selected item")

    # Open network connection
    require("ap->is_open" in search_body and 'begin_wifi_connection(ap->ssid, "")' in search_body,
            "SEARCH_SELECT Enter on open AP must connect without password")

    # Saved network connection
    require("wifi_storage_find(ap->ssid" in search_body,
            "SEARCH_SELECT Enter on protected AP must check saved credentials")
    require("wipe_bytes(saved_pwd" in search_body,
            "SEARCH_SELECT Enter must sanitize saved_pwd after starting connection")

    # Protected new network password prompt
    require("wifi_ui_state_t::SEARCH_PASSWORD" in search_body,
            "SEARCH_SELECT Enter on unauthenticated protected AP must transition to SEARCH_PASSWORD")
    require("clear_editor()" in search_body,
            "SEARCH_SELECT password prompt must clear editor and line buffers")


def test_wifi_password_and_connecting_contract(session: str) -> None:
    handle_key = function_body(session, "void session::handle_key(")
    execute_fn = function_body(session, "void session::execute_line(")

    # SEARCH_PASSWORD ESC cancel
    pwd_body = state_body(handle_key, "SEARCH_PASSWORD")
    require("pressed == key::esc" in pwd_body and "wifi_state = wifi_ui_state_t::IDLE" in pwd_body,
            "SEARCH_PASSWORD must handle ESC to cancel")
    require("clear_editor()" in pwd_body,
            "SEARCH_PASSWORD cancellation must wipe secret from the session line")
    require("invalidate_wifi_connection()" in pwd_body,
            "SEARCH_PASSWORD cancellation must clear the connection tokens")

    # SEARCH_PASSWORD Enter submits password in execute_line
    exec_body = state_body(execute_fn, "SEARCH_PASSWORD")
    require("begin_wifi_connection(selected_ap_ssid_.c_str(), line.c_str())" in exec_body,
            "SEARCH_PASSWORD execution must pass selected SSID and entered password to connection")
    require("wipe_string(line)" in exec_body,
            "SEARCH_PASSWORD execution must wipe entered password string")

    # CONNECTING state handling
    require("wifi_state == wifi_ui_state_t::CONNECTING && pressed == key::esc" in handle_key,
            "CONNECTING state must support ESC cancellation")
    require("host_.wifi_cancel_connection()" in handle_key,
            "CONNECTING ESC must cancel through the host port")
    require("wifi_mgr_" not in handle_key,
            "the session must not call the Wi-Fi manager directly")
    require("invalidate_wifi_connection()" in handle_key,
            "CONNECTING ESC must invalidate both connection tokens")


def test_saved_networks_menu_contract(session: str) -> None:
    handle_key = function_body(session, "void session::handle_key(")

    # SAVED_SELECT transitions to SAVED_CONFIRM on Enter
    require("wifi_state = wifi_ui_state_t::SAVED_CONFIRM" in handle_key,
            "SAVED_SELECT on Enter must transition to SAVED_CONFIRM")

    # SAVED_CONFIRM forget action on Enter
    require("host_.wifi_forget(" in handle_key,
            "SAVED_CONFIRM on Enter must forget through the host port")

    # An unexpected connect action may still carry a password, so the early
    # return must wipe the taken actions instead of dropping them.
    connect = function_body(session, "bool session::begin_wifi_connection(")
    early = connect[connect.index("take_actions()"):connect.index("const std::uint64_t model_token")]
    require("wipe_wifi_actions(actions)" in early,
            "an unexpected connect action must have its password wiped")
    require("take_actions" in early and "return false" in early,
            "begin_wifi_connection must keep the guarded early return")


def test_ui_delegates_to_session(source: str) -> None:
    local_key_def = re.search(
        r"void\s+local_key\s*\(\s*uint32_t\s+\w+\s*\)\s*\{[^}]*\}", source)
    require(local_key_def is not None and
            "s_shell_session.handle_key(translate_session_key(key))" in local_key_def.group(0),
            "UI local_key facade must delegate to the extracted session key handler")


def main() -> int:
    source = UI.read_text(encoding="utf-8")
    session = SESSION.read_text(encoding="utf-8")
    test_dispatch_routing(source)
    test_ui_delegates_to_session(source)
    test_wifi_search_select_contract(session)
    test_wifi_password_and_connecting_contract(session)
    test_saved_networks_menu_contract(session)
    print("PASS: wifi event dispatch, menu selection, and security contracts")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, ValueError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
