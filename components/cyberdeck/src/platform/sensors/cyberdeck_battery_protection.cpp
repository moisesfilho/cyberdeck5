#include "platform/sensors/cyberdeck_battery_protection.h"
#include "platform/sensors/battery_status.h"
#include "platform/sensors/cyberdeck_battery_soc.h"

#include <cstdint>
#include <cstdio>
#include <memory>

namespace cyberdeck_battery_protection {

charge_signal decode_chg_stat(bool valid, bool raw_low)
{
    if (!valid) {
        return charge_signal::unknown;
    }
    return raw_low ? charge_signal::charging : charge_signal::not_charging;
}

const char *state_name(battery_state value)
{
    switch (value) {
    case battery_state::unknown: return "unknown";
    case battery_state::battery: return "battery";
    case battery_state::external: return "external";
    case battery_state::charging: return "charging";
    case battery_state::absent: return "absent";
    }
    return "unknown";
}

const char *charge_signal_name(charge_signal value)
{
    switch (value) {
    case charge_signal::unknown: return "unknown";
    case charge_signal::not_charging: return "not_charging";
    case charge_signal::charging: return "charging";
    }
    return "unknown";
}

int format_status_line(char *buffer, std::size_t capacity, const snapshot &value)
{
    if (buffer == nullptr || capacity == 0) {
        return 0;
    }
    return std::snprintf(
        buffer, capacity,
        "battery: state=%s charge=%s available=%s voltage_mv=%ld current_ma=%ld percentage=%ld protection=%s charger=%s\n",
        state_name(value.state), charge_signal_name(value.charge),
        value.available ? "true" : "false",
        static_cast<long>(value.bus_voltage_mv), static_cast<long>(value.current_ma),
        static_cast<long>(value.percentage),
        value.protection_active ? "active" : "inactive",
        value.charger_enabled ? "on" : "off");
}

struct state::impl {
    battery_state current_state_{battery_state::unknown};
    charge_signal current_charge_{charge_signal::unknown};
    bool available_{false};
    std::int32_t percentage_{0};
    std::int32_t bus_voltage_mv_{0};
    std::int32_t current_ma_{0};

    bool protection_enabled_{default_protection_enabled};
    bool protection_active_{false};
    bool charger_enabled_{true};

    cyberdeck_battery_soc::state soc_{};
    bool soc_seeded_{false};

    battery_state last_safe_state_{battery_state::unknown};
    charge_signal last_safe_charge_{charge_signal::unknown};
    bool last_safe_available_{false};
    std::int32_t last_safe_percentage_{0};
    std::int32_t last_safe_bus_voltage_mv_{0};
    std::int32_t last_safe_current_ma_{0};

    std::uint8_t consecutive_votes_{0};
    battery_state voted_state_{battery_state::unknown};
    std::uint8_t no_source_votes_{0};

    state::snapshot_t view() const
    {
        return state::snapshot_t{
            current_state_,
            current_charge_,
            available_,
            percentage_,
            bus_voltage_mv_,
            current_ma_,
            protection_enabled_,
            protection_active_,
            charger_enabled_,
        };
    }

    state::snapshot_t last_safe_view() const
    {
        return state::snapshot_t{
            last_safe_state_,
            last_safe_charge_,
            last_safe_available_,
            last_safe_percentage_,
            last_safe_bus_voltage_mv_,
            last_safe_current_ma_,
            protection_enabled_,
            protection_active_,
            charger_enabled_,
        };
    }

    bool evaluate_protection_conditions() const
    {
        if (!protection_enabled_) {
            return false;
        }
        if (current_state_ != battery_state::charging) {
            return false;
        }
        /* Entry uses the terminal bus voltage: it is the measurement the
         * charger actually reacts to, and the hardware reached 8200 mV there. */
        return bus_voltage_mv_ >= protection_enter_voltage_mv;
    }

    /* Consecutive samples whose confirmed classification has no external
     * source (`battery` or `absent`), used by the latch.  Only the confirmed
     * state counts: a 2S pack at ~92 % rests near 8.0 V, so a voltage-only
     * "external" test would never see a pack that is genuinely running on the
     * battery, and the counter would never reach five.  `battery` is immediate
     * (current above the uncertainty band) and `absent` keeps its five votes. */
    void update_no_source_votes()
    {
        const bool no_source = current_state_ == battery_state::battery ||
                               current_state_ == battery_state::absent;
        if (!no_source) {
            no_source_votes_ = 0;
            return;
        }
        if (no_source_votes_ < no_source_vote_count) {
            ++no_source_votes_;
        }
    }

    /* Latch release conditions, in the approved order: the estimated state of
     * charge alone releases on the first sample, and five consecutive samples
     * without an external source release a pack that is no longer charging.
     * The state-of-charge release is skipped while the pack still classifies as
     * charging: releasing there would re-arm the charger and cut again on the
     * next 8200 mV sample, recreating the cycle this policy removes. */
    bool latch_release_requested() const
    {
        if (soc_seeded_ &&
            percentage_ <= protection_exit_percentage &&
            current_state_ != battery_state::charging) {
            return true;
        }
        return no_source_votes_ >= no_source_vote_count;
    }

    void update_protection()
    {
        if (!protection_enabled_) {
            protection_active_ = false;
            charger_enabled_ = true;
            return;
        }
        if (!protection_active_) {
            if (evaluate_protection_conditions()) {
                protection_active_ = true;
                charger_enabled_ = false;
            }
            return;
        }
        if (latch_release_requested()) {
            protection_active_ = false;
            charger_enabled_ = true;
        }
    }

