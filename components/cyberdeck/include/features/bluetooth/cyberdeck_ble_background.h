#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "features/bluetooth/ble_mgr.h"
#include "features/bluetooth/cyberdeck_ble_state_machine.h"
#include "features/bluetooth/cyberdeck_ble_types.h"

namespace cyberdeck_ble_background {

/* Periodic background discovery window: the observer reconnects only known
 * bonds after spontaneous loss.  Manual disconnect blocks the cycle until an
 * explicit Enter re-arms it.  Bounded: at most one scan token and
 * k_max_reconnect_attempts per announcement cycle. */
inline constexpr std::uint32_t k_window_interval_ms = 10000;
inline constexpr std::uint8_t k_restore_retry_limit = 3;
inline constexpr std::uint64_t k_token_base = (std::uint64_t{1} << 63);

class scheduler {
public:
    scheduler() = default;
    scheduler(const scheduler &) = delete;
    scheduler &operator=(const scheduler &) = delete;

    /* Restore persisted bonds exactly once at boot: copy identity snapshots
     * from the adapter, seed the model target list, and arm the background
     * cycle only when a last-connected bond exists.  Never scans, connects,
     * or touches the visible screen here. */
    void restore_bonds_once(cyberdeck_ble::state_machine &model);

    /* Consume one LVGL tick from the discovery window, clamped at zero. */
    void tick(std::uint32_t elapsed_ms);

    bool scan_active() const { return scan_active_; }
    std::uint64_t scan_token() const { return scan_token_; }
    std::uint64_t abandoned_token() const { return abandoned_token_; }

    /* Reconnect only known bonds: the armed target or the restored list. */
    bool address_known(const cyberdeck_ble::state_machine &model, const std::string &address,
                       cyberdeck_ble::address_type type) const;

    /* A fresh announcement from the armed target opens a new budget window
     * after the previous cycle was exhausted.  Unknown peers and
     * non-connectable reports never re-arm; a manual disconnect block is
     * never cleared here. */
    void note_advertisement(const std::string &address, cyberdeck_ble::address_type type,
                            bool connectable, std::uint64_t generation,
                            cyberdeck_ble::state_machine &model);

    /* Give every restored bond a bounded opportunity while the
     * last-connected bond remains the first target after boot. */
    void advance_target(cyberdeck_ble::state_machine &model);

    /* Spontaneous loss starts the background scan; the scan itself never
     * emits CONNECT.  Each matching advertisement consumes one budget slot
     * and schedules a single `reconnect` via the model.  Manual disconnect
     * keeps the cycle blocked until explicit Enter. */
    void maybe_reconnect(cyberdeck_ble::state_machine &model);

    void on_scan_result(const ble_mgr_event_t &event, cyberdeck_ble::state_machine &model);
    void on_scan_finished(const ble_mgr_event_t &event, cyberdeck_ble::state_machine &model);

    /* A manual scan/pair/connect preempts an active background discovery.
     * The abandoned token stays reserved so its late callbacks are dropped;
     * the terminal SCAN_FINISHED still closes the scan slot. */
    void preempt_for_manual();

private:
    bool restored_ = false;
    std::uint8_t restore_attempts_ = 0;
    bool scan_active_ = false;
    std::uint64_t scan_token_ = 0;
    std::uint64_t abandoned_token_ = 0;
    std::uint32_t wait_ms_ = 0;
    std::string last_adv_;
    cyberdeck_ble::address_type last_adv_type_ = cyberdeck_ble::address_type::public_address;
    std::uint64_t last_gen_ = 0;
    bool reconnect_pending_ = false;
    cyberdeck_ble::device pending_device_;
};

} // namespace cyberdeck_ble_background
