#include "apps/bluetooth/cyberdeck_ble_background.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
std::vector<ble_bond_snapshot_t> bonds;
std::vector<ble_mgr_cmd_t> commands;
}

extern "C" size_t ble_bonds_copy(ble_bond_snapshot_t *out, size_t capacity)
{
    const size_t count = bonds.size() < capacity ? bonds.size() : capacity;
    for (size_t i = 0; i < count; ++i) out[i] = bonds[i];
    return count;
}

extern "C" esp_err_t ble_mgr_enqueue_cmd(const ble_mgr_cmd_t *cmd, TickType_t)
{
    commands.push_back(*cmd);
    return ESP_OK;
}

int main()
{
    using namespace cyberdeck_ble;
    using namespace cyberdeck_ble_background;
    state_machine model;
    scheduler scheduler;
    ble_bond_snapshot_t first{};
    std::strcpy(first.address, "AA:BB:CC:DD:EE:01"); first.addr_type = 2; first.last_connected = true;
    std::strcpy(first.name, "known"); bonds = {first};
    scheduler.restore_bonds_once(model);
    scheduler.restore_bonds_once(model); // restore is one-shot
    assert(model.has_background_target());
    assert(scheduler.address_known(model, first.address, address_type::random_resolvable));
    scheduler.maybe_reconnect(model);
    assert(scheduler.scan_active() && commands.back().kind == BLE_MGR_CMD_SCAN_START);
    scheduler.tick(k_window_interval_ms);
    ble_mgr_event_t event{}; event.token = scheduler.scan_token();
    std::strcpy(event.scan_result.address, first.address); event.scan_result.addr_type = 2;
    event.scan_result.connectable = true; event.scan_result.paired = true;
    scheduler.on_scan_result(event, model);
    assert(commands.back().kind == BLE_MGR_CMD_SCAN_CANCEL);
    ble_mgr_event_t finished{}; finished.token = scheduler.scan_token();
    finished.scan_finished.outcome = static_cast<int>(notice::cancelled);
    scheduler.on_scan_finished(finished, model);
    assert(!scheduler.scan_active());
    scheduler.preempt_for_manual();
    finished.token = scheduler.scan_token() + 1;
    scheduler.on_scan_finished(finished, model); // stale completion is ignored
    std::cout << "BLE background tests passed\n";
}
