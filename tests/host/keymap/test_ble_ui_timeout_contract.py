#!/usr/bin/env python3
"""Regression contract for the BLE UI timeout/render gate."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"


def function_body(source: str, signature: str) -> str:
    marker = source.find(signature)
    if marker < 0:
        raise AssertionError(f"missing function {signature!r}")
    opening = source.find("{", marker)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated function {signature!r}")


def main() -> int:
    body = function_body(UI.read_text(encoding="utf-8"), "void process_ble_events(")
    changed = "bool changed = s_ble_model.owns_input();"
    advance = "s_ble_model.advance_time(100);"
    event_loop = "while (s_ble_event_queue != nullptr &&"
    assert body.count(changed) == 1, "render gate needs one pre-advance ownership snapshot"
    assert body.count(advance) == 1, "every BLE timer tick must advance the model exactly once"
    changed_at = body.index(changed)
    advance_at = body.index(advance)
    loop_at = body.index(event_loop)
    assert changed_at < advance_at < loop_at, "ownership snapshot, unconditional advance, then queue delivery"
    advance_line = body[body.rfind("\n", 0, advance_at) + 1:body.find("\n", advance_at)]
    changed_line = body[body.rfind("\n", 0, changed_at) + 1:body.find("\n", changed_at)]
    assert advance_line.strip() == advance, "advance_time must remain a direct statement"
    assert len(advance_line) - len(advance_line.lstrip()) == len(changed_line) - len(changed_line.lstrip()), (
        "deadline advancement must not be nested under the ownership snapshot"
    )
    after_loop = body[loop_at:]
    final_gate = after_loop.find("if (changed)")
    assert final_gate >= 0, "queued model changes need a final changed gate"
    rendered = after_loop[final_gate:]
    assert rendered.count("render_terminal();") == 1, "changed model state must render exactly once"
    assert rendered.index("ble_submit_actions();") < rendered.index("render_terminal();"), (
        "actions must be submitted before rendering the changed snapshot"
    )
    print("PASS: BLE UI timeout/render gate contract")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error!r}")
        sys.exit(1)
