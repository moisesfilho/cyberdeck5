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

/*
 * The host contract fixes the capacity_mah member name.  The Tab5 INA226
 * adapter supplies the approved battery voltage window in mV on this input;
 * both units use the same saturated 6000..8400 numeric scale.
 */
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

std::int32_t percentage_from_capacity_mah(std::int32_t capacity_mah);
charge_class classify_current_ma(std::int32_t current_ma);
snapshot classify_sample(const sample &value);

} // namespace cyberdeck_battery
