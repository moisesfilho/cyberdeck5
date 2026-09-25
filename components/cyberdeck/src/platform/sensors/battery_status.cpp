#include "platform/sensors/battery_status.h"

namespace cyberdeck_battery {

std::int32_t percentage_from_bus_voltage_mv(const std::int32_t bus_voltage_mv)
{
    if (bus_voltage_mv <= empty_bus_voltage_mv) {
        return 0;
    }
    if (bus_voltage_mv >= full_bus_voltage_mv) {
        return 100;
    }

    const std::int64_t window = full_bus_voltage_mv - empty_bus_voltage_mv;
    const std::int64_t above_empty = bus_voltage_mv - empty_bus_voltage_mv;
    return static_cast<std::int32_t>((above_empty * 100) / window);
}

charge_class classify_current_ma(const std::int32_t current_ma)
{
    if (current_ma == 0) {
        return charge_class::neutral;
    }
    return current_ma < 0 ? charge_class::charging : charge_class::discharging;
}

snapshot classify_sample(const sample &value)
{
    if (!value.present) {
        return {false, 0, charge_class::absent};
    }
    if (!value.valid) {
        return {false, 0, charge_class::unavailable};
    }

    return {
        true,
        percentage_from_bus_voltage_mv(value.bus_voltage_mv),
        classify_current_ma(value.current_ma),
    };
}

} // namespace cyberdeck_battery
