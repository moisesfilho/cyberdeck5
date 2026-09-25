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

std::int32_t percentage_from_bus_voltage_mv(std::int32_t bus_voltage_mv);
charge_class classify_current_ma(std::int32_t current_ma);
snapshot classify_sample(const sample &value);

} // namespace cyberdeck_battery
