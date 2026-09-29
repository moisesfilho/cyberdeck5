#include "features/bluetooth/cyberdeck_ble_background.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace cyberdeck_ble_background {

void scheduler::restore_bonds_once(cyberdeck_ble::state_machine &model)
{
    if (restored_) {
        return;
    }
    ble_bond_snapshot_t snapshots[16]{};
    const size_t count = ble_bonds_copy(snapshots, 16);
    if (count == 0) {
        ++restore_attempts_;
        if (restore_attempts_ >= k_restore_retry_limit) {
            restored_ = true;
        }
        return;
    }
    restored_ = true;
    std::vector<cyberdeck_ble::device> paired;
    paired.reserve(count);
    /* Index (not pointer): paired grows inside this loop, so a raw pointer
     * to paired.back() could dangle after the next push_back. */
    size_t last_connected_index = count;
    for (size_t i = 0; i < count; ++i) {
        cyberdeck_ble::device item;
        item.address = snapshots[i].address;
        item.addr_type = static_cast<cyberdeck_ble::address_type>(snapshots[i].addr_type);
        item.name = snapshots[i].name;
        item.rssi = cyberdeck_ble::k_min_rssi;
        item.kind = static_cast<cyberdeck_ble::device_kind>(snapshots[i].kind);
        item.paired = true;
        item.connectable = true;
        paired.push_back(item);
        if (snapshots[i].last_connected) {
            last_connected_index = paired.size() - 1;
        }
    }
    if (paired.empty()) {
        return;
    }
    model.set_paired_devices(paired);
    /* First post-boot reconnect targets only the last-connected bond. */
    if (last_connected_index < paired.size()) {
        const cyberdeck_ble::device last_connected_copy = paired[last_connected_index];
        model.arm_background_reconnect(last_connected_copy);
    }
}

void scheduler::tick(std::uint32_t elapsed_ms)
{
    if (wait_ms_ > elapsed_ms) wait_ms_ -= elapsed_ms;
    else wait_ms_ = 0;
}

bool scheduler::address_known(const cyberdeck_ble::state_machine &model,
                              const std::string &address,
                              cyberdeck_ble::address_type type) const
{
    if (model.has_background_target()) {
        const cyberdeck_ble::device target = model.background_target();
        if (target.address == address && target.addr_type == type) {
            return true;
        }
    }
    const cyberdeck_ble::device_list &known = model.devices();
    return known.find(address, type) != nullptr;
}

void scheduler::note_advertisement(const std::string &address,
                                   cyberdeck_ble::address_type type, bool connectable,
                                   std::uint64_t generation,
                                   cyberdeck_ble::state_machine &model)
{
    if (!model.has_background_target() || !connectable) {
        return;
    }
    const cyberdeck_ble::device target = model.background_target();
    if (target.address != address || target.addr_type != type) {
        return;
    }
    if (address != last_adv_ || type != last_adv_type_ || generation != last_gen_) {
        last_adv_ = address;
        last_adv_type_ = type;
        last_gen_ = generation;
        model.reset_background_cycle();
    }
}

void scheduler::advance_target(cyberdeck_ble::state_machine &model)
{
    if (!model.has_background_target()) return;
    const auto &known = model.devices();
    if (known.size() == 0) return;
    const cyberdeck_ble::device current = model.background_target();
    std::size_t current_index = known.size();
    for (std::size_t i = 0; i < known.size(); ++i) {
        const cyberdeck_ble::device *candidate = known.at(i);
        if (candidate != nullptr && candidate->address == current.address &&
            candidate->addr_type == current.addr_type) {
            current_index = i;
            break;
        }
    }
    if (current_index == known.size()) return;
    for (std::size_t offset = 1; offset <= known.size(); ++offset) {
        const cyberdeck_ble::device *candidate = known.at((current_index + offset) % known.size());
        if (candidate != nullptr && candidate->paired && candidate->connectable) {
            model.arm_background_reconnect(*candidate);
            return;
        }
    }
}

void scheduler::maybe_reconnect(cyberdeck_ble::state_machine &model)
{
    if (!model.background_reconnect_armed()) {
        return;
    }
    if (model.is_connected() || model.owns_input()) {
        return;
    }
    if (wait_ms_ != 0) return;
    if (!scan_active_) {
        ble_mgr_cmd_t cmd{};
        cmd.kind = BLE_MGR_CMD_SCAN_START;
        cmd.token = scan_token_ == 0 ? k_token_base : scan_token_ + 1;
        if (cmd.token < k_token_base) cmd.token = k_token_base;
        if (ble_mgr_enqueue_cmd(&cmd, 0) == ESP_OK) {
            scan_token_ = cmd.token;
            scan_active_ = true;
            wait_ms_ = k_window_interval_ms;
        }
        else {
            /* A failed queue submission still consumes this window. */
            wait_ms_ = k_window_interval_ms;
        }
    }
}

void scheduler::on_scan_result(const ble_mgr_event_t &event,
                               cyberdeck_ble::state_machine &model)
{
    if (!scan_active_ || event.token != scan_token_) {
        return;
    }
    if (!event.scan_result.connectable || !event.scan_result.paired) {
        return;
    }
    const auto type = static_cast<cyberdeck_ble::address_type>(event.scan_result.addr_type);
    note_advertisement(event.scan_result.address, type, event.scan_result.connectable,
                       event.token, model);
    if (!address_known(model, event.scan_result.address, type)) {
        return;
    }
    if (!model.background_reconnect_armed()) {
        return;
    }
    if (!model.consume_background_attempt()) {
        return;
    }
    /* Stop discovery before opening the connection. */
    ble_mgr_cmd_t cancel{};
    cancel.kind = BLE_MGR_CMD_SCAN_CANCEL;
    cancel.token = scan_token_;
    if (ble_mgr_enqueue_cmd(&cancel, 0) == ESP_OK) {
        abandoned_token_ = scan_token_;
        reconnect_pending_ = true;
    } else {
        /* Never connect while discovery may still be active. */
        wait_ms_ = k_window_interval_ms;
        return;
    }
    cyberdeck_ble::device item;
    item.address = event.scan_result.address;
    item.addr_type = type;
    item.name = event.scan_result.name;
    item.rssi = event.scan_result.rssi;
    item.kind = static_cast<cyberdeck_ble::device_kind>(event.scan_result.kind);
    item.paired = true;
    item.connectable = true;
    pending_device_ = item;
}

void scheduler::on_scan_finished(const ble_mgr_event_t &event,
                                 cyberdeck_ble::state_machine &model)
{
    if (!scan_active_ || event.token != scan_token_) {
        return;
    }
    scan_active_ = false;
    wait_ms_ = k_window_interval_ms;
    const bool manually_preempted = abandoned_token_ == event.token;
    if (manually_preempted) {
        abandoned_token_ = 0;
        reconnect_pending_ = false;
        pending_device_ = {};
    } else if (reconnect_pending_) {
        reconnect_pending_ = false;
        if (event.scan_finished.outcome != static_cast<int>(cyberdeck_ble::notice::failed)) {
            model.schedule_reconnect(pending_device_);
        }
    } else {
        advance_target(model);
    }
    pending_device_ = {};
}

void scheduler::preempt_for_manual()
{
    abandoned_token_ = scan_token_;
    reconnect_pending_ = false;
    pending_device_ = {};
    wait_ms_ = k_window_interval_ms;
}

} // namespace cyberdeck_ble_background
