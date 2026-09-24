#include "platform/display/cyberdeck_screen_protection.h"

namespace cyberdeck_screen_protection {
namespace {

bool is_boundary_whitespace(char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
           value == '\v' || value == '\f';
}

bool is_digit(char value)
{
    return value >= '0' && value <= '9';
}

} // namespace

timeout_parse_result parse_timeout_minutes(const char *text,
                                          std::uint16_t &out)
{
    if (text == nullptr) {
        return timeout_parse_result::missing;
    }

    const char *begin = text;
    while (*begin != '\0' && is_boundary_whitespace(*begin)) {
        ++begin;
    }
    if (*begin == '\0') {
        return timeout_parse_result::missing;
    }

    const char *end = begin;
    while (*end != '\0') {
        ++end;
    }
    while (end > begin && is_boundary_whitespace(end[-1])) {
        --end;
    }
    if (end == begin) {
        return timeout_parse_result::missing;
    }

    std::uint32_t value = 0;
    for (const char *cursor = begin; cursor != end; ++cursor) {
        if (!is_digit(*cursor)) {
            return timeout_parse_result::invalid;
        }

        const std::uint32_t digit = static_cast<std::uint32_t>(*cursor - '0');
        if (value > max_timeout_minutes / 10 ||
            (value == max_timeout_minutes / 10 &&
             digit > max_timeout_minutes % 10)) {
            return timeout_parse_result::out_of_range;
        }
        value = value * 10 + digit;
    }

    out = static_cast<std::uint16_t>(value);
    return timeout_parse_result::ok;
}

state::state()
    : effective_minutes_(default_timeout_minutes),
      last_positive_minutes_(default_timeout_minutes),
      screen_on_(true)
{
}

bool state::set_timeout_minutes(std::uint16_t minutes)
{
    if (minutes > max_timeout_minutes) {
        return false;
    }

    effective_minutes_ = minutes;
    if (minutes != 0) {
        last_positive_minutes_ = minutes;
    }
    return true;
}

persisted_timeout state::persistence_view() const
{
    return persisted_timeout{effective_minutes_, last_positive_minutes_};
}

bool state::restore(const persisted_timeout &value)
{
    if (value.last_positive_minutes == 0 ||
        value.last_positive_minutes > max_timeout_minutes ||
        value.effective_minutes > max_timeout_minutes ||
        (value.effective_minutes != 0 &&
         value.effective_minutes != value.last_positive_minutes)) {
        return false;
    }

    effective_minutes_ = value.effective_minutes;
    last_positive_minutes_ = value.last_positive_minutes;
    return true;
}

bool state::timeout_enabled() const
{
    return effective_minutes_ != 0;
}

std::uint16_t state::effective_timeout_minutes() const
{
    return effective_minutes_;
}

std::uint16_t state::last_positive_timeout_minutes() const
{
    return last_positive_minutes_;
}

std::uint32_t state::timeout_ms() const
{
    return static_cast<std::uint32_t>(effective_minutes_) * milliseconds_per_minute;
}

void state::turn_on()
{
    screen_on_ = true;
}

void state::turn_off()
{
    screen_on_ = false;
}

bool state::screen_on() const
{
    return screen_on_;
}

bool state::evaluate_inactivity(std::uint32_t inactive_ms)
{
    if (!screen_on_ || !timeout_enabled()) {
        return false;
    }
    if (inactive_ms < timeout_ms()) {
        return false;
    }

    screen_on_ = false;
    return true;
}

} // namespace cyberdeck_screen_protection
