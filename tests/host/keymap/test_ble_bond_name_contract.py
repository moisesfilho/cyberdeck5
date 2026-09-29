#!/usr/bin/env python3
"""Focused structural contract for BLE bond restore and display-name flow."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
MGR = ROOT / "components/cyberdeck/src/features/bluetooth/ble_mgr.cpp"
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
SESSION = ROOT / "components/cyberdeck/src/features/shell/cyberdeck_shell_session.cpp"
BG = ROOT / "components/cyberdeck/src/features/bluetooth/cyberdeck_ble_background.cpp"
TYPES = ROOT / "components/cyberdeck/src/features/bluetooth/cyberdeck_ble_types.cpp"
STORE = ROOT / "components/cyberdeck/src/features/bluetooth/cyberdeck_ble_store.cpp"
EVENTS = ROOT / "components/cyberdeck/src/features/bluetooth/cyberdeck_ble_event_dispatch.cpp"


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
    session = SESSION.read_text(encoding="utf-8")
    bg = BG.read_text(encoding="utf-8")
    types = TYPES.read_text(encoding="utf-8")
    store = STORE.read_text(encoding="utf-8")
    events = EVENTS.read_text(encoding="utf-8")

    restore = body(bg, "void scheduler::restore_bonds_once")
    assert "ble_bonds_copy(snapshots, 16)" in restore
    assert "for (size_t i = 0; i < count; ++i)" in restore
    assert "item.name = snapshots[i].name" in restore
    assert "item.addr_type = static_cast<cyberdeck_ble::address_type>(snapshots[i].addr_type)" in restore
    assert "last_connected_index" in restore
    assert restore.index("last_connected_index") < restore.index("arm_background_reconnect")
    assert "BLE_MGR_CMD_PAIR" not in restore
    assert "BLE_MGR_CMD_CONNECT" not in restore

    # Every restored bond gets a bounded background opportunity; last_connected
    # is only the first target, not the only bond eligible after a window ends.
    advance = body(bg, "void scheduler::advance_target")
    assert "for (std::size_t offset = 1; offset <= known.size(); ++offset)" in advance
    assert "candidate->paired && candidate->connectable" in advance
    finished = body(bg, "void scheduler::on_scan_finished")
    assert "advance_target(model)" in finished

    # Five-second discovery windows, bounded retries, and no auto-pair path.
    task = body(mgr, "static void ble_mgr_task")
    scan_start = body(mgr, "static void start_scan_command(")
    assert "ble_gap_disc(own_addr_type, 5000" in scan_start
    assert "filter_duplicates = 1" in scan_start
    result = body(bg, "void scheduler::on_scan_result")
    assert "!event.scan_result.connectable || !event.scan_result.paired" in result
    assert "consume_background_attempt()" in result
    assert "BLE_MGR_CMD_PAIR" not in result
    assert "BLE_MGR_CMD_RECONNECT" not in result
    assert "BLE_MGR_CMD_SCAN_CANCEL" in result
    assert "reconnect_pending_ = true" in result
    assert "event.scan_finished.outcome != static_cast<int>(cyberdeck_ble::notice::failed)" in finished

    # Identity matching includes resolved RPA and address type, so equal address
    # text with a different type cannot merge or trigger a reconnect.
    scan = body(mgr, "static void scan_report_adv")
    assert "ble_gap_rpa_resolve" in scan
    assert "s_scan_peers[i].record.address == addr_str" in scan
    assert "s_scan_peers[i].record.addr_type == addr_type" in scan
    assert "s_store.find(addr_str, addr_type)" in scan
    assert "if (!primary && !peer.primary_seen) return" in scan
    assert "peer.record.connectable = disc->event_type" in scan
    assert "target.addr_type != type" in body(bg, "void scheduler::note_advertisement")
    assert "find(address, type)" in body(bg, "bool scheduler::address_known")

    # ADV and SCAN_RSP names are sanitized before they enter bounded snapshots;
    # empty names stay empty in storage and are rendered as unnamed by the model.
    assert "cyberdeck_ble::sanitize_name" in scan
    assert "name == cyberdeck_ble::k_unnamed_placeholder ? \"\" : name" in scan
    connected = body(mgr, "static void handle_connection_result")
    assert "record.name = scan_name_for_peer(active_address, s_connection_addr_type)" in connected
    assert connected.index("record.name = scan_name_for_peer") < connected.index("save_bonds_to_nvs()")
    assert "strlcpy(slot.name, record.name.c_str(), sizeof(slot.name))" in body(mgr, "size_t ble_bonds_copy")
    # The `bluetooth paired` command switch moved to the extracted session.
    paired = body(session, "case CYBERDECK_CMD_BLUETOOTH_PAIRED:")
    assert "item.name = paired_snapshots[i].name" in paired
    assert "host_.ble_model().set_paired_devices(paired)" in paired
    assert "host_.ble_model().begin_paired()" in paired
    assert "return k_unnamed_placeholder" in body(types, "std::string display_name")
    assert "display_name(d)" in types
    assert "name=" in body(store, "std::string encode_bond")
    assert "sanitize_for_encode(record.name.c_str()" in body(store, "std::string encode_bond")

    # The bounded hand-off must retain identity/name snapshots and reject stale
    # generations without exposing pairing material in summaries.
    assert "k_max_pending_events = 8" in events or "k_max_pending_events" in events
    assert "event.token !=" in events or "token !=" in events
    assert "sanitize_name(event.device_record.name.data()" in events
    assert "passkey" in events

    print("PASS: BLE bond restore/name/reconnect contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
