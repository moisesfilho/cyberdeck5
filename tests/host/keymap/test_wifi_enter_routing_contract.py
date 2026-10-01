#!/usr/bin/env python3
"""Regression contract for Enter routing in the `wifi search` flow (host).

The ESP-IDF/LVGL UI is not host-linkable, so - following the project's
structural-contract pattern - this test inspects the real extracted session
(cyberdeck_shell_session.cpp) and locks the pure routing decision that Enter
follows:

1. SEARCH_SELECT / SAVED_SELECT / SAVED_CONFIRM -> LOCAL selection: the state
   block inside handle_key() consumes the Enter key and returns before the
   single execute_line() tail; it never delegates to execute_line and performs
   menu-local work only.
2. SEARCH_PASSWORD -> password send: handle_key() does NOT consume Enter for
   that state (ESC only), so Enter falls through to execute_line(), whose
   SEARCH_PASSWORD branch sends the password through begin_wifi_connection()
   and returns before any shell dispatch.
3. No empty command: execute_line() rejects a blank line via
   find_first_not_of(" \\t") with an early return, before
   local_shell().execute() / cyberdeck_parse_command().

This test only reads production source; it changes nothing.
"""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
SESSION = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp"

# Enter in these states must be a menu-local selection, never line execution.
LOCAL_SELECTION_STATES = ("SEARCH_SELECT", "SAVED_SELECT", "SAVED_CONFIRM")

# Enter in this state must fall through to execute_line() to send the password.
PASSWORD_STATE = "SEARCH_PASSWORD"

