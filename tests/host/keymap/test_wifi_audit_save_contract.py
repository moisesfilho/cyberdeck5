#!/usr/bin/env python3
"""RED structural contract for the simplified Wi-Fi audit save flow.

The firmware UI and its local-clock path are not host-linkable.  This contract
therefore checks the real sources for the seams that the behavioral C++ test
cannot execute, while preserving the existing passive-audit and durable-write
safety requirements.  It deliberately checks behavior/order rather than a
particular private helper name.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
SHELL = ROOT / "components/cyberdeck/src/features/shell/cyberdeck_shell_utils.cpp"
SHELL_HEADER = ROOT / "components/cyberdeck/include/features/shell/cyberdeck_shell_utils.h"
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
AUDIT = ROOT / "components/cyberdeck/src/features/wifi/cyberdeck_wifi_audit.cpp"
PERSISTENCE = ROOT / "components/cyberdeck/src/features/wifi/cyberdeck_wifi_audit_persistence.cpp"
PERSISTENCE_HEADER = ROOT / "components/cyberdeck/include/features/wifi/cyberdeck_wifi_audit_persistence.h"
SERIAL = ROOT / "components/cyberdeck/src/features/serial/cyberdeck_serial_bridge.cpp"
MAKEFILE = ROOT / "tests/host/keymap/Makefile"
CODEMAP = ROOT / "code-map.md"
GITIGNORE = ROOT / ".gitignore"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def without_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//[^\n]*", "", source)


def function_body(source: str, signature: str) -> str:
    """Return one function body, skipping a forward declaration."""
    start = 0
    while True:
        marker = source.find(signature, start)
        require(marker >= 0, f"missing function {signature}")
        opening = source.find("{", marker)
        semicolon = source.find(";", marker)
        if semicolon >= 0 and semicolon < opening:
            start = semicolon + 1
            continue
        require(opening >= 0, f"missing body for {signature}")
        depth = 0
        for index in range(opening, len(source)):
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
                if depth == 0:
                    return source[opening + 1:index]
        raise AssertionError(f"unterminated function {signature}")


def block_after(source: str, marker: str) -> str:
    start = source.find(marker)
    require(start >= 0, f"missing marker {marker!r}")
    opening = source.find("{", start + len(marker))
    require(opening >= 0, f"marker {marker!r} is not followed by a block")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    raise AssertionError(f"unterminated block after {marker!r}")


def require_before(body: str, first: str, second: str, message: str) -> None:
    require(first in body and second in body, message)
    require(body.index(first) < body.index(second), message)


def require_absent(body: str, tokens: tuple[str, ...], message: str) -> None:
    for token in tokens:
        require(token not in body, f"{message}: found {token!r}")


def first_index_any(body: str, tokens: tuple[str, ...]) -> int:
    positions = [body.find(token) for token in tokens]
    positions = [position for position in positions if position >= 0]
    return min(positions) if positions else -1


def check_parser(shell: str, audit: str, header: str) -> None:
    shell_body = without_comments(function_body(shell, "cyberdeck_cmd_t cyberdeck_parse_command("))
    audit_body = without_comments(function_body(audit, "command_line parse_command("))
    for body, name in ((shell_body, "shell"), (audit_body, "audit")):
        require('"wifi audit save"' in body,
                f"{name} parser must recognize the explicit save command")
        require('"wifi audit export"' not in body,
                f"{name} parser must not retain the retired export command")
        require('"wifi audit"' in body,
                f"{name} parser must retain the ordinary audit command")
    require("CYBERDECK_CMD_WIFI_AUDIT_SAVE" in header or
            "CYBERDECK_CMD_WIFI_AUDIT" in header,
            "shell enum must expose a distinct save/audit command contract")


def check_direct_render(audit: str) -> None:
    render = without_comments(function_body(audit, "std::string render_ui("))
    for field in ("ssid", "bssid", "ip"):
        require(field in render,
                f"wifi audit UI must render the {field} field")
    require("<missing>" in render,
            "wifi audit UI must mark every unavailable field as <missing>")
    require("status" in render or "ready" in render or "error" in render,
            "wifi audit UI must render the snapshot status")
    require("safe_export" not in render,
            "ordinary wifi audit rendering must not become an export operation")
    # The state contract is intentionally checked in the real renderer: a
    # collecting snapshot has no user-visible snapshot/fields, while an error
    # carries only its status line.  The host C++ test exercises the exact
    # bytes; these structural checks keep the gate visible to reviewers.
    require(re.search(r"state::collecting", render) is not None,
            "render_ui must explicitly gate collecting state")
    require(re.search(r"state::error", render) is not None,
            "render_ui must explicitly gate error state")
    require("return" in render,
            "non-terminal renderer states must have an explicit empty-output path")
    require("wifi audit: status=error" in render,
            "error rendering must expose a status-only error path")


def check_standard_flow_has_no_io(ui: str) -> None:
    branch = block_after(ui, "case CYBERDECK_CMD_WIFI_AUDIT:")
    require_absent(
        branch,
        ("enqueue_export", "enqueue_save", "export_file", "open_exclusive",
         "write_all", "fsync", "rename(", "mkdir", "ensure_directory"),
        "ordinary wifi audit must not perform persistence or filesystem I/O")
    require("drain_export" not in branch and "drain_save" not in branch,
            "ordinary wifi audit must not wait for or drain a save completion")
    require_absent(branch, ("append_line", "render_terminal"),
                   "the audit command must not publish the collecting snapshot; "
                   "the LVGL timer is the sole state-to-output gate")
    require("s_wifi_audit.begin" in branch,
            "the audit command must start a versioned audit request")


def check_save_flow_is_explicit(ui: str) -> None:
    save_branch = block_after(ui, "case CYBERDECK_CMD_WIFI_AUDIT_SAVE:")
    require(first_index_any(save_branch, ("enqueue_save", "enqueue_export", "save_audit")) >= 0,
            "wifi audit save must hand off to the explicit save worker")
    require("drain_save" not in save_branch and "drain_export" not in save_branch,
            "the command path must not synchronously wait for persistence")
    require_absent(
        save_branch,
        ("open_exclusive", "write_all", "fsync", "rename(", "export_file("),
        "the UI save command must remain a non-blocking hand-off")
    require("wifi audit save" in save_branch or "wifi audit saved" in save_branch,
            "save branch must retain the user-visible save command contract")


def check_process_wifi_audit_state_gate(ui: str) -> None:
    timer = without_comments(function_body(ui, "void process_wifi_audit("))
    require("snapshot_view" in timer,
            "the audit timer must inspect the versioned snapshot")
    require("render_ui" in timer and "append_line" in timer,
            "the audit timer must be the state-to-terminal renderer")
    require("s_wifi_audit_reported_token" in timer and
            "s_wifi_audit_reported_state" in timer,
            "the audit timer must retain token/state deduplication")
    require(re.search(r"s_wifi_audit_reported_state\s*=\s*value\.status", timer)
            is not None,
            "the timer must record the rendered state before output")
    require(re.search(r"value\.status\s*==\s*(?:cyberdeck_wifi_audit::)?state::ready", timer)
            is not None,
            "the timer must render ready only through an explicit ready-state gate")
    require(re.search(r"value\.status\s*==\s*(?:cyberdeck_wifi_audit::)?state::error", timer)
            is not None,
            "the timer must render error only through an explicit error-state gate")
    require(re.search(r"if\s*\(\s*value\.token\s*!=\s*0", timer) is not None,
            "the timer gate must ignore empty/unversioned snapshots")
    require("value.status != s_wifi_audit_reported_state" in timer,
            "the timer must suppress repeated terminal output for the same token")
    require_absent(
        timer,
        ('"wifi audit: status=collecting', '"ssid: <missing>"',
         '"bssid: <missing>"', '"ip: <missing>"'),
        "the timer must not synthesize collecting or missing-field lines")
    require_absent(
        timer,
        ('"wifi audit: status=collecting', '"ssid: <missing>"',
         '"bssid: <missing>"', '"ip: <missing>"'),
        "the timer must not synthesize collecting or missing-field lines")


def check_directory_timestamp_and_collision(ui: str, audit: str, persistence: str) -> None:
    combined = "\n".join((ui, audit, persistence))
    clean_combined = without_comments(combined)
    require("/sdcard/wifi-audit/" in clean_combined,
            "save target and sidecars must be confined below /sdcard/wifi-audit/")
    require("wifi-audit-" in clean_combined and ".txt" in clean_combined,
            "save must use the timestamped wifi-audit-YYYYMMDD-HHMMSS.txt name")
    require("%H%M%S" in clean_combined or "HHMMSS" in clean_combined,
            "save filename must encode HHMMSS")
    require("CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN" in clean_combined or
            "-180" in clean_combined or "cyberdeck_clock_from_utc" in clean_combined,
            "save timestamp must use the fixed GMT-3 offset")
    require("localtime_r" not in without_comments(ui) + without_comments(audit),
            "save timestamp must not depend on the process timezone")
    require(any(token in clean_combined for token in
                ("mkdir", "ensure_directory", "create_directories", "make_directory")),
            "the default wifi-audit directory must be created by the save flow")
    require("O_EXCL" in clean_combined,
            "candidate creation must remain exclusive")
    require("has_existing" in clean_combined or "collision" in clean_combined or
            "existing" in clean_combined,
            "save flow must explicitly handle an existing target/collision")
    require("return false;" in clean_combined or "failed" in clean_combined,
            "a collision or invalid target must fail closed")

    for suffix in (".tmp", ".bak"):
        require(suffix in persistence,
                f"durable transaction must retain {suffix} sidecars")
    persistence_clean = without_comments(persistence)
    require("/sdcard/wifi-audit/" in persistence_clean or
            "wifi-audit/" in persistence_clean,
            "persistence sidecar paths must be rooted in the default directory")
    require("/sdcard/wifi-audit.txt" not in persistence_clean,
            "the retired root-level target must not remain the save destination")
    require("O_EXCL" in persistence_clean and "fsync" in persistence_clean and
            "rename" in persistence_clean and "rollback" in persistence_clean.lower(),
            "durable save contract must retain exclusive write/fsync/rename/rollback")


def check_ack_order(ui: str, persistence: str) -> None:
    timer = function_body(ui, "void process_wifi_audit(")
    drain = first_index_any(timer, ("drain_save", "drain_export", "drain_persistence"))
    ack = first_index_any(timer, ("persisted", "saved", "save failed"))
    require(drain >= 0 and ack >= 0,
            "UI timer must drain a save completion and render an ACK")
    require(drain < ack, "save ACK must be rendered only after completion drain")
    require(re.search(r"if\s*\(\s*(?:exported|saved|result)\.ok", timer) is not None,
            "save success ACK must be gated by a successful completion")

    transaction = function_body(persistence, "completion run_transaction(")
    require_before(transaction, "write_all(operations", "operations.fsync(fd)",
                   "write must precede fsync")
    require_before(transaction, "operations.fsync(fd)", "operations.close(fd)",
                   "fsync must precede close")
    require_before(transaction, "operations.close(fd)", "operations.rename(",
                   "close must precede publication")
    require_before(transaction, "operations.rename(", "result.data = request.data;",
                   "payload/ACK data must follow durable publication")
    require("publish(result)" in function_body(persistence,
                                               "bool audit_persistence::pump_one()"),
            "persistence pump must publish the completion")


def check_serial_ui_compatibility(serial: str) -> None:
    require('"ui.type"' in serial,
            "serial bridge must retain the host-testable UI text command")
    require("exec_ui_type" in serial,
            "serial UI text must be forwarded to the UI executor")

    # The executor is allowed to keep the keyboard seam behind the bounded
    # injection helpers.  Verify the transitive path instead of requiring an
    # artificial direct cyberdeck_keyboard_input call in exec_ui_type.
    executor = without_comments(function_body(serial, "void exec_ui_type("))
    text_injector = without_comments(function_body(serial, "void inject_text_segmented("))
    enter_injector = without_comments(function_body(serial, "void inject_enter("))
    text_helper = without_comments(function_body(serial, "void inject_text("))

    require("inject_text_segmented" in executor and "inject_enter" in executor,
            "serial UI text must use segmented text injection and submit with Enter")
    require("inject_text(" in text_injector,
            "segmented serial text must delegate to the bounded text injector")
    require("cyberdeck_keyboard_input" in text_helper and
            "cyberdeck_keyboard_input" in enter_injector,
            "serial text and Enter injection must reach cyberdeck_keyboard_input transitively")
    require("wifi audit export" not in without_comments(serial),
            "serial bridge must not reintroduce the retired export command")


def check_wiring() -> None:
    makefile = MAKEFILE.read_text(encoding="utf-8")
    codemap = CODEMAP.read_text(encoding="utf-8")
    gitignore = GITIGNORE.read_text(encoding="utf-8")
    require("test_wifi_audit_save.cpp" in makefile,
            "Makefile must register the new behavioral save test")
    require("test_wifi_audit_save_contract.py" in makefile,
            "Makefile must register the structural save contract")
    require("test_wifi_audit_save.cpp" in codemap,
            "code-map.md must map the new save behavioral test")
    require("test_wifi_audit_save_contract.py" in codemap,
            "code-map.md must map the new save structural contract")
    require("process_wifi_audit" in codemap,
            "code-map.md must map the UI audit state gate")
    require("!tests/host/keymap/test_wifi_audit_save.cpp" in gitignore and
            "!tests/host/keymap/test_wifi_audit_save_contract.py" in gitignore,
            "new test sources must be trackable despite the host test ignore rule")


def main() -> int:
    shell = SHELL.read_text(encoding="utf-8")
    header = SHELL_HEADER.read_text(encoding="utf-8")
    ui = UI.read_text(encoding="utf-8")
    audit = AUDIT.read_text(encoding="utf-8")
    persistence = PERSISTENCE.read_text(encoding="utf-8")
    persistence_header = PERSISTENCE_HEADER.read_text(encoding="utf-8")
    serial = SERIAL.read_text(encoding="utf-8")
    require("class audit_persistence" in persistence_header,
            "production persistence header must remain the host-testable seam")
    check_parser(shell, audit, header)
    check_direct_render(audit)
    check_standard_flow_has_no_io(ui)
    check_save_flow_is_explicit(ui)
    check_process_wifi_audit_state_gate(ui)
    check_directory_timestamp_and_collision(ui, audit, persistence)
    check_ack_order(ui, persistence)
    check_serial_ui_compatibility(serial)
    check_wiring()
    print("PASS: simplified Wi-Fi audit save structural contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
