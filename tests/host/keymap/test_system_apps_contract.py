#!/usr/bin/env python3
"""Structural contract for the fixed Tab5 system application registry."""

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
APPS = ROOT / "components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp"
SSH = ROOT / "components/cyberdeck/src/apps/ssh/ssh_client.cpp"
APP_MAIN = ROOT / "main/app_main.cpp"
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
RUNTIME = ROOT / "components/cyberdeck/include/apps/runtime/cyberdeck_app_runtime.h"
RUNTIME_SOURCE = ROOT / "components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp"
QUOTA_HEADER = ROOT / "components/cyberdeck/include/apps/runtime/cyberdeck_app_quota.h"
PORTS = ROOT / "components/cyberdeck/src/apps/system/cyberdeck_service_ports.cpp"
PORTS_HEADER = ROOT / "components/cyberdeck/include/apps/system/cyberdeck_service_ports.h"
CMAX = ROOT / "components/cyberdeck/CMakeLists.txt"

MANIFEST_CALL = "make_manifest("
RESOURCE_ARGUMENT = 4
CAPABILITY_ARGUMENT = 7


def manifest_arguments(source):
    """Split every make_manifest(...) call into its top-level arguments."""
    calls = []
    search_from = 0
    while True:
        start = source.find(MANIFEST_CALL, search_from)
        if start < 0:
            return calls
        position = start + len(MANIFEST_CALL)
        depth = 1
        current = []
        arguments = []
        while position < len(source) and depth:
            character = source[position]
            if character in "({[":
                depth += 1
            elif character in ")}]":
                depth -= 1
                if depth == 0:
                    break
            if character == "," and depth == 1:
                arguments.append("".join(current))
                current = []
            else:
                current.append(character)
            position += 1
        arguments.append("".join(current))
        calls.append(arguments)
        search_from = position


def known_resources(source):
    """Read the runtime's resource vocabulary from resource_name()."""
    body = re.search(
        r"const char \*resource_name\(resource value\)\s*\{(?P<body>.*?)\n\}", source, re.S)
    if body is None:
        return set()
    return set(re.findall(r'return "(?P<name>[a-z_]+)";', body.group("body")))


def declared_resources(arguments):
    return set(re.findall(r'"(?P<name>[^"]*)"', arguments))


def check_manifest_vocabulary(apps_source, runtime_source, quota_source, failures):
    """Every declared resource/grant must exist in the runtime vocabulary.

    register_application() rejects a manifest whose resource name is unknown and
    app_main() asserts on that failure, so an invented name aborts at boot and
    the bootloader rolls the update back.
    """
    vocabulary = known_resources(runtime_source)
    if not vocabulary:
        failures.append("could not read the runtime resource vocabulary")
        return
    calls = manifest_arguments(apps_source)
    if len(calls) < 10:
        failures.append(f"expected to parse every make_manifest() call, found {len(calls)}")
    checked = 0
    for arguments in calls:
        for index in (RESOURCE_ARGUMENT, CAPABILITY_ARGUMENT):
            if index >= len(arguments):
                continue
            for name in declared_resources(arguments[index]):
                checked += 1
                if name not in vocabulary:
                    failures.append(
                        f"manifest declares unknown resource {name!r}; "
                        f"runtime vocabulary is {sorted(vocabulary)}")
    if checked == 0:
        failures.append("no manifest resource declaration was inspected")

    capacity = re.search(r"k_max_applications\s*=\s*(\d+)\s*;", quota_source)
    registered = re.search(r"application \*const k_apps\[\]\s*=\s*\{(?P<body>.*?)\n\};",
                           apps_source, re.S)
    if capacity is None or registered is None:
        failures.append("could not read the runtime application capacity")
        return
    entries = [entry for entry in registered.group("body").split(",") if entry.strip()]
    if len(entries) > int(capacity.group(1)):
        failures.append(
            f"k_apps registers {len(entries)} applications but k_max_applications is "
            f"{capacity.group(1)}; registration would fail and abort at boot")


