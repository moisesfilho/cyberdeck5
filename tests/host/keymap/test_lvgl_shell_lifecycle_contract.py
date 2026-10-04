#!/usr/bin/env python3
"""TEST-LVGL-01/02: lifecycle-to-LVGL and cat-worker handoff contracts.

The production UI is not host-linkable.  These assertions therefore protect
the observable scheduling boundary: application start cannot render, the
first prompt is dirty-rendered by the LVGL timer, and cat results are queued,
woken, generation-checked, and quiesced before teardown releases state.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
APP = (ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_app.cpp").read_text()
UI = (ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp").read_text()
WORKER = (ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_cat_worker.cpp").read_text()


def body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unclosed {signature}")


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    failures: list[str] = []
    start = body(APP, "bool application::start()")
    init = body(UI, 'extern "C" esp_err_t cyberdeck_ui_init(void)')
    teardown = body(UI, "void destroy_ui_resource_handles()")
    result = body(UI, "void on_cat_result(")
    worker_task = body(WORKER, "void cat_worker_task(void *)")
    worker_start = body(WORKER, "bool cyberdeck_cat_worker_start(")
    worker_stop = body(WORKER, "void cyberdeck_cat_worker_teardown(void)")

    # TEST-LVGL-01: no direct render in application lifecycle; initial prompt
    # is deferred to the existing timer callback in the LVGL task.
    require("render_terminal" not in start and "host_->render" not in start and "lv_" not in start,
            "TEST-LVGL-01: shell start must not render or call LVGL", failures)
    require("s_terminal_output_timer = lv_timer_create(process_terminal_output, 100" in init,
            "TEST-LVGL-01: UI must own a periodic LVGL output timer", failures)
    require("s_terminal_output_dirty = true;" in init,
            "TEST-LVGL-01: init must defer the first prompt as dirty output", failures)
    require("if (s_terminal_output_dirty) render_terminal();" in UI,
            "TEST-LVGL-01: dirty output must render from the LVGL timer", failures)

    # TEST-LVGL-02: accepted async work wakes the port once; failed scheduling
    # rolls back ownership, and teardown invalidates/joins before queue release.
    require("lv_async_call(deliver_cat_result, result)" in worker_task,
            "TEST-LVGL-02: cat result must be deferred through LVGL", failures)
    require("if (scheduled != LV_RESULT_OK)" in worker_task and "delete result;" in worker_task,
            "TEST-LVGL-02: failed LVGL scheduling must roll back the result", failures)
    require("lvgl_port_task_wake(LVGL_PORT_EVENT_USER, nullptr)" in worker_task,
            "TEST-LVGL-02: accepted result must explicitly wake the LVGL port", failures)
    require("result->generation == s_generation" in WORKER and "!s_closing" in WORKER,
            "TEST-LVGL-02: late callbacks must be rejected by close and generation", failures)
    require("++s_generation;" in worker_start and "(void)xSemaphoreTake(s_stopped, 0);" in worker_start,
            "TEST-LVGL-02: restart must consume stop state and advance generation", failures)
    require("xTaskNotifyGive(task)" in worker_stop and "xSemaphoreTake(s_stopped" in worker_stop,
            "TEST-LVGL-02: teardown must signal and join the worker", failures)
    require("vQueueDelete(s_queue)" in worker_stop and "s_callback = nullptr;" in worker_stop,
            "TEST-LVGL-02: teardown must clear callback before releasing queue", failures)
    require(teardown.index("cyberdeck_cat_worker_teardown()") < teardown.index("s_shell_app.detach_console()"),
            "TEST-LVGL-02: UI teardown quiesces cat callbacks before detaching console", failures)
    require("render_terminal();" in result,
            "TEST-LVGL-02: accepted cat output is applied only in the LVGL callback", failures)

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("PASS: LVGL shell lifecycle contracts (TEST-LVGL-01/02)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
