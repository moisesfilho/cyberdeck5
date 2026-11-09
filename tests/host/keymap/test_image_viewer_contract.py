#!/usr/bin/env python3
"""Structural contract for the isolated image viewer (REQ/AC-IMG-01..05,
REQ/IMGF-01..08).

The behavioural suites (``test_image_bmp``, ``test_image_fit``,
``test_image_decode``, ``test_image_list``, ``test_image_navigation``,
``test_image_view``) prove what the production code does.  This contract proves
the wiring around it, which no single binary can observe:

* the manifest is the approved one (id, foreground type, resources, single
  ``image`` command, no console ownership, and *not* requiring an active
  foreground - that last field is what makes ``image <path>`` with an argument
  reach the application at all);
* the registry capacity was raised in **both** independent constants and the
  new application fits, so ``register_application()`` cannot fail and abort the
  boot;
* the application is registered in ``k_apps`` and listed in the failure
  diagnostics so a degraded start stays visible;
* the CHANGELOG-free versioning rules of ``AGENTS.md`` hold: ``version.txt`` is
  ``MAJOR.MINOR.PATCH`` without zero padding and never regresses below the
  delivered floor, and the app carries its own semver;
* the LVGL surface is created, torn down and key-gated by the UI, and the
  application/view never touch the filesystem from the LVGL task or LVGL from
  the worker;
* a failed *navigation* keeps the viewer open with a typed error (recoverable
  channel C8..C13), while a failed initial `image <path>` still reports to the
  terminal and returns to the shell;
* the Makefile keeps every new target reachable from the aggregate suite.

It reads source text only: no LVGL, ESP-IDF, simulator, bridge or device.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SYSTEM_APPS = ROOT / "components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp"
RUNTIME_HEADER = ROOT / "components/cyberdeck/include/apps/runtime/cyberdeck_app_runtime.h"
QUOTA_HEADER = ROOT / "components/cyberdeck/include/apps/runtime/cyberdeck_app_quota.h"
APP_SOURCE = ROOT / "components/cyberdeck/src/apps/image/cyberdeck_image_app.cpp"
WORKER_STATE_SOURCE = ROOT / "components/cyberdeck/src/apps/image/cyberdeck_image_worker_state.cpp"
WORKER_STATE_HEADER = ROOT / "components/cyberdeck/include/apps/image/cyberdeck_image_worker_state.h"
APP_HEADER = ROOT / "components/cyberdeck/include/apps/image/cyberdeck_image_app.h"
VIEW_SOURCE = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_image_view.cpp"
VIEW_HEADER = ROOT / "components/cyberdeck/include/platform/display/cyberdeck_image_view.h"
BMP_SOURCE = ROOT / "components/cyberdeck/src/apps/image/cyberdeck_image_bmp.cpp"
BMP_HEADER = ROOT / "components/cyberdeck/include/apps/image/cyberdeck_image_bmp.h"
FIT_SOURCE = ROOT / "components/cyberdeck/src/apps/image/cyberdeck_image_fit.cpp"
DECODE_SOURCE = ROOT / "components/cyberdeck/src/apps/image/cyberdeck_image_decode.cpp"
LIST_SOURCE = ROOT / "components/cyberdeck/src/apps/image/cyberdeck_image_list.cpp"
LIST_HEADER = ROOT / "components/cyberdeck/include/apps/image/cyberdeck_image_list.h"
NAVIGATION_SOURCE = ROOT / "components/cyberdeck/src/apps/image/cyberdeck_image_navigation.cpp"
NAVIGATION_HEADER = ROOT / "components/cyberdeck/include/apps/image/cyberdeck_image_navigation.h"
UI_SOURCE = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
CMAKE = ROOT / "components/cyberdeck/CMakeLists.txt"
VERSION = ROOT / "version.txt"
MAKEFILE = ROOT / "tests/host/keymap/Makefile"
CODE_MAP = ROOT / "code-map.md"
MANUAL = ROOT / "tests/manual/image-viewer-validation.pt-BR.md"

# Positional field order of `struct manifest`; a reordered header must fail this
# contract instead of silently shifting every index below.
MANIFEST_FIELDS = (
    "id", "name", "version", "description", "command",
    "dependencies", "dependency_count", "resources", "resource_count",
    "lifecycle_timeout_ms", "api_version", "type", "capabilities",
    "capability_count", "stack_bytes", "queue_depth", "commands",
    "command_count", "owns_console", "command_help_entries",
    "commands_require_active_foreground",
)

APP_ID = "cyberdeck.image_viewer"
APP_COMMAND = "image"
MIN_VERSION = (1, 18, 0)
# App semver floor of this delivery: recoverable navigation is 1.0.3.
APP_MIN_VERSION = (1, 0, 3)

PURE_SOURCES = (BMP_SOURCE, FIT_SOURCE, DECODE_SOURCE, LIST_SOURCE, NAVIGATION_SOURCE)


def split_top_level(text):
    """Splits an aggregate initializer body on its top-level commas."""
    fields = []
    depth = 0
    current = []
    in_string = False
    index = 0
    while index < len(text):
        char = text[index]
        if in_string:
            current.append(char)
            if char == "\\" and index + 1 < len(text):
                current.append(text[index + 1])
                index += 2
                continue
            if char == '"':
                in_string = False
            index += 1
            continue
        if char == '"':
            in_string = True
        elif char in "({[":
            depth += 1
        elif char in ")}]":
            depth -= 1
        elif char == "," and depth == 0:
            fields.append("".join(current).strip())
            current = []
            index += 1
            continue
        current.append(char)
        index += 1
    tail = "".join(current).strip()
    if tail:
        fields.append(tail)
    return fields


def manifest_block(source):
    start = source.find("k_manifest{")
    if start < 0:
        raise SystemExit("cyberdeck_image_app.cpp: k_manifest literal not found")
    start = source.find("{", start)
    depth = 0
    in_string = False
    for index in range(start, len(source)):
        char = source[index]
        if in_string:
            if char == "\\":
                continue
            if char == '"':
                in_string = False
            continue
        if char == '"':
            in_string = True
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start + 1:index]
    raise SystemExit("cyberdeck_image_app.cpp: unterminated k_manifest literal")


def registration_symbols(source):
    match = re.search(r"application \*const k_apps\[\]\s*=\s*\{(?P<body>.*?)\n\};", source, re.S)
    if match is None:
        raise SystemExit("cyberdeck_system_apps.cpp: k_apps not found")
    return tuple(re.findall(r"&[\w:]+(?:\(\))?", match.group("body")))


def main():
    failures = []

    def require(condition, message):
        if not condition:
            failures.append(message)

    app_source = APP_SOURCE.read_text(encoding="utf-8")
    runtime_header = RUNTIME_HEADER.read_text(encoding="utf-8")
    quota_header = QUOTA_HEADER.read_text(encoding="utf-8")
    system_apps = SYSTEM_APPS.read_text(encoding="utf-8")
    ui = UI_SOURCE.read_text(encoding="utf-8")
    cmake = CMAKE.read_text(encoding="utf-8")
    makefile = MAKEFILE.read_text(encoding="utf-8")
    code_map = CODE_MAP.read_text(encoding="utf-8")

    # TEST-IMG-APP-01: the approved manifest.
    fields = split_top_level(manifest_block(app_source))
    require(len(fields) == len(MANIFEST_FIELDS),
            f"manifest must declare {len(MANIFEST_FIELDS)} fields, found {len(fields)}")
    if len(fields) == len(MANIFEST_FIELDS):
        values = dict(zip(MANIFEST_FIELDS, fields))
        require(values["id"] == f'"{APP_ID}"', "manifest id must be cyberdeck.image_viewer")
        require(values["name"] == '"Image viewer"', "manifest name must be Image viewer")
        require(values["type"] == "cyberdeck_apps::app_type::foreground",
                "the image viewer must be a foreground application")
        require(values["resources"] == '{"display", "input", "storage"}',
                "the viewer must declare display, input and storage")
        require(values["resource_count"] == "3", "resource_count must match the three resources")
        require(values["command"] == '""', "the primary command field stays empty; commands[] carries `image`")
        require(values["commands"] == f'{{"{APP_COMMAND}"}}', "commands[] must be exactly {\"image\"}")
        require(values["command_count"] == "1", "command_count must be 1")
        require(values["owns_console"] == "false", "the viewer must not own the console")
        require(values["commands_require_active_foreground"] == "false",
                "`image <path>` carries an argument, so it may not require an already active foreground")
        require(f'"{APP_COMMAND} <path>"' in values["command_help_entries"],
                "the manifest must document `image <path>`")
        require("left/right" in values["command_help_entries"] or "left/right" in app_source,
                "the usage text must document the arrow navigation")
        require("Esc" in values["command_help_entries"], "the usage text must document Esc")
        version = values["version"].strip('"')
        require(re.fullmatch(r"\d+\.\d+\.\d+", version) is not None,
                f"app version {version!r} must be MAJOR.MINOR.PATCH")
        require(all(not (len(part) > 1 and part.startswith("0")) for part in version.split(".")),
                f"app version {version!r} must not use zero padding")
        require(tuple(int(part) for part in version.split(".")) >= APP_MIN_VERSION,
                f"app version {version!r} is below the multiformat delivery floor {APP_MIN_VERSION}")

    # TEST-IMGF-APP-01: the app resolves the format from the extension and the
    # declared help describes the real support (REQ-IMGF-01/AC-IMGF-01).
    require("format_from_name" in app_source,
            "execute()/run_job() must resolve the decoder with format_from_name")
    require("image_format::none" in app_source,
            "an unrecognized extension must fail closed instead of reaching the decoder")
    # D3: an empty file is ineligible in the listing seam (st_size <= 0), so the
    # application has no reachable "empty file" branch.  The contract forbids
    # resurrecting a dead claim and points at the executable proof instead.
    require("empty file" not in app_source,
            "the application must not claim a reachable `image: empty file` (D3)")
    require("file_bytes == 0" not in app_source,
            "the zero-byte branch was removed: the listing/open seam already rejects st_size <= 0")
    local_shell = (ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp").read_text(
        encoding="utf-8")
    require("info.st_size <= 0" in local_shell,
            "the storage seam must keep rejecting empty files, which is what makes them ineligible")
    require('"image: no supported image in directory' in app_source,
            "an explicit open of an empty-only directory must report no supported image")
    for extension in (".jpg", ".jpeg", ".png"):
        require(extension in values["command_help_entries"],
                f"the app help must document the {extension} support")
    require("decode_descriptor(ops, descriptor, file_bytes, format, decoded)" in app_source,
            "run_job() must pass the declared format to decode_descriptor")
    # TEST-IMGF-PUB-04: the failure text is composed from the typed reason of the
    # format that failed; re-adding per-status literals would desynchronize it.
    require("result.reason" in app_source,
            "run_job() must compose the failure message from result.reason")
    for stale_literal in ("image: invalid request", "image: read failed"):
        require(stale_literal not in app_source,
                f"run_job() must not restate the status text ({stale_literal})")

    # TEST-IMGF-LIST-01: the listing accepts every recognized extension and the
    # upload predicate stays BMP-only.
    list_source = (ROOT / "components/cyberdeck/src/apps/image/cyberdeck_image_list.cpp").read_text(encoding="utf-8")
    require("is_viewer_name(name)" in list_source,
            "list_images() must filter with the recognized extension predicate")
    require("format_from_name" in list_source, "the extension table must stay single-sourced in formats")
    upload_source = (ROOT / "components/cyberdeck/src/apps/control/cyberdeck_control_upload.cpp").read_text(
        encoding="utf-8")
    require("validate_upload_name" in upload_source and
            "ascii_lower(name[extension + 1]) == 'b'" in upload_source and
            "ascii_lower(name[extension + 2]) == 'm'" in upload_source and
            "ascii_lower(name[extension + 3]) == 'p'" in upload_source,
            "validate_upload_name must keep its BMP-only extension check")

    # Documentation must describe what the firmware really opens.
    for document in (ROOT / "README.md", ROOT / "README.pt-BR.md"):
        text = document.read_text(encoding="utf-8")
        require("JPEG" in text and "PNG" in text, f"{document.name} must describe the supported formats")
    manual = ROOT / "tests/manual/image-viewer-validation.pt-BR.md"
    manual_text = manual.read_text(encoding="utf-8")
    require("## F." in manual_text, "the manual script must carry the multiformat section F")
    require("4:1:1" in manual_text, "the manual script must document that JPEG 4:1:1 is refused")
    require("empty" in manual_text, "the manual script must document the zero-byte outcome (D3)")
    require("image: empty file" not in manual_text, "the manual must not repeat the unreachable message")

    # TEST-IMG-APP-02: both capacities raised, equal, and the registry fits.
    runtime_capacity = re.search(r"constexpr std::size_t k_max_applications\s*=\s*(\d+)\s*;", runtime_header)
    quota_capacity = re.search(r"static constexpr std::size_t k_max_applications\s*=\s*(\d+)\s*;", quota_header)
    require(runtime_capacity is not None, "cyberdeck_app_runtime.h must declare k_max_applications")
    require(quota_capacity is not None, "cyberdeck_app_quota.h must declare k_max_applications")
    require("registry and quota capacities must stay equal" in runtime_header,
            "the runtime header must keep the compile-time capacity equality guard")
    symbols = registration_symbols(system_apps)
    image_symbols = [symbol for symbol in symbols if "cyberdeck_image::global_application" in symbol]
    require(len(image_symbols) == 1, "k_apps must register the image viewer exactly once")
    if runtime_capacity and quota_capacity:
        runtime_value = int(runtime_capacity.group(1))
        quota_value = int(quota_capacity.group(1))
        require(runtime_value == quota_value,
                f"capacities disagree: runtime={runtime_value} quota={quota_value}")
        require(len(symbols) <= runtime_value,
                f"k_apps registers {len(symbols)} applications but the runtime capacity is {runtime_value}")
        require(len(symbols) <= quota_value,
                f"k_apps registers {len(symbols)} applications but the quota capacity is {quota_value}")
        require(runtime_value >= 21, "the registry must hold at least the 21 registered applications")

    # TEST-IMG-APP-03/04: registration wiring and bounded diagnostics.
    require('#include "apps/image/cyberdeck_image_app.h"' in system_apps,
            "cyberdeck_system_apps.cpp must include the image application header")
    require(f'"{APP_ID}"' in system_apps,
            "the failure diagnostics list must include the image viewer id")
    require("src/apps/image/cyberdeck_image_bmp.cpp" in cmake,
            "CMakeLists.txt must compile cyberdeck_image_bmp.cpp")
    for source in ("cyberdeck_image_fit.cpp", "cyberdeck_image_decode.cpp", "cyberdeck_image_list.cpp",
                   "cyberdeck_image_app.cpp", "cyberdeck_image_navigation.cpp",
                   "cyberdeck_image_worker_state.cpp"):
        require(f"src/apps/image/{source}" in cmake, f"CMakeLists.txt must compile {source}")
    require("src/platform/display/cyberdeck_image_view.cpp" in cmake,
            "CMakeLists.txt must compile the image view")

    # TEST-IMG-LIFE-01/02/10: the UI owns the surface and its teardown.
    require("cyberdeck_image_view::view s_image_view" in ui, "the UI must own exactly one image view")
    require("lv_timer_create(process_image_status" in ui, "the UI must poll the application from a timer")
    require("s_image_status_timer" in ui and "lv_timer_del(s_image_status_timer)" in ui,
            "the image timer must be deleted during teardown")
    require("s_image_view.destroy()" in ui, "teardown must destroy the image view")
    require("classify_image_key(" in ui, "key routing must use the shared ownership predicate")
    require("image_view_active()" in ui, "text input must be gated by the image ownership predicate")
    require("switch_foreground(\"cyberdeck.shell\")" in ui, "Esc must return to the shell")
    require("list_images(value.directory.data(), listing, scan, value.name.data())" in app_source,
            "the explicit open must offer the requested file to the bounded scan")
    require("std::string_view requested = {}" in LIST_HEADER.read_text(encoding="utf-8"),
            "list_images must keep the opt-in pin parameter with an empty default")
    require("out.entries[out.count - 1] = promoted;" in LIST_SOURCE.read_text(encoding="utf-8"),
            "an out-of-window request must replace the last ordered entry, not the first")
    # TEST-IMG-NAV-10 (navigation): the bounded scan with the requested-file pin
    # must exist only on the explicit-open branch.  `navigate()` opens a job with
    # `by_index`, which reuses the published page; a rescan there would drop the
    # promoted entry on the first arrow press.  The application TU needs
    # FreeRTOS, so this wiring is pinned as text and the page/pin behaviour
    # itself is proven by the host `test_image_list` scenarios.
    by_index_start = app_source.find("if (value.by_index) {")
    require(by_index_start >= 0, "run_job() must keep the by_index (navigation) branch")
    else_marker = app_source.find("} else {", by_index_start)
    require(else_marker > by_index_start, "the by_index branch must stay separate from the open branch")
    by_index_body = app_source[by_index_start:else_marker]
    require("list_images(" not in by_index_body,
            "navigate() must reuse the published page instead of rescanning or re-pinning")
    require(app_source.count("list_images(") == 1,
            "the requested-file pin must exist only on the explicit open path")
    require("take_published(" in ui, "the UI must install frames published by the application")
    require("app.release(" in ui, "the UI must release a frame it could not install")

    # TEST-IMG-LIFE-10: layering.  The pure modules and the application stay
    # free of LVGL, and only the view includes it.
    for source in PURE_SOURCES:
        text = source.read_text(encoding="utf-8")
        require('"lvgl.h"' not in text, f"{source.name} must not include LVGL")
        require('"esp_lcd' not in text, f"{source.name} must not include the panel driver")
    require('"lvgl.h"' not in app_source, "the image application must not include LVGL")

    # TEST-IMG-LIFE-05/06: one persistent worker, a pure handoff state machine
    # and a bounded join.  The regression for the stranding window lives in
    # test_image_worker_state.cpp and the cancellation proof in test_image_list.
    worker_state = WORKER_STATE_SOURCE.read_text(encoding="utf-8")
    require("worker_state::next" in worker_state and "worker_state::enqueue" in worker_state,
            "the handoff protocol must expose the enqueue/next state machine")
    require("worker_.enqueue()" in app_source,
            "request() must go through worker_state::enqueue instead of creating tasks")
    require("worker_.next(" in app_source,
            "the worker must decide with worker_state::next under the request mutex")
    require("request_mutex_" in app_source,
            "the handoff must stay inside the request mutex critical section")
    request_body = app_source.split("bool application::request(", 1)[1].split("\nvoid application::worker_entry", 1)[0]
    require("xTaskCreate" not in request_body,
            "request() must not create tasks: the worker is created once by start()")
    require(app_source.count("xTaskCreate(") == 1,
            "exactly one xTaskCreate is allowed (start()), so no create/terminate handoff window exists")
    start_body = app_source.split("bool application::start()", 1)[1].split("bool application::stop()", 1)[0]
    require("xTaskCreate(" in start_body, "start() owns the single worker task")
    require("k_worker_join_budget_ms" in app_source,
            "the LVGL-path join must use a bounded budget constant")
    require("pdMS_TO_TICKS(k_worker_join_budget_ms)" in app_source,
            "stop() must join with the bounded budget")
    stop_body = app_source.split("bool application::stop()", 1)[1].split("cyberdeck_apps::result application::execute", 1)[0]
    require("5000" not in stop_body and "k_worker_join_timeout_ms" not in stop_body,
            "stop() must not keep the old five-second wait on the LVGL path")
    require("scan.cancelled" in app_source and "scan.cancel_context" in app_source,
            "the directory walk must poll the application cancel flag")
    list_source = LIST_SOURCE.read_text(encoding="utf-8")
    require("ops.cancelled(ops.cancel_context)" in list_source,
            "list_images() must poll cancellation per entry")

    # BUG-IMG-01: a ~8 KiB directory page on the 6144-byte image_load stack
    # rebooted the Tab5.  No worker-path function may declare one locally; the
    # page lives on the heap through `directory_workspace`.
    run_job_body = app_source.split("void application::run_job(", 1)[1].split(
        "void application::publish(", 1)[0]
    require("image_directory listing{}" not in run_job_body,
            "BUG-IMG-01: run_job must not declare an image_directory on the task stack")
    require("directory_workspace" in run_job_body,
            "BUG-IMG-01: run_job must stage the page through directory_workspace")
    require("image_directory staging{}" not in list_source,
            "BUG-IMG-01: list_images must fill the caller page instead of a local staging copy")
    require("image_directory *page_ = nullptr" in LIST_HEADER.read_text(encoding="utf-8"),
            "BUG-IMG-01: directory_workspace must own the page through a pointer")
    types_header = (ROOT / "components/cyberdeck/include/apps/image/cyberdeck_image_types.h").read_text(
        encoding="utf-8")
    require("k_worker_stack_bytes = 6144" in types_header,
            "the worker stack constant lives in cyberdeck_image_types.h and stays at its measured size")
    require("static_cast<std::uint32_t>(k_worker_stack_bytes)" in app_source,
            "xTaskCreate must use the shared k_worker_stack_bytes constant")
    # BUG-IMG-02: no value-initialized page anywhere in production code.
    for name, text in (("app", app_source), ("list", list_source)):
        stripped = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        stripped = re.sub(r"//[^\n]*", "", stripped)
        require("image_directory{}" not in stripped,
                f"BUG-IMG-02: {name} must reset the page with clear(), never with a value-initialized temporary")
    require("void clear() noexcept" in LIST_HEADER.read_text(encoding="utf-8"),
            "BUG-IMG-02: image_directory must expose the in-place clear() reset")
    require("new (memory) image_directory;" in list_source,
            "lifetime: directory_workspace must start the page with placement default-init (no braces)")
    require("page_->~image_directory();" in list_source,
            "lifetime: directory_workspace::release must end the page lifetime explicitly")
    require("is_trivially_destructible<image_directory>" in list_source,
            "the explicit destructor call must stay justified by a trivial-destructor assertion")
    require("memset(memory, 0, sizeof(image_directory))" not in list_source,
            "zeroing raw storage does not start a C++ lifetime; use placement default-init")
    require("k_worker_join_budget_ms = 200" in app_source,
            "the bounded join budget stays at 200 ms")
    # TEST-IMG-LIFE-06 (recovery after a failed stop): on a join timeout the
    # worker may still own the application, so the failure branch must keep the
    # semaphores and the published frame (freeing them is a use-after-free) and
    # start() must either reap the exited worker or refuse to create a second
    # one.  A later Esc/stop retry is then the documented recovery path.
    if "stop failed stage=worker_join" in stop_body:
        timeout_path = stop_body.split("stop failed stage=worker_join", 1)[1].split("return false;", 1)[0]
        require("vSemaphoreDelete" not in timeout_path and "release(" not in timeout_path,
                "the join-timeout path must not free semaphores or frames while the worker may be alive")
        require("return false;" in stop_body.split("stop failed stage=worker_join", 1)[1],
                "a join timeout must fail the stop so the supervisor keeps the grant and a retry is possible")
    else:
        require(False, "stop() must keep the join-timeout failure branch")
    require("worker_.reset()" in stop_body,
            "only a completed stop may reset the handoff state (a failed stop keeps it for the retry)")
    require("worker_not_reaped" in start_body,
            "start() must refuse a second worker while the previous one was not reaped")
    require(
        'if (cyberdeck_apps::global_runtime().switch_foreground("cyberdeck.shell"))\n        s_image_view.destroy();'
        in ui,
        "Esc must destroy the image view only after the supervisor actually returned to the shell")
    require("xTaskCreate" in app_source, "decoding must run on a dedicated worker task")
    require("open_readonly_stream" in app_source,
            "the viewer must use the confined streaming seam, not bounded_read")
    require("bounded_read" not in app_source,
            "bounded_read is capped at 12 KiB and cannot serve a 5 MiB image")
    require("bounded_read_bytes" not in app_source,
            "the viewer must not depend on the bounded_read quota")
    require("lv_image_set_src" in VIEW_SOURCE.read_text(encoding="utf-8"),
            "the view must install the frame through lv_image_set_src")
    require("lv_image_set_scale" in VIEW_SOURCE.read_text(encoding="utf-8"),
            "the view must apply the fit through lv_image_set_scale")
    require("LV_COLOR_FORMAT_RGB565" in VIEW_SOURCE.read_text(encoding="utf-8"),
            "the published frame must be RGB565")

    # TEST-IMG-LIFE-05 (A9): a superseded worker result may never publish.  The
    # application TU is allowlisted (FreeRTOS-bound), so no host binary executes
    # this gate; pin the production text instead, or deleting it would survive
    # every focused target while a slow decode from an Escaped image could
    # overwrite the next one.
    def method_body(name):
        start = app_source.find(f"application::{name}(")
        if start < 0:
            return ""
        start = app_source.rfind("\n", 0, start) + 1
        end = app_source.find("\n}\n", start)
        return app_source[start:end if end >= 0 else len(app_source)]

    stale_gate = re.compile(r"generation\s*(==|!=)\s*pending_generation_\.load\(")
    require(stale_gate.search(method_body("disposition")) is not None,
            "disposition() must compare the completion generation with the newest request")
    for method in ("publish", "fail", "fail_recoverable"):
        require("disposition(value" in method_body(method),
                f"{method}() must classify via the pure disposition model so a stale "
                f"result cannot overwrite the current image")
    initial_body = method_body("fail_initial")
    require(stale_gate.search(initial_body) is not None and "cancel_.load(" in initial_body,
            "C13: fail_initial() must re-check the generation and cancellation before "
            "reporting an initial error")
    require(method_body("run_job").count("disposition(value") >= 2,
            "run_job() must drop a superseded request before decoding and before publishing")
    fail_body = method_body("fail")
    require("fail_recoverable(value, reason)" in fail_body and
            "fail_initial(value.generation, reason)" in fail_body,
            "fail() must route the recoverable and the initial errors to different channels")
    require(method_body("worker_entry").count("cancel_.load") >= 1,
            "the worker loop must observe cancellation between jobs")

    # REQ-IMGF-06/07/08, contracts C8..C13: recoverable navigation.  The
    # application TU is FreeRTOS-bound, so its channel/page ownership is pinned
    # here; the pure decision and chrome are executed by test_image_navigation
    # and the LVGL surface by test_image_view.
    navigation_source = NAVIGATION_SOURCE.read_text(encoding="utf-8")
    navigation_header = NAVIGATION_HEADER.read_text(encoding="utf-8")
    app_header = APP_HEADER.read_text(encoding="utf-8")
    view_source = VIEW_SOURCE.read_text(encoding="utf-8")
    view_header = VIEW_HEADER.read_text(encoding="utf-8")
    require("completion_disposition_for(" in navigation_source,
            "C8: the pure model must implement completion_disposition_for()")
    for symbol in ("completion_origin", "completion_outcome", "completion_disposition",
                   "viewer_chrome", "chrome_after_publish", "chrome_after_error",
                   "chrome_after_accept", "navigation_target"):
        require(symbol in navigation_header, f"C8/C9: the navigation header must declare {symbol}")
    require("take_recoverable_error(" in app_header,
            "C11: the recoverable error must travel on its own typed channel")
    require("navigate(int direction, navigation_target *accepted = nullptr)" in app_header,
            "C12: navigate() must expose the accepted target")
    require("completion_disposition_for(" in app_source,
            "C8: the application must call the pure completion model")
    for disposition in ("completion_disposition::discard", "completion_disposition::recoverable_error",
                        "completion_disposition::initial_error"):
        require(disposition in app_source, f"C8: the application must handle {disposition}")

    # C11: one-shot channel, separate from the terminal notice.
    take_error_body = method_body("take_recoverable_error")
    require("has_recoverable_error_ = false;" in take_error_body and
            "recoverable_error_ = viewer_chrome{};" in take_error_body,
            "C11: take_recoverable_error() must be one-shot")
    fail_initial_body = method_body("fail_initial")
    require("recoverable_error_ = viewer_chrome{};" in fail_initial_body,
            "C11: an initial failure must clear the recoverable channel")
    require("notice_.assign(" in fail_initial_body and "notice_.push_back('\\n')" in fail_initial_body,
            "AC-IMGF-08: the initial error still owns the terminal message and its LF")
    require("chrome_after_error(" in method_body("fail_recoverable"),
            "C9: the recoverable error is built as one complete chrome")
    require("has_recoverable_error_ = true;" in method_body("fail_recoverable"),
            "C11: fail_recoverable() must publish through the recoverable channel")
    request_body = method_body("request")
    require(request_body.count("recoverable_error_ = viewer_chrome{};") == 1 and
            request_body.count("has_recoverable_error_ = false;") == 1,
            "C9/NEG-IMGF-RNAV-33: an accepted request clears a pending recoverable error "
            "so the old failure is never painted over the new target")
    # F9: the clear may only happen after the handoff accepts the request.  When
    # `worker_.enqueue()` is rejected (no live worker) the UI keeps the error
    # chrome on screen; clearing the channel there would strand that chrome with
    # no channel able to update it.  App TU is FreeRTOS-bound, so the order is
    # pinned deterministically here.
    enqueue_position = request_body.find("worker_.enqueue()")
    clear_position = request_body.find("recoverable_error_ = viewer_chrome{};")
    require(enqueue_position >= 0 and clear_position > enqueue_position,
            "F9: request() must call worker_.enqueue() before clearing the recoverable error")
    rejected_path = request_body[enqueue_position:clear_position]
    require("return false;" in rejected_path and
            "recoverable_error_ = viewer_chrome{};" not in rejected_path and
            "has_recoverable_error_ = false;" not in rejected_path,
            "F9: a rejected enqueue must preserve the pending recoverable error/chrome")
    require("app.take_published(" in ui and "app.take_notice(" in ui and
            "app.take_recoverable_error(" in ui,
            "C11: the UI must consume the three channels separately")

    # C12: a recoverable failure keeps the page and the accepted index.
    fail_recoverable_body = method_body("fail_recoverable")
    require("has_directory_" in fail_recoverable_body and
            "current_index_ = index;" in fail_recoverable_body,
            "C12: the published page and the accepted index survive a recoverable failure")
    require("has_directory_ = false" not in fail_recoverable_body and
            "directory_.clear()" not in fail_recoverable_body,
            "C12: a recoverable failure must not clear the page")
    require("directory_path_" not in fail_recoverable_body,
            "C12: a recoverable failure must keep the directory path used by the next navigation")
    navigate_body = method_body("navigate")
    require("has_directory_" in navigate_body and "step_image_index(" in navigate_body,
            "C12: navigate() must compute the target from the published page with clamp")
    require("list_images(" not in navigate_body,
            "TEST-IMGF-LIST-08: navigate() must reuse the page instead of rescanning")
    require("current_index_ = next;" in navigate_body and "accepted->index = next;" in navigate_body,
            "D11: an accepted request moves the index and reports the target")
    require("copy_bounded(value.directory, directory_path_.data());" in navigate_body,
            "C12: the navigation job carries the published directory path")

    # C10: the view drops the frame before releasing it and owns the error label.
    def view_body(name):
        start = view_source.find(f"view::{name}(")
        if start < 0:
            return ""
        start = view_source.rfind("\n", 0, start) + 1
        end = view_source.find("\n}\n", start)
        return view_source[start:end if end >= 0 else len(view_source)]

    drop_body = view_body("drop_frame")
    require(drop_body.find("lv_image_set_src(image_, nullptr)") >= 0 and
            0 <= drop_body.find("lv_image_set_src(image_, nullptr)") < drop_body.find("clear_image()"),
            "C10/NEG-IMGF-RNAV-05: the widget must be repointed before the frame is released")
    require("clear_error();" in view_body("set_image"),
            "C10: a new frame clears the recoverable error")
    apply_body = view_body("apply_chrome")
    require("drop_frame();" in apply_body and "lv_label_set_text(error_, error_text_.c_str())" in apply_body,
            "C9/C10: apply_chrome() drops the frame and writes the typed message atomically")
    for accessor in ("has_error()", "error_text()", "position_text()", "apply_chrome("):
        require(accessor in view_header, f"C10: the view must expose {accessor}")

    # C13: the UI consumes the recoverable channel without leaving the viewer or
    # writing to the terminal, and the initial branch keeps both.
    status_start = ui.find("void process_image_status(lv_timer_t *) {")
    status_end = ui.find("\n}\n", status_start)
    status_body = ui[status_start:status_end if status_end >= 0 else len(ui)]
    recoverable_start = status_body.find("if (app.take_recoverable_error(recovered)) {")
    notice_start = status_body.find("std::string notice;")
    require(recoverable_start >= 0 and notice_start > recoverable_start,
            "C13: process_image_status() must consume the recoverable channel on its own branch")
    recoverable_branch = status_body[recoverable_start:notice_start]
    require("switch_foreground(" not in recoverable_branch,
            "AC-IMGF-07: a recoverable error must not return to the shell")
    require("append_output(" not in recoverable_branch,
            "D10: a recoverable error must not be written to the terminal")
    require("if (image_viewer_foreground()) {" in recoverable_branch,
            "NEG-IMGF-RNAV-11: a recoverable error must be discarded when the shell owns "
            "the foreground instead of recreating the viewer surface")
    require("s_image_chrome = recovered;" in recoverable_branch and
            "apply_chrome(s_image_chrome);" in recoverable_branch,
            "AC-IMGF-09: the recovered chrome (name/indicator/message) is what gets applied, "
            "not an empty/default state")
    require("apply_chrome(" in recoverable_branch,
            "AC-IMGF-09: the recoverable branch applies the error chrome to the view")
    notice_branch = status_body[notice_start:]
    require("abandon_image_view(notice.c_str())" in notice_branch,
            "AC-IMGF-08: the initial notice still abandons the viewer back to the shell")
    abandon_start = ui.find("void abandon_image_view(")
    abandon_end = ui.find("\n}\n", abandon_start)
    abandon_body = ui[abandon_start:abandon_end if abandon_end >= 0 else len(ui)]
    require("append_output(" in abandon_body and 'switch_foreground("cyberdeck.shell")' in abandon_body,
            "AC-IMGF-08: the initial error keeps the terminal message and the foreground switch")
    require("classify_image_key(true, s_image_view.exists()" in ui and
            "classify_image_key(true, s_image_view.has_image()" not in ui,
            "TEST-IMGF-RNAV-14/NEG-IMGF-RNAV-15: key ownership follows the surface, so an error keeps the arrows")
    require("image_viewer_foreground() && s_image_view.exists()" in ui,
            "TEST-IMGF-RNAV-14: the text-input gate must follow the surface, so typing cannot "
            "leak to the shell while the recoverable error is displayed")
    require("image_navigate(" in ui and "chrome_after_accept(" in ui,
            "C9: the UI applies the accepted target chrome in the key event")
    require("re.search" not in status_body and "strstr(" not in status_body,
            "C11: the UI must never parse the notice text to tell the channels apart")
    cancelled_branch = re.search(r"decode_status::cancelled\)\s*\n\s*return;", method_body("run_job"))
    require(cancelled_branch is not None,
            "NEG-IMGF-RNAV-18: a cancelled decode returns without publishing a diagnostic")
    require("has_recoverable_error_ = false;" in method_body("stop"),
            "NEG-IMGF-RNAV-19: stop() clears the pending recoverable error")

    # TEST-IMG-APP-06: versioning rules from AGENTS.md.
    version_text = VERSION.read_text(encoding="utf-8").strip()
    require(re.fullmatch(r"\d+\.\d+\.\d+", version_text) is not None,
            f"version.txt {version_text!r} must be MAJOR.MINOR.PATCH")
    require(all(not (len(part) > 1 and part.startswith("0")) for part in version_text.split(".")),
            f"version.txt {version_text!r} must not use zero padding")
    parts = tuple(int(part) for part in version_text.split("."))
    require(parts >= MIN_VERSION, f"version.txt {version_text!r} is below the delivered floor")

    # TEST-IMG-GATE-02: every focused target stays reachable.
    for target in ("test_image_bmp", "test_image_fit", "test_image_formats", "test_image_jpeg",
                   "test_image_png", "test_image_decode", "test_image_list", "test_image_view",
                   "test_image_navigation", "test_image_worker_state", "test_image_capture_orientation",
                   "test_image_stack_frame_contract", "test_image_decoders_contract",
                   "test_image_viewer_contract"):
        require(f".PHONY: {target}\n" in makefile, f"Makefile must declare {target} phony")
        require(f"@$(MAKE) --no-print-directory {target}\n" in makefile,
                f"the aggregate `test` target must run {target}")
    require("BINS += $(IMAGE_BMP_BIN)" in makefile,
            "BINS must carry the image artifacts so clean/test-build cover them")
    for artifact in ("test_image_bmp_bin", "test_image_fit_bin", "test_image_decode_bin",
                     "test_image_list_bin", "test_image_view_bin", "test_image_navigation_bin"):
        require(f":= {artifact}" in makefile, f"Makefile must name the {artifact} artifact")

    # Documentation stays consistent with the delivery.
    require("cyberdeck_image_app" in code_map or "image/viewer" in code_map.lower(),
            "code-map.md must index the image viewer module")
    require("cyberdeck_image_navigation" in code_map and "REQ-IMGF-06" in code_map,
            "code-map.md must index the recoverable navigation model/matrix")
    for document in (ROOT / "README.md", ROOT / "README.pt-BR.md"):
        text = document.read_text(encoding="utf-8")
        require("recoverable" in text or "recuperável" in text,
                f"{document.name} must describe the recoverable navigation error")
    for document in (ROOT / "docs/ARCHITECTURE.md", ROOT / "docs/ARCHITECTURE.pt-BR.md"):
        text = document.read_text(encoding="utf-8")
        require("recoverable" in text or "recuperável" in text,
                f"{document.name} must describe the recoverable navigation error")
    for step in ("F21", "F22", "F23", "F24", "F25", "F26", "F27"):
        require(step in manual_text, f"the manual script must document the recoverable step {step}")
    require(MANUAL.exists() and MANUAL.stat().st_size > 0,
            "tests/manual/image-viewer-validation.pt-BR.md must document the device validation")

    # The approved limits live once, in the shared format module; every other
    # module must consume them from there instead of restating the numbers.
    formats_header = ROOT / "components/cyberdeck/include/apps/image/cyberdeck_image_formats.h"
    require(formats_header.exists(), "the shared limit/format header must exist")
    if formats_header.exists():
        limits = formats_header.read_text(encoding="utf-8")
        require("k_max_file_bytes = 5u * 1024u * 1024u" in limits,
                "the approved file cap is 5 MiB")
        require("k_max_dimension = 1280" in limits, "the approved dimension limit is 1280")
    bmp_header = BMP_HEADER.read_text(encoding="utf-8")
    require('#include "apps/image/cyberdeck_image_formats.h"' in bmp_header,
            "cyberdeck_image_bmp.h must take the limits from the shared header")
    require("k_max_file_bytes =" not in bmp_header and "k_max_dimension =" not in bmp_header,
            "the limits must not be restated in the BMP header")

    for failure in failures:
        print(f"FAIL: {failure}")
    print(f"test_image_viewer_contract: {len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
