#!/usr/bin/env python3
"""Structural contract for the Tab-completion worker (REQ-TAB-05).

The worker body is `#if defined(ESP_PLATFORM)`: the host build compiles only the
fail-closed half (asserted behaviorally by `test_shell_completion`).  FreeRTOS is
intentionally not faked here, so this contract pins the lifecycle and ownership
rules that no host execution can prove:

  * the SD directory is enumerated only while a READ lease of the SD coordinator
    is held, and a missing lease/scan yields an empty result (line preserved);
  * a request is bounded and refused (never truncated silently) when it cannot be
    queued, with token 0 and post-teardown requests rejected;
  * a delivery is gated by the lifecycle lock, closing flag, delivery flag and
    callback ownership, and its heap payload is released on every path;
  * teardown invalidates delivery before waking the worker, joins with a bounded
    deadline, and only then releases the queue.

The LVGL boundary lives in `platform/display/cyberdeck_ui_post`: the application
worker schedules an opaque payload and never mentions LVGL, and the port
schedules before waking the display task, reports refusal before any wake and
never owns the payload.  Its behavior (callback ran, wake counted, refusal
without wake) is executed by `test_shell_completion`.

Sensitivity is proven by applying mutations to an in-memory copy of the sources
and requiring the contract to fail for each of them.
"""

from __future__ import annotations

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
WORKER = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_completion_worker.cpp"
HEADER = ROOT / "components/cyberdeck/include/apps/shell/cyberdeck_shell_completion.h"
WORKER_HEADER = ROOT / "components/cyberdeck/include/apps/shell/cyberdeck_completion_worker.h"
PORT = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui_post.cpp"
PORT_HEADER = ROOT / "components/cyberdeck/include/platform/display/cyberdeck_ui_post.h"
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
MAKEFILE = ROOT / "tests/host/keymap/Makefile"


class Violations(list):
    def require(self, condition: bool, message: str) -> None:
        if not condition:
            self.append(message)

    def before(self, text: str, first: str, second: str, message: str) -> None:
        if first not in text or second not in text:
            self.append(message + " (missing marker)")
            return
        if text.index(first) > text.index(second):
            self.append(message)


