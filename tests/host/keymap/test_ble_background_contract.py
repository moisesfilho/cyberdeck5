#!/usr/bin/env python3
"""Structural contract for boot bond restore and background BLE reconnect."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
MGR = ROOT / "components/cyberdeck/src/features/bluetooth/ble_mgr.cpp"
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
BG = ROOT / "components/cyberdeck/src/features/bluetooth/cyberdeck_ble_background.cpp"
BG_HDR = ROOT / "components/cyberdeck/include/features/bluetooth/cyberdeck_ble_background.h"
STATE = ROOT / "components/cyberdeck/src/features/bluetooth/cyberdeck_ble_state_machine.cpp"
MAKEFILE = ROOT / "tests/host/keymap/Makefile"


def body(source: str, signature: str) -> str:
    start = 0
    while True:
        start = source.find(signature, start)
        if start < 0:
            raise AssertionError(f"missing {signature}")
        opening = source.find("{", start)
        semicolon = source.find(";", start)
        if opening >= 0 and (semicolon < 0 or opening < semicolon):
            break
        start += len(signature)
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
    bg = BG.read_text(encoding="utf-8")
    bg_hdr = BG_HDR.read_text(encoding="utf-8")
    state = STATE.read_text(encoding="utf-8")
    makefile = MAKEFILE.read_text(encoding="utf-8")

    # The periodic observer is a 10 s window, not a per-tick retry loop.
    # Scheduling state lives in the background service; the UI tick only
    # consumes one window slice before progressing the observer.
    assert "k_window_interval_ms = 10000" in bg_hdr
    assert "k_ble_background_window_interval_ms" not in ui
    process = body(ui, "void process_ble_events(")
    assert "s_ble_background.tick(100)" in process
    assert process.index("s_ble_background.tick(100)") < process.index(
        "s_ble_background.maybe_reconnect(s_ble_model)"
    )

    load = body(mgr, "static void load_bonds_from_nvs")
    assert "BLE_MGR_NVS_KEY_V2" in mgr
    assert "s_store.deserialize(buffer.data(), required_size)" in mgr
    assert "new pairing is required" in mgr

    restore = body(bg, "void scheduler::restore_bonds_once")
    assert "ble_bonds_copy(snapshots, 16)" in restore
    assert "model.set_paired_devices(paired)" in restore
    assert "arm_background_reconnect(last_connected_copy)" in restore
    assert "last_connected_index" in restore
    assert "&paired.back()" not in restore
    assert "= &paired" not in restore
    assert "BLE_MGR_CMD_SCAN_START" not in restore
    assert "BLE_MGR_CMD_CONNECT" not in restore

    # Inspect the command case in the central dispatcher, not the helper that
    # only emits terminal results while the NimBLE host is pre-sync.
    dispatcher = body(mgr, "static void ble_mgr_task(void *arg)\n{")
    gate = dispatcher.index("if (!s_host_synced && cmd.kind != BLE_MGR_CMD_STOP)")
    switch = dispatcher.index("switch (cmd.kind)")
    assert gate < switch
    pre_sync = dispatcher[gate:switch]
    assert "reject_pre_sync_command(cmd)" in pre_sync
    assert "continue;" in pre_sync
    # GAP calls are owned by the manager task but are never wrapped by the
    # dispatch mutex; callbacks only enqueue snapshots for this task.
    assert "xSemaphoreTake(s_dispatch_mutex" not in dispatcher
    assert "xSemaphoreGive(s_dispatch_mutex)" not in dispatcher

    scan = body(bg, "bool scheduler::address_known")
    assert "target.address == address && target.addr_type == type" in scan
    assert "known.find(address, type)" in scan
    note = body(bg, "void scheduler::note_advertisement")
    assert "reset_background_cycle" in note
    assert "generation" in note
    assert "generation != last_gen_" in note
    assert "target.addr_type != type" in note
    assert "reset_background_cycle" in note
    result = body(bg, "void scheduler::on_scan_result")
    assert "!event.scan_result.connectable || !event.scan_result.paired" in result
    assert "!address_known" in result
    assert "consume_background_attempt" in result
    # A matching advertisement reserves an attempt and requests cancellation;
    # reconnect is not queued until the terminal scan callback confirms it.
    assert "schedule_reconnect(item)" not in result
    assert "abandoned_token_ = scan_token_" in result
    assert "cancel.token = scan_token_" in result

    finished = body(bg, "void scheduler::on_scan_finished")
    assert "!scan_active_ || event.token != scan_token_" in finished
    assert "scan_active_ = false" in finished
    assert "wait_ms_ = k_window_interval_ms" in finished
    assert "reconnect_pending_" in finished
    assert "event.scan_finished.outcome != static_cast<int>(cyberdeck_ble::notice::failed)" in finished
    assert finished.index("event.scan_finished.outcome") < finished.index("schedule_reconnect")

    # A failed cancellation is terminal and must not spin or open a link.
    assert "ble_gap_disc_cancel" in mgr
    cancel_start = dispatcher.index("case BLE_MGR_CMD_SCAN_CANCEL:", switch)
    cancel_end = dispatcher.index("case BLE_MGR_CMD_PAIR:", cancel_start)
    cancel = dispatcher[cancel_start:cancel_end]
    assert "reject_pre_sync_command" not in cancel
    assert "BLE_MGR_CMD_PAIR" not in cancel
    assert "BLE_HS_EALREADY" in cancel
    assert "scan_active ? ble_gap_disc_cancel() : BLE_HS_EALREADY" in cancel
    assert "publish_scan_finished" in cancel
    assert "cyberdeck_ble::notice::failed" in cancel
    assert "ble_gap_connect" not in cancel

    # REQ-BLE-02/AC-BLE-02 (revisado): a cancel whose token is no longer the
    # active generation is ignored. The dispatcher rejects events emitted
    # with a non-active token, so publishing a terminal outcome for a stale
    # generation would be rejected/no-op; the previous DISC_COMPLETE terminal
    # remains the valid source. Mutation: drop the stale branch and the
    # assertions below fail.
    assert "cmd.token != s_scan_token" in cancel
    assert "cmd.token != s_dispatch.active_scan_token()" in cancel
    assert "BLE scan cancel stale token" in cancel
    assert cancel.count("publish_scan_finished") >= 2
    stale = cancel[cancel.index("cmd.token != s_scan_token"):cancel.index("const bool scan_active")]
    assert "ignoring" in stale
    assert "break" in stale
    # TEST-BLE-04: stale branch ignores before any active cancel, publish,
    # connect, or drain. No terminal event is emitted for the obsolete
    # generation and the active generation is untouched.
    assert "publish_scan_finished" not in stale
    assert "notice::cancelled" not in stale
    assert "cmd.token, cyberdeck_ble::notice::cancelled" not in stale
    assert "drain_dispatch_events" not in stale
    assert "ble_gap_disc_cancel" not in stale
    assert "ble_gap_disc_active" not in stale
    assert "ble_gap_connect" not in stale
    assert "handle_scan_finished" not in stale
    # TEST-BLE-04: zero-token cancel drops silently without terminal output.
    zero_guard = cancel[:cancel.index("cmd.token != s_scan_token")]
    assert "cmd.token == 0" in zero_guard
    assert "publish_scan_finished" not in zero_guard
    assert "drain_dispatch_events" not in zero_guard
    assert "ble_gap_disc_cancel" not in zero_guard
    # TEST-BLE-04/REQ-BLE-02: stale cancel must not invalidate active generation.
    assert "s_scan_token =" not in stale
    assert "s_scan_cancel_pending" not in stale
    assert "s_scan_cancel_token" not in stale
    assert "s_scan_next_generation_ready" not in stale

    # ble_gap_disc returning EALREADY means a previous discovery is still
    # active.  The manager must force a cancel to restore the GAP state
    # instead of degrading permanently.  Mutation: remove the forced
    # ble_gap_disc_cancel from the EALREADY recovery and this fails.
    scan_cmd = body(mgr, "static void start_scan_command(const ble_mgr_cmd_t &cmd)\n{")
    assert "BLE_HS_EALREADY" in scan_cmd
    recovery = scan_cmd[scan_cmd.index("BLE_HS_EALREADY"):]
    recovery = recovery[: recovery.index("if (rc != 0)")]
    assert "ble_gap_disc_cancel" in recovery, (
        "EALREADY from ble_gap_disc must force a cancel to clear stale discovery"
    )
    assert "ble_gap_disc_active" in recovery
    assert recovery.index("ble_gap_disc_cancel") < recovery.index("ble_gap_disc_active")
    # REQ-BLE-01/TEST-BLE-01+02: EALREADY stays terminal-coherent for the
    # current generation via handle_scan_finished(token, rc). Host cannot
    # simulate NimBLE timing, so next-window liveness remains a device gap.
    assert scan_cmd.index("BLE_HS_EALREADY") < scan_cmd.index("if (rc != 0)")
    terminal = scan_cmd[scan_cmd.index("if (rc != 0)"):]
    assert "handle_scan_finished(token, rc)" in terminal
    assert terminal.index("handle_scan_finished(token, rc)") < terminal.index("drain_dispatch_events")

    # Cancellation is serialized by the manager task and its terminal callback
    # is delivered through the dedicated DISC_COMPLETE snapshot slot (or the
    # already-stopped terminal branch).
    gap = body(mgr, "static int ble_gap_event_cb(struct ble_gap_event *event, void *arg)\n{")
    assert "s_dispatch_mutex" not in gap
    assert "xQueueOverwrite(queue, &snapshot)" in gap
    assert "xQueueSend(queue, &snapshot, 0)" in gap
    complete = body(mgr, "static void process_gap_event(")
    complete = complete[complete.index("case BLE_GAP_EVENT_DISC_COMPLETE:"):]
    assert "handle_scan_finished(token, event->disc_complete.reason)" in complete
    assert "s_scan_cancel_pending = false" in complete

    # Snapshot contention retries are bounded, and restoration cannot remain
    # pending forever when the adapter never yields a snapshot.
    assert "k_restore_retry_limit = 3" in bg_hdr
    assert "restore_attempts_ >= k_restore_retry_limit" in restore

    # A successful connection clears every previous marker before setting the
    # current typed bond as the sole last-connected record.
    connection_result = body(mgr, "static void handle_connection_result(int status)\n{")
    assert "if (connected)" in connection_result
    assert "cleared.last_connected = false" in connection_result
    assert "record.last_connected = true" in connection_result
    assert connection_result.index("cleared.last_connected = false") < connection_result.index(
        "record.last_connected = true"
    )
    assert "s_connection_addr_type" in connection_result
    assert "s_store.find(active_address, s_connection_addr_type)" in connection_result

    observer = body(bg, "void scheduler::maybe_reconnect")
    assert "model.is_connected() || model.owns_input()" in observer
    assert "BLE_MGR_CMD_SCAN_START" in observer
    assert "BLE_MGR_CMD_RECONNECT" not in observer
    assert "model.is_connected() || model.owns_input()" in observer

    # Manual scans/pair/connect preempt only an active background discovery;
    # their callbacks remain on the normal model path.  The scheduler owns the
    # abandoned token; the UI only routes the preemption request.
    assert "void scheduler::preempt_for_manual" in bg
    submit = body(ui, "void ble_submit_actions")
    assert "s_ble_background.scan_active()" in submit
    assert "s_ble_background.preempt_for_manual()" in submit
    assert "BLE_MGR_CMD_SCAN_CANCEL" in submit
    assert "action.kind == cyberdeck_ble::action_kind::start_scan" in submit
    assert "action.kind == cyberdeck_ble::action_kind::pair" in submit
    assert "action.kind == cyberdeck_ble::action_kind::connect" in submit
    assert "s_ble_background_scan_token" not in submit

    # Background events are isolated by active token; abandoned callbacks are
    # dropped before the normal scan result/terminal handling.
    event_loop = process
    assert "event.token == s_ble_background.abandoned_token()" in event_loop
    assert event_loop.index("s_ble_background.abandoned_token()") < event_loop.index(
        "const bool background_event"
    )
    assert "if (background_event)" in event_loop
    assert "s_ble_scan_devices.clear()" in event_loop

    # The pure model owns the three-attempt cap and explicit re-arm semantics.
    consume = body(state, "bool state_machine::consume_background_attempt")
    assert "max_reconnect_attempts" in consume
    assert "background_manually_blocked" in consume
    arm = body(state, "void state_machine::arm_background_reconnect")
    assert "background_cycle_attempts = 0" in arm
    block = body(state, "void state_machine::block_background_reconnect")
    assert "background_armed = false" in block
    assert "background_manually_blocked = true" in block
    assert "background_cycle_attempts = 0" in block
    reset = body(state, "void state_machine::reset_background_cycle")
    assert "background_manually_blocked" in reset
    assert "background_armed = true" in reset

    # Pure-model callbacks keep manual and interactive operations independent
    # from the background budget and preserve identity/token data.
    assert "background_attempt_reserved" in body(state, "bool state_machine::consume_background_attempt")
    assert "state_->background_connection_in_flight = state_->background_attempt_reserved" in body(
        state, "void state_machine::schedule_reconnect"
    )
    assert "state_->background_connection_in_flight = false" in body(
        state, "void state_machine::connection_finished"
    )
    assert "state_->background_manually_blocked = true" in body(state, "void state_machine::press")

    # The focused checks must be part of the aggregate host gate, not only
    # individually invokable targets.
    assert "test_ble_background_contract" in makefile
    assert "@./test_ble_state_machine" in makefile
    assert "@$(MAKE) --no-print-directory test_ble_background_contract" in makefile

    print("PASS: BLE boot restore/background reconnect contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
