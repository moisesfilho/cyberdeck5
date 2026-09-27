#!/usr/bin/env python3
"""Minimal host-only contracts for the BLE scan runtime regressions.

The NimBLE adapter and LVGL UI are not host-linkable.  These source contracts
exercise the safety-critical ordering and bounded logging seams without
opening hardware, a simulator, or the Serial Automation Bridge.
"""
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[3]
MGR = ROOT / "components/cyberdeck/src/features/bluetooth/ble_mgr.cpp"
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"


def function_body(source: str, signature: str) -> str:
    marker = 0
    while True:
        marker = source.find(signature, marker)
        if marker < 0:
            raise AssertionError(f"missing {signature}")
        opening = source.find("{", marker)
        semicolon = source.find(";", marker)
        if opening >= 0 and (semicolon < 0 or opening < semicolon):
            break
        marker += len(signature)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated {signature}")


def main() -> int:
    mgr = MGR.read_text(encoding="utf-8")
    ui = UI.read_text(encoding="utf-8")

    task = function_body(mgr, "static void ble_mgr_task(")
    start = task[task.index("case BLE_MGR_CMD_SCAN_START:"):]
    start = start[:start.index("case BLE_MGR_CMD_SCAN_CANCEL:")]
    cancel = "(void)ble_gap_disc_cancel();"
    assert start.count(cancel) == 1, "scan preemption must have one cancel site"
    cancel_at = start.index(cancel)
    active_guard = start.rfind("if (ble_gap_disc_active())", 0, cancel_at)
    assert active_guard >= 0, "preemption cancel must be guarded by active scan state"
    assert start[active_guard:cancel_at].count("{") == 1

    gap_complete = function_body(mgr, "case BLE_GAP_EVENT_DISC_COMPLETE:")
    assert "BLE GAP discovery complete" in gap_complete
    assert "event->disc_complete.reason" in gap_complete
    assert "handle_scan_finished(event->disc_complete.reason)" in gap_complete

    finish = function_body(mgr, "static void handle_scan_finished(")
    first_publish = finish.index("s_dispatch.publish_scan_finished(token, outcome)")
    drain = finish.index("drain_dispatch_events()", first_publish)
    retry = finish.index("s_dispatch.publish_scan_finished(token, outcome)", drain)
    assert first_publish < drain < retry, "terminal event must retry after dispatch recovery"
    assert "s_dispatch.pending()" in finish

    # Only fixed-width counters/identifiers may occur in scan terminal logs;
    # no advertising bytes, names, SSIDs, passkeys, or key material.
    log_lines = [
        line for line in mgr.splitlines()
        if '"BLE GAP discovery complete' in line
        or '"BLE scan finish' in line
        or '"BLE scan terminal queue full' in line
        or '"BLE scan terminal event dropped' in line
    ]
    assert len(log_lines) == 4
    joined = "\n".join(log_lines).lower()
    assert all("%s" not in line and len(line) <= 220 for line in log_lines)
    for forbidden in ("payload", "data", "name", "ssid", "passkey", "pin", "key", "irk", "ltk"):
        assert forbidden not in joined, f"secret/payload field leaked into scan log: {forbidden}"

    # Observer callbacks are copied under the mutex, then invoked only after
    # the critical section has been released.
    publish = function_body(mgr, "static void publish_event_to_observers(")
    snapshot_start = publish.index("struct observer_snapshot")
    release = publish.index("xSemaphoreGive(s_ble_mutex);", snapshot_start)
    callback = publish.index("snapshots[i].cb(event, snapshots[i].user_ctx);", release)
    assert "snapshots[count++] = {s_observers[i].cb, s_observers[i].user_ctx};" in publish
    assert release < callback, "observer callback must run outside the mutex"
    assert publish.count("xSemaphoreTake(s_ble_mutex") == 1
    assert publish.count("xSemaphoreGive(s_ble_mutex)") == 1
    assert "BLE event has no observer kind=%d token=%llu" in publish
    assert "BLE event publication mutex timeout kind=%d" in publish
    observer_logs = "\n".join(line for line in publish.splitlines() if "ESP_LOG" in line).lower()
    assert all("%s" not in line and len(line) <= 220 for line in observer_logs.splitlines())
    for forbidden in ("payload", "data", "name", "ssid", "passkey", "pin", "key", "irk", "ltk"):
        assert forbidden not in observer_logs, f"observer log exposes payload/secret field: {forbidden}"

    process = function_body(ui, "void process_ble_events(")
    advance = process.index("s_ble_model.advance_time(100);")
    queue_guard = process.index("while (s_ble_event_queue != nullptr &&")
    assert process.count("s_ble_model.advance_time(100);") == 1
    assert advance < queue_guard, "timeout must advance before queue/observer delivery"
    assert "ble_submit_actions();" in process[advance:queue_guard]
    # The snapshot controls rendering only.  Deadline advancement is an
    # unconditional tick operation, including when ownership is false and the
    # event queue is unavailable.
    changed = "bool changed = s_ble_model.owns_input();"
    changed_at = process.index(changed)
    assert changed_at < advance
    changed_line = process[process.rfind("\n", 0, changed_at) + 1:process.find("\n", changed_at)]
    advance_line = process[process.rfind("\n", 0, advance) + 1:process.find("\n", advance)]
    assert advance_line.strip() == "s_ble_model.advance_time(100);"
    assert len(advance_line) - len(advance_line.lstrip()) == len(changed_line) - len(changed_line.lstrip())
    assert "if (s_ble_model.owns_input())" not in process

    # Successful scans with devices have no notice text, so the terminal
    # event itself must trigger list rendering. Empty/timeout outcomes retain
    # their notice and the shared terminal path remains prompt-safe.
    terminal = process.index("case BLE_MGR_EVT_SCAN_FINISHED:")
    outcome = process.index("const cyberdeck_ble::notice outcome", terminal)
    failed = process.index("s_ble_model.scan_failed(event.token)", outcome)
    timed_out = process.index("s_ble_model.scan_timed_out(event.token)", outcome)
    finished = process.index("s_ble_model.scan_finished(event.token, s_ble_scan_devices.snapshot())", outcome)
    terminal_marker = process.index("sync_ble_transient_block();", terminal)
    assert failed < terminal_marker and timed_out < terminal_marker and finished < terminal_marker
    assert process.index("if (!notice.empty() && notice != s_ble_last_notice)") > terminal_marker
    # Results are now transient: the model-backed list is rendered by the
    # common repaint path and consolidated only when ownership is released.
    assert process.index("render_terminal();", terminal_marker) > terminal_marker
    assert "append_line(s_ble_model.devices().render());" not in process
    assert "append_line(notice + \"\\n\")" in process

    render = function_body(ui, "void render_terminal()")
    marker = render.index("const std::string marker =")
    assert "s_ble_model.owns_input()" in render[marker:]
    assert "fit_prompt_marker(s_local_shell.cwd() + \"$ \")" in render[marker:]
    assert "render_terminal();" in process[process.index("if (changed)"):]

    print("PASS: BLE runtime regressions (active cancel/terminal recovery/UI timeout/log secrecy)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        sys.exit(1)
