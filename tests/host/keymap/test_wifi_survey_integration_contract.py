#!/usr/bin/env python3
"""Structural wiring guard; behavioral rules are tested against real host TUs."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
UI = (ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp").read_text()
MGR = (ROOT / "components/cyberdeck/src/apps/wifi/wifi_mgr.cpp").read_text()
SHELL = (ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp").read_text()
AUDIT = (ROOT / "components/cyberdeck/src/apps/wifi/cyberdeck_wifi_audit.cpp").read_text()
PERSISTENCE = (ROOT / "components/cyberdeck/src/apps/wifi/cyberdeck_wifi_audit_persistence.cpp").read_text()

checks = []
def require(condition, description):
    checks.append((condition, description))

def body(source, marker):
    start = source.find(marker)
    if start < 0:
        return ""
    opening = source.find("{", start)
    if opening < 0:
        return ""
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    return ""

def braced_after(source, marker):
    start = source.find(marker)
    if start < 0:
        return ""
    opening = source.find("{", start)
    if opening < 0:
        return ""
    return body(source[opening:], "{")

require("WIFI_SCAN_TYPE_PASSIVE" in MGR and "wifi_mgr_scan_passive" in MGR,
        "manager exposes passive scan configuration")
survey_scan_start = MGR.find("esp_err_t wifi_mgr_scan_passive(")
survey_scan_end = MGR.find("bool wifi_mgr_cancel_scan(", survey_scan_start)
survey_scan = MGR[survey_scan_start:survey_scan_end] if survey_scan_start >= 0 and survey_scan_end > survey_scan_start else ""
scan_impl_start = MGR.find("static esp_err_t wifi_mgr_scan_impl")
scan_impl_end = MGR.find("esp_err_t wifi_mgr_scan(", scan_impl_start)
scan_impl = MGR[scan_impl_start:scan_impl_end] if scan_impl_start >= 0 and scan_impl_end > scan_impl_start else ""
require("survey_config.scan_time.passive = WIFI_PASSIVE_SCAN_DEFAULT_TIME" in survey_scan,
        "survey config explicitly selects ESP-IDF's 360 ms passive dwell constant")
require("esp_wifi_scan_start(scan_config, false)" in scan_impl and
        "wifi_mgr_scan_impl(cb, ctx, NULL)" in MGR,
        "legacy non-passive scan path remains distinct")
require("service_ports::wifi_scan_passive(on_wifi_survey_scan_done" in UI,
        "survey integration dispatches through passive scan service port")
require("s_wifi_survey.complete_scan(result->request" in UI and
        "s_wifi_survey.expire(now_ms)" in UI,
        "UI routes scan snapshots through session tokens and expiry")
require("wifi_cancel_scan(on_wifi_survey_scan_done, scan)" in UI,
        "survey cancellation uses owned scan callback/context")
require("scan_busy || !s_wifi_survey.start(duration_seconds" in UI,
        "session start refuses when interactive scan is active")
require("s_wifi_survey.active() || !s_wifi_survey.finished()" in UI,
        "export confirmation cannot be staged during active/unfinished survey")
require("s_wifi_survey_confirmation_pending" in UI and
        "s_wifi_survey_confirmation_masked" in UI and "now > s_wifi_survey_confirmation_deadline_ms" in UI,
        "export requires matching unexpired confirmation choice")
require("/sdcard/wifi-survey/wifi-survey-" in UI,
        "survey export path is fixed beneath survey directory")
require("destination_policy::survey" in AUDIT and "destination_policy::survey" in PERSISTENCE,
        "shared persistence forwards explicit survey policy")
require("/sdcard/wifi-survey/" in PERSISTENCE and ".tmp" in PERSISTENCE and ".bak" in PERSISTENCE,
        "survey transaction has confined root and sidecars")
require(not re.search(r"esp_wifi_(connect|disconnect)\s*\(",
                      (ROOT / "components/cyberdeck/src/apps/wifi/cyberdeck_wifi_survey.cpp").read_text()),
        "pure survey model contains no association API")

cleanup = body(UI, "void destroy_ui_resource_handles()")
cancel = body(UI, "bool cancel_wifi_survey_scan(bool wait_for_callback)")
wait = body(UI, "bool wait_wifi_survey_callback(std::uint64_t sequence)")
quiet_start = cleanup.find("if (survey_scan_quiet)")
quiet_branch = braced_after(cleanup, "if (survey_scan_quiet)")
else_start = cleanup.find("} else {", quiet_start)
else_branch = braced_after(cleanup[else_start:], "else") if else_start >= 0 else ""
require("cancel_wifi_survey_scan(true)" in cleanup and
        cleanup.index("cancel_wifi_survey_scan(true)") < cleanup.index("lv_timer_del(s_wifi_survey_timer)"),
        "UI teardown waits for survey callback quiescence before deleting timer")
require("wait_for_callback && wait_wifi_survey_callback(sequence)" in cancel and
        "xSemaphoreTake(s_wifi_survey_callback_done, remaining)" in wait and
        "elapsed >= budget" in wait,
        "survey callback wait is sequence-matched and bounded")
quiet_order = [quiet_branch.find(token) for token in (
    "drain_wifi_survey_scan_results()",
    "vQueueDelete(s_wifi_survey_scan_queue)",
    "vSemaphoreDelete(s_wifi_survey_context_mutex)",
    "vSemaphoreDelete(s_wifi_survey_callback_done)",
)]
require(all(position >= 0 for position in quiet_order) and
        quiet_order == sorted(quiet_order),
        "quiet teardown drains queued results before deleting queue and mutex")
require("Timeout aguardando callback survey" in else_branch,
        "semaphore is deleted only after quiescence; timeout enters quarantine")
require(all(token not in else_branch for token in
            ("vQueueDelete(s_wifi_survey_scan_queue)",
             "vSemaphoreDelete(s_wifi_survey_context_mutex)",
         "vSemaphoreDelete(s_wifi_survey_callback_done)")),
        "timeout quarantine preserves survey queue, mutex, and completion semaphore")

export_start = UI.find("bool shell_session_host::wifi_survey_export(bool confirm, bool masked)")
export_end = UI.find("std::string shell_session_host::wifi_survey_render()", export_start)
export = UI[export_start:export_end] if export_start >= 0 and export_end > export_start else ""
worker_start = UI.find("void wifi_survey_export_worker(void *argument)")
worker_end = UI.find("void process_wifi_survey(lv_timer_t *)", worker_start)
worker = UI[worker_start:worker_end] if worker_start >= 0 and worker_end > worker_start else ""
require("xSemaphoreCreateBinary()" in export and
        export.index("xTaskCreate(wifi_survey_export_worker") < export.index("wifi survey export accepted:") <
        export.index("xSemaphoreGive(job->start_gate)"),
        "worker is gated until accepted is logged, then released")
require("xSemaphoreTake(job->start_gate, portMAX_DELAY)" in worker and
        worker.index("xSemaphoreTake(job->start_gate, portMAX_DELAY)") <
        worker.index("stage=started"),
        "worker cannot log started before consuming the acceptance gate")
create_failure_start = export.find('if (xTaskCreate(wifi_survey_export_worker')
create_failure = export[create_failure_start:] if create_failure_start >= 0 else ""
require('reason=create_task_failed' in create_failure and
        'vSemaphoreDelete(job->start_gate)' in create_failure and
        create_failure.index('vSemaphoreDelete(job->start_gate)') < create_failure.index('delete job;') <
        create_failure.index('s_wifi_survey_export_busy.store(false') and
        create_failure.index('reason=create_task_failed') > create_failure.index('s_wifi_survey_export_busy.store(false'),
        "task creation failure reports a distinct reason and releases gate/job/busy")
gate_failure_start = export.find('if (job->start_gate == nullptr)')
gate_failure_end = export.find('s_wifi_survey_export_result = {}', gate_failure_start)
gate_failure = export[gate_failure_start:gate_failure_end] if gate_failure_start >= 0 and gate_failure_end > gate_failure_start else ""
require('reason=start_gate_creation_failed' in gate_failure and
        gate_failure.index('delete job;') < gate_failure.index('s_wifi_survey_export_busy.store(false') <
        gate_failure.index('reason=start_gate_creation_failed'),
        "gate creation failure reports a distinct reason and releases job/busy")
require('persist_document(' in worker and
        worker.index('persist_document(') < worker.index('stage=completed outcome=published') <
        worker.rindex('s_wifi_survey_export_done.store(true'),
        "success completion/ACK signal follows successful persistence publication")
published_line = next((line.strip() for line in worker.splitlines()
                       if 'stage=completed outcome=published' in line), '')
completion_start = UI.find('void process_wifi_survey(lv_timer_t *)')
completion_end = UI.find('void ', completion_start + len('void process_wifi_survey(lv_timer_t *)'))
completion = UI[completion_start:completion_end] if completion_start >= 0 and completion_end > completion_start else ""
require(published_line.startswith('if (ok) ESP_LOGI') and
        'append_line(std::string("wifi survey saved: ")' in completion and
        completion.index('if (s_wifi_survey_export_result.ok)') <
        completion.index('append_line(std::string("wifi survey saved: ")'),
        "saved ACK is conditional on persistence reporting published success")
export_log_lines = [line for line in export.splitlines() if 'ESP_LOG' in line]
worker_log_lines = [line for line in worker.splitlines() if 'ESP_LOG' in line]
require(all(not re.search(r'payload|ssid|bssid|job->path|path\.c_str', line, re.I)
            for line in export_log_lines + worker_log_lines),
        "export diagnostic logs do not include SSID, BSSID, payload, or destination")

for passed, description in checks:
    if not passed:
        print(f"FAIL: {description}")
print(f"wifi survey integration wiring: {sum(ok for ok, _ in checks)}/{len(checks)} checks")
sys.exit(0 if all(ok for ok, _ in checks) else 1)
