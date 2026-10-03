#!/usr/bin/env python3
"""Fail closed when gcovr scope drifts from production C++ sources."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


# These adapters require ESP-IDF/BSP/FreeRTOS services and have no host
# implementation.  Every other production translation unit must appear in the
# gcovr JSON.  Keep this list deliberately small and review each addition.
ALLOWLIST = {
    "components/cyberdeck/src/apps/bluetooth/ble_mgr.cpp":
        "NimBLE/ESP-Hosted and FreeRTOS adapter",
    "components/cyberdeck/src/apps/wifi/wifi_mgr.cpp":
        "ESP-IDF Wi-Fi/event/NVS adapter",
    "components/cyberdeck/src/apps/wifi/wifi_storage.cpp":
        "BSP/SD-card storage adapter",
    "components/cyberdeck/src/apps/screenshot/screenshot_server.cpp":
        "ESP HTTP server/LVGL adapter",
    "components/cyberdeck/src/apps/ssh/ssh_client.cpp":
        "libssh session with ESP-IDF/BSP/FreeRTOS integration",
    "components/cyberdeck/src/apps/system/cyberdeck_service_ports.cpp":
        "thin forwarding to the wifi_mgr/ssh_client/ble_mgr/serial bridge adapters",
    "components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp":
        "system app composition/lifecycle bound to hardware services; covered by "
        "structural Python contracts (test_system_apps_contract.py, "
        "test_shell_app_contract.py, test_serial_lifecycle_contract.py)",
    "components/cyberdeck/src/apps/system/cyberdeck_recovery.cpp":
        "NVS/ESP-IDF recovery adapter; exercised by the Phase 10 structural "
        "contract and intentionally not host-linkable",
    "components/cyberdeck/src/platform/display/cyberdeck_display_port.cpp":
        "BSP/LVGL capture adapter (bsp_display_lock, lv_snapshot_take); no host build",
    "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp":
        "LVGL/BSP/FreeRTOS composition adapter; behavior is in host-tested models",
    "components/cyberdeck/src/platform/display/screen_off.cpp":
        "BSP/LVGL/NVS/FreeRTOS adapter",
    "components/cyberdeck/src/platform/input/tab5_keyboard.cpp":
        "BSP/I2C/GPIO/FreeRTOS adapter",
    "components/cyberdeck/src/platform/sensors/imu_reader.cpp":
        "BSP/FreeRTOS sensor adapter",
    "components/cyberdeck/src/platform/sensors/ina226_reader.cpp":
        "BSP/I2C/FreeRTOS sensor adapter",
    "components/cyberdeck/src/platform/sensors/battery_protection.cpp":
        "BSP/expander/NVS adapter",
    "components/cyberdeck/src/platform/logging/event_log.cpp":
        "ESP timer/FreeRTOS logging adapter",
}

FORBIDDEN_ALLOWLIST = {
    "components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_background.cpp",
    "components/cyberdeck/src/apps/shell/cyberdeck_cat_worker.cpp",
    "components/cyberdeck/src/platform/input/cyberdeck_keyboard_dispatch.cpp",
    "components/cyberdeck/src/platform/display/cyberdeck_header_view.cpp",
    "components/cyberdeck/src/platform/display/cyberdeck_terminal_view.cpp",
}


def relative_source(path: str, root: Path) -> str:
    candidate = Path(path)
    if not candidate.is_absolute():
        candidate = root / candidate
    return candidate.resolve().relative_to(root.resolve()).as_posix()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--gcovr-json", type=Path, required=True)
    args = parser.parse_args()

    root = args.root.resolve()
    source_root = args.source_root.resolve()
    if not source_root.is_relative_to(root):
        print("coverage scope error: source root is outside repository", file=sys.stderr)
        return 1

    production = {
        path.relative_to(root).as_posix()
        for path in source_root.rglob("*.cpp")
    }
    invalid_allowlist = set(ALLOWLIST) & FORBIDDEN_ALLOWLIST
    outside_allowlist = set(ALLOWLIST) - production
    if invalid_allowlist or outside_allowlist:
        print("coverage scope error: invalid allowlist", file=sys.stderr)
        for path in sorted(invalid_allowlist):
            print(f"  forbidden eligible TU: {path}", file=sys.stderr)
        for path in sorted(outside_allowlist):
            print(f"  outside production source root: {path}", file=sys.stderr)
        return 1

    try:
        report = json.loads(args.gcovr_json.read_text(encoding="utf-8"))
        report_files = report["files"]
        covered = {
            relative_source(entry["file"], root)
            for entry in report_files
        }
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
        print(f"coverage scope error: invalid gcovr JSON: {error}", file=sys.stderr)
        return 1

    outside = covered - production
    if outside:
        print("coverage scope error: gcovr JSON contains files outside production scope:", file=sys.stderr)
        for path in sorted(outside):
            print(f"  {path}", file=sys.stderr)
        return 1

    missing = production - covered - set(ALLOWLIST)
    allowlisted_and_covered = set(ALLOWLIST) & covered
    if allowlisted_and_covered:
        print("coverage scope error: allowlisted TUs are covered and must be removed from allowlist:", file=sys.stderr)
        for path in sorted(allowlisted_and_covered):
            print(f"  {path}: {ALLOWLIST[path]}", file=sys.stderr)
        return 1
    if missing:
        print("coverage scope error: production TUs missing from gcovr JSON:", file=sys.stderr)
        for path in sorted(missing):
            print(f"  {path}", file=sys.stderr)
        return 1

    print(f"coverage scope OK: {len(covered)} covered, {len(ALLOWLIST)} hardware allowlisted")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
