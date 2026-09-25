#include "platform/sensors/cyberdeck_battery_protection.h"

#include <cstdint>
#include <memory>

namespace cyberdeck_battery_protection {

charge_signal decode_chg_stat(bool valid, bool raw_low)
{
    if (!valid) {
        return charge_signal::unknown;
    }
    return raw_low ? charge_signal::charging : charge_signal::not_charging;
}

struct state::impl {
    battery_state current_state_{battery_state::unknown};
    bool available_{false};
    std::int32_t percentage_{0};
    std::int32_t bus_voltage_mv_{0};
    std::int32_t current_ma_{0};

    bool protection_enabled_{default_protection_enabled};
    bool protection_active_{false};
    bool charger_enabled_{true};

    battery_state last_safe_state_{battery_state::unknown};
    bool last_safe_available_{false};
    std::int32_t last_safe_percentage_{0};
    std::int32_t last_safe_bus_voltage_mv_{0};
    std::int32_t last_safe_current_ma_{0};

    std::uint8_t consecutive_votes_{0};
    battery_state voted_state_{battery_state::unknown};

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

    battery_state classify_state(const observation &obs)
    {
        const charge_signal chg = decode_chg_stat(obs.chg_valid, obs.raw_chg_stat_low);

        if (chg == charge_signal::charging) {
            consecutive_votes_ = 0;
            voted_state_ = battery_state::unknown;
            return battery_state::charging;
        }

        const std::int32_t abs_current = obs.current_ma >= 0 ? obs.current_ma : -obs.current_ma;
        if (abs_current > current_uncertainty_ma) {
            consecutive_votes_ = 0;
            voted_state_ = battery_state::unknown;
            return obs.current_ma > 0 ? battery_state::battery : battery_state::charging;
        }

        if (obs.bus_voltage_mv >= absent_voltage_mv) {
            if (voted_state_ == battery_state::absent) {
                if (++consecutive_votes_ >= state_vote_count) {
                    consecutive_votes_ = 0;
                    voted_state_ = battery_state::unknown;
                    return battery_state::absent;
                }
            } else {
                consecutive_votes_ = 1;
                voted_state_ = battery_state::absent;
            }
            return current_state_;
        }

        if (obs.bus_voltage_mv >= external_voltage_mv) {
            if (voted_state_ == battery_state::external) {
                if (++consecutive_votes_ >= state_vote_count) {
                    consecutive_votes_ = 0;
                    voted_state_ = battery_state::unknown;
                    return battery_state::external;
                }
            } else {
                consecutive_votes_ = 1;
                voted_state_ = battery_state::external;
            }
            return current_state_;
        }

        consecutive_votes_ = 0;
        voted_state_ = battery_state::unknown;
        return battery_state::battery;
    }

    void update_last_safe()
    {
        last_safe_state_ = current_state_;
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

    if (!value.ina_valid) {
        return snapshot_t{
            impl.current_state_,
            impl.available_,
            impl.percentage_,
            impl.bus_voltage_mv_,
            impl.current_ma_,
            impl.protection_enabled_,
            impl.protection_active_,
            impl.charger_enabled_,
        };
    }

    if (!value.chg_valid) {
        return snapshot_t{
            impl.current_state_,
            impl.available_,
            impl.percentage_,
            impl.bus_voltage_mv_,
            impl.current_ma_,
            impl.protection_enabled_,
            impl.protection_active_,
            impl.charger_enabled_,
        };
    }

    impl.percentage_ = value.percentage;
    impl.bus_voltage_mv_ = value.bus_voltage_mv;
    impl.current_ma_ = value.current_ma;

    const battery_state new_state = impl.classify_state(value);
    impl.current_state_ = new_state;
    impl.available_ = true;

    impl.update_protection();

    impl.update_last_safe();

    return snapshot_t{
        impl.current_state_,
        impl.available_,
        impl.percentage_,
        impl.bus_voltage_mv_,
        impl.current_ma_,
        impl.protection_enabled_,
        impl.protection_active_,
        impl.charger_enabled_,
    };
}

state::snapshot_t state::snapshot() const
{
    const auto &impl = *pimpl_;
    return snapshot_t{
        impl.current_state_,
        impl.available_,
        impl.percentage_,
        impl.bus_voltage_mv_,
        impl.current_ma_,
        impl.protection_enabled_,
        impl.protection_active_,
        impl.charger_enabled_,
    };
}

state::snapshot_t state::last_safe_snapshot() const
{
    const auto &impl = *pimpl_;
    return snapshot_t{
        impl.last_safe_state_,
        impl.last_safe_available_,
        impl.last_safe_percentage_,
        impl.last_safe_bus_voltage_mv_,
        impl.last_safe_current_ma_,
        impl.protection_enabled_,
        impl.protection_active_,
        impl.charger_enabled_,
    };
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