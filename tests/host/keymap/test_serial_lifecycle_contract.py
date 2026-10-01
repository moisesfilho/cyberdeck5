#!/usr/bin/env python3
"""Structural contract for the Serial-JTAG task lifecycle."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
BRIDGE = ROOT / "components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp"
HEADER = ROOT / "components/cyberdeck/include/apps/serial/cyberdeck_serial_bridge.h"
PORTS = ROOT / "components/cyberdeck/src/apps/system/cyberdeck_service_ports.cpp"
APPS = ROOT / "components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp"


def main() -> int:
    bridge = BRIDGE.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    ports = PORTS.read_text(encoding="utf-8")
    apps = APPS.read_text(encoding="utf-8")
    failures = []

    for token in (
        "std::atomic<TaskHandle_t> s_task_handle{nullptr};",
        "std::atomic<bool> s_ready_ok{false};",
        "xSemaphoreGive(s_ready);",
        'xTaskCreate(bridge_task_entry, "serial_brg", 8192',
        "bool bridge_stop(std::uint32_t timeout_ms)",
        "while (s_task_handle.load(std::memory_order_acquire) != nullptr)",
    ):
        if token not in bridge:
            failures.append(f"Serial lifecycle missing: {token}")
    if "bool bridge_stop(std::uint32_t timeout_ms);" not in header:
        failures.append("header must expose bounded bridge_stop")
    if "bool serial_stop(std::uint32_t timeout_ms)" not in ports:
        failures.append("service ports must expose serial_stop")
    if "service_ports::serial_stop(2000)" not in apps:
        failures.append("system app must stop Serial through its service port")
    if "k_serial_task_stack_bytes = 8192" not in apps:
        failures.append("Serial manifest stack must match the task stack")
    if "k_serial_task_stack_bytes, 0)" not in apps:
        failures.append("Serial manifest must not claim a nonexistent queue")
    if "start_serial, stop_serial, true" not in apps:
        failures.append("Serial service must opt into idempotent lifecycle hooks")
    if "usb_serial_jtag_driver_uninstall" in bridge:
        failures.append("Serial stop must not uninstall the shared USB driver")
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("serial lifecycle contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
