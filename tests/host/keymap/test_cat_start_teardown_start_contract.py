#!/usr/bin/env python3
"""Structural lifecycle contract for repeated cat-worker start/teardown.

FreeRTOS/LVGL are intentionally not faked on the host.  The contract catches
the lifecycle ordering that cannot be proven by the pure shell tests: a
binary stop semaphore must be drained for every generation, the join must be
the terminal observation of the worker, and no stale queue/callback state may
cross a restart.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
WORKER = ROOT / "components/cyberdeck/src/features/shell/cyberdeck_cat_worker.cpp"


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
    source = WORKER.read_text(encoding="utf-8")
    start = body(source, "bool cyberdeck_cat_worker_start")
    teardown = body(source, "void cyberdeck_cat_worker_teardown(void)")
    worker = body(source, "void cat_worker_task")
    delivery = body(source, "void deliver_cat_result")

    # Each generation must consume a stale binary-semaphore token before its
    # task can be created; otherwise start-teardown-start can join immediately.
    require("xSemaphoreCreateBinary()" in start, "stop semaphore must be binary")
    before(start, "xSemaphoreTake(s_stopped, 0)", "xTaskCreate(",
           "start must drain s_stopped before creating a new worker")
    require("s_closing = false;" in start, "restart must reopen lifecycle state")

    # The worker announces termination and the task-present teardown joins it.
    # Deleting a handle after the task has already ended is explicitly forbidden.
    require("xSemaphoreGive(s_stopped)" in worker,
            "worker must publish its stopped state")
    no_task = body(teardown, "if (s_task == nullptr)")
    require("vQueueDelete(s_queue)" in no_task and "s_queue = nullptr" in no_task,
            "no-task teardown must release queue state without a join")
    require("xSemaphoreTake(s_stopped" not in no_task and "xTaskNotifyGive" not in no_task,
            "no-task teardown must not claim a task acknowledgement")
    no_task_end = teardown.find("return;", teardown.find("if (s_task == nullptr)"))
    require(no_task_end >= 0, "no-task teardown branch must return")
    task_branch = teardown[teardown.find("s_closing = true;", no_task_end):]
    before(task_branch, "xTaskNotifyGive", "xSemaphoreTake(s_stopped, portMAX_DELAY)",
           "task teardown must wake before joining")
    before(task_branch, "xSemaphoreTake(s_stopped, portMAX_DELAY)", "vQueueDelete(s_queue)",
           "queue must survive until the worker join")
    require("vTaskDelete(task)" not in task_branch,
            "teardown must not delete an already-ended task handle")

    # Queue and all callback/root state must be invalidated after the join and
    # must not leak into the next start.
    before(task_branch, "vQueueDelete(s_queue)", "s_queue = nullptr",
           "queue handle must be cleared after destruction")
    before(task_branch, "xSemaphoreTake(s_stopped, portMAX_DELAY)",
           "xSemaphoreTake(s_lifecycle_mutex, portMAX_DELAY)",
           "post-join reset must reacquire lifecycle synchronization")
    for state in ("s_task = nullptr", "std::memset(s_host_root, 0",
                  "s_callback = nullptr", "s_context = nullptr"):
        require(state in source, f"restart state must reset {state}")

    # A queued LVGL callback may run after teardown; it must be gated while the
    # lifecycle lock is held, and teardown must invalidate callback ownership
    # before waking the worker.
    before(delivery, "xSemaphoreTake(s_lifecycle_mutex", "if (!s_closing",
           "late callback must be lifecycle-gated")
    before(teardown, "s_callback = nullptr", "xTaskNotifyGive",
           "teardown must invalidate callback ownership before wakeup")
    require("s_context = nullptr" in teardown,
            "teardown must invalidate callback context")
    require("++s_generation" in start and "result->generation == s_generation" in delivery,
            "restart must invalidate stale callback generations")

    print("PASS: cat repeated start/teardown lifecycle contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