def main() -> int:
    apps = APPS.read_text(encoding="utf-8")
    ssh = SSH.read_text(encoding="utf-8")
    app_main = APP_MAIN.read_text(encoding="utf-8")
    ui = UI.read_text(encoding="utf-8")
    runtime = RUNTIME.read_text(encoding="utf-8")
    runtime_source = RUNTIME_SOURCE.read_text(encoding="utf-8")
    quota_header = QUOTA_HEADER.read_text(encoding="utf-8")
    ports = PORTS.read_text(encoding="utf-8")
    ports_header = PORTS_HEADER.read_text(encoding="utf-8")
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
    if "event_log_init()" not in apps or "cyberdeck.event_log" not in apps:
        failures.append("event log must be a registered supervisor-owned system app")
    if "set_logger(&s_event_logger)" not in apps:
        failures.append("system apps must inject the supervisor logger")
    if "event_log_write(" in ui or "event_log_latest(" in ui:
        failures.append("UI must use the supervisor logger port instead of event_log directly")
    if "logger *app_logger() const" not in runtime:
        failures.append("runtime must expose the supervisor logger port")
    for marker in ("wifi_connect", "ssh_connect", "ble_enqueue", "serial_start"):
        if marker not in ports or marker not in ports_header:
            failures.append(f"missing service port {marker}")
    for direct_call in ("wifi_mgr_", "ssh_client_", "ble_mgr_", "bridge_start"):
        if f"{direct_call}(" in ui:
            failures.append(f"UI still references service backend directly: {direct_call}")
    for dependency in ("cyberdeck.wifi", "cyberdeck.shell", "cyberdeck.event_log"):
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

    stop_match = re.search(r"bool stop_ssh\(\)\s*\{(?P<body>.*?)\n\}", apps, re.S)
    if stop_match is None or "ssh_client_disconnect_and_wait(k_ssh_lifecycle_timeout_ms)" not in stop_match.group("body"):
        failures.append("SSH stop must request disconnect and wait with the manifest timeout")
    if "constexpr uint32_t k_ssh_lifecycle_timeout_ms = 1000;" not in apps:
        failures.append("SSH lifecycle timeout must remain bounded at 1000 ms")
    if "constexpr uint32_t k_ssh_task_stack_bytes = 24576;" not in apps:
        failures.append("SSH manifest stack must match the dynamic task stack")
    if "constexpr uint32_t k_ssh_event_queue_depth = 8;" not in apps:
        failures.append("SSH event queue depth must remain declared as 8")
    if "constexpr std::size_t k_ssh_event_queue_capacity = 8;" not in ui:
        failures.append("UI SSH event queue capacity must remain 8")
    ssh_manifest = re.search(r'service_application s_ssh\{(?P<body>.*?)\n\s*start_ssh, stop_ssh, true\};', apps, re.S)
    if ssh_manifest is None:
        failures.append("SSH service must opt into idempotent lifecycle hooks")
    # BUG-SSH-001: the 24576-byte stack no longer fits the internal heap, so the
    # caps-aware API is mandatory; the match is whitespace/line-break tolerant
    # but still pins the entry point, the task name and the stack size.
    if re.search(r'xTaskCreateWithCaps\(\s*ssh_client_task,\s*"ssh_client",\s*24576\b', ssh) is None:
        failures.append("SSH task stack must remain 24576 bytes")
    if "xTaskCreate(" in ssh:
        failures.append("SSH task must not fall back to the internal-heap xTaskCreate()")
    if "ssh_client_disconnect_and_wait" not in ssh or "s_task_handle.load" not in ssh:
        failures.append("SSH disconnect contract must join by observing the task handle")
    join_position = ui.find("ssh_disconnect_and_wait(k_ssh_disconnect_timeout_ms)")
    queue_delete_position = ui.find("vQueueDelete(s_ssh_event_queue)")
    if join_position < 0 or queue_delete_position < 0 or join_position > queue_delete_position:
        failures.append("SSH event queue must be destroyed only after the task join")

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
    if "cyberdeck_service_ports.cpp" not in cmake:
        failures.append("service port source must be registered in CMake")
    bluetooth_manifest = re.search(
        r'service_application s_bluetooth\{(?P<body>.*?)\n\s*start_bluetooth, stop_bluetooth, (?P<idempotent>true|false)\};',
        apps,
        re.S,
    )
    if bluetooth_manifest is None or bluetooth_manifest.group("idempotent") != "false":
        failures.append("BLE startup must remain fail-closed after a partial NimBLE init failure")

    check_manifest_vocabulary(apps, runtime_source, quota_header, failures)

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("PASS: system apps contract")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
