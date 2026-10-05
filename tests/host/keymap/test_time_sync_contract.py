import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
system = (ROOT / "components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp").read_text()
sync = (ROOT / "components/cyberdeck/src/apps/system/cyberdeck_time_sync.cpp").read_text()
event_log = (ROOT / "components/cyberdeck/src/platform/logging/event_log.cpp").read_text()
event_header = (ROOT / "components/cyberdeck/include/platform/logging/event_log.h").read_text()
task_shim = (ROOT / "tests/host/keymap/time_sync_shim/freertos/task.h").read_text()
runtime_test = (ROOT / "tests/host/keymap/test_time_sync.cpp").read_text()
traceability = (ROOT / "tests/host/keymap/time_sync_traceability.md").read_text()
registration = system[system.index('service_application s_time_sync'):system.index('cyberdeck_apps::application *const k_apps')]
assert '"cyberdeck.time_sync"' in registration
assert 'start_time_sync' in system and 'stop_time_sync' in system
assert '"cyberdeck.event_log", "cyberdeck.wifi"' in registration
assert 'app_type::background' in registration
assert '"network", "clock"' in registration
assert '16384, 1' in registration
assert '6144' not in registration
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
assert 'event_log_write_durable' in sync
assert 'event_log_write(' in system
assert 'fsync(fileno(file))' in event_log
assert 'xQueueSend(s_durable_queue' in event_log
assert 'for (int attempt = 0; attempt < 3; ++attempt)' in event_log
assert 'DURABLE_QUEUE_LENGTH = 4' in event_log
assert 'xQueueSend(s_durable_queue, &request_pointer, 0)' in event_log
assert 'vTaskDelay(pdMS_TO_TICKS(20))' in event_log
assert 'text projection queue full; durable event retained' in event_log
assert 'request->persisted = write_record' in event_log
normal_enqueue = event_log[event_log.index('void enqueue_record'):event_log.index('void remember_record')]
assert re.search(r'xQueueSend\(s_queue, &record, 0\).*wake_log_task\(\)', normal_enqueue, re.S)
durable_enqueue = event_log[event_log.index('extern "C" esp_err_t event_log_write_durable'):
                            event_log.index('extern "C" size_t event_log_latest')]
assert re.search(r'xQueueSend\(s_durable_queue, &request_pointer, 0\).*wake_log_task\(\)', durable_enqueue, re.S)
assert 'xSemaphoreTake(request->complete, pdMS_TO_TICKS(250))' in durable_enqueue
write_record = event_log[event_log.index('bool write_record'):event_log.index('void complete_durable_request')]
assert 'fsync(fileno(file))' in write_record
completion = event_log[event_log.index('void complete_durable_request'):event_log.index('void log_task')]
assert completion.index('write_record') < completion.index('xSemaphoreGive(request->complete)')
assert 'event_log_write_durable' in event_header
assert 'TEST-TIME-11' in traceability and 'TEST-TIME-12' in traceability
assert 'TEST-TIME-STACK-01' in traceability
assert 'TEST-TIME-STACK-01' in runtime_test
assert 'https://timeapi.io/api/time/current/zone?timeZone=UTC' in sync
assert 'https://worldtimeapi.org/api/timezone/Etc/UTC' in sync
assert 'fetch_and_parse(kTimeApiUrl' in sync and 'fetch_and_parse(kWorldTimeApiUrl' in sync
for scenario in ('TEST-TIME-13', 'TEST-TIME-14', 'TEST-TIME-15', 'TEST-TIME-16', 'TEST-TIME-17'):
    assert scenario in traceability
assert 'route_matrix' in runtime_test
assert 'test_time_sync.cpp' in traceability
for scenario in ('TEST-TIMELOG-01', 'TEST-TIMELOG-02', 'TEST-TIMELOG-03', 'TEST-TIMELOG-04'):
    assert scenario in traceability
assert 'TEST-TIMELOG-06' in traceability
assert 'TEST-TIMELOG-06' in runtime_test
assert 'TEST-TIMELOG-07' in runtime_test
assert 'TEST-TIMELOG-08' in runtime_test
assert 'durable_failure_result = ESP_ERR_TIMEOUT' in runtime_test
assert 'durable_failure_result = ESP_ERR_NO_MEM' in runtime_test
assert 'Logger runtime ausente' in traceability
assert 'durable_failures_remaining' in runtime_test
print("TEST-TIME-09 + TEST-TIME-STACK-01 + TEST-TIME-11..17 + TEST-TIMELOG-01..08: wiring, stack, wake, durability and traceability OK")
