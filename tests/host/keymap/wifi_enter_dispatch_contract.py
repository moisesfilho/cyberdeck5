#!/usr/bin/env python3
"""Regression contract for Wi-Fi menu Enter dispatch."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"


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
    async_consumer = function_body(source, "void process_keyboard_event_async(")
    terminal_changed = function_body(source, "void terminal_changed(")
    local_key = function_body(source, "void local_key(uint32_t key)")

    # Enter must reach the stateful Wi-Fi menu handler instead of executing an
    # empty shell line. Password entry remains handled by execute_line().
    require("local_key(event->special_key)" in async_consumer,
            "physical special keys must reach local_key")
    require("if (event->special_key == LV_KEY_ENTER) execute_line(false);" not in async_consumer,
            "physical Enter must not bypass local_key")
    require("local_key(LV_KEY_ENTER)" in terminal_changed,
            "virtual Enter must reach local_key")

    # The existing local handler is the single owner of menu selection.
    require("s_wifi_search_menu.selected_item()" in local_key,
            "search Enter must inspect the selected AP")
    require("s_wifi_saved_menu.selected_ssid()" in local_key,
            "saved-menu Enter must inspect the selected SSID")
    require("s_wifi_ui_state == wifi_ui_state_t::SEARCH_PASSWORD" in local_key,
            "password state must retain its cancellation path")

    print("PASS: Wi-Fi Enter dispatch contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, ValueError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
