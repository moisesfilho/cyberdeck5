/*
 * Host-only contract for the pure screen-protection policy and state seam.
 *
 * This header is intentionally test-owned.  The production implementation must
 * expose the same ABI from
 * components/cyberdeck/include/platform/display/cyberdeck_screen_protection.h
 * and link from cyberdeck_screen_protection.cpp without ESP-IDF, LVGL, NVS,
 * FreeRTOS, BSP, or hardware.
 */
#pragma once

#include <cstdint>

#if __has_include("platform/display/cyberdeck_screen_protection.h")
#include "platform/display/cyberdeck_screen_protection.h"
#else
namespace cyberdeck_screen_protection {

inline constexpr std::uint16_t default_timeout_minutes = 2;
inline constexpr std::uint16_t max_timeout_minutes = 1440;
inline constexpr std::uint32_t milliseconds_per_minute = 60000;

enum class timeout_parse_result {
    ok,
    missing,
    invalid,
    out_of_range,
};

struct persisted_timeout {
    std::uint16_t effective_minutes;
    std::uint16_t last_positive_minutes;
};

/*
 * Strict decimal parser for the command argument.  Only 0..1440 is accepted;
 * whitespace at the boundaries is harmless.  On every failure path, out is
 * left unchanged.
 */
timeout_parse_result parse_timeout_minutes(const char *text,
                                          std::uint16_t &out);

/*
 * Pure state shared by the command/UI adapter and the LVGL timer adapter.
 * Timeout zero disables automatic screen-off but retains the last positive
 * value.  Persistence must retain both fields so a disabled state survives a
 * reboot without losing the value needed to re-enable it.
 */
class state {
public:
    state();

    bool set_timeout_minutes(std::uint16_t minutes);
    persisted_timeout persistence_view() const;
    bool restore(const persisted_timeout &value);

    bool timeout_enabled() const;
    std::uint16_t effective_timeout_minutes() const;
    std::uint16_t last_positive_timeout_minutes() const;
    std::uint32_t timeout_ms() const;

    void turn_on();
    void turn_off();
    bool screen_on() const;

    /* Returns true only for an on -> off transition caused by inactivity. */
    bool evaluate_inactivity(std::uint32_t inactive_ms);
};

} // namespace cyberdeck_screen_protection
#endif
