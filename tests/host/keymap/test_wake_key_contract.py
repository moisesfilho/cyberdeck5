#!/usr/bin/env python3
"""Structural contract for REQ-WAKE-KEY-01/02 / AC-WAKE-KEY-01/02/03.

When the screen is off, any physical key must wake it WITHOUT executing the
key's normal action.  When the screen is active, the key must flow through
the normal UI path unchanged.

The UI is not linkable on host (LVGL/BSP/NVS/FreeRTOS); this contract inspects
the real production source and verifies the guard clause is present, ordered,
and returns before any action dispatch.
"""
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"


def function_body(source: str, signature: str) -> str:
    """Return the body of a function, skipping forward declarations."""
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing function: {signature}")
    opening = source.find("{", start)
    semicolon = source.find(";", start)
    if semicolon >= 0 and semicolon < opening:
        # Forward declaration; find the next occurrence
        next_start = source.find(signature, semicolon)
        if next_start < 0:
            raise AssertionError(f"no body for: {signature}")
        opening = source.find("{", next_start)
        start = next_start
    if opening < 0:
        raise AssertionError(f"missing body for: {signature}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unclosed function: {signature}")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    ui = UI.read_text(encoding="utf-8")

    consumer = function_body(ui, "void on_keyboard_event(")

    # AC-WAKE-KEY-01: any physical key wakes the screen when it is off.
    require("screen_off_is_active()" in consumer,
            "on_keyboard_event must check screen_off_is_active()")
    require("screen_off_turn_on()" in consumer,
            "screen_off_turn_on() must be called when screen is off")

    # AC-WAKE-KEY-02: the key must NOT execute its normal action.
    # The return must appear before any dispatch to local_key or insert_physical.
    guard_start = consumer.find("screen_off_is_active()")
    require(guard_start >= 0,
            "screen_off_is_active() guard must be present")
    return_pos = consumer.find("return", guard_start)
    local_key_pos = consumer.find("local_key(", guard_start)
    insert_text_pos = consumer.find("insert_physical_text(", guard_start)
    require(return_pos >= 0,
            "wake guard must return without dispatching the key")
    if local_key_pos >= 0:
        require(return_pos < local_key_pos,
                "return must precede local_key dispatch")
    if insert_text_pos >= 0:
        require(return_pos < insert_text_pos,
                "return must precede insert_physical_text dispatch")

    # AC-WAKE-KEY-03: when the screen is active, the normal path must remain.
    # The normal dispatch code must exist AFTER the guard block.
    normal_section = consumer[guard_start:]
    require("local_key(" in normal_section or "insert_physical_text(" in normal_section,
            "normal key dispatch must remain after the wake guard")

    # The guard must be the FIRST actionable statement in the function (no
    # preceding code that could consume the key before the screen wake check).
    # Allow only whitespace, the opening brace, and comments before the guard.
    prefix = consumer[:guard_start]
    # Strip C-style block comments
    prefix_no_comments = re.sub(r"/\*.*?\*/", "", prefix, flags=re.S)
    stripped = prefix_no_comments.strip()
    # The guard is `if (screen_off_is_active())`; allow only whitespace,
    # comments, and the opening `if (` before the guard.
    require(stripped == "" or stripped == "if (",
            "screen_off_is_active() must be the first actionable statement")

    print("PASS: wake-key guard contract (REQ-WAKE-KEY-01/02)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
