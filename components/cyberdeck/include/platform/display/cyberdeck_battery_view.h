#pragma once

#include <cstdint>

#include "platform/sensors/cyberdeck_battery_protection.h"

namespace cyberdeck_battery_view {

/*
 * Semantic glyph of the single header power indicator.  The level is never
 * encoded in the glyph: the numeric percentage rendered next to it owns the
 * level, so the same pictogram is used for every percentage of the same
 * power source.
 */
enum class power_glyph {
    none,     /* nothing observable about the power source */
    charging, /* the charger reports charging and a pack is present */
    battery,  /* a pack is present, discharging or on external power */
    external, /* no pack in the bay, the bus is powered externally */
};

inline constexpr std::int32_t min_percentage = 0;
inline constexpr std::int32_t max_percentage = 100;

/*
 * Everything the header needs to know about the power source.  The inputs are
 * exactly the observable evidence: the resolved state, the raw charger signal
 * and the measurement availability/percentage.  No other input can change the
 * presentation.
 */
struct input {
    cyberdeck_battery_protection::battery_state state{
        cyberdeck_battery_protection::battery_state::unknown};
    cyberdeck_battery_protection::charge_signal signal{
        cyberdeck_battery_protection::charge_signal::unknown};
    bool available{false};
    std::int32_t percentage{0};
};

struct presentation {
    bool visible{false};
    bool show_percentage{false};
    std::int32_t percentage{0};
    power_glyph glyph{power_glyph::none};
};

/* Clamps the raw reading into the renderable 0..100 range. */
std::int32_t clamp_percentage(std::int32_t percentage);

/* Projects a pure policy snapshot into the view inputs. */
input from_snapshot(const cyberdeck_battery_protection::snapshot &value);

/*
 * Total mapping from state/signal/availability/percentage to what the header
 * renders.  Absence and an unresolved state are decided before the charger
 * signal, so a stuck-low or unreadable CHG_STAT can never fabricate a pack:
 *
 *   unavailable / unknown      -> hidden, no glyph, no percentage
 *   absent                     -> visible external glyph, no percentage
 *   charging (state or signal) -> visible charge glyph + percentage
 *   battery / external         -> visible battery glyph + percentage
 *
 * There is no dependency on ESP-IDF, FreeRTOS, LVGL, I2C, NVS or the BSP.
 */
presentation resolve(const input &value);

} // namespace cyberdeck_battery_view
