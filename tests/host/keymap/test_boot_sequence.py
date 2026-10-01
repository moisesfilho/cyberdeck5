#!/usr/bin/env python3
"""Structural host-side contracts for SD/UI boot ordering.

This deliberately does not compile app_main.cpp: doing so would duplicate
ESP-IDF/BSP definitions instead of testing the real integration point.
"""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[3]
APP_MAIN = ROOT / "main" / "app_main.cpp"
UI_SOURCE = ROOT / "components" / "cyberdeck" / "src" / "platform" / "display" / "cyberdeck_ui.cpp"
LOCAL_SHELL_HEADER = ROOT / "components" / "cyberdeck" / "include" / "apps" / "shell" / "cyberdeck_local_shell.h"


def check(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    app = APP_MAIN.read_text(encoding="utf-8")
    ui = UI_SOURCE.read_text(encoding="utf-8")
    local_shell_header = LOCAL_SHELL_HEADER.read_text(encoding="utf-8")
    failures: list[str] = []

    mount_calls = list(re.finditer(r"\bbsp_sdcard_mount\s*\(\s*\)", app))
    event_init = list(re.finditer(r"\bevent_log_init\s*\(\s*\)", app))
    handle_check = re.search(
        r"if\s*\(\s*bsp_sdcard_get_handle\s*\(\s*\)\s*==\s*nullptr\s*\)\s*return\s*;",
        app,
    )

    check(len(mount_calls) == 1, "app_main must contain exactly one explicit bsp_sdcard_mount() call", failures)
    check(len(event_init) == 1, "app_main must contain exactly one event_log_init() call", failures)
    if mount_calls and event_init:
        check(
            mount_calls[0].start() < event_init[0].start(),
            "bsp_sdcard_mount() must precede event_log_init() in app_main",
            failures,
        )
    check(handle_check is not None, "app_main must retain the null BSP SD-card handle check", failures)
    if mount_calls and handle_check:
        check(
            mount_calls[0].end() < handle_check.start(),
            "the BSP SD-card handle must be checked after mounting",
            failures,
        )

    ui_init = list(re.finditer(r"\bcyberdeck_ui_init\s*\(\s*\)", app))
    check(len(ui_init) == 1, "app_main must contain exactly one cyberdeck_ui_init() call", failures)

    shell_ctor = re.search(
        r"cyberdeck_local_shell\s+s_local_shell\s*\(\s*['\"]/sdcard['\"]"
        r"(?:\s*,\s*['\"]/['\"]\s*)?\)",
        ui,
    )
    check(shell_ctor is not None,
          "the terminal shell must retain /sdcard as its physical host root",
          failures)
    check(re.search(r'virtual_root\s*=\s*"/"', local_shell_header) is not None,
          "the public shell contract must default the virtual root to /",
          failures)

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("PASS: SD/UI boot ordering, physical /sdcard mount, and virtual / root contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
