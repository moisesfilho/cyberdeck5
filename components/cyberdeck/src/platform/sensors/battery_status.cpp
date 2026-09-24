#include "platform/sensors/battery_status.h"

namespace cyberdeck_battery {

std::int32_t percentage_from_capacity_mah(const std::int32_t capacity_mah)
{
    if (capacity_mah <= empty_capacity_mah) {
        return 0;
    }
    if (capacity_mah >= full_capacity_mah) {
        return 100;
    }

    const std::int64_t window = full_capacity_mah - empty_capacity_mah;
    const std::int64_t above_empty = capacity_mah - empty_capacity_mah;
    return static_cast<std::int32_t>((above_empty * 100) / window);
}

charge_class classify_current_ma(const std::int32_t current_ma)
{
    return current_ma < 0 ? charge_class::charging : charge_class::consuming;
}

snapshot classify_sample(const sample &value)
{
    if (!value.present || !value.valid) {
        return {};
    }

    return {
        true,
        percentage_from_capacity_mah(value.capacity_mah),
        classify_current_ma(value.current_ma),
    };
}

} // namespace cyberdeck_battery
