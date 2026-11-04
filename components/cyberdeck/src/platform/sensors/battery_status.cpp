#include "platform/sensors/battery_status.h"

namespace cyberdeck_battery {

namespace {

constexpr std::int32_t compensation_current_limit_ma = 500;

std::int32_t clamp_bus_voltage_mv(const std::int32_t bus_voltage_mv)
{
    if (bus_voltage_mv <= empty_bus_voltage_mv) {
        return empty_bus_voltage_mv;
    }
    if (bus_voltage_mv >= full_bus_voltage_mv) {
        return full_bus_voltage_mv;
    }
    return bus_voltage_mv;
}

} // namespace

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

std::int32_t current_ma_from_raw(const std::int32_t raw)
{
    const std::int64_t microamp = static_cast<std::int64_t>(raw) * current_lsb_microamp;
    std::int32_t current_ma = static_cast<std::int32_t>(microamp / 1000);
    if (current_ma == 0 && microamp != 0) {
        current_ma = microamp < 0 ? -1 : 1;
    }
    return current_ma;
}

std::int32_t ocv_from_terminal_mv(const std::int32_t bus_voltage_mv,
                                 const std::int32_t current_ma)
{
    const std::int32_t abs_current = current_ma >= 0 ? current_ma : -current_ma;
    std::int32_t compensated_mv = bus_voltage_mv;
    if (abs_current <= compensation_current_limit_ma) {
        const std::int64_t compensation =
            static_cast<std::int64_t>(current_ma) * equivalent_series_mohm / 1000;
        compensated_mv = static_cast<std::int32_t>(
            static_cast<std::int64_t>(bus_voltage_mv) + compensation);
    }
    return clamp_bus_voltage_mv(compensated_mv);
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
