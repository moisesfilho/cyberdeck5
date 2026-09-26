/*
 * Host contract for the pure battery power-indicator view.
 *
 * This is a test-owned ABI.  The firmware exposes the same declarations from
 * components/cyberdeck/include/platform/display/cyberdeck_battery_view.h and
 * implements them in src/platform/display/cyberdeck_battery_view.cpp with no
 * ESP-IDF, FreeRTOS, LVGL, I2C, NVS, BSP or hardware dependency.
 *
 * The contract exists so the host test is written against the intended seam
 * (REQ-BAT-UI-003 / AC-BAT-UI-004) instead of a duplicated copy of the
 * rendering rule.  The mapping under test is:
 *
 *   unavailable / unknown      -> hidden, no glyph, no percentage
 *   absent                     -> visible, external glyph, no percentage
 *   charging (state or signal) -> visible, charge glyph, percentage
 *   battery / external         -> visible, battery glyph, percentage
 *
 * The level is never encoded in the glyph: the numeric percentage rendered
 * beside it is the only source of level (REQ-BAT-UI-005 / AC-BAT-UI-006).
 */
#pragma once

#include <cstdint>

#include "cyberdeck_battery_protection.h"

#if defined(__has_include)
#  if __has_include("platform/display/cyberdeck_battery_view.h")
#    include "platform/display/cyberdeck_battery_view.h"
#    define CYBERDECK_BATTERY_VIEW_HAS_PRODUCTION_HEADER 1
#  endif
#endif

#ifndef CYBERDECK_BATTERY_VIEW_HAS_PRODUCTION_HEADER

namespace cyberdeck_battery_view {

enum class power_glyph {
    none,
    charging,
    battery,
    external,
};

inline constexpr std::int32_t min_percentage = 0;
inline constexpr std::int32_t max_percentage = 100;

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

std::int32_t clamp_percentage(std::int32_t percentage);

input from_snapshot(const cyberdeck_battery_protection::snapshot &value);

presentation resolve(const input &value);

} // namespace cyberdeck_battery_view

#endif // CYBERDECK_BATTERY_VIEW_HAS_PRODUCTION_HEADER
