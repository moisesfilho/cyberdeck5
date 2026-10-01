#!/usr/bin/env python3
"""Structural contract for the fixed Tab5 system application registry."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
APPS = ROOT / "components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp"
APP_MAIN = ROOT / "main/app_main.cpp"
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
CMAX = ROOT / "components/cyberdeck/CMakeLists.txt"


def main() -> int:
    apps = APPS.read_text(encoding="utf-8")
    app_main = APP_MAIN.read_text(encoding="utf-8")
    ui = UI.read_text(encoding="utf-8")
    cmake = CMAX.read_text(encoding="utf-8")
    failures = []

    required_ids = (
        "cyberdeck.shell", "cyberdeck.wifi", "cyberdeck.serial",
        "cyberdeck.ssh", "cyberdeck.screenshot", "cyberdeck.bluetooth",
    )
    for app_id in required_ids:
        if f'"{app_id}"' not in apps:
            failures.append(f"missing manifest id {app_id}")

    if "runtime.start_all()" not in apps:
        failures.append("startup must be delegated to the supervisor")
    for dependency in ('{"cyberdeck.wifi"}', '{"cyberdeck.shell"}'):
        if dependency not in apps:
            failures.append(f"missing declarative dependency {dependency}")
    if "lifecycle_timeout_ms" not in apps:
        failures.append("system manifests must declare lifecycle timeout support")
    for marker in ("cyberdeck_apps::app_type", "capability_count", "stack_bytes", "queue_depth",
                   "command_count"):
        if marker not in apps:
            failures.append(f"missing expanded manifest field {marker}")
    if "virtual bool init()" not in (ROOT / "components/cyberdeck/include/apps/runtime/cyberdeck_app_runtime.h").read_text(encoding="utf-8"):
        failures.append("application contract must expose init hook")
    if "virtual bool teardown()" not in (ROOT / "components/cyberdeck/include/apps/runtime/cyberdeck_app_runtime.h").read_text(encoding="utf-8"):
        failures.append("application contract must expose teardown hook")

    if "cyberdeck_system_apps_register()" not in app_main:
        failures.append("app_main must register system apps")
    if "cyberdeck_system_apps_start()" not in app_main:
        failures.append("app_main must start system apps")
    for direct_call in ("wifi_mgr_start()", "bridge_start()", "ble_mgr_start()",
                        "screenshot_server_init()"):
        if direct_call in app_main:
            failures.append(f"app_main still starts service directly: {direct_call}")
    if "global_runtime()" not in ui:
        failures.append("UI must use the global app runtime")
    if "cyberdeck_system_apps.cpp" not in cmake:
        failures.append("system app source must be registered in CMake")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("PASS: system apps contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
