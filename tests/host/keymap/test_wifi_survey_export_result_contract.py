#!/usr/bin/env python3
"""Structural guards for survey persistence result and start-gate ownership."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
UI = (ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp").read_text()


def body(marker):
    start = UI.find(marker)
    opening = UI.find("{", start)
    if start < 0 or opening < 0:
        return ""
    depth = 0
    for index in range(opening, len(UI)):
        if UI[index] == "{":
            depth += 1
        elif UI[index] == "}":
            depth -= 1
            if depth == 0:
                return UI[opening:index + 1]
    return ""


worker = body("void wifi_survey_export_worker(void *argument)")
completion = body("void process_wifi_survey(lv_timer_t *)")
export = body("bool shell_session_host::wifi_survey_export(bool confirm, bool masked)")
gate_failure_start = export.find("if (job->start_gate == nullptr)")
gate_failure_end = export.find("s_wifi_survey_export_result = {}", gate_failure_start)
gate_failure = export[gate_failure_start:gate_failure_end]
task_failure_start = export.find("if (xTaskCreate(")
task_failure = export[task_failure_start:]
create_task_call = export.find("if (xTaskCreate(")
free_sample = export.find("heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)")
largest_sample = export.find("heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)")
create_task_end = export.find(") != pdPASS)", create_task_call)
failure_log = task_failure[task_failure.find('"wifi survey export rejected: reason=create_task_failed'):]

checks = [
    ("const bool ok = cyberdeck_wifi_audit::persist_document(" in worker,
     "worker captures persist_document result"),
    ("s_wifi_survey_export_result.ok = ok;" in worker and
     "s_wifi_survey_export_result.ok = true;" not in worker,
     "result.ok derives from persistence bool, never forced true"),
    ("if (ok) ESP_LOGI" in worker and "outcome=published" in worker and
     "else ESP_LOGW" in worker and "outcome=failed" in worker,
     "success and failure diagnostics follow persistence result"),
    ("if (s_wifi_survey_export_result.ok)" in completion and
     completion.find("if (s_wifi_survey_export_result.ok)") <
     completion.find('append_line(std::string("wifi survey saved: ")') and
     'append_line("wifi survey export failed\\n")' in completion,
     "completion emits saved only on success and failure otherwise"),
    (worker.find("xSemaphoreTake(job->start_gate, portMAX_DELAY)") >= 0 and
     worker.find("xSemaphoreTake(job->start_gate, portMAX_DELAY)") <
     worker.find("vSemaphoreDelete(job->start_gate)") <
     worker.find("job->start_gate = nullptr;") < worker.find("persist_document("),
     "successful worker consumes, deletes, and nulls gate before persistence"),
    ("vSemaphoreDelete(job->start_gate);" in task_failure and
     task_failure.find("vSemaphoreDelete(job->start_gate);") <
     task_failure.find("delete job;") <
     task_failure.find("s_wifi_survey_export_busy.store(false"),
     "task-creation failure deletes gate before job and busy release"),
    ("delete job;" in gate_failure and
     "vSemaphoreDelete(job->start_gate);" not in gate_failure and
     gate_failure.find("delete job;") <
     gate_failure.find("s_wifi_survey_export_busy.store(false"),
     "gate-creation failure deletes job only, avoiding invalid/double gate delete"),
    (task_failure.find("vSemaphoreDelete(job->start_gate);") >= 0 and
     task_failure.find("delete job;") > task_failure.find("vSemaphoreDelete(job->start_gate);") and
     worker.find("vSemaphoreDelete(job->start_gate)") >= 0 and
     worker.find("job->start_gate = nullptr;") > worker.find("vSemaphoreDelete(job->start_gate)"),
     "gate ownership has one deletion on each mutually exclusive task outcome"),
    ("s_wifi_survey_export_done.store(true" in worker and
     worker.find("persist_document(") <
     worker.find("s_wifi_survey_export_result.ok = ok;") <
     worker.rfind("s_wifi_survey_export_done.store(true"),
      "completion signal follows stored persistence result"),
    (0 <= free_sample < largest_sample < create_task_call < create_task_end,
     "internal free/largest heap metrics are sampled before task creation"),
    ('requested_stack=8192' in failure_log and
     'reason=create_task_failed' in failure_log and
     'internal_free_bytes=%u' in failure_log and
     'internal_largest_block_bytes=%u' in failure_log,
     "task-create failure diagnostic reports requested stack and sampled metrics"),
    (task_failure.find("ESP_LOGW") >= 0 and
     task_failure.find("ESP_LOGW") < task_failure.find("return false") and
     export.count("internal_free_bytes=%u") == 1 and
     export.count("internal_largest_block_bytes=%u") == 1 and
     export.find("internal_free_bytes=%u") >= create_task_end,
     "heap metrics are emitted only on the create-task failure path"),
    (all(secret not in export for secret in ("ssid=", "bssid=", "payload=", "path=")),
     "export diagnostics do not disclose SSID, BSSID, payload, or path"),
    ('reason=create_task_failed' in task_failure and
     'reason=task_creation_failed' not in task_failure and
     'wifi survey export accepted: mode=' in export and
     'wifi_survey_save_path()' in export and
     's_wifi_survey.export_text(masked)' in export,
     "diagnostic preserves synchronous failure reason, export policy, and UI flow"),
]

for passed, description in checks:
    if not passed:
        print(f"FAIL: {description}")
print(f"wifi survey export result/lifecycle: {sum(ok for ok, _ in checks)}/{len(checks)} checks")
sys.exit(0 if all(ok for ok, _ in checks) else 1)
