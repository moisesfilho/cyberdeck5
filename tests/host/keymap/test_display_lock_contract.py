#!/usr/bin/env python3
"""Structural concurrency contracts for boot and Serial-JTAG display access."""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
APP = ROOT / "main/app_main.cpp"
BRIDGE = ROOT / "components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp"
HTTP = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_display_port.cpp"
DISPATCH = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_lvgl_dispatch.cpp"
DISPATCH_HEADER = ROOT / "components/cyberdeck/include/platform/display/cyberdeck_lvgl_dispatch.h"


def function_body(source: str, name: str) -> str:
    start = source.index(name)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:index]
    raise AssertionError(f"unclosed function: {name}")


def main() -> int:
    app = function_body(APP.read_text(encoding="utf-8"), "app_main")
    source = BRIDGE.read_text(encoding="utf-8")
    capture = function_body(source, "bool capture_screen(capture &out)")
    callback = function_body(source, "void capture_screen_on_lvgl(void *)")
    helper = DISPATCH.read_text(encoding="utf-8")
    http = HTTP.read_text(encoding="utf-8")
    failures: list[str] = []

    def require(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    lock = re.search(r"if\s*\(\s*!bsp_display_lock\s*\(\s*pdMS_TO_TICKS\(\s*1000\s*\)\s*\)\s*\)", app)
    require(lock is not None, "boot must fail closed when the display lock is unavailable")
    if lock:
        protected_start = min(
            (pos for token in ("imu_reader_start(", "cyberdeck_ui_init(", "screen_off_init(")
             if (pos := app.find(token)) >= 0),
            default=len(app),
        )
        require(lock.start() < protected_start, "boot lock guard must precede display startup")
        require("return;" in app[lock.start():protected_start], "boot lock failure must return")

    require("cyberdeck_lvgl_dispatch::run(capture_screen_on_lvgl" in capture,
            "serial capture must use the shared LVGL dispatcher")
    require("ensure_snapshot_storage()" in capture and "initialize_snapshot_draw_buf()" in capture,
            "serial capture must reuse persistent PSRAM storage and draw buffer")
    require("std::vector<std::uint16_t>" not in source, "screen capture must not allocate a packed frame copy")
    require(source.count("lv_snapshot_take_to_draw_buf(") == 1 and "lv_snapshot_take_to_draw_buf(" in callback,
            "serial rendering must be confined to its dispatched LVGL callback")
    require(helper.count("lv_async_call(") == 1 and "lv_snapshot_take" not in helper,
            "shared dispatcher owns the only LVGL async dispatch, not rendering")
    require("std::call_once" in helper and "xSemaphoreTake(s_admission, timeout)" in helper,
            "dispatcher must safely initialize static synchronization and serialize admission with deadline")
    state = (ROOT / "components/cyberdeck/src/platform/display/cyberdeck_lvgl_dispatch_state.cpp").read_text(encoding="utf-8")
    require("fetch_add" in helper and "compare_exchange_strong" in state and
            "State::queued" in state and "State::running" in state and
            "State::detached" in state,
            "dispatcher must use monotonic tokens and explicit atomic job states")
    require("cyberdeck_lvgl_dispatch_state::cancel(s_job)" in helper and
            "detach_or_observe_done(s_job)" in helper,
            "timeout must cancel queued jobs or transfer release ownership for running jobs")

    # The two reviewer defects of the shared dispatcher, pinned structurally:
    # (1) the LVGL callback may read the payload only after its claim succeeded,
    #     otherwise a stale callback executes the context of a newer operation;
    # (2) the callback releases admission only when the state machine transferred
    #     ownership, and a failed claim must return before finish()/release.
    callback_body = function_body(helper, "void dispatch_callback(void *token_ptr)")
    claim_at = callback_body.find("start(s_job, token)")
    payload_at = callback_body.find("claimed_payload(s_job)")
    require(claim_at >= 0 and payload_at > claim_at,
            "dispatch_callback must claim the token before reading the payload")
    failed_claim = re.search(r"if\s*\(\s*!\s*cyberdeck_lvgl_dispatch_state::start\(s_job,\s*token\)\s*\)\s*return\s*;",
                             callback_body)
    require(failed_claim is not None, "a failed claim must return before running or releasing")
    finish_at = callback_body.find("finish(s_job")
    release_at = callback_body.find("xSemaphoreGive(s_admission)")
    require(failed_claim is not None and finish_at > failed_claim.end() and release_at > failed_claim.end(),
            "a failed claim must neither finish the job nor release admission")
    require(re.search(r"if\s*\(\s*owner\s*==\s*AdmissionOwner::callback\s*\)\s*xSemaphoreGive\(s_admission\)\s*;",
                      callback_body) is not None,
            "dispatch_callback must release admission only for callback ownership")
    require(callback_body.count("xSemaphoreGive(s_admission)") == 1,
            "dispatch_callback must have a single admission release site")
    require("s_callback_fn" not in helper and "s_callback_ctx" not in helper,
            "dispatcher must not keep the callback payload in shared globals")
    require(re.search(r"queue\(s_job,\s*token,\s*payload\)", helper) is not None,
            "dispatcher must publish the payload through the job queue, not a global")
    require(re.search(r"Payload payload\{fn, ctx\};", helper) is not None,
            "run() must build a per-operation payload on its own stack")

    # Documented completion budget: lock (1000) + timeout (2000) + grace (250).
    dispatch_header = DISPATCH_HEADER.read_text(encoding="utf-8")
    lock_ms = re.search(r"k_display_lock_timeout_ms\s*=\s*(\d+)", dispatch_header)
    grace_ms = re.search(r"k_detach_grace_ms\s*=\s*(\d+)", dispatch_header)
    require(lock_ms is not None and lock_ms.group(1) == "1000",
            "display lock budget constant must stay 1000 ms")
    require(grace_ms is not None and grace_ms.group(1) == "250",
            "detach grace constant must stay 250 ms")
    require("3250" in dispatch_header, "header must document the 3250 ms worst-case budget")
    require(re.search(r"bsp_display_lock\(pdMS_TO_TICKS\(\s*" + lock_ms.group(1) + r"\s*\)\)", helper) is not None,
            "dispatcher lock wait must equal k_display_lock_timeout_ms")
    require(f"timeout_ms + k_detach_grace_ms" in helper,
            "dispatcher grace deadline must use the documented grace constant")
    require("portMAX_DELAY" not in helper and "bsp_display_lock(pdMS_TO_TICKS(1000))" in helper,
            "dispatcher must use bounded waits and acquire display lock before dispatch")

    require("xSemaphoreGive(s_done)" in helper and "cyberdeck_lvgl_dispatch_state::start(s_job, token)" in helper,
            "stale callbacks must neither execute nor signal")
    require("run(void (*fn)(void *), void *ctx" in DISPATCH_HEADER.read_text(encoding="utf-8") and
            "remain valid until the callback completes" in DISPATCH_HEADER.read_text(encoding="utf-8"),
            "dispatcher API must document persistent callback context lifetime")
    require("portMAX_DELAY" not in capture, "serial display flow must not use unbounded waits")
    require(re.search(r'xTaskCreate\(bridge_task_entry,\s*"serial_brg",\s*8192\b', source) is not None,
            "serial_brg stack must remain 8192 bytes")
    require(re.search(r"k_task_stack_bytes\s*=\s*6144", (ROOT / "components/cyberdeck/src/apps/screenshot/screenshot_server.cpp").read_text(encoding="utf-8")) is not None,
            "HTTP screenshot stack must remain 6144 bytes")
    http_capture = function_body(http, 'extern "C" esp_err_t cyberdeck_display_capture')
    http_callback = function_body(http, "void take_snapshot_on_lvgl(void *)")
    require(http.count("lv_snapshot_take_to_draw_buf(") == 1 and
            "lv_snapshot_take_to_draw_buf(" in http_callback,
            "HTTP rendering must be confined to its dispatched LVGL callback")
    run_at = http_capture.find("cyberdeck_lvgl_dispatch::run(")
    require(run_at >= 0 and http_capture.find("std::malloc(size)") > run_at and
            http_capture.find("std::memcpy(data") > run_at,
            "HTTP frame allocation/copy must happen only after successful dispatcher return")
    require("lv_snapshot_take_to_draw_buf(" in http_callback and "std::malloc" not in http_callback and "std::memcpy" not in http_callback,
            "HTTP callback must snapshot to persistent draw buffer without allocating/copying frame")
    require("s_snapshot_initialized" in http_callback and "MALLOC_CAP_SPIRAM" in http,
            "HTTP snapshot must use a persistent PSRAM draw buffer")
    require(http_capture.find("*out = {}") < run_at and "if (data == nullptr) return ESP_ERR_NO_MEM" in http_capture,
            "HTTP failures must leave no partial frame")
    release = function_body(http, 'extern "C" void cyberdeck_display_release')
    require("std::free(frame->data)" in release and "*frame = {}" in release,
            "HTTP release must free and clear the frame")
    serializer = function_body(source, "bool screen_dump_init_strided(")
    require("source_stride_bytes" in serializer and "source_bytes" in serializer,
            "BMP serializer must consume snapshot stride and bounded source size")
    require("screen_dump_init_strided(req.rid.c_str()" in source,
            "screen.dump must serialize the caller-owned snapshot directly")
    dump = function_body(source, "device_result exec_screen_dump(")
    require(dump.find("capture_screen(cap)") < dump.find("screen_dump_init_strided(req.rid.c_str()"),
            "screen.dump must serialize only after capture has released LVGL lock")
    first_chunk_check = dump.find("screen_dump_get_chunk(req.rid.c_str(), 0")
    start_frame = dump.find('start += ",\\\"ok\\\":true,\\\"event\\\":\\\"start\\\"')
    require(first_chunk_check >= 0 and first_chunk_check < start_frame,
            "screen.dump must validate chunk 0 before emitting a success start frame")
    cancellation_check = dump.find("if (stop_requested())")
    require(first_chunk_check < cancellation_check < start_frame,
            "screen.dump must reject cancellation after chunk 0 and before success start")
    cancellation_return = dump.find('"cancelado"', cancellation_check, start_frame)
    require(cancellation_return >= 0 and "send_frame(build_error_envelope" in
            dump[cancellation_check:start_frame] and "return d;" in
            dump[cancellation_return:start_frame],
            "pre-start cancellation must emit an explicit error and return without success start")
    first_chunk_failure = dump.find('send_frame(build_error_envelope(req.rid, dispatch_error::internal, "chunk falhou"))')
    require(first_chunk_failure >= 0 and first_chunk_failure < start_frame and
            "return d;" in dump[first_chunk_failure:start_frame],
            "chunk 0 failure must return an explicit error before any success start frame")
    require(dump.find('if (failed)') < dump.find('send_frame(frame_event(req, "end"))'),
            "stream failure must be handled before any success end event")
    failure_branch = dump[dump.find("if (failed)"):dump.find('send_frame(frame_event(req, "end"))')]
    require("build_error_envelope" in failure_branch and "return d;" in failure_branch,
            "failed/cancelled chunk stream must emit an error and return without success end")

    ui_dump = function_body(source, 'if (req.type == "ui.dump")')
    require(ui_dump.count("bsp_display_lock(") == 1, "ui.dump must acquire one display lock")
    require(ui_dump.count("bsp_display_unlock()") == 1, "ui.dump must release its display lock")
    require(ui_dump.find("dump_obj(") < ui_dump.find("bsp_display_unlock()"),
            "ui.dump traversal must precede unlock")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("PASS: display lock, fail-closed boot, snapshot lifetime, and LVGL access")
    return 0


if __name__ == "__main__":
    sys.exit(main())
