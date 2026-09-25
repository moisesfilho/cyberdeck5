/*
 * Host-only contract for the pure battery voltage/state seam.
 *
 * This is deliberately a test contract, not a production implementation.  The
 * implementation must provide the same declarations from the public production
 * header and link without ESP-IDF, FreeRTOS, I2C, LVGL, or hardware.
 *
 * REQ-BAT-001: the percentage input is the measured bus voltage in mV, not a
 * fabricated capacity in mAh.  The approved validation window is 6000..8230
 * mV (0..100%), with saturation on both sides.
 * REQ-BAT-002: presence and read validity are separate.  A missing battery is
 * absent, a present battery with a failed/invalid read is unavailable, and a
 * valid present sample is charging, discharging, or neutral.  A zero or
 * indeterminate current is neutral and must never be classified as charging
 * or absent.  The absent state is reserved for an explicit present=false
 * signal; an invalid read is not an absence signal.
 * REQ-BAT-003: the UI consumes the percentage and semantic state only.
 * REQ-BAT-004: reader startup is non-fatal and has no charger-control seam.
 * REQ-BAT-005: the INA226 task/mutex/one-second snapshot seam is preserved.
 */
#pragma once

#include <cstdint>

namespace cyberdeck_battery {

inline constexpr std::int32_t empty_bus_voltage_mv = 6000;
inline constexpr std::int32_t full_bus_voltage_mv = 8230;

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

/* Pure, total percentage calculation from measured bus voltage. */
std::int32_t percentage_from_bus_voltage_mv(std::int32_t bus_voltage_mv);

/* Positive current is discharging; negative current is charging; zero or
 * otherwise indeterminate current is neutral.  This classifier never invents
 * an absent state. */
charge_class classify_current_ma(std::int32_t current_ma);

/* Pure composition of presence, read validity, voltage, and current state. */
snapshot classify_sample(const sample &value);

} // namespace cyberdeck_battery
