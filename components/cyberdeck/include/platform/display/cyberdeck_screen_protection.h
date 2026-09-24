#pragma once

#include <cstdint>

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
 * Parse a command argument as a decimal number in the inclusive 0..1440
 * range.  Boundary whitespace is ignored, while all other non-digits are
 * rejected.  The output is changed only on success.
 */
timeout_parse_result parse_timeout_minutes(const char *text,
                                          std::uint16_t &out);

/*
 * Host-testable screen protection policy and state.  It has no dependency on
 * the display, storage, RTOS, or hardware.
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

    /* Returns true only for an on-to-off transition caused by inactivity. */
    bool evaluate_inactivity(std::uint32_t inactive_ms);

private:
    std::uint16_t effective_minutes_;
    std::uint16_t last_positive_minutes_;
    bool screen_on_;
};

} // namespace cyberdeck_screen_protection
