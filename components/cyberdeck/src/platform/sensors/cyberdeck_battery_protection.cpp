#include "platform/sensors/cyberdeck_battery_protection.h"
#include "platform/sensors/battery_status.h"

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

    battery_state last_safe_state_{battery_state::unknown};
    charge_signal last_safe_charge_{charge_signal::unknown};
    bool last_safe_available_{false};
    std::int32_t last_safe_percentage_{0};
    std::int32_t last_safe_bus_voltage_mv_{0};
    std::int32_t last_safe_current_ma_{0};

    std::uint8_t consecutive_votes_{0};
    battery_state voted_state_{battery_state::unknown};

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

    bool evaluate_protection_conditions()
    {
        if (!protection_enabled_) {
            return false;
        }
        if (current_state_ != battery_state::charging) {
            return false;
        }
        if (percentage_ < protection_enter_percentage) {
            return false;
        }
        if (bus_voltage_mv_ < protection_enter_voltage_mv) {
            return false;
        }
        return true;
    }

    void update_protection()
    {
        const bool should_protect = evaluate_protection_conditions();
        if (should_protect && !protection_active_) {
            protection_active_ = true;
            charger_enabled_ = false;
        } else if (!should_protect && protection_active_) {
            if (percentage_ <= protection_exit_percentage ||
                current_state_ != battery_state::charging) {
                protection_active_ = false;
                charger_enabled_ = true;
            }
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
    impl.percentage_ = value.percentage;
    impl.bus_voltage_mv_ = value.bus_voltage_mv;
    impl.current_ma_ = value.current_ma;
    impl.current_charge_ = decode_chg_stat(value.chg_valid, value.raw_chg_stat_low);

    impl.current_state_ = impl.classify_state(value, impl.current_charge_);
    impl.available_ = true;

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
    if (!enabled) {
        impl.protection_active_ = false;
        impl.charger_enabled_ = true;
    } else {
        impl.update_protection();
    }
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
