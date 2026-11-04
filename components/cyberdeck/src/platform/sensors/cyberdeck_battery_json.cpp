#include "platform/sensors/cyberdeck_battery_json.h"

#include <cstdio>
#include <cstring>

namespace cyberdeck_battery_json {

namespace {

constexpr std::size_t kNumberBytes = 32;

/* Content must stay inside the documented body limit and leave room for the
 * NUL terminator inside the caller buffer. */
bool fits(std::size_t used, std::size_t extra, std::size_t capacity)
{
    return used + extra <= kMaxJsonBytes && used + extra < capacity;
}

bool append_raw(char *buffer, std::size_t capacity, std::size_t &used, const char *text)
{
    const std::size_t length = std::strlen(text);
    if (!fits(used, length, capacity)) return false;
    std::memcpy(buffer + used, text, length);
    used += length;
    buffer[used] = '\0';
    return true;
}

bool append_number(char *buffer, std::size_t capacity, std::size_t &used, std::int64_t value)
{
    char digits[kNumberBytes] = {};
    const int written = std::snprintf(digits, sizeof(digits), "%lld", static_cast<long long>(value));
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(digits)) return false;
    return append_raw(buffer, capacity, used, digits);
}

bool append_bool(char *buffer, std::size_t capacity, std::size_t &used, bool value)
{
    return append_raw(buffer, capacity, used, value ? "true" : "false");
}

/* A key always carries its leading separator, so the document is composed by
 * appending one complete `,"name":` token at a time. */
bool append_nullable_bool(char *buffer, std::size_t capacity, std::size_t &used,
                          const char *key, bool known, bool value)
{
    if (!append_raw(buffer, capacity, used, key)) return false;
    return known ? append_bool(buffer, capacity, used, value)
                 : append_raw(buffer, capacity, used, "null");
}

bool append_nullable_number(char *buffer, std::size_t capacity, std::size_t &used,
                            const char *key, bool known, std::int64_t value)
{
    if (!append_raw(buffer, capacity, used, key)) return false;
    return known ? append_number(buffer, capacity, used, value)
                 : append_raw(buffer, capacity, used, "null");
}

/* Escapes a version string in place of a copy: quotes/backslashes are escaped,
 * control bytes become \u00XX and UTF-8 bytes pass through unchanged.  A string
 * that does not fit fails closed instead of being emitted truncated. */
bool append_json_string(char *buffer, std::size_t capacity, std::size_t &used, const char *text)
{
    if (text == nullptr) return append_raw(buffer, capacity, used, "null");
    if (!append_raw(buffer, capacity, used, "\"")) return false;
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        const unsigned char byte = static_cast<unsigned char>(*cursor);
        char piece[7] = {};
        if (byte == '"') {
            std::memcpy(piece, "\\\"", 2);
        } else if (byte == '\\') {
            std::memcpy(piece, "\\\\", 2);
        } else if (byte < 0x20) {
            std::snprintf(piece, sizeof(piece), "\\u%04x", static_cast<unsigned int>(byte));
        } else {
            piece[0] = *cursor;
        }
        if (!append_raw(buffer, capacity, used, piece)) return false;
    }
    return append_raw(buffer, capacity, used, "\"");
}

} // namespace

int format_json(char *buffer, std::size_t capacity, const view &value)
{
    if (buffer == nullptr || capacity == 0) return -1;
    buffer[0] = '\0';

    const bool available = value.snapshot_valid && value.policy.available;
    const bool sampled = value.sampled_at_us > 0;
    const bool aged = sampled && value.now_us >= value.sampled_at_us;
    const std::int64_t age_ms = aged ? (value.now_us - value.sampled_at_us) / 1000 : 0;

    std::size_t used = 0;
    const bool ok =
        append_raw(buffer, capacity, used, "{\"schema\":1,\"state\":") &&
        append_json_string(buffer, capacity, used,
                           value.snapshot_valid
                               ? cyberdeck_battery_protection::state_name(value.policy.state)
                               : "unavailable") &&
        append_raw(buffer, capacity, used, ",\"charge\":") &&
        append_json_string(buffer, capacity, used,
                           value.snapshot_valid
                               ? cyberdeck_battery_protection::charge_signal_name(value.policy.charge)
                               : "unavailable") &&
        append_raw(buffer, capacity, used, ",\"available\":") &&
        append_bool(buffer, capacity, used, available) &&
        append_nullable_number(buffer, capacity, used, ",\"bus_voltage_mv\":", available,
                               value.policy.bus_voltage_mv) &&
        append_nullable_number(buffer, capacity, used, ",\"current_ma\":", available,
                               value.policy.current_ma) &&
        append_nullable_number(buffer, capacity, used, ",\"percentage\":", available,
                               value.policy.percentage) &&
        append_nullable_bool(buffer, capacity, used, ",\"protection_enabled\":",
                             value.snapshot_valid, value.policy.protection_enabled) &&
        append_nullable_bool(buffer, capacity, used, ",\"protection_active\":",
                             value.snapshot_valid, value.policy.protection_active) &&
        append_nullable_bool(buffer, capacity, used, ",\"charger_enabled\":",
                             value.snapshot_valid, value.policy.charger_enabled) &&
        append_nullable_number(buffer, capacity, used, ",\"sampled_at_us\":", sampled,
                               value.sampled_at_us) &&
        append_nullable_number(buffer, capacity, used, ",\"sampled_age_ms\":", aged, age_ms) &&
        append_raw(buffer, capacity, used, ",\"fw_version\":") &&
        append_json_string(buffer, capacity, used, value.fw_version) &&
        append_nullable_number(buffer, capacity, used, ",\"uptime_s\":", true, value.uptime_s) &&
        append_raw(buffer, capacity, used, "}");

    if (!ok) {
        buffer[0] = '\0';
        return -1;
    }
    return static_cast<int>(used);
}

} // namespace cyberdeck_battery_json
