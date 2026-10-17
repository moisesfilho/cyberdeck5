#!/usr/bin/env python3
"""Guard controller-based confirmation, queue, worker, and UI result wiring."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
UI_PATH = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
HEADER_PATH = ROOT / "components/cyberdeck/include/apps/wifi/cyberdeck_wifi_survey_export_controller.h"
UI = UI_PATH.read_text()
HEADER = HEADER_PATH.read_text()


def body(source, marker):
    start = source.find(marker)
    opening = source.find("{", start)
    if start < 0 or opening < 0:
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


confirm = body(UI, "bool shell_session_host::wifi_survey_export(bool confirm, bool masked)")
port_commit = body(UI, "void commit(std::uint32_t token) override")
worker = body(UI, "void wifi_survey_export_worker(void *argument)")
completion = body(UI, "void process_wifi_survey(lv_timer_t *)")
admit = body(UI, "cyberdeck_wifi_survey_export::admission_result admit(")
checks = [
    ("cyberdeck_wifi_survey_export::controller" in UI and
     "auto &controller = wifi_survey_export_controller()" in confirm and
     "controller.confirm(masked, now)" in confirm,
     "UI confirmation is routed through the shared production controller"),
    ("xTaskCreate" not in confirm and "xSemaphoreGive" not in confirm and
     "xSemaphoreGive" not in confirm and "new (std::nothrow)" not in confirm,
     "confirm path neither creates a task nor allocates/releases a start gate"),
    ("admission_result admit(" in UI and "admission_result commit" not in UI and
     "void commit(std::uint32_t token) override" in UI and
     port_commit.count("xSemaphoreGive") == 1,
     "accepted admission gate is released exactly once by the port commit seam"),
    ("xQueueCreate(WIFI_SURVEY_EXPORT_QUEUE_CAPACITY" in UI and
     "WIFI_SURVEY_EXPORT_QUEUE_CAPACITY = 1" in UI,
     "worker handoff and result queues remain capacity one"),
    ("xSemaphoreTake(owned_job->start_gate, portMAX_DELAY)" in worker and
     worker.find("start_gate, portMAX_DELAY") < worker.find("persist_document("),
     "app-lifetime worker waits for accepted commit before persistence"),
    ("if (controller.started(export_result.token))" in completion and
     "controller.complete(export_result.token, export_result.ok)" in completion and
     completion.index("if (controller.started(export_result.token))") <
     completion.index("controller.complete(export_result.token, export_result.ok)") and
     "completion_result::published" in completion and
     "completion_result::failed" in completion,
      "UI completion validates token/start before classifying published or failed result"),
    ("s_wifi_survey_export_busy.load(std::memory_order_acquire)" in admit and
     "admission_result::full" in admit and
     "s_wifi_survey_export_busy.store(true" in admit,
     "busy queue-port admission rejects without rearming/clearing active ownership"),
    ("s_wifi_survey_export_failed_token.store(owned_job->token" in worker and
     worker.count("s_wifi_survey_export_failed_token.store(owned_job->token") == 2,
     "both terminal worker handoff failures publish the admitted job token"),
    ("failed_token = s_wifi_survey_export_failed_token.exchange(0" in completion and
     "fail_admitted(failed_token)" in completion and
     "s_wifi_survey_export_busy.store(false" in completion,
     "pump consumes terminal failure token and releases busy ownership"),
    (completion.find("fail_admitted(failed_token)") <
     completion.find('append_line("wifi survey export failed\\n")', completion.find("failed_token")) and
     completion.find("append_line(\"wifi survey export failed\\n\")", completion.find("failed_token")) <
     completion.find("s_wifi_survey_export_busy.store(false", completion.find("failed_token")),
     "only a current admitted-token failure is reported before busy is released"),
    ("if (controller.started(export_result.token))" in completion and
     "completion_result::published && export_result.ok" in completion and
     "completion_result::failed" in completion and
     'append_line("wifi survey export failed\\n")' in completion,
     "only current-token persisted success emits success ACK; current failure emits failure only"),
    ("s_wifi_survey_export_busy.store(false" in completion and
     completion.index("controller.complete(export_result.token, export_result.ok)") <
     completion.index("s_wifi_survey_export_busy.store(false") and
     "std::fill(std::begin(export_result.path), std::end(export_result.path), '\\0')" in completion,
     "pump consumes retained completion before releasing slot and wipes result path"),
    ("outcome == cyberdeck_wifi_survey_export::completion_result::failed" in completion and
     'append_line("wifi survey export failed\\n")' in completion and
     completion.find('append_line("wifi survey export failed\\n")') <
     completion.find("s_wifi_survey_export_busy.store(false") <
     completion.find("render_terminal();"),
     "accepted persistence failure remains visible and releases/renders completion"),
    ("s_wifi_survey_export_queue" not in body(UI, "void destroy_ui_resource_handles()") and
     "s_wifi_survey_export_results" not in body(UI, "void destroy_ui_resource_handles()") and
     "s_wifi_survey_export_task" not in body(UI, "void destroy_ui_resource_handles()"),
     "export resources intentionally outlive UI deinit and retain queued completion for reinit"),
    ("xTaskCreate" not in confirm and "xQueueCreate" not in confirm and "xSemaphoreCreate" not in confirm,
     "confirmation does not create worker, queues, or gate resources"),
    ("string_wiper payload_wiper{payload}" in confirm and
     "string_wiper path_wiper{path}" in confirm and
     "s_wifi_survey_export_port.clear_staged()" in confirm,
     "confirmation wipes transient payload/path and staged port data on all exits"),
]

for passed, description in checks:
    if not passed:
        print(f"FAIL: {description}")
print(f"wifi survey export result/lifecycle: {sum(ok for ok, _ in checks)}/{len(checks)} checks")
sys.exit(0 if all(ok for ok, _ in checks) else 1)
