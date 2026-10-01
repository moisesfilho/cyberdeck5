#!/usr/bin/env python3
"""Structural contract for the bounded physical-keyboard handoff."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
SESSION = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp"
DISPATCH = ROOT / "components/cyberdeck/src/platform/input/cyberdeck_keyboard_dispatch.cpp"


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
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
    dispatch = DISPATCH.read_text(encoding="utf-8")
    submit = function_body(dispatch, "void dispatcher::submit(")
    process = function_body(dispatch, "void process_async(")
    discard = function_body(dispatch, "void discard_from_queue(")

    require("constexpr UBaseType_t k_queue_capacity = 8" in dispatch,
            "keyboard handoff must declare an explicit capacity of 8")
    require("xQueueCreate(k_queue_capacity, sizeof(event *))" in dispatch,
            "keyboard handoff must use a bounded pointer queue")
    require("xQueueSend(s_queue, &pending, 0)" in submit,
            "keyboard handoff must enqueue snapshots")
    require("free(pending)" in submit,
            "rejected snapshots must be released")
    require("lv_async_call(process_async, nullptr)" in submit,
            "physical input must use lv_async_call")
    require("xSemaphoreTake(s_mutex" in submit and
            "xSemaphoreGive(s_mutex)" in submit,
            "producer must serialize enqueue and async rollback")
    require("discard_from_queue(pending)" in submit,
            "async scheduling failure must remove the queued snapshot")
    require("xQueueReceive(s_queue" in discard and
            "pending_count < k_queue_capacity" in discard and
            "if (pending[i] == rejected) continue" in discard,
            "rollback must preserve bounded FIFO survivors")
    require("xQueueReceive(s_queue" in process and
            "xSemaphoreGive(s_mutex)" in process and
            "free(pending)" in process,
            "consumer must release the mutex before UI callback and cleanup")
    require("bsp_display_lock" not in process and "bsp_display_unlock" not in process,
            "async consumer must not take a nested display lock")

    producer = function_body(ui, "extern \"C\" void cyberdeck_keyboard_input(")
    require("s_keyboard_dispatch.submit" in producer and
            "execute_line" not in producer and "render_terminal" not in producer,
            "public physical input must only submit a snapshot")
    consumer = function_body(ui, "void on_keyboard_event(")
    require("local_key(special_key)" in consumer,
            "special keys must remain routed through the UI local_key facade")
    require("s_shell_session.insert_physical_text(text, length)" in consumer,
            "physical text must be routed through the session, which owns the decision")
    require("s_shell_session.insert_modified_key(" in consumer,
            "a modified physical key must be routed through the session")
    require("insert_physical(" not in consumer and "insert_virtual(" not in consumer,
            "the UI facade must not decide how text or a modified key is interpreted")
    session_source = (ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp").read_text(encoding="utf-8")
    require("insert_physical_text" in session_source and
            "insert_modified_key" in session_source,
            "the session must implement both physical input paths")
    # The facade definition must delegate to the session key handler; the
    # forward declaration (which has no body) is skipped explicitly.
    local_key_def = re.search(
        r"void\s+local_key\s*\(\s*uint32_t\s+\w+\s*\)\s*\{[^}]*\}", ui)
    require(local_key_def is not None and
            "s_shell_session.handle_key(translate_session_key(key))" in local_key_def.group(0),
            "local_key must delegate to the extracted session key handler")

    terminal_changed = function_body(ui, "void terminal_changed(")
    require("local_key(LV_KEY_ENTER)" in terminal_changed,
            "virtual Enter must remain routed through local_key")
    session = SESSION.read_text(encoding="utf-8")
    for required in ("virtual_keyboard_changed", "terminal_insert", "ssh_client_connect"):
        require(required in ui, f"existing UI input path missing: {required}")
    # The shell command and local execution moved to the extracted session; the
    # UI keeps only the SSH host seam.
    require("local_shell().execute(line)" in session and "cat_enqueue(" in session,
            "existing shell/local input path missing from the extracted session")

    print("PASS: keyboard async-dispatch contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, ValueError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
