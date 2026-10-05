import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
system = (ROOT / "components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp").read_text()
sync = (ROOT / "components/cyberdeck/src/apps/system/cyberdeck_time_sync.cpp").read_text()
task_shim = (ROOT / "tests/host/keymap/time_sync_shim/freertos/task.h").read_text()
runtime_test = (ROOT / "tests/host/keymap/test_time_sync.cpp").read_text()
traceability = (ROOT / "tests/host/keymap/time_sync_traceability.md").read_text()
registration = system[system.index('service_application s_time_sync'):system.index('cyberdeck_apps::application *const k_apps')]
assert '"cyberdeck.time_sync"' in registration
assert 'start_time_sync' in system and 'stop_time_sync' in system
assert '"cyberdeck.event_log", "cyberdeck.wifi"' in registration
assert 'app_type::background' in registration
assert '"network", "clock"' in registration
assert '6144, 1' in registration
assert 'k_time_sync_lifecycle_timeout_ms,' in registration
assert 'k_time_sync_lifecycle_timeout_ms = 9000' in system
assert 'cyberdeck_time_sync_boot_ready();' in system
assert 'cyberdeck_time_sync_stop(k_time_sync_lifecycle_timeout_ms)' in system
assert 'start_all()' in system and 'cyberdeck_time_sync_start' in sync
assert re.search(r"if\s*\(\s*result\s*==\s*ESP_OK\s*\)\s*"
                r"cyberdeck_time_sync_boot_ready\s*\(\s*\)\s*;", system)
assert 'wifi_mgr_add_state_callback' in sync and 'wifi_mgr_remove_state_callback' in sync
assert 'ctx.ready.store(false' in sync and 'xSemaphoreTake(ctx.quiesced' in sync
assert 'vTaskDeleteWithCaps(nullptr);' in sync
assert 'vTaskDeleteWithCaps(TaskHandle_t)' in task_shim
assert 'task_deletions' in runtime_test
assert all(scenario in runtime_test for scenario in ('TEST-TIME-07', 'TEST-TIME-08', 'TEST-TIME-17'))
assert '"time_sync", "success"' in sync
assert 'app_logger()' in sync and 'logger != nullptr' in sync
assert 'TEST-TIME-11' in traceability and 'TEST-TIME-12' in traceability
assert 'https://timeapi.io/api/time/current/zone?timeZone=UTC' in sync
assert 'https://worldtimeapi.org/api/timezone/Etc/UTC' in sync
assert 'fetch_and_parse(kTimeApiUrl' in sync and 'fetch_and_parse(kWorldTimeApiUrl' in sync
for scenario in ('TEST-TIME-13', 'TEST-TIME-14', 'TEST-TIME-15', 'TEST-TIME-16', 'TEST-TIME-17'):
    assert scenario in traceability
assert 'route_matrix' in runtime_test
assert 'test_time_sync.cpp' in traceability
print("TEST-TIME-09/11..17: runtime wiring and approved endpoint matrix contract OK")
