/*
 * Host-only contract for the pure battery percentage/classification seam.
 *
 * This is deliberately a test contract, not a production implementation.  The
 * implementation must provide the same declarations from the public production
 * header and link without ESP-IDF, FreeRTOS, I2C, LVGL, or hardware.
 *
 * Capacity is expressed in mAh.  The approved battery window is 6000..8400
 * mAh (0..100%), with saturation on both sides.  A positive current means the
 * device is consuming power; a negative current means it is charging.  Zero is
 * deliberately classified as consuming so a non-negative/zero-current sensor
 * never displays the charging state.  Presence and read validity are separate
 * inputs: either one being false makes the sample unavailable.
 */
#pragma once

#include <cstdint>

namespace cyberdeck_battery {

inline constexpr std::int32_t empty_capacity_mah = 6000;
inline constexpr std::int32_t full_capacity_mah = 8400;

enum class charge_class {
    unavailable,
    consuming,
    charging,
};

struct sample {
    bool present{false};
    bool valid{false};
    std::int32_t capacity_mah{0};
    std::int32_t current_ma{0};
};

struct snapshot {
    bool available{false};
    std::int32_t percentage{0};
    charge_class charge{charge_class::unavailable};
};

/* Pure, total percentage calculation with saturation at the approved limits. */
std::int32_t percentage_from_capacity_mah(std::int32_t capacity_mah);

/* Pure current-sign classification. Zero is consuming, not charging. */
charge_class classify_current_ma(std::int32_t current_ma);

/* Pure composition of availability, percentage, and current classification. */
snapshot classify_sample(const sample &value);

} // namespace cyberdeck_battery
