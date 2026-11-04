#pragma once

#include <cstdint>

namespace cyberdeck_battery {

/* Approved validation window, 6000..8100 mV.  The ceiling is the resting
 * terminal voltage measured after the charger cut at 8200 mV (recorded anchors:
 * 8106 and 8116 mV), not a fully charged pack, so 100 % means "maximum charge of
 * this policy".  The window is 2100 mV wide: one point is 21 mV and
 * percentage_from_bus_voltage_mv truncates. */
inline constexpr std::int32_t empty_bus_voltage_mv = 6000;
inline constexpr std::int32_t full_bus_voltage_mv = 8100;

/* Equivalent series resistance of the pack plus wiring, measured on the
 * device (~330 mOhm).  It converts a terminal measurement into the open-circuit
 * voltage the state-of-charge estimate is anchored to. */
inline constexpr std::int32_t equivalent_series_mohm = 330;

/* Approved INA226 shunt-current LSB: 300 uA per raw count. */
inline constexpr std::int32_t current_lsb_microamp = 300;

enum class charge_class {
    absent,
    unavailable,
    discharging,
    charging,
    neutral,
};

struct sample {
    bool present{false};
    bool valid{false};
    std::int32_t bus_voltage_mv{0};
    std::int32_t current_ma{0};
};

struct snapshot {
    bool available{false};
    std::int32_t percentage{0};
    charge_class charge{charge_class::absent};
};

std::int32_t percentage_from_bus_voltage_mv(std::int32_t bus_voltage_mv);
charge_class classify_current_ma(std::int32_t current_ma);
snapshot classify_sample(const sample &value);

/* Signed INA226 shunt-current register converted with the approved 300 uA/LSB
 * scale: 3/10 mA per raw count, truncated toward zero and raised to +/-1 mA
 * when a non-zero reading would otherwise truncate to zero. */
std::int32_t current_ma_from_raw(std::int32_t raw);

/* Terminal bus voltage compensated back to the open-circuit voltage with the
 * measured equivalent series resistance, saturated to the approved battery
 * window.  The compensation is only trusted up to +/-500 mA; above that the
 * terminal measurement is used unchanged. */
std::int32_t ocv_from_terminal_mv(std::int32_t bus_voltage_mv, std::int32_t current_ma);

} // namespace cyberdeck_battery
