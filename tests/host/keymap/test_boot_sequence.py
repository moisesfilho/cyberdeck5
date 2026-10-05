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
SYSTEM_APPS_SOURCE = ROOT / "components" / "cyberdeck" / "src" / "apps" / "system" / "cyberdeck_system_apps.cpp"
RECOVERY_SOURCE = ROOT / "components" / "cyberdeck" / "src" / "apps" / "system" / "cyberdeck_recovery.cpp"
LOCAL_SHELL_HEADER = ROOT / "components" / "cyberdeck" / "include" / "apps" / "shell" / "cyberdeck_local_shell.h"


def check(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    app = APP_MAIN.read_text(encoding="utf-8")
    ui = UI_SOURCE.read_text(encoding="utf-8")
    system_apps = SYSTEM_APPS_SOURCE.read_text(encoding="utf-8")
    recovery = RECOVERY_SOURCE.read_text(encoding="utf-8")
    local_shell_header = LOCAL_SHELL_HEADER.read_text(encoding="utf-8")
    failures: list[str] = []

    mount_calls = list(re.finditer(r"\bbsp_sdcard_mount\s*\(\s*\)", app))
    event_init = list(re.finditer(r"\bevent_log_init\s*\(\s*\)", system_apps))
    handle_check = re.search(
        r"if\s*\(\s*bsp_sdcard_get_handle\s*\(\s*\)\s*==\s*nullptr\s*\)\s*return\s*;",
        app,
    )

    check(len(mount_calls) == 1, "app_main must contain exactly one explicit bsp_sdcard_mount() call", failures)
    check(len(event_init) == 1, "system apps must contain exactly one event_log_init() call", failures)
    check(handle_check is not None, "app_main must retain the null BSP SD-card handle check", failures)
    if mount_calls and handle_check:
        check(
            mount_calls[0].end() < handle_check.start(),
            "the BSP SD-card handle must be checked after mounting",
            failures,
        )
    logging_start = re.search(r"cyberdeck_system_apps_start_logging\s*\(\s*\)", app)
    check(logging_start is not None, "app_main must start logging through the supervisor", failures)
    if handle_check and logging_start:
        check(handle_check.end() < logging_start.start(),
               "the BSP SD-card handle must be checked before supervisor logging starts", failures)

    namespace = re.search(r'constexpr\s+char\s+k_namespace\[\]\s*=\s*"([^"]+)"', recovery)
    check(namespace is not None and namespace.group(1) == "cyberdeck_rec",
          "recovery must open the valid cyberdeck_rec NVS namespace", failures)
    check("nvs_flash_erase" not in app and "nvs_flash_erase" not in recovery,
          "boot recovery must not erase NVS", failures)

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

    boot_ready = app.index("bool boot_ready = true")
    commit_ready = app.index("cyberdeck_system_apps_commit_ready()")
    check(boot_ready < commit_ready and "if (boot_ready)" in app[boot_ready:commit_ready],
          "commit_ready must follow the boot_ready gate", failures)
    check("boot_ready = false" in app[boot_ready:commit_ready] and
          "recovery checkpoint deferred after startup failure" in app,
          "startup failure must preserve diagnostics and defer the checkpoint", failures)

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("PASS: SD/UI boot ordering, physical /sdcard mount, and virtual / root contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
