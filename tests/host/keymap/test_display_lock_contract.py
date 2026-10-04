#!/usr/bin/env python3
"""Structural concurrency contracts for boot and Serial-JTAG display access."""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
APP = ROOT / "main/app_main.cpp"
BRIDGE = ROOT / "components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp"


def function_body(source: str, name: str) -> str:
    start = source.index(name)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:index]
    raise AssertionError(f"unclosed function: {name}")


def main() -> int:
    app = function_body(APP.read_text(encoding="utf-8"), "app_main")
    source = BRIDGE.read_text(encoding="utf-8")
    capture = function_body(source, "bool capture_screen(capture &out)")
    failures: list[str] = []

    def require(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    lock = re.search(r"if\s*\(\s*!bsp_display_lock\s*\(\s*pdMS_TO_TICKS\(\s*1000\s*\)\s*\)\s*\)", app)
    require(lock is not None, "boot must fail closed when the display lock is unavailable")
    if lock:
        protected_start = min(
            (pos for token in ("imu_reader_start(", "cyberdeck_ui_init(", "screen_off_init(")
             if (pos := app.find(token)) >= 0),
            default=len(app),
        )
        require(lock.start() < protected_start, "boot lock guard must precede display startup")
        require("return;" in app[lock.start():protected_start], "boot lock failure must return")

    lock_at = capture.find("bsp_display_lock(")
    unlock_at = capture.rfind("bsp_display_unlock()")
    require(lock_at >= 0 and unlock_at > lock_at, "screen capture must unlock after protected work")
    require("if (!bsp_display_lock" in capture, "screen capture must handle lock failure")
    for token in ("lv_snapshot_take(", "out.snap->header", "out.snap->data", "lv_draw_buf_destroy("):
        at = capture.find(token)
        require(lock_at < at < unlock_at, f"{token} must execute while display lock is held")
    destroys = [m.start() for m in re.finditer(r"lv_draw_buf_destroy\s*\(", capture)]
    require(destroys, "screen capture must destroy every snapshot")
    require(all(pos < unlock_at for pos in destroys), "snapshot destruction must remain under display lock")
    require(capture.find("std::memcpy(out.packed.data()") < capture.rfind("lv_draw_buf_destroy("),
            "snapshot copy must complete before destruction")

    ui_dump = function_body(source, 'if (req.type == "ui.dump")')
    require(ui_dump.count("bsp_display_lock(") == 1, "ui.dump must acquire one display lock")
    require(ui_dump.count("bsp_display_unlock()") == 1, "ui.dump must release its display lock")
    require(ui_dump.find("dump_obj(") < ui_dump.find("bsp_display_unlock()"),
            "ui.dump traversal must precede unlock")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("PASS: display lock, fail-closed boot, snapshot lifetime, and LVGL access")
    return 0


if __name__ == "__main__":
    sys.exit(main())