# Tokens each selection state's Enter branch must perform (local work).
ENTER_LOCAL_TOKENS = {
    "SEARCH_SELECT": ("wifi_search_menu().selected_item()",),
    "SAVED_SELECT": ("wifi_saved_menu().selected_ssid()", "SAVED_CONFIRM"),
    "SAVED_CONFIRM": ("host_.wifi_forget(", "wifi_saved_menu().remove_selected()"),
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def function_body(source: str, signature: str) -> str:
    """Extract a function definition body, skipping forward declarations."""
    pos = 0
    while True:
        start = source.find(signature, pos)
        if start == -1:
            raise AssertionError(f"function not found: {signature}")
        opening = source.find("{", start)
        if opening == -1:
            raise AssertionError(f"opening brace not found: {signature}")
        semi = source.find(";", start)
        if semi != -1 and semi < opening:
            pos = semi + 1
            continue
        depth = 0
        for index in range(opening, len(source)):
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
                if depth == 0:
                    return source[opening + 1:index]
        raise AssertionError(f"unclosed function: {signature}")


def brace_block(text: str, open_at: int) -> str:
    depth = 0
    for index in range(open_at, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[open_at + 1:index]
    raise AssertionError("unclosed brace block")


def state_block(text: str, state: str) -> tuple:
    """Locate `if (wifi_state == wifi_ui_state_t::STATE) { ... }`."""
    pattern = re.compile(
        rf"(?:else\s+)?if\s*\(\s*wifi_state\s*==\s*wifi_ui_state_t::{state}\s*\)")
    match = pattern.search(text)
    require(match is not None, f"missing branch for state {state}")
    open_at = text.find("{", match.end())
    require(open_at != -1, f"missing body for state {state}")
    return match.start(), brace_block(text, open_at)


def enter_branch(body: str, state: str) -> str:
    match = re.search(r"pressed\s*==\s*key::enter", body)
    require(match is not None, f"{state} must handle Enter")
    open_at = body.find("{", match.end())
    require(open_at != -1, f"{state} Enter branch must have a body")
    return brace_block(body, open_at)


def local_enter_routing(session: str) -> None:
    local = function_body(session, "void session::handle_key(")

    # The only path from Enter to line execution is a single fall-through tail.
    tails = list(re.finditer(
        r"if\s*\(\s*pressed\s*==\s*key::enter\s*\)\s*execute_line\s*\(\s*\)\s*;", local))
    require(len(tails) == 1, "handle_key must reach execute_line from exactly one Enter tail")
    require(local.count("execute_line(") == 1,
            "execute_line must be called only by the Enter tail")
    tail_index = tails[0].start()

    # 1) Selection states: handled locally, before the tail, and they return.
    for state in LOCAL_SELECTION_STATES:
        start, body = state_block(local, state)
        require(start < tail_index, f"{state} must be handled before the execute_line tail")
        require("execute_line" not in body,
                f"{state} must not delegate Enter to execute_line")
        branch = enter_branch(body, state)
        require("return;" in branch, f"{state} Enter must return and stay local")
        for token in ENTER_LOCAL_TOKENS[state]:
            require(token in branch,
                    f"{state} Enter must perform local selection ({token})")

    # 2) SEARCH_PASSWORD: handle_key keeps only ESC; Enter falls through.
    pwd_index, pwd_body = state_block(local, PASSWORD_STATE)
    require(pwd_index < tail_index,
            "SEARCH_PASSWORD must be dispatched before the Enter tail")
    require(re.search(r"pressed\s*==\s*key::enter", pwd_body) is None,
            "SEARCH_PASSWORD must not consume Enter in handle_key")
    require(re.search(r"pressed\s*==\s*key::esc", pwd_body) is not None,
            "SEARCH_PASSWORD must keep ESC cancel in handle_key")


def password_and_blank_routing(session: str) -> None:
    execute = function_body(session, "void session::execute_line(")

    # 2) SEARCH_PASSWORD sends the password and returns before any shell work.
    pwd_index, pwd_body = state_block(execute, PASSWORD_STATE)
    require(
        "begin_wifi_connection(selected_ap_ssid_.c_str(), line.c_str())" in pwd_body,
        "SEARCH_PASSWORD must send SSID + entered password to the connection")
    require("wipe_string(line)" in pwd_body,
            "SEARCH_PASSWORD must wipe the password after sending")
    require(re.search(r"\breturn\s*;", pwd_body) is not None,
            "SEARCH_PASSWORD must return after sending")

    shell_at = execute.find("local_shell().execute(line)")
    require(shell_at != -1, "execute_line must keep the local shell dispatch")
    parse_at = execute.find("cyberdeck_parse_command(line.c_str())")
    require(parse_at != -1, "execute_line must keep command parsing")
    require(pwd_index < shell_at, "SEARCH_PASSWORD must send before the shell dispatch")

    # 3) A blank line returns before any command can be executed.
    # `line` is the local entered-payload copy that is wiped after the guard.
    guard_needle = 'line.find_first_not_of(" \\t")'
    guard_at = execute.find(guard_needle)
    require(guard_at != -1, "execute_line must reject blank input")
    guard_open = execute.find("{", guard_at)
    require(guard_open != -1, "blank-input guard must have a body")
    require(re.search(r"\breturn\s*;", brace_block(execute, guard_open)) is not None,
            "blank-input guard must return early")
    require(guard_at < shell_at, "blank input must be rejected before shell execution")
    require(guard_at < parse_at, "blank input must be rejected before command parsing")


def ui_delegation(ui: str) -> None:
    local_key_def = re.search(
        r"void\s+local_key\s*\(\s*uint32_t\s+\w+\s*\)\s*\{[^}]*\}", ui)
    require(local_key_def is not None and
            "s_shell_session.handle_key(translate_session_key(key))" in local_key_def.group(0),
            "UI local_key facade must delegate to the extracted session key handler")


def main() -> int:
    try:
        ui = UI.read_text(encoding="utf-8")
        session = SESSION.read_text(encoding="utf-8")
        ui_delegation(ui)
        local_enter_routing(session)
        password_and_blank_routing(session)
    except (AssertionError, OSError, ValueError) as error:
        print(f"FAIL: {error}")
        return 1
    print("PASS: wifi search Enter routing (local selection / password send / no empty command)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
