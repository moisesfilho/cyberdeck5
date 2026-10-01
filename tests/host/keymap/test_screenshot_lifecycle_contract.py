#!/usr/bin/env python3
"""Structural lifecycle contract for the Screenshot service."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SERVER = ROOT / "components/cyberdeck/src/apps/screenshot/screenshot_server.cpp"
HEADER = ROOT / "components/cyberdeck/include/apps/screenshot/screenshot_server.h"
WIFI = ROOT / "components/cyberdeck/src/apps/wifi/wifi_mgr.cpp"
WIFI_HEADER = ROOT / "components/cyberdeck/include/apps/wifi/wifi_mgr.h"
APPS = ROOT / "components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp"


def main() -> int:
    server = SERVER.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    wifi = WIFI.read_text(encoding="utf-8")
    wifi_header = WIFI_HEADER.read_text(encoding="utf-8")
    apps = APPS.read_text(encoding="utf-8")
    failures = []

    for symbol in ("s_control_queue", "s_task", "s_ready", "s_quiesced"):
        if symbol not in server:
            failures.append(f"missing Screenshot lifecycle resource {symbol}")
    for symbol in ("screenshot_server_start", "screenshot_server_stop"):
        if symbol not in header or symbol not in server:
            failures.append(f"missing public API {symbol}")
    if "xQueueCreate(k_control_queue_depth" not in server:
        failures.append("Screenshot control queue must be bounded")
    if "k_screenshot_control_queue_depth = 8" not in apps:
        failures.append("Screenshot manifest queue must match the controller queue")
    if "k_screenshot_task_stack_bytes = 6144" not in apps:
        failures.append("Screenshot manifest stack must match the controller task")
    if "xSemaphoreTake(s_ready" not in server:
        failures.append("Screenshot start must wait for task readiness")
    if "xSemaphoreTake(s_quiesced" not in server:
        failures.append("Screenshot stop must join through quiescence")
    if "vTaskDelete(NULL)" not in server or "s_task.store(NULL" not in server:
        failures.append("task must clear its handle before self-delete")
    if "s_quarantined = true" not in server:
        failures.append("timeout must quarantine resources")
    callback_body = server.split("void screenshot_server_wifi_state", 1)[-1].split("\n}", 1)[0]
    if "start_server()" in callback_body or "stop_server()" in callback_body:
        failures.append("Wi-Fi callback must not start/stop HTTPD")
    if "wifi_mgr_remove_state_callback" not in wifi_header or "wifi_mgr_remove_state_callback" not in wifi:
        failures.append("Wi-Fi listener removal API is missing")
    if "xSemaphoreTake(s_state_listener_mutex" not in wifi:
        failures.append("Wi-Fi listener invocation must be synchronized")
    if "stop_screenshot" not in apps or "screenshot_server_stop(1000)" not in apps:
        failures.append("system apps must wire Screenshot stop")
    if "start_screenshot, stop_screenshot, true" not in apps:
        failures.append("Screenshot lifecycle must be idempotent")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("PASS: Screenshot lifecycle contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
