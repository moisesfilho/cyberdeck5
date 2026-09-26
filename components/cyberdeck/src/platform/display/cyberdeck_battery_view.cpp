#include "platform/display/cyberdeck_battery_view.h"

namespace cyberdeck_battery_view {

std::int32_t clamp_percentage(std::int32_t percentage)
{
    if (percentage < min_percentage) {
        return min_percentage;
    }
    if (percentage > max_percentage) {
        return max_percentage;
    }
    return percentage;
}

input from_snapshot(const cyberdeck_battery_protection::snapshot &value)
{
    input out{};
    out.state = value.state;
    out.signal = value.charge;
    out.available = value.available;
    out.percentage = value.percentage;
    return out;
}

presentation resolve(const input &value)
{
    presentation out{};

    /* An unreadable measurement or an unresolved state means there is no
     * observable energy source: render nothing instead of a fabricated zero. */
    if (!value.available || value.state == cyberdeck_battery_protection::battery_state::unknown) {
        return out;
    }

    /* Absence/presence precedence: a voted absence is the only evidence that
     * there is no pack, and it outranks the charger signal. */
    if (value.state == cyberdeck_battery_protection::battery_state::absent) {
        out.visible = true;
        out.glyph = power_glyph::external;
        return out;
    }

    out.visible = true;
    out.percentage = clamp_percentage(value.percentage);
    out.show_percentage = true;

    const bool charging =
        value.state == cyberdeck_battery_protection::battery_state::charging ||
        value.signal == cyberdeck_battery_protection::charge_signal::charging;
    out.glyph = charging ? power_glyph::charging : power_glyph::battery;
    return out;
}

} // namespace cyberdeck_battery_view
