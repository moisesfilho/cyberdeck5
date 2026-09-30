#!/usr/bin/env python3
"""Structural contract for UI resource cleanup and partial initialization."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"


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
    source = UI.read_text(encoding="utf-8")
    cleanup = function_body(source, "void destroy_ui_resource_handles(")
    for handle, deleter in (
        ("s_wifi_scan_context_mutex", "vSemaphoreDelete"),
        ("s_wifi_scan_queue", "vQueueDelete"),
        ("s_wifi_state_queue", "vQueueDelete"),
        ("s_ble_event_queue", "vQueueDelete"),
    ):
        block = re.search(rf"if\s*\(\s*{handle}\s*!=\s*nullptr\s*\)\s*\{{(?P<body>.*?)\}}",
                          cleanup, re.S)
        require(block is not None, f"cleanup must guard {handle}")
        if block is not None:
            body = block.group("body")
            require(deleter in body, f"cleanup must delete {handle}")
            require(re.search(rf"{handle}\s*=\s*nullptr\s*;", body) is not None,
                    f"cleanup must zero {handle}")
    require("s_keyboard_dispatch.stop();" in cleanup,
            "cleanup must stop the keyboard dispatcher")

    init = function_body(source, "extern \"C\" esp_err_t cyberdeck_ui_init(")
    require("s_keyboard_dispatch.start(on_keyboard_event, nullptr)" in init,
            "init must start the keyboard dispatcher")
    require(init.count("return ESP_ERR_NO_MEM") == 5,
            "init must retain one failure exit for each resource/timer phase")
    for handle in ("s_wifi_state_queue", "s_wifi_scan_queue",
                   "s_wifi_scan_context_mutex", "s_ble_event_queue"):
        failure = re.search(rf"if\s*\([^)]*{handle}\s*==\s*nullptr[^)]*\)\s*\{{(?P<body>.*?)\}}",
                            init, re.S)
        require(failure is not None, f"{handle} allocation must have a failure branch")
        if failure is not None:
            require("destroy_ui_resource_handles()" in failure.group("body"),
                    f"{handle} failure must clean prior resources")
    timer_failure = re.search(
        r"if\s*\(\s*s_terminal_output_timer\s*==\s*nullptr\s*\)\s*\{(?P<body>.*?)\}",
        init,
        re.S,
    )
    require(timer_failure is not None,
            "terminal output timer allocation must have a failure branch")
    if timer_failure is not None:
        require("destroy_ui_resource_handles()" in timer_failure.group("body"),
                "timer failure must clean prior resources")

    print("PASS: UI partial-allocation cleanup and keyboard-dispatch contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, ValueError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