def body(source: str, marker: str) -> str:
    start = source.find(marker)
    if start < 0:
        raise AssertionError(f"missing {marker}")
    opening = source.find("{", start)
    if opening < 0:
        raise AssertionError(f"missing body for {marker}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    raise AssertionError(f"unterminated body for {marker}")


def code_only(source: str) -> str:
    """Source without comments: a comment may name a forbidden API."""
    stripped = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//[^\n]*", "", stripped)


def violations(sources: dict[str, str]) -> Violations:
    found = Violations()
    worker = sources["worker"]
    header = sources["header"]
    worker_header = sources["worker_header"]
    port = sources["port"]
    port_header = sources["port_header"]
    ui = sources["ui"]
    makefile = sources["makefile"]

    fill = body(worker, "void fill_result(")
    enqueue = body(worker, "bool enqueue(")
    task = body(worker, "void completion_task(")
    deliver = body(worker, "void deliver_result(")
    stop = body(worker, "bool stop_worker(")
    start = body(worker, "bool start(")
    quiesce = body(worker, "bool quiesce(")
    teardown = body(worker, "void teardown()")

    # --- SD lease: enumeration only under a READ lease, empty result if refused.
    found.require("s_sd_io->acquire(cyberdeck_sd_io::access::read)" in fill,
                  "fill_result must acquire the READ lease of the SD coordinator")
    found.require("access::write" not in fill,
                  "fill_result must never take a write lease")
    found.before(fill, "s_sd_io->acquire(", "operation();",
                 "the lease must be acquired before the scan runs")
    found.require("if (read_lease)\n            operation();" in fill,
                  "the scan must run only while the lease is held (fail-closed)")
    found.require("cyberdeck_shell_completion::scan_directory" in fill,
                  "fill_result must call the confined scanner")
    found.require("candidates.clear()" in fill,
                  "a refused scan must clear candidates so the line is preserved")

    # --- enqueue: bounded, non-blocking, token/generation gated.
    found.require("token == 0" in enqueue, "enqueue must reject token 0")
    found.require("s_shutdown" in enqueue and "s_delivery_disabled" in enqueue,
                  "enqueue must reject requests during/after teardown")
    found.require("request.directory.front() != '/'" in enqueue,
                  "enqueue must reject a non-absolute directory")
    found.require("k_max_directory_bytes" in enqueue and "k_max_name_bytes" in enqueue,
                  "enqueue must bound directory and name prefix")
    found.require("std::memcpy(incoming.directory, request.directory.data(), "
                  "request.directory.size())" in enqueue,
                  "enqueue must copy exactly the bounded directory bytes")
    found.require("std::memcpy(incoming.name_prefix, request.name_prefix.data(), "
                  "request.name_prefix.size())" in enqueue,
                  "enqueue must copy exactly the bounded name prefix bytes")
    found.require("xQueueSend(s_queue, &incoming, 0)" in enqueue,
                  "enqueue must never block: a full queue is refused")
    found.require("!s_closing && s_queue != nullptr" in enqueue,
                  "enqueue must check closing and the live queue under the lock")

    # --- delivery: lifecycle-gated callback with owned payload.
    found.before(deliver, "xSemaphoreTake(s_lifecycle_mutex", "if (!s_closing",
                 "a late callback must be lifecycle-gated")
    found.require("!s_closing && !s_delivery_disabled.load(" in deliver,
                  "delivery must check both closing and the delivery flag")
    found.require("s_callback != nullptr" in deliver,
                  "delivery must require a live callback")
    found.require("s_callback(*completed, s_context)" in deliver,
                  "delivery must invoke the stored callback with its context")
    found.require(deliver.count("delete completed;") == 1,
                  "the delivery payload must be released exactly once")

    # --- worker task: termination token and no leaked payload.
    found.require("s_stopped != nullptr" in task and "xSemaphoreGive(s_stopped)" in task,
                  "the worker must publish its stopped state")
    found.require('#include "platform/display/cyberdeck_ui_post.h"' in worker,
                  "the app-layer worker must schedule through the display-task port")
    found.require("cyberdeck_ui_post::post(deliver_result, completed)" in task,
                  "the worker must hand the result to the display-task port")
    found.before(task, "xSemaphoreTake(s_lifecycle_mutex",
                 "cyberdeck_ui_post::post(deliver_result, completed)",
                 "the delivery must be scheduled under the lifecycle lock")
    found.require("if (!scheduled)\n            delete completed;" in task,
                  "a refused scheduling must release its payload too")
    found.require(task.count("delete completed;") == 2,
                  "both non-delivery paths (unavailable lifecycle lock, refused scheduling) "
                  "must release the payload, and none on the accepted path")
    # The application TU owns no LVGL knowledge: no include, no call, no wake.
    worker_code = code_only(worker)
    for forbidden in ("lvgl", "lv_", "esp_lvgl_port", "LV_RESULT"):
        found.require(forbidden not in worker_code,
                      f"the app-layer worker must stay free of LVGL ({forbidden})")

    # --- teardown: invalidate delivery, bounded join, then release the queue.
    found.require(stop.startswith("{") and "s_delivery_disabled.store(true" in stop,
                  "teardown must disable delivery before anything else")
    found.before(stop, "s_delivery_disabled.store(true", "xSemaphoreTake(s_lifecycle_mutex",
                 "delivery must be disabled before waiting for the lifecycle lock")
    found.require(stop.count("s_callback = nullptr;") == 2,
                  "both teardown branches must invalidate the callback")
    found.require(stop.count("s_context = nullptr;") == 2,
                  "both teardown branches must invalidate the callback context")
    found.require("portMAX_DELAY" not in stop,
                  "teardown waits must be bounded (no portMAX_DELAY)")
    join = stop[stop.index("xTaskNotifyGive("):]
    found.require("vQueueDelete(s_queue)" in join and
                  "xSemaphoreTake(s_stopped, remaining())" in join,
                  "the task branch must join and release the queue")
    found.before(join, "xSemaphoreTake(s_stopped, remaining())", "vQueueDelete(s_queue)",
                 "the queue must outlive the worker join")
    found.require("if (xSemaphoreTake(s_stopped, remaining()) != pdTRUE)\n        return false;" in stop,
                  "a join timeout must return without releasing live resources")
    found.require("vTaskDelete(task)" not in stop,
                  "teardown must not delete an already-ended task handle")
    found.require("teardown()" in worker and "stop_worker(2000)" in teardown,
                  "teardown must join with a bounded deadline")

    # --- start/quiesce: idempotent restart, permanent shutdown fence.
    found.require("if (s_shutdown.load(std::memory_order_acquire))\n        return false;" in start,
                  "start must refuse after a permanent shutdown fence")
    found.require("if (s_task != nullptr)" in start and "return running;" in start,
                  "start must be idempotent while the worker is alive")
    found.before(start, "xSemaphoreTake(s_stopped, 0)", "xTaskCreate(",
                 "start must drain a stale stop token before creating the task")
    found.require("s_closing = false;" in start, "restart must reopen the lifecycle state")
    found.require("s_delivery_disabled.store(false" in start,
                  "restart must re-enable delivery")
    found.require("std::strlen(host_root) >= sizeof(s_host_root)" in start,
                  "start must bound the host root against the fixed buffer")
    stack = worker.find("k_worker_stack_size =")
    found.require(stack >= 0, "the worker stack must be named, not a literal at the call site")
    if stack >= 0:
        digits = ""
        for char in worker[stack + len("k_worker_stack_size ="):]:
            if char.isdigit():
                digits += char
            elif digits:
                break
        found.require(bool(digits) and int(digits) >= 4096,
                      "the worker stack must keep at least a 4 KiB budget")
    found.require("xTaskCreate(completion_task, \"completion_task\", k_worker_stack_size" in start,
                  "the worker task must use the named stack budget")
    found.require("s_shutdown.store(true" in quiesce and "stop_worker(timeout_ms)" in quiesce,
                  "quiesce must fence shutdown before stopping the worker")

    # --- display-task port: the only place the LVGL scheduling is spelled.
    post = body(port, "bool post(")
    found.require(port.count("lv_async_call(") == 1,
                  "the display port must own the single LVGL async scheduling call")
    found.require("lvgl_port_task_wake(" in post,
                  "the display port must wake the display task after scheduling")
    found.before(post, "lv_async_call(", "lvgl_port_task_wake(",
                 "the port must schedule before waking the display task")
    found.require("if (fn == nullptr)\n        return false;" in post,
                  "a null callback must be refused without touching LVGL")
    found.require("if (lv_async_call(fn, payload) != LV_RESULT_OK)\n        return false;" in post,
                  "a refused scheduling must report failure before any wake")
    found.require("delete" not in post and "free(" not in post,
                  "the port must never own or release the caller payload")
    found.require("using callback = void (*)(void *payload);" in port_header and
                  "bool post(callback fn, void *payload)" in port,
                  "the port must take an opaque callback and payload only")
    port_header_code = code_only(port_header)
    for forbidden in ("lvgl", "lv_", "esp_", "freertos", "xTask", "xSemaphore"):
        found.require(forbidden not in port_header_code,
                      f"the port header must stay free of platform types ({forbidden})")

    # --- module boundary: I/O and platform headers stay out of the pure part.
    for forbidden in ("<dirent.h>", "lvgl", "esp_", "freertos"):
        found.require(forbidden not in header,
                      f"the pure completion header must not include {forbidden}")
    found.require("bool scan_directory(" in header,
                  "the pure header must declare the single I/O entry point")
    found.require("lvgl" not in worker_header and "freertos" not in worker_header,
                  "the worker header must stay linkable on the host")
    found.require("cyberdeck_sd_io::coordinator &coordinator" in worker_header,
                  "the worker header must expose the SD coordinator binding")

    # --- UI wiring: port, binding, start and teardown.
    session_port = body(ui, "bool shell_session_host::completion_scan_async(")
    found.require("return s_completion_worker_ready && cyberdeck_completion_worker::enqueue(" in session_port,
                  "the UI port must refuse when the worker is not ready")
    bind = body(ui, "void cyberdeck_ui_bind_sd_io(")
    found.require("cyberdeck_completion_worker::bind_sd_io(coordinator);" in bind,
                  "the UI must bind the SD coordinator to the completion worker")
    init = body(ui, "extern \"C\" esp_err_t cyberdeck_ui_init(void)")
    found.require("cyberdeck_completion_worker::start(\"/sdcard\", on_completion_result, nullptr)" in init,
                  "the UI must start the worker with the confined host root")
    found.before(init, "cyberdeck_cat_worker_start(", "cyberdeck_completion_worker::start(",
                 "the shell worker must start after the cat worker composition point")
    destroy = body(ui, "void destroy_ui_resource_handles()")
    found.require("cyberdeck_completion_worker::teardown();" in destroy,
                  "destroy_ui_resource_handles must tear the worker down")
    found.before(destroy, "s_keyboard_dispatch.stop();",
                 "cyberdeck_completion_worker::teardown();",
                 "no completion delivery may outlive the stopped keyboard dispatch")
    found.require("s_shell_app.apply_completion_result(completed.token, completed.items, "
                  "completed.count,\n                                        completed.truncated);" in ui,
                  "the LVGL callback must forward token, items, count and truncation")
    found.require("s_completion_worker_ready = false;" in ui,
                  "the UI must clear the ready flag on deinit")

    # --- host build wiring: the ESP-only TU must still be compiled by a host
    # target, otherwise the coverage scope guard rejects it as a production TU
    # missing from the gcovr report.  The port is a new production TU, so it is
    # host-compiled and executed by the focused target instead of allowlisted.
    found.require("test_shell_completion/completion_worker.o: "
                  "../../../components/cyberdeck/src/apps/shell/cyberdeck_completion_worker.cpp"
                  in makefile,
                  "a host target must compile the completion worker TU")
    found.require("test_shell_completion/completion_worker.o" in makefile,
                  "the worker object must belong to the focused completion target")
    found.require("test_shell_completion/ui_post.o: "
                  "../../../components/cyberdeck/src/platform/display/cyberdeck_ui_post.cpp"
                  in makefile,
                  "a host target must compile the display-task port TU")
    found.require("test_shell_completion/ui_post.o" in
                  makefile.split("SHELL_COMPLETION_OBJS :=", 1)[-1].split("\n#", 1)[0],
                  "the port object must belong to the focused completion target")
    found.require("test_shell_completion_bin" in makefile.split("BINS :=", 1)[-1].split("\n", 1)[0],
                  "the focused completion binary must stay in BINS so the object is built"
                  " and reported by coverage")
    return found


MUTATIONS = [
    ("lease removed",
     "worker",
     "auto read_lease = s_sd_io->acquire(cyberdeck_sd_io::access::read);",
     "auto read_lease = cyberdeck_sd_io::lease{};"),
    ("scan outside the lease",
     "worker", "        if (read_lease)\n            operation();", "        operation();"),
    ("blocking enqueue",
     "worker", "xQueueSend(s_queue, &incoming, 0)", "xQueueSend(s_queue, &incoming, portMAX_DELAY)"),
    ("token 0 accepted",
     "worker", "if (token == 0 || s_shutdown", "if (s_shutdown"),
    ("delivery flag not disabled",
     "worker",
     "s_delivery_disabled.store(true, std::memory_order_release);\n    const TickType_t started",
     "const TickType_t started"),
    ("callback not invalidated",
     "worker", "    s_closing = true;\n    s_callback = nullptr;",
     "    s_closing = true;"),
    ("unbounded join",
     "worker",
     "if (xSemaphoreTake(s_stopped, remaining()) != pdTRUE)\n        return false",
     "if (xSemaphoreTake(s_stopped, portMAX_DELAY) != pdTRUE)\n        return false"),
    ("payload leaked on refused scheduling",
     "worker", "        if (!scheduled)\n            delete completed;", "        (void)scheduled;"),
    ("start does not drain the stop token",
     "worker", "(void)xSemaphoreTake(s_stopped, 0);", ""),
    ("worker schedules LVGL itself",
     "worker", "cyberdeck_ui_post::post(deliver_result, completed)",
     "lv_async_call(deliver_result, completed)"),
    ("display port wake removed",
     "port", "    (void)lvgl_port_task_wake(LVGL_PORT_EVENT_USER, nullptr);\n", ""),
    ("display port wakes before scheduling",
     "port",
     "    if (lv_async_call(fn, payload) != LV_RESULT_OK)\n        return false;\n"
     "    (void)lvgl_port_task_wake(LVGL_PORT_EVENT_USER, nullptr);",
     "    (void)lvgl_port_task_wake(LVGL_PORT_EVENT_USER, nullptr);\n"
     "    if (lv_async_call(fn, payload) != LV_RESULT_OK)\n        return false;"),
    ("display port keeps the payload on refusal",
     "port",
     "    if (lv_async_call(fn, payload) != LV_RESULT_OK)\n        return false;",
     "    if (lv_async_call(fn, payload) != LV_RESULT_OK) {\n"
     "        delete static_cast<char *>(payload);\n        return false;\n    }"),
]

UI_MUTATIONS = [
    ("port ignores readiness",
     "ui",
     "return s_completion_worker_ready && cyberdeck_completion_worker::enqueue(request, token);",
     "return cyberdeck_completion_worker::enqueue(request, token);"),
    ("teardown missing",
     "ui", "    cyberdeck_completion_worker::teardown();\n", ""),
]

HEADER_MUTATIONS = [
    ("platform header leaks into the pure module",
     "header", '#include <string_view>', '#include <string_view>\n#include <dirent.h>'),
]

WORKER_HEADER_MUTATIONS = [
    ("worker header drags FreeRTOS in",
     "worker_header", "#include <cstdint>", "#include <cstdint>\n#include \"freertos/FreeRTOS.h\""),
]

PORT_HEADER_MUTATIONS = [
    ("port header exposes an LVGL type",
     "port_header", "using callback = void (*)(void *payload);",
     "#include \"lvgl.h\"\nusing callback = void (*)(void *payload);"),
]

MAKEFILE_MUTATIONS = [
    ("worker TU dropped from the host build",
     "makefile",
     "test_shell_completion/completion_worker.o: "
     "../../../components/cyberdeck/src/apps/shell/cyberdeck_completion_worker.cpp",
     "test_shell_completion_orphan.o: "
     "../../../components/cyberdeck/src/apps/shell/cyberdeck_completion_worker.cpp"),
    ("port TU dropped from the host build",
     "makefile",
     "test_shell_completion/ui_post.o: "
     "../../../components/cyberdeck/src/platform/display/cyberdeck_ui_post.cpp",
     "test_shell_completion_orphan_post.o: "
     "../../../components/cyberdeck/src/platform/display/cyberdeck_ui_post.cpp"),
    ("focused binary dropped from BINS",
     "makefile", " $(BATTERY_PROTECTION_BIN) test_shell_completion_bin", " $(BATTERY_PROTECTION_BIN)"),
]

ALL_MUTATIONS = (MUTATIONS + UI_MUTATIONS + HEADER_MUTATIONS + WORKER_HEADER_MUTATIONS
                 + PORT_HEADER_MUTATIONS + MAKEFILE_MUTATIONS)


def main() -> int:
    sources = {
        "worker": WORKER.read_text(encoding="utf-8"),
        "header": HEADER.read_text(encoding="utf-8"),
        "worker_header": WORKER_HEADER.read_text(encoding="utf-8"),
        "port": PORT.read_text(encoding="utf-8"),
        "port_header": PORT_HEADER.read_text(encoding="utf-8"),
        "ui": UI.read_text(encoding="utf-8"),
        "makefile": MAKEFILE.read_text(encoding="utf-8"),
    }

    current = violations(sources)
    if current:
        for message in current:
            print(f"FAIL: {message}")
        raise SystemExit(1)

    for label, target, old, new in ALL_MUTATIONS:
        if old not in sources[target]:
            print(f"FAIL: mutation anchor not found: {label}")
            raise SystemExit(1)
        mutated = dict(sources)
        mutated[target] = sources[target].replace(old, new, 1)
        try:
            detected = violations(mutated)
        except AssertionError:
            detected = ["body parse error"]
        if not detected:
            print(f"FAIL: contract did not detect mutation: {label}")
            raise SystemExit(1)

    print(f"PASS: shell completion worker contract ({len(ALL_MUTATIONS)} mutations rejected)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
