#!/usr/bin/env python3
"""Structural contracts for the passive/local Wi-Fi audit integration.

The worker/controller and the injectable persistence transaction are separate
production seams.  This contract follows the real persistence source/header,
rather than looking for a retired transaction implementation in the controller
module.  It keeps the controller/UI safety checks and checks the durable
write/fsync/rename, recovery, rollback, ACK, and sidecar rules at the seam that
now owns them.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
AUDIT = ROOT / "components/cyberdeck/src/features/wifi/cyberdeck_wifi_audit.cpp"
PERSISTENCE = ROOT / "components/cyberdeck/src/features/wifi/cyberdeck_wifi_audit_persistence.cpp"
PERSISTENCE_HEADER = ROOT / "components/cyberdeck/include/features/wifi/cyberdeck_wifi_audit_persistence.h"
PERSISTENCE_CONTRACT = ROOT / "tests/host/keymap/contracts/cyberdeck_wifi_audit_persistence.h"
COMPONENT = ROOT / "components/cyberdeck/CMakeLists.txt"
SDKCONFIG_DEFAULTS = ROOT / "sdkconfig.defaults"
MAKEFILE = ROOT / "tests/host/keymap/Makefile"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def function_body(source: str, name: str) -> str:
    """Return one definition body, including its outer braces."""
    marker = source.find(name)
    require(marker >= 0, f"source must define {name}")
    opening = source.find("{", marker)
    require(opening >= 0, f"{name} must have a body")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    raise AssertionError(f"unterminated body for {name}")


def block_after(source: str, marker: str) -> str:
    """Return the braced block after a marker (with or without its opening brace)."""
    start = source.find(marker)
    require(start >= 0, f"missing marker {marker!r}")
    if marker.endswith("{"):
        opening = start + marker.rfind("{")
    else:
        opening = source.find("{", start + len(marker))
    require(opening >= 0, f"marker {marker!r} must be followed by a block")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    raise AssertionError(f"unterminated block after {marker!r}")


def preprocessor_block(source: str, marker: str) -> str:
    """Return a complete #if block, including nested preprocessor guards."""
    start = source.find(marker)
    require(start >= 0, f"missing preprocessor marker {marker!r}")
    depth = 0
    offset = start
    for line in source[start:].splitlines(keepends=True):
        stripped = line.lstrip()
        if re.match(r"#\s*if(?:def|ndef)?\b", stripped):
            depth += 1
        elif re.match(r"#\s*endif\b", stripped):
            depth -= 1
            if depth == 0:
                return source[start:offset + len(line)]
        offset += len(line)
    raise AssertionError(f"unterminated preprocessor block after {marker!r}")


def require_before(body: str, first: str, second: str, message: str) -> None:
    require(first in body, f"missing {first!r} while checking {message}")
    require(second in body, f"missing {second!r} while checking {message}")
    require(body.index(first) < body.index(second), message)


def require_absent(body: str, symbols: tuple[str, ...], message: str) -> None:
    for symbol in symbols:
        require(symbol not in body, f"{message}: found {symbol!r}")


def without_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//[^\n]*", "", source)


