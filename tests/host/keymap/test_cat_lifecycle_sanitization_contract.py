#!/usr/bin/env python3
"""Host structural contracts for the non-host-testable cat/UI seam.

FreeRTOS and LVGL are deliberately not faked here.  These checks constrain
ordering and byte-level behavior in the production seam without weakening the
runtime tests for the pure local shell.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
WORKER = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_cat_worker.cpp"
HEADER = ROOT / "components/cyberdeck/include/apps/shell/cyberdeck_cat_worker.h"
SHELL = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp"
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def body(source: str, marker: str) -> str:
    start = source.find(marker)
    require(start >= 0, f"missing {marker}")
    opening = source.find("{", start)
    require(opening >= 0, f"missing body for {marker}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    raise AssertionError(f"unterminated body for {marker}")


def before(text: str, first: str, second: str, message: str) -> None:
    require(first in text and second in text, message)
    require(text.index(first) < text.index(second), message)


def main() -> int:
    worker = WORKER.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    shell = SHELL.read_text(encoding="utf-8")
    ui = UI.read_text(encoding="utf-8")

    # Lifecycle: the no-task branch may only release queue/state; the task
    # branch must invalidate, wake, acknowledge/join, then destroy state.
    teardown = body(worker, "void cyberdeck_cat_worker_teardown(void)")
    no_task = body(teardown, "if (s_task == nullptr)")
    require("s_closing = true;" in no_task, "no-task teardown must close lifecycle state")
    require("vQueueDelete(s_queue)" in no_task and "s_queue = nullptr" in no_task,
            "no-task teardown must release and clear an orphaned queue")
    require("xTaskNotifyGive" not in no_task and "xSemaphoreTake(s_stopped" not in no_task,
            "no-task teardown must not notify or join a nonexistent task")

    no_task_end = teardown.find("return;", teardown.find("if (s_task == nullptr)"))
    require(no_task_end >= 0, "no-task teardown branch must return")
    task_branch = teardown[teardown.find("s_closing = true;", no_task_end):]
    before(task_branch, "s_closing = true;", "xTaskNotifyGive", "teardown must cancel before wakeup")
    before(task_branch, "xTaskNotifyGive", "xSemaphoreTake(s_stopped, portMAX_DELAY)",
           "task teardown must wake before waiting for the stop acknowledgement")
    before(task_branch, "xSemaphoreTake(s_stopped, portMAX_DELAY)", "vQueueDelete(s_queue)",
           "queue must survive until the worker is joined")
    require("vTaskDelete(task)" not in task_branch,
            "join acknowledgement must replace deletion of an already-ended task")
    require("xSemaphoreTake(s_lifecycle_mutex, portMAX_DELAY)" in task_branch,
            "post-join state reset must be synchronized")
    require("Idempotent" in header and "cyberdeck_cat_worker_teardown" in header,
            "public contract must document idempotent teardown")

    delivery = body(worker, "void deliver_cat_result")
    before(delivery, "xSemaphoreTake(s_lifecycle_mutex", "if (!s_closing", "callback must lock and gate lifecycle")
    require("s_callback(result->output.data(), result->output.size(), result->accepted, s_context)" in delivery,
            "accepted callback must preserve bounded result ownership")
    require("scheduled != LV_RESULT_OK" in worker and "delete result" in worker,
            "failed async scheduling must release the result")
    require("++s_generation" in worker, "each start must publish a new callback generation")
    require("result->generation == s_generation" in delivery,
            "late callbacks must be rejected across generations")

    enqueue = body(worker, "bool cyberdeck_cat_worker_enqueue")
    require("!s_closing" in enqueue and "xQueueSend(s_queue, &request, 0)" in enqueue,
            "cancelled cat requests must not block or enter the queue")
    require(re.search(r"k_cat_work_queue_capacity\s*=\s*8", worker),
            "cat queue must remain bounded at eight requests")

    # Cat itself must be bounded before and during descriptor reads.
    require("k_cat_max_output_bytes = 12288" in shell, "cat output budget must be 12288")
    require("k_cat_read_chunk_bytes = 1024" in shell, "cat reads must use a bounded chunk")
    cat = shell[shell.find('if (command == "cat")'):]
    require("fstat" in cat and "file_info.st_size" in cat, "cat must preflight the opened descriptor")
    require("output.size() > k_cat_max_output_bytes" in cat, "cat must enforce a bounded accumulated output")

    # Sanitization contract: invalid UTF-8 is replaced, not emitted raw; C0/DEL
    # are replaced too, while TAB/LF/CR and valid UTF-8 survive byte-for-byte.
    sanitizer = body(ui, "auto sanitize_for_lvgl")
    require('clean.append("\\xEF\\xBF\\xBD")' in sanitizer,
            "sanitizer must use the UTF-8 replacement character")
    require("first == '\\n' || first == '\\r' || first == '\\t'" in sanitizer,
            "sanitizer must preserve permitted controls")
    require("first != 0x7F" in sanitizer and "first >= 0x20" in sanitizer,
            "sanitizer must replace C0 controls and DEL")
    for marker in ("(bytes[i + j] & 0xC0) != 0x80", "codepoint < 0x80",
                   "codepoint < 0x800", "codepoint > 0x10FFFF", "0xD800"):
        require(marker in sanitizer, f"sanitizer must reject malformed UTF-8: {marker}")
    require("clean.size() + length > limit" in sanitizer,
            "sanitizer must stop before exceeding the terminal bound")
    require("append_line(safe_output)" in ui and "append_line(output != nullptr" in ui,
            "both accepted and rejected cat results must use sanitized output")
    require("append_line(output, output_length)" not in ui,
            "raw cat output must never bypass sanitization")

    print("PASS: cat lifecycle, boundedness and sanitization structural contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
