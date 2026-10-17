#!/usr/bin/env python3
"""Structural guards for survey persistence result and start-gate ownership."""
from pathlib import Path
import re
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
heap_samples = {
    "8bit_free": export.find("heap_caps_get_free_size(MALLOC_CAP_8BIT)"),
    "8bit_largest": export.find("heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)"),
    "internal_free": export.find("heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)"),
    "internal_largest": export.find(
        "heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)"
    ),
}
create_task_end = export.find(") != pdPASS)", create_task_call)
failure_log = task_failure[task_failure.find('"wifi survey export rejected: reason=create_task_failed'):]


def call_arguments(source, marker):
    call_start = source.find(marker)
    opening = source.find("(", call_start)
    if call_start < 0 or opening < 0:
        return []
    depth = 0
    in_string = False
    escaped = False
    start = opening + 1
    arguments = []
    for index in range(opening, len(source)):
        char = source[index]
        if in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            continue
        if char == '"':
            in_string = True
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                arguments.append(source[start:index].strip())
                return arguments
        elif char == "," and depth == 1:
            arguments.append(source[start:index].strip())
            start = index + 1
    return []


failure_log_call = call_arguments(task_failure, "ESP_LOGW")
failure_log_format = "".join(
    re.findall(r'"((?:\\.|[^"\\])*)"', failure_log_call[1])
) if len(failure_log_call) >= 2 else ""
diagnostic_bindings = [
    ("heap_8bit_free_bytes=%u", "heap_8bit_free_bytes"),
    ("heap_8bit_largest_block_bytes=%u", "heap_8bit_largest_block_bytes"),
    ("internal_free_bytes=%u", "internal_heap_free_bytes"),
    ("internal_largest_block_bytes=%u", "internal_heap_largest_block_bytes"),
]
diagnostic_arguments = failure_log_call[2:]
diagnostic_fields = re.findall(r"([a-z0-9_]+)=%u", failure_log_format)
expected_fields = [field.partition("=")[0] for field, _ in diagnostic_bindings]
diagnostic_arguments_match = [
    re.fullmatch(
        rf"static_cast<unsigned>\(\s*{re.escape(variable)}\s*\)", argument
    ) is not None
    for (_, variable), argument in zip(diagnostic_bindings, diagnostic_arguments)
]
diagnostic_samples_match = [
    re.search(
        rf"const std::size_t {re.escape(variable)}\s*=\s*{re.escape(getter)}\s*;",
        export,
    )
    for variable, getter in (
        ("heap_8bit_free_bytes", "heap_caps_get_free_size(MALLOC_CAP_8BIT)"),
        ("heap_8bit_largest_block_bytes", "heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)"),
        ("internal_heap_free_bytes", "heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)"),
        ("internal_heap_largest_block_bytes",
         "heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)"),
    )
]

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
    (all(position >= 0 for position in heap_samples.values()) and
     max(heap_samples.values()) < create_task_call < create_task_end,
     "general and internal free/largest heap metrics are sampled before task creation"),
     ('requested_stack=8192' in failure_log and
      'reason=create_task_failed' in failure_log and
     'heap_8bit_free_bytes=%u' in failure_log and
     'heap_8bit_largest_block_bytes=%u' in failure_log and
     'internal_free_bytes=%u' in failure_log and
      'internal_largest_block_bytes=%u' in failure_log,
      "typed task-create failure diagnostic reports stack and all sampled metrics"),
     (task_failure.find("ESP_LOGW") >= 0 and
      task_failure.find("ESP_LOGW") < task_failure.find("return false") and
      export.find('"wifi survey export rejected: reason=create_task_failed') >= create_task_end and
      len(diagnostic_arguments) == len(diagnostic_bindings),
      "all heap metrics are emitted only on the create-task failure path"),
     (diagnostic_fields == expected_fields and
      len(diagnostic_arguments_match) == len(diagnostic_bindings) and
      all(diagnostic_arguments_match) and
      len(diagnostic_samples_match) == len(diagnostic_bindings) and
      all(diagnostic_samples_match) and
      [sample.start() for sample in diagnostic_samples_match] ==
      sorted(sample.start() for sample in diagnostic_samples_match),
      "ordered heap getters, format fields, and arguments remain bound one-to-one"),
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