    /* Runs one vote for a voltage-derived state and returns true only when the
     * approved number of consecutive votes confirms it. */
    bool confirm_voted_state(battery_state candidate)
    {
        if (voted_state_ == candidate) {
            ++consecutive_votes_;
        } else {
            voted_state_ = candidate;
            consecutive_votes_ = 1;
        }
        if (consecutive_votes_ < state_vote_count) {
            return false;
        }
        consecutive_votes_ = 0;
        voted_state_ = battery_state::unknown;
        return true;
    }

    void reset_votes()
    {
        consecutive_votes_ = 0;
        voted_state_ = battery_state::unknown;
    }

    battery_state classify_state(const observation &obs, charge_signal chg)
    {
        /* Absence/presence precedence: a bus voltage outside the approved
         * battery window means there is no usable pack in the bay.  The upper
         * branch covers external power; the lower branch covers the floating
         * bus observed with the pack disconnected (currently about 1.9 V).
         * Decide this before the charger signal so a stuck-low CHG_STAT cannot
         * fabricate a charging battery. */
        if (obs.bus_voltage_mv >= absent_voltage_mv ||
            obs.bus_voltage_mv < cyberdeck_battery::empty_bus_voltage_mv) {
            if (confirm_voted_state(battery_state::absent)) {
                return battery_state::absent;
            }
            return current_state_;
        }

        if (chg == charge_signal::charging) {
            reset_votes();
            return battery_state::charging;
        }
        const std::int32_t abs_current = obs.current_ma >= 0 ? obs.current_ma : -obs.current_ma;
        if (abs_current > current_uncertainty_ma) {
            reset_votes();
            return obs.current_ma > 0 ? battery_state::battery : battery_state::charging;
        }

        if (obs.bus_voltage_mv >= external_voltage_mv) {
            if (confirm_voted_state(battery_state::external)) {
                return battery_state::external;
            }
            return current_state_;
        }

        reset_votes();
        return battery_state::battery;
    }

    void update_last_safe()
    {
        last_safe_state_ = current_state_;
        last_safe_charge_ = current_charge_;
        last_safe_available_ = available_;
        last_safe_percentage_ = percentage_;
        last_safe_bus_voltage_mv_ = bus_voltage_mv_;
        last_safe_current_ma_ = current_ma_;
    }
};

state::state()
    : pimpl_(std::make_unique<impl>())
{
}

state::~state() = default;

state::snapshot_t state::observe(const observation &value)
{
    auto &impl = *pimpl_;

    /* A failed INA226 read is the only case that keeps the previous snapshot:
     * nothing can be measured, so the last safe values and the protection
     * decision are preserved instead of being recomputed from zeros. */
    if (!value.ina_valid) {
        /* Keep the last safe policy decision for charger protection, but mark
         * the current measurement unavailable so presentation consumers do not
         * keep rendering a stale battery after the sensor disappears. */
        impl.available_ = false;
        return impl.view();
    }

    /* A valid measurement is published even when CHG_STAT could not be read.
     * The missing pin is an unknown signal, never a not-charging answer, so a
     * transient expander error can no longer freeze the snapshot and hide the
     * indicator. */
    impl.bus_voltage_mv_ = value.bus_voltage_mv;
    impl.current_ma_ = value.current_ma;
    impl.current_charge_ = decode_chg_stat(value.chg_valid, value.raw_chg_stat_low);

    /* Classification comes first: the state-of-charge estimate is only
     * re-anchored for a pack that is present and not charging, so the pure
     * policy has to decide the state before feeding the estimator. */
    impl.current_state_ = impl.classify_state(value, impl.current_charge_);

    const cyberdeck_battery_soc::reading soc = impl.soc_.observe({
        true,
        impl.current_state_ != battery_state::absent,
        impl.current_state_ == battery_state::charging,
        value.bus_voltage_mv,
        value.current_ma,
        value.elapsed_ms,
    });
    impl.soc_seeded_ = soc.seeded;
    impl.percentage_ = soc.seeded ? soc.percentage : 0;
    impl.available_ = true;

    impl.update_no_source_votes();
    impl.update_protection();

    impl.update_last_safe();

    return impl.view();
}

state::snapshot_t state::snapshot() const
{
    return pimpl_->view();
}

state::snapshot_t state::last_safe_snapshot() const
{
    return pimpl_->last_safe_view();
}

bool state::set_protection_enabled(bool enabled)
{
    auto &impl = *pimpl_;
    impl.protection_enabled_ = enabled;
    /* Disabling releases the latch and re-enables the charger immediately;
     * re-enabling only re-engages from a fresh charging sample at or above the
     * entry voltage. */
    impl.update_protection();
    return true;
}

bool state::restore_protection_enabled(bool enabled)
{
    return set_protection_enabled(enabled);
}

bool state::protection_enabled() const
{
    return pimpl_->protection_enabled_;
}

bool state::protection_active() const
{
    return pimpl_->protection_active_;
}

bool state::charger_enabled() const
{
    return pimpl_->charger_enabled_;
}

cyberdeck_battery_protection::persisted_option state::persistence_view() const
{
    return {pimpl_->protection_enabled_};
}

} // namespace cyberdeck_battery_protection
