/*
 * RED host contract for the pure battery power/protection policy.
 *
 * This is a test-owned ABI.  The firmware must eventually expose the same
 * declarations from
 * components/cyberdeck/include/platform/sensors/cyberdeck_battery_protection.h
 * and implement them in cyberdeck_battery_protection.cpp without ESP-IDF,
 * FreeRTOS, I2C, NVS, LVGL, BSP, or hardware dependencies.
 *
 * The policy deliberately separates sensing from control.  INA226 remains the
 * sensor-only owner of bus voltage/current; this seam receives decoded CHG_STAT
 * and decides the externally visible state and the desired CHG_EN level.
 */
#pragma once

#include <cstdint>

#if defined(__has_include)
#  if __has_include("platform/sensors/cyberdeck_battery_protection.h")
#    include "platform/sensors/cyberdeck_battery_protection.h"
#    define CYBERDECK_BATTERY_PROTECTION_HAS_PRODUCTION_HEADER 1
#  endif
#endif

#ifndef CYBERDECK_BATTERY_PROTECTION_HAS_PRODUCTION_HEADER

namespace cyberdeck_battery_protection {

inline constexpr std::int32_t current_uncertainty_ma = 15;
inline constexpr std::int32_t external_voltage_mv = 7900;
inline constexpr std::int32_t absent_voltage_mv = 8330;
inline constexpr std::uint8_t state_vote_count = 5;
inline constexpr std::int32_t protection_enter_percentage = 90;
inline constexpr std::int32_t protection_enter_voltage_mv = 8200;
inline constexpr std::int32_t protection_exit_percentage = 85;
inline constexpr bool default_protection_enabled = true;

enum class battery_state {
    unknown,
    battery,
    external,
    charging,
    absent,
};

enum class charge_signal {
    unknown,
    not_charging,
    charging,
};

/* raw_chg_stat_low is the electrical level read from expander-B pin 6.  The
 * CHG_STAT signal is active-low: a valid low means charging, a valid high means
 * not charging.  chg_valid/ina_valid are independent so a failed read can be
 * tested without fabricating a safe state. */
struct observation {
    bool ina_valid{false};
    bool chg_valid{false};
    bool raw_chg_stat_low{false};
    std::int32_t bus_voltage_mv{0};
    std::int32_t current_ma{0};
    std::int32_t percentage{0};
};

struct snapshot {
    battery_state state{battery_state::unknown};
    bool available{false};
    std::int32_t percentage{0};
    std::int32_t bus_voltage_mv{0};
    std::int32_t current_ma{0};
    bool protection_enabled{true};
    bool protection_active{false};
    bool charger_enabled{true};
};

struct persisted_option {
    bool protection_enabled{true};
};

charge_signal decode_chg_stat(bool valid, bool raw_low);

/*
 * Pure state/policy implementation.
 *
 * Classification precedence is intentional:
 *   - an invalid INA or CHG observation is unknown and cannot mutate the
 *     last safe output;
 *   - a valid active-low CHG_STAT or a current below -15 mA is charging;
 *   - current above +15 mA is battery discharge;
 *   - near-zero current at >= 7900 mV is external only after five consecutive
 *     votes, and >= 8330 mV is absent only after five consecutive votes;
 *   - a valid lower-voltage sample is battery.
 *
 * Protection is disabled only by the complete charging && pct >= 90 &&
 * voltage >= 8200 conjunction.  Once active it remains latched through the
 * hysteresis band and releases at pct <= 85 (or when charging stops).  Setting
 * the option off releases immediately and makes the desired charger level high
 * again; a later valid sample is required before the option can disable it.
 */
class state {
public:
    state();

    snapshot observe(const observation &value);
    snapshot snapshot() const;
    snapshot last_safe_snapshot() const;

    bool set_protection_enabled(bool enabled);
    bool restore_protection_enabled(bool enabled);
    bool protection_enabled() const;
    bool protection_active() const;
    bool charger_enabled() const;
    persisted_option persistence_view() const;

private:
    /* Private layout is intentionally opaque to the host contract. */
    struct impl;
    impl *pimpl_{nullptr};
};

} // namespace cyberdeck_battery_protection

#endif // CYBERDECK_BATTERY_PROTECTION_HAS_PRODUCTION_HEADER