def main() -> int:
    ui = UI.read_text(encoding="utf-8")
    simplified_flow = "wifi audit save" in ui
    require("wifi audit" in ui, "TUI must recognize wifi audit")
    if simplified_flow:
        require("wifi audit export" not in ui,
                "simplified flow must retire the old export command")
    else:
        require("wifi audit export" in ui, "legacy TUI must recognize audit export")
        require("confirmed" in ui or "confirm" in ui,
                "legacy export needs explicit confirmation")
    require(PERSISTENCE.exists(), "audit persistence implementation is missing")
    require(PERSISTENCE_HEADER.exists(), "audit persistence public header is missing")
    require(PERSISTENCE_CONTRACT.exists(), "audit persistence host contract is missing")

    audit = AUDIT.read_text(encoding="utf-8")
    persistence = PERSISTENCE.read_text(encoding="utf-8")
    persistence_header = PERSISTENCE_HEADER.read_text(encoding="utf-8")
    component = COMPONENT.read_text(encoding="utf-8")
    sdkconfig_defaults = SDKCONFIG_DEFAULTS.read_text(encoding="utf-8")
    makefile = MAKEFILE.read_text(encoding="utf-8")
    codemap = (ROOT / "code-map.md").read_text(encoding="utf-8")

    # Passive/local audit safety remains a controller/worker contract.
    lowered = audit.lower()
    for word in ("esp_wifi_scan_start", "esp_wifi_scan_get_ap_records", "probe",
                 "pcap", "promiscuous", "capture"):
        require(word not in lowered, f"audit must not use forbidden API/data: {word}")
    sinks = "\n".join(function_body(audit, name) for name in
                       ("std::string render_ui", "std::string render_log", "std::string safe_export"))
    for word in ("password", "passphrase", "psk"):
        require(word not in sinks.lower(), f"audit sink must not expose secret data: {word}")
    require("worker" in audit or "task" in audit, "audit must run outside callbacks/UI")
    require("snapshot" in audit and "token" in audit, "audit needs snapshot/token handoff")
    require("/sdcard" in audit, "export must be confined to /sdcard")
    require("cyberdeck_wifi_audit.cpp" in component,
            "component build must register audit source")
    require("cyberdeck_wifi_audit_persistence.cpp" in component,
            "component build must register the real persistence source")
    require("features/wifi/cyberdeck_wifi_audit.h" in makefile,
            "host test must include the production audit header")
    require("test_wifi_audit" in makefile,
            "host Makefile must register audit tests")
    require("cyberdeck_wifi_audit_persistence.cpp" in makefile,
            "host Makefile must link the real persistence source")
    require("cyberdeck_wifi_audit_persistence.h" in makefile,
            "host Makefile must depend on the real persistence header")
    require("test_wifi_audit_persistence" in codemap,
            "code-map.md must map the real persistence seam")

    # The host safety seam remains structural because ESP-IDF hardware is not
    # linkable here.  It still verifies that stale work cannot collect hardware
    # and that the worker owns the only export handoff.
    run = function_body(audit, "void run()")
    require("struct work_item" in audit and "bool export_job" in audit,
            "worker items must distinguish audit from export work")
    require("operation::audit" in run and "operation::export_job" in run,
            "worker must have separate audit and export operations")
    require_before(run, "if (live && item.token != 0 && item.token == token)",
                   "esp_wifi_sta_get_ap_info", "lifecycle/token gate must precede hardware")
    require_before(run, "if (item.export_job && status == state::ready)",
                   "esp_wifi_sta_get_ap_info", "export classification must precede hardware")
    require_before(run, "if (!item.export_job && status == state::collecting)",
                   "esp_wifi_sta_get_ap_info", "audit classification must precede hardware")
    require("operation::stale" in run and "must not collect" in run,
            "stale/cancelled/teardown work must have an explicit discard path")
    stale = run[run.index("} else {", run.index("operation::export_job")):]
    require("esp_wifi_sta_get_ap_info" not in stale and
            "esp_netif_get_handle_from_ifkey" not in stale,
            "discard path must not collect from hardware")
    publish = function_body(audit, "void publish(")
    require_before(publish, "if (!live || t != token || status != state::collecting) return;",
                   "if (failed || !a.associated)", "publish must revalidate lifecycle/token first")

    export_branch = block_after(run, "else if (next_operation == operation::export_job)")
    stale_live = block_after(export_branch, "} else {")
    require("export_in_flight = false;" in stale_live,
            "an export invalidated during worker completion must release its guard")
    stale_marker = "// The item was cancelled, superseded, or arrived after"
    stale_item = run[run.index(stale_marker):]
    require("if (item.export_job) export_in_flight = false;" in stale_item,
            "a stale export item must release its guard")
    failure_handoff = block_after(export_branch,
                                  "if (xQueueSend(exports, &out, 0) != pdTRUE)")
    require("out.ok = false;" in failure_handoff and
            "overflow_export_ready = true;" in failure_handoff,
            "failed export delivery must remain owned until drain releases the guard")

    success_start = export_branch.index("if (export_file(item.path, item.token, data))")
    require("out.ok = false;" in export_branch[:success_start] and
            "out.bytes = 0;" in export_branch[:success_start],
            "failed firmware export must retain an empty, unsuccessful result")
    require_before(export_branch,
                   "if (export_file(item.path, item.token, data))",
                   "std::memcpy(out.data, data.data(), data.size())",
                   "firmware payload copy must be gated by successful persistence")
    require("std::memcpy(out.data, data.data(), data.size())" not in
            export_branch[:success_start],
            "firmware payload must not be copied before export_file succeeds")
    require("std::memcpy(out.data, data.data(), data.size())" in export_branch,
            "successful firmware export must copy the persisted payload")

    audit_teardown = function_body(audit, "void audit_controller::teardown()")
    require_before(audit_teardown, "s.live = false;", "xQueueSend(s.work, &stop, portMAX_DELAY)",
                   "teardown must invalidate lifecycle before wakeup")
    require_before(audit_teardown, "s.token = 0;", "xQueueSend(s.work, &stop, portMAX_DELAY)",
                   "teardown must invalidate token before wakeup")
    require_before(audit_teardown, "xSemaphoreTake(s.stopped, portMAX_DELAY)",
                   "vQueueDelete(s.exports)",
                   "teardown must join worker before deleting queues")
    require("#if CYBERDECK_AUDIT_FIRMWARE" in audit,
            "firmware audit path must remain isolated from host-only state")
    require("bool export_in_flight = false;" in audit,
            "firmware export must own a single-shot guard")
    require(audit_teardown.count("s.export_in_flight = false;") >= 2,
            "audit teardown must clear the guard during join and after queue cleanup")
    require_before(audit_teardown, "s.export_in_flight = false;",
                   "xQueueSend(s.work, &stop, portMAX_DELAY)",
                   "audit teardown must clear the guard before waking the worker")
    require(audit_teardown.rfind("s.export_in_flight = false;") >
            audit_teardown.index("vQueueDelete(s.exports)"),
            "audit teardown must leave the guard clear after deleting completion queues")

    # The public persistence seam is the source of truth for the durable
    # transaction.  Do not satisfy these checks from test-only declarations or
    # a retired implementation in the controller module.
    for symbol in (
        "class file_ops",
        "class completion_sink",
        "class audit_persistence",
        "open_exclusive",
        "std::ptrdiff_t write",
        "virtual int fsync",
        "virtual int close",
        "virtual int rename",
        "virtual int unlink",
        "initialize",
        "teardown",
        "enqueue",
        "pump_one",
        "drain",
        "in_flight",
        "pending",
    ):
        require(symbol in persistence_header,
                f"production persistence header must expose {symbol}")
    for symbol in (
        "namespace cyberdeck_wifi_audit_persistence",
        "run_transaction",
        "recover_stale_artifacts",
        "write_all",
        "completion_sink",
        "audit_persistence::enqueue",
        "audit_persistence::pump_one",
        "audit_persistence::drain",
        "audit_persistence::teardown",
    ):
        require(symbol in persistence,
                f"production persistence source must own {symbol}")
    for symbol in (
        "artifact_state",
        "persistence_error",
        "submit_status",
        "queue_capacity",
        "max_payload",
        "rejected_queue_full",
    ):
        require(symbol in persistence_header,
                f"production persistence header must define {symbol}")
    require("static_assert(queue_capacity == 1" in persistence,
            "persistence seam must retain its one-slot queue")
    require("std::mutex" in persistence and "std::atomic" in persistence,
            "persistence seam must serialize lifecycle and concurrent operations")

    # The injected adapter remains responsible for exclusive, non-following
    # candidate creation.  The transaction coordinator owns the stable names.
    native_adapter = block_after(audit, "class firmware_file_ops")
    require("O_EXCL" in native_adapter and "O_NOFOLLOW" in native_adapter,
            "firmware adapter must create candidates with O_EXCL/O_NOFOLLOW")
    require("#if defined(ESP_PLATFORM)" in audit,
            "firmware transaction must retain the ESP/FatFs branch")
    if simplified_flow:
        require("/sdcard/wifi-audit/" in persistence,
                "simplified save sidecars must stay in the default directory")
        require("/sdcard/wifi-audit.txt" not in persistence,
                "simplified flow must retire the root-level audit target")
    else:
        for path in ("/sdcard/wifi-audit.txt", "/sdcard/.wifi-audit.tmp",
                     "/sdcard/.wifi-audit.bak"):
            require(path in persistence, f"persistence seam must use stable path {path}")
    require('"/sdcard/"' in persistence and "sdcard_prefix" in persistence,
            "persistence seam must confine paths below /sdcard")
    path_check = function_body(persistence, "bool valid_path(")
    require("name.find(\"..\")" in path_check and
            "name.find('\\0')" in path_check,
            "persistence path validation must reject traversal and embedded NUL")

    make_paths = function_body(persistence, "bool make_transaction_paths(")
    if simplified_flow:
        # The sidecars must be derived from the requested destination itself;
        # the fixed directory is validated by valid_path(), not repeated as a
        # literal inside make_transaction_paths().
        compact_paths = re.sub(r"\s+", "", without_comments(make_paths))
        require(
            "destination.rfind('/')" in compact_paths and
            "destination.substr(0,slash+1)" in compact_paths and
            "destination.substr(slash+1)" in compact_paths,
            "simplified save must derive its sidecar parent and basename from the destination")
        for field, suffix_symbol in (("temporary", "temporary_suffix"),
                                     ("backup", "backup_suffix")):
            require(
                f"paths.{field}.assign(parent.data(),parent.size())" in compact_paths and
                f"paths.{field}.append(name.data(),name.size())" in compact_paths and
                f"paths.{field}.append({suffix_symbol}.data(),{suffix_symbol}.size())" in compact_paths,
                f"the {field} sidecar must stay beside the destination and use its bounded suffix")
        require(
            re.search(r'temporary_suffix\s*=\s*"\.tmp"', without_comments(persistence)) is not None and
            re.search(r'backup_suffix\s*=\s*"\.bak"', without_comments(persistence)) is not None,
            "transaction suffixes must be exactly .tmp and .bak")
    else:
        require("canonical_destination" in make_paths and
                "canonical_temporary" in make_paths and "canonical_backup" in make_paths,
                "legacy audit export must retain its canonical sidecar slots")
        require("temporary_suffix" in make_paths and "backup_suffix" in make_paths,
                "transaction must use bounded .tmp/.bak sidecars")

    recovery = function_body(persistence, "bool recover_stale_artifacts(")
    for state_name in ("destination_state", "temporary_state", "backup_state"):
        for invalid_state in ("error", "other"):
            require(f"{state_name} == artifact_state::{invalid_state}" in recovery,
                    f"recovery must fail closed for {state_name}={invalid_state}")
    restore_backup = block_after(
        recovery,
        "if (destination_state == artifact_state::missing &&")
    stale_backup = block_after(
        recovery,
        "if (destination_state == artifact_state::regular &&")
    candidate_cleanup = block_after(
        recovery, "if (temporary_state != artifact_state::missing)")
    require_before(
        recovery,
        "if (destination_state == artifact_state::missing &&",
        "if (destination_state == artifact_state::regular &&",
        "recovery must restore a backup before treating it as stale")
    require("if (operations.rename(paths.backup, destination) != 0) return false;" in restore_backup,
            "backup restoration failure must fail closed")
    require_before(restore_backup, "operations.rename(paths.backup, destination)",
                   "destination_state = artifact_state::regular;",
                   "backup restoration must publish the destination state only after rename")
    require("if (!remove_regular(operations, paths.backup)) return false;" in stale_backup,
            "stale-backup cleanup must fail closed")
    require_before(
        recovery,
        "if (destination_state == artifact_state::missing &&",
        "if (temporary_state != artifact_state::missing)",
        "backup recovery must precede candidate cleanup")
    require("if (destination_state != artifact_state::regular) return false;" in candidate_cleanup,
            "stale .tmp must be preserved when no regular destination can be proven")
    require("if (!remove_regular(operations, paths.temporary)) return false;" in candidate_cleanup,
            "candidate cleanup must remain fail-closed")

    transaction = function_body(persistence, "completion run_transaction(")
    require_before(transaction, "recover_stale_artifacts(operations",
                   "open_exclusive(paths.temporary)",
                   "stale recovery must precede candidate creation")
    require_before(transaction, "recover_stale_artifacts(operations",
                   "write_all(operations",
                   "stale recovery must precede any new candidate write")
    require("if (!make_transaction_paths(request.path, paths) ||" in transaction,
            "transaction must validate the destination and sidecar paths")
    require("!recover_stale_artifacts(operations, request.path, paths)" in transaction and
            "return failed(persistence_error::recovery_failed);" in transaction,
            "transaction must stop before writing when recovery fails")

    write_all = function_body(persistence, "bool write_all(")
    for token in ("while (written < data.size())",
                  "if (amount <= 0) return false;",
                  "if (count > data.size() - written) return false;",
                  "written += count;"):
        require(token in write_all, f"bounded write loop must contain {token}")

    # The durable order is deliberately structural: no completion can precede
    # the write loop, fsync, descriptor close, and the candidate rename.
    write_call = "if (!write_all(operations, fd, request.data))"
    fsync_call = "operations.fsync(fd) != 0"
    close_call = "operations.close(fd) != 0"
    candidate_rename = "operations.rename(paths.temporary, request.path)"
    require(write_call in transaction and fsync_call in transaction and
            close_call in transaction and candidate_rename in transaction,
            "transaction must execute write, fsync, close, and rename")
    require_before(transaction, write_call, fsync_call,
                   "write must complete before fsync")
    require_before(transaction, fsync_call, close_call,
                   "fsync must complete before close")
    require_before(transaction, close_call, candidate_rename,
                   "candidate descriptor must close before publication")
    require_before(transaction, candidate_rename, "result.data = request.data;",
                   "payload may be published only after durable rename")
    require("result.ok = true;" in transaction and "return result;" in transaction,
            "successful transaction must return a published completion")

    existing_start = transaction.index("if (!has_existing)")
    existing = transaction[existing_start:]
    backup_move = "if (operations.rename(request.path, paths.backup) != 0)"
    candidate_publish = "if (operations.rename(paths.temporary, request.path) == 0)"
    rollback_publish = "} else if (operations.rename(paths.backup, request.path) == 0)"
    require(backup_move in existing and candidate_publish in existing and
            rollback_publish in existing,
            "existing-destination path must move backup, publish candidate, and support rollback")
    require_before(existing, backup_move, candidate_publish,
                   "old destination must be moved to .bak before candidate publication")
    require_before(existing, candidate_publish, "operations.unlink(paths.backup)",
                   "backup cleanup must follow successful candidate publication")
    rollback_success = block_after(existing, rollback_publish)
    require("discard_candidate_if_safe(operations, request.path, paths.temporary)" in rollback_success,
            "successful rollback must discard only a candidate proven unsafe")
    rollback_start = existing.index(rollback_publish)
    rollback_failure = block_after(existing[rollback_start:], "} else {")
    require("return failed(persistence_error::rollback_failed);" in rollback_failure,
            "rollback failure must return an explicit persistence failure")
    require_absent(rollback_failure, ("unlink", "remove_regular", "discard_candidate_if_safe"),
                   "rollback failure must preserve both recoverable sidecars")
    safe_cleanup = function_body(persistence, "void discard_candidate_if_safe(")
    require("operations.inspect(destination) == artifact_state::regular" in safe_cleanup,
            "candidate cleanup must require a proven regular destination")
    require("operations.unlink(temporary)" in safe_cleanup,
            "safe candidate cleanup must unlink only the proven candidate")

    # Guard and ACK ownership are part of the real seam, not the old controller
    # helper.  enqueue never performs I/O; pump publishes only after the durable
    # transaction; a rejected sink publication retains exactly one completion.
    enqueue = function_body(persistence, "submit_status audit_persistence::enqueue(")
    pump = function_body(persistence, "bool audit_persistence::pump_one()")
    drain = function_body(persistence, "bool audit_persistence::drain(")
    persistence_teardown = function_body(persistence, "void audit_persistence::teardown()")
    require_absent(enqueue,
                   ("inspect(", "open_exclusive(", ".write(", ".fsync(",
                    ".close(", ".rename(", ".unlink("),
                   "enqueue must not perform file I/O")
    require("impl_->active.load" in enqueue and "impl_->active.store(true" in enqueue,
            "single-shot guard must be atomic")
    require("impl_->has_request = true;" in enqueue and
            "impl_->active.store(true, std::memory_order_release);" in enqueue,
            "queue guard must arm only after the request is copied")
    require_before(enqueue, "impl_->has_request = true;",
                   "impl_->active.store(true, std::memory_order_release);",
                   "the slot must be marked occupied after ownership is copied")
    require("impl_->torn_down.load" in enqueue and
            "if (!impl_->initialized.load" in enqueue,
            "enqueue must reject a torn-down/not-initialized seam")
    require("run_transaction(impl_->operations, item)" in pump and
            "impl_->sink.publish(result)" in pump,
            "pump must run the transaction and hand its completion to the sink")
    require_before(pump, "run_transaction(impl_->operations, item)",
                   "impl_->sink.publish(result)",
                   "ACK/result delivery must follow the durable transaction")
    require("if (!impl_->initialized.load(std::memory_order_acquire))" in pump and
            "impl_->active.store(false" in pump,
            "teardown race must suppress a late completion and release ownership")
    require("if (!impl_->sink.publish(result))" in pump and
            "impl_->has_retained_completion = true;" in pump,
            "a rejected ACK must retain a bounded completion")
    require("out = std::move(impl_->retained_completion);" in drain and
            "impl_->clear_retained_completion();" in drain and
            "impl_->active.store(false, std::memory_order_release);" in drain,
            "drain must deliver and release the retained single-shot result")
    require("if (result.ok)" not in pump,
            "failed results must reach the same ACK/drain ownership path")
    require("impl_->torn_down.store(true, std::memory_order_release);" in persistence_teardown and
            "impl_->initialized.store(false, std::memory_order_release);" in persistence_teardown,
            "teardown must publish terminal lifecycle state before joining")
    require_before(persistence_teardown,
                   "impl_->torn_down.store(true, std::memory_order_release);",
                   "std::lock_guard<std::mutex> lock(impl_->mutex);",
                   "teardown must invalidate before waiting for an in-flight pump")
    require("impl_->clear_locked();" in persistence_teardown,
            "teardown must clear queued and retained ownership")
    require("if (impl_->torn_down.load(std::memory_order_acquire)) return false;" in
            function_body(persistence, "bool audit_persistence::initialize()"),
            "initialize must not revive a torn-down seam")

    # The firmware adapter must delegate the complete transaction; there is no
    # legacy controller-side write/rename path that could bypass this seam.
    delegated = function_body(audit, "bool export_file_via_persistence(")
    require("wifi_audit_persistence::audit_persistence" in delegated and
            "persistence.enqueue" in delegated and "persistence.pump_one()" in delegated,
            "firmware export must delegate enqueue/pump to the real persistence seam")
    wrapper = function_body(audit, "bool export_file(")
    require("export_file_via_persistence" in wrapper,
            "firmware export wrapper must use the injectable persistence seam")
    require_absent(wrapper, ("write_all(", "recover_stale_artifacts(", "run_transaction("),
                   "firmware wrapper must not reintroduce a legacy transaction")

    # The FreeRTOS/FATFS backend is deliberately checked structurally.  This
    # host contract must not fake or execute FreeRTOS, SemaphoreHandle_t, or a
    # VFS call; the task-level ownership/deadline is a device/build contract.
    firmware_marker = "#if CYBERDECK_AUDIT_FIRMWARE"
    require(firmware_marker in audit,
            "the FreeRTOS audit backend must stay behind the firmware guard")
    firmware_backend = preprocessor_block(audit, firmware_marker)
    task_creation_marker = "xTaskCreate(export_backend_task"
    require(task_creation_marker in firmware_backend,
            "the firmware must create the dedicated backend task")
    backend_task = function_body(audit, "void export_backend_task(void *arg)")
    backend = function_body(audit, "bool export_file(")
    backend_request = block_after(audit, "struct export_backend_request {")
    destroy_request = function_body(audit, "void destroy_export_request(")

    require("std::atomic<bool> export_backend_busy{false};" in audit,
            "the backend must have an atomic single-operation quarantine guard")
    require("constexpr std::uint32_t export_backend_timeout_ms = 2000;" in audit,
            "the backend operation timeout must remain exactly 2000 ms")
    require("constexpr std::uint32_t export_backend_release_timeout_ms = 2000;" in audit,
            "backend release/join must also have a bounded 2000 ms deadline")
    require("xTaskCreate(export_backend_task, \"wifi_audit_io\"," in firmware_backend,
            "the firmware must create the dedicated wifi_audit_io backend task")
    require("export_backend_stack_size" in backend and
            "export_backend_priority" in backend,
            "wifi_audit_io creation must specify an explicit stack and priority")
    require(backend.count("xSemaphoreCreateBinary()") == 2,
            "backend ownership must use separate completion and release semaphores")
    for field in (
            "std::uint64_t token{0};",
            "char path[field_capacity]{};",
            "std::string data{};",
            "SemaphoreHandle_t completed{nullptr};",
            "SemaphoreHandle_t release{nullptr};",
            "std::atomic<bool> result{false};"):
        require(field in backend_request,
                f"backend request must own {field}")

    # Arm the quarantine before allocating anything.  A second request must
    # fail while the first task may still be touching the sidecars.
    require_before(
        backend,
        "if (!export_backend_busy.compare_exchange_strong(",
        "auto *request = new (std::nothrow) export_backend_request{};",
        "the backend guard must be acquired before request allocation")
    busy_rejection = block_after(
        backend, "if (!export_backend_busy.compare_exchange_strong(")
    require("return false;" in busy_rejection,
            "a quarantined backend must reject a concurrent request")
    require("expected, true, std::memory_order_acq_rel" in backend,
            "quarantine acquisition must use release/acquire ownership ordering")
    require_before(
        backend,
        "request->completed = xSemaphoreCreateBinary();",
        "xTaskCreate(export_backend_task",
        "completion ownership must be initialized before task creation")
    require_before(
        backend,
        "request->release = xSemaphoreCreateBinary();",
        "xTaskCreate(export_backend_task",
        "release ownership must be initialized before task creation")
    require_before(
        backend,
        "xTaskCreate(export_backend_task",
        "const bool completed",
        "the caller must wait only after the backend task has been created")

    # The task publishes its result, then waits for the caller's release token.
    # Only the task may destroy the request; the caller must not free memory
    # that a timed-out VFS call can still be using.
    require("request->transaction != nullptr" in backend_task and
            "request->transaction(request->path, request->token, request->data)" in backend_task,
            "wifi_audit_io must invoke the injected transaction on the owned request")
    require_before(
        backend_task,
        "request->transaction(request->path, request->token, request->data)",
        "request->result.store(ok, std::memory_order_release);",
        "the result must be published only after the injected transaction returns")
    require_before(
        backend_task,
        "request->result.store(ok, std::memory_order_release);",
        "(void)xSemaphoreGive(request->completed);",
        "the result must be published before completion is signalled")
    require_before(
        backend_task,
        "(void)xSemaphoreGive(request->completed);",
        "xSemaphoreTake(request->release, pdMS_TO_TICKS(export_backend_release_timeout_ms))",
        "the task must retain ownership after completion until release")
    require_before(
        backend_task,
        "xSemaphoreTake(request->release, pdMS_TO_TICKS(export_backend_release_timeout_ms)",
        "destroy_export_request(request)",
        "request destruction must follow the bounded release hand-off")
    require_before(
        backend_task,
        "destroy_export_request(request)",
        "export_backend_busy.store(false, std::memory_order_release);",
        "quarantine must be released only after owned cleanup")
    require(backend_task.count("export_backend_busy.store(false") == 1,
            "only the task may clear the quarantine after ownership is released")
    require(backend_task.rfind("vTaskDelete(nullptr);") >
            backend_task.rfind("export_backend_busy.store(false"),
            "wifi_audit_io must self-terminate after owned cleanup")
    require("portMAX_DELAY" not in backend_task + backend,
            "backend completion/release hand-offs must not wait indefinitely")

    # The caller uses a bounded completion wait, gives the release token even
    # on timeout, and returns false when completion is absent.  In particular,
    # it must not clear export_backend_busy or destroy the request on that path:
    # a blocked VFS call keeps the slot quarantined until the task finishes.
    completion_wait = (
        "xSemaphoreTake(request->completed, pdMS_TO_TICKS(export_backend_timeout_ms)) "
        "== pdTRUE;")
    require(completion_wait in backend,
            "the caller must enforce the 2000 ms backend completion deadline")
    normal_backend = backend[backend.index("const bool completed"):]
    require("bool ok = false;" in normal_backend and
            "if (completed) {" in normal_backend,
            "backend success must be gated by a completed hand-off")
    require_before(
        normal_backend,
        completion_wait,
        "request->result.load(std::memory_order_acquire);",
        "the caller must read the result only after the completion hand-off")
    require_before(
        normal_backend,
        completion_wait,
        "(void)xSemaphoreGive(request->release);",
        "the caller must signal release after its bounded completion wait")
    require("request->result.load(std::memory_order_acquire);" in normal_backend and
            "return completed && ok;" in normal_backend,
            "a timeout must return false and must not synthesize a success")
    require_absent(
        normal_backend,
        ("destroy_export_request(request)", "vSemaphoreDelete", "delete request",
         "export_backend_busy.store(false"),
        "the caller must leave post-task request ownership and quarantine to the task")
    release_give = "(void)xSemaphoreGive(request->release);"
    before_release = normal_backend[:normal_backend.index(release_give)]
    require("return" not in before_release,
            "a timeout must still hand release to the backend task")
    require("request->" not in normal_backend[normal_backend.index(release_give) +
                                             len(release_give):],
            "the caller must not access the request after handing release to the task")

    # Every pre-task failure releases the guard and destroys anything already
    # created; post-task cleanup is intentionally absent from the caller.
    allocation_failure = block_after(backend, "if (request == nullptr)")
    require("export_backend_busy.store(false, std::memory_order_release);" in allocation_failure,
            "allocation failure must release the backend quarantine")
    setup_failure = block_after(
        backend, "if (request->data.size() != data.size() ||")
    require("destroy_export_request(request)" in setup_failure and
            "export_backend_busy.store(false, std::memory_order_release);" in setup_failure,
            "partial backend setup must clean request and release quarantine")
    task_create_failure = block_after(
        backend, "if (xTaskCreate(export_backend_task")
    require("destroy_export_request(request)" in task_create_failure and
            "export_backend_busy.store(false, std::memory_order_release);" in task_create_failure,
            "task-creation failure must clean request and release quarantine")
    require("vTaskDelete" not in backend,
            "the caller must not delete a task it does not own")

    require("if (request == nullptr) return;" in destroy_request,
            "request cleanup must tolerate a null pointer")
    require_before(
        destroy_request,
        "if (request->release != nullptr) vSemaphoreDelete(request->release);",
        "if (request->completed != nullptr) vSemaphoreDelete(request->completed);",
        "release semaphore cleanup must precede completion semaphore cleanup")
    require_before(
        destroy_request,
        "if (request->completed != nullptr) vSemaphoreDelete(request->completed);",
        "delete request;",
        "both semaphores must be released before the request is deleted")

    # FatFs/VFS configuration must be effective in the firmware defaults, not
    # merely mentioned in comments or documentation.
    fatfs_values = {}
    for key, expected in (
            ("CONFIG_FATFS_FS_LOCK", "5"),
            ("CONFIG_FATFS_TIMEOUT_MS", "1000")):
        assignments = re.findall(
            rf"^{re.escape(key)}=([^\n#]*)", sdkconfig_defaults, re.MULTILINE)
        require(assignments == [expected],
                f"{key} must have one effective default of {expected}")
        fatfs_values[key] = int(assignments[0])
    require("fatfs" in component.lower(),
            "the component must retain the ESP-IDF FATFS dependency")
    require(fatfs_values["CONFIG_FATFS_TIMEOUT_MS"] < 2000,
            "the FatFs mutex timeout must remain below the backend deadline")

    # I/O and durable serialization stay out of the UI/LVGL command and timer
    # paths.  The legacy branch keeps its old checks; the approved simplified
    # branch only hands the explicit save request to a bounded worker.
    if simplified_flow:
        request = block_after(ui, "case CYBERDECK_CMD_WIFI_AUDIT_SAVE:")
        require("cmd.confirmed" not in request,
                "wifi audit save is explicit without a second confirmation verb")
        require("drain_save(" not in request and "drain_export(" not in request,
                "the save command must not wait for persistence")
        require_absent(request,
                       ("export_file(", "write_all(", "fsync(", "rename(", "open(",
                        "safe_export("),
                       "the UI save command must not perform blocking file I/O")
        audit_enqueue = function_body(
            audit, "bool audit_controller::enqueue_export("
            if "enqueue_export(" in request else "bool audit_controller::enqueue_save(")
        require("xQueueSend(s.work, &item, 0)" in audit_enqueue,
                "UI save enqueue must use a non-blocking bounded worker queue")
        require("portMAX_DELAY" not in request + audit_enqueue,
                "UI save paths must not wait indefinitely for persistence")
    else:
        request = block_after(ui, "case CYBERDECK_CMD_WIFI_AUDIT_EXPORT:")
        require_before(request, "cmd.confirmed", "enqueue_export(",
                       "the UI must enforce explicit confirmation before enqueueing")
        require('"/sdcard/wifi-audit.txt"' in request,
                "the UI must enqueue the fixed /sdcard/wifi-audit.txt target")
        require("drain_export(" not in request,
                "the command/UI path must not wait for persistence")
        require("wifi audit export persisted" not in request,
                "the command path must not emit a persistence ACK before worker completion")
        audit_enqueue = function_body(audit, "bool audit_controller::enqueue_export(")
        require("xQueueSend(s.work, &item, 0)" in audit_enqueue,
                "UI enqueue must use a non-blocking bounded worker queue")
        require("const bool queued = xQueueSend(s.work, &item, 0) == pdTRUE;" in audit_enqueue and
                "if (queued) s.export_in_flight = true;" in audit_enqueue,
                "firmware export guard must arm only after a successful queue send")
        require_before(audit_enqueue,
                       "const bool queued = xQueueSend(s.work, &item, 0) == pdTRUE;",
                       "if (queued) s.export_in_flight = true;",
                       "export guard must arm after queue success")
        audit_drain = function_body(audit, "export_result audit_controller::drain_export(")
        require("if (delivered) s.export_in_flight = false;" in audit_drain,
                "drain must release the firmware export guard for every completion")

    timer = function_body(ui, "void process_wifi_audit(")
    drain_call = min((position for position in (
        timer.find("s_wifi_audit.drain_export()"),
        timer.find("s_wifi_audit.drain_save()")) if position >= 0), default=-1)
    ack_tokens = ("wifi audit save persisted", "wifi audit saved",
                  "wifi audit export persisted")
    ack_positions = [timer.find(token) for token in ack_tokens]
    ack_positions = [position for position in ack_positions if position >= 0]
    ack_call = min(ack_positions) if ack_positions else -1
    require(drain_call >= 0 and ack_call >= 0,
            "LVGL timer must drain completion and render the persistence ACK")
    require(drain_call < ack_call,
            "persistence ACK must be rendered only after draining completion")
    require("if (exported.ok)" in timer or "if (saved.ok)" in timer or "if (result.ok)" in timer,
            "the persisted ACK must remain gated by the successful result")
    require_absent(request + timer,
                   ("export_file(", "write_all(", "fsync(", "rename(", "open(",
                    "safe_export("),
                   "UI/LVGL completion paths must not perform blocking file I/O")
    require("portMAX_DELAY" not in request + timer,
            "UI/LVGL audit paths must not wait indefinitely for persistence")
    require_absent(persistence.lower(),
                   ("lvgl", "bsp_display", "cyberdeck_ui", "xqueuesend", "xtaskcreate"),
                   "persistence seam must remain independent of UI/worker callbacks")

    print("PASS: wifi audit structural and safety contracts")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
