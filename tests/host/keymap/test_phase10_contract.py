#!/usr/bin/env python3
"""Structural host contracts for Phase 10's ESP-only seams.

The checks intentionally inspect production seams instead of reproducing NVS,
FreeRTOS or Serial-JTAG.  They keep the acceptance matrix executable while
remaining deterministic on a host.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
RECOVERY = ROOT / "components/cyberdeck/src/apps/system/cyberdeck_recovery.cpp"
POLICY = ROOT / "components/cyberdeck/src/apps/system/cyberdeck_recovery_policy.cpp"
POLICY_H = ROOT / "components/cyberdeck/include/apps/system/cyberdeck_recovery_policy.h"
SERIAL = ROOT / "components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp"
APPS = ROOT / "components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp"
RUNTIME = ROOT / "components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp"
MAIN = ROOT / "main/app_main.cpp"
RECOVERY_NAMESPACE = "cyberdeck_rec"

def require(condition, message):
    if not condition:
        raise AssertionError(message)

def body(source, start, end):
    first = source.index(start)
    return source[first:source.index(end, first)]

def test_recovery_blob_schema_checksum_limits_and_fail_closed():
    source = RECOVERY.read_text()
    header = POLICY_H.read_text()
    namespace = re.search(r'constexpr\s+char\s+k_namespace\[\]\s*=\s*"([^"]+)"', source)
    require(namespace is not None and namespace.group(1) == RECOVERY_NAMESPACE,
            "recovery must use the approved NVS namespace cyberdeck_rec")
    require(len(RECOVERY_NAMESPACE) <= 15,
            "the recovery NVS namespace must fit ESP-IDF's 15-character limit")
    require("k_state_version" in header and "k_max_app_errors" in header,
            "recovery state must have explicit schema version and bounded error count")
    require("nvs_get_blob" in source and "size != sizeof(s_state)" in source,
            "recovery must validate persisted blob size")
    require("s_state.version != k_state_version" in source,
            "recovery must reject an incompatible schema")
    require("nvs_flash_erase" not in source and "nvs_flash_erase" not in MAIN.read_text(),
            "NVS corruption/unavailability must never trigger automatic erase")
    require("nvs_open(k_namespace, NVS_READWRITE" in source and
            "nvs_set_blob(handle, k_key, &s_state, sizeof(s_state))" in source and
            "nvs_commit(handle)" in source,
            "recovery must preserve NVS persistence through the approved namespace")
    require("nvs_close(handle);\n        return ESP_ERR_INVALID_SIZE" in source,
            "corrupt recovery data must fail closed before becoming available")
    require("k_app_id_size" in header and "k_error_size" in header,
            "app and error fields need bounded storage")
    require("app.size() >= k_app_id_size" in POLICY.read_text(),
            "app error identifiers must reject overflow")

def test_boot_attempt_checkpoint_and_three_interruptions():
    source = POLICY.read_text()
    require("value.boot_pending" in source and "interrupted_boots" in source,
            "policy must model pending attempts and interruption count")
    require("k_interrupted_boot_limit = 3" in POLICY_H.read_text(),
            "safe-mode threshold must be exactly three interrupted boots")
    require("if (value.boot_pending" in source and "value.boot_pending = true" in source,
            "begin_boot must count only a previously pending attempt")
    require("value.boot_pending = false" in source and "value.interrupted_boots = 0" in source,
            "ready checkpoint must clear pending attempt and counter")

def test_safe_mode_latch_and_explicit_reset_services():
    apps = APPS.read_text()
    main = MAIN.read_text()
    serial = SERIAL.read_text()
    require("cyberdeck_recovery::safe_mode()" in main and
            "cyberdeck_system_apps_start_safe_mode()" in main,
            "boot must select the safe-mode surface from persisted state")
    safe = body(apps, "cyberdeck_system_apps_start_safe_mode", "cyberdeck_system_apps_commit_ready")
    for service in ("event_log", "shell", "serial"):
        require(service in safe, "safe mode must allow only event log, shell and serial")
    require("wifi" not in safe and "screenshot" not in safe and "bluetooth" not in safe,
            "safe mode must not start optional services")
    require('req.type == "sys.safe_mode.clear"' in serial and
            "cyberdeck_recovery::clear_safe_mode()" in serial,
            "safe-mode latch must have an explicit Serial-JTAG reset command")

def test_sys_info_and_app_info_expose_recovery_diagnostics():
    serial = SERIAL.read_text()
    runtime = RUNTIME.read_text()
    require('\\"safe_mode\\"' in serial, "sys.info must expose safe_mode")
    require('\\"interrupted_boots\\"' in serial, "sys.info must expose interrupted_boots")
    require('\\"recovery_persisted\\"' in serial, "sys.info must expose persistence status")
    require("cyberdeck_recovery::current().interrupted_boots" in serial,
            "sys.info must source interruption count from recovery state")
    require('app info' in runtime and "failures_" in runtime,
            "app info must expose the last app failure")
    require("record_error" in POLICY.read_text() and "error_for" in POLICY.read_text(),
            "last error must be addressable per application")
    require("record_app_error" in RECOVERY.read_text() and "save()" in RECOVERY.read_text(),
            "recovery diagnostics must remain persisted, not only held in RAM")

def test_boot_ready_is_the_only_path_to_recovery_commit():
    main = MAIN.read_text()
    ready = main.index("bool boot_ready = true")
    commit = main.index("cyberdeck_system_apps_commit_ready()")
    require(ready < commit and "if (boot_ready)" in main[ready:commit],
            "recovery commit must be reached only after boot_ready")
    require("boot_ready = false" in main[ready:commit],
            "startup failures must prevent the recovery commit")
    require("recovery checkpoint deferred after startup failure" in main,
            "failed startup must preserve the pending recovery attempt")

def test_lifecycle_logs_are_bounded_and_emitted():
    runtime = RUNTIME.read_text()
    apps = APPS.read_text()
    require("logger_" in runtime, "runtime must retain an injected lifecycle logger")
    require(re.search(r"logger_.*write|logger\(\).*write|\.write\(", runtime, re.S),
            "runtime lifecycle transitions must emit logs")
    require("lifecycle_timeout_ms" in apps, "system manifests must define lifecycle budgets")
    require("QUEUE_LENGTH = 32" in (ROOT / "components/cyberdeck/src/platform/logging/event_log.cpp").read_text(),
            "event log ingress must remain bounded")

def main():
    tests = [test_recovery_blob_schema_checksum_limits_and_fail_closed,
             test_boot_attempt_checkpoint_and_three_interruptions,
             test_safe_mode_latch_and_explicit_reset_services,
             test_sys_info_and_app_info_expose_recovery_diagnostics,
             test_boot_ready_is_the_only_path_to_recovery_commit,
             test_lifecycle_logs_are_bounded_and_emitted]
    failures = []
    for test in tests:
        try:
            test()
        except (AssertionError, ValueError) as exc:
            failures.append(f"{test.__name__}: {exc}")
    for failure in failures:
        print("FAIL " + failure)
    if failures:
        return 1
    print(f"PASS: Phase 10 contract ({len(tests)} scenarios)")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
