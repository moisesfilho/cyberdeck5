#pragma once

#include <cstddef>
#include <cstdint>

#include "platform/sensors/cyberdeck_battery_protection.h"

namespace cyberdeck_battery_json {

/* Upper bound for one GET /battery body.  The callers size their buffer as
 * kMaxJsonBytes + 1 (the extra byte holds the NUL terminator). */
inline constexpr std::size_t kMaxJsonBytes = 384;

struct view {
    /* False when the cached policy snapshot could not be read.  The document
     * then reports state/charge "unavailable" and null telemetry instead of
     * inventing a battery reading. */
    bool snapshot_valid{false};
    cyberdeck_battery_protection::snapshot policy{};
    /* Timestamp of the policy sample in microseconds; 0 means the policy was
     * never sampled (or the caller could not read the timestamp). */
    std::int64_t sampled_at_us{0};
    /* Current time used to derive sampled_age_ms; null when the age cannot be
     * computed (never sampled or a clock that went backwards). */
    std::int64_t now_us{0};
    /* Firmware version string; nullptr renders null. */
    const char *fw_version{nullptr};
    /* Device uptime in seconds. */
    std::int64_t uptime_s{0};
};

/* Renders the fixed-order read-only battery document into `buffer`:
 *   schema, state, charge, available, bus_voltage_mv, current_ma, percentage,
 *   protection_enabled, protection_active, charger_enabled, sampled_at_us,
 *   sampled_age_ms, fw_version, uptime_s
 *
 * Only integers/booleans/strings are emitted; a value the caller cannot know is
 * rendered null.  The function never allocates and never truncates: it returns
 * the number of bytes written (excluding the NUL terminator) or -1 when the
 * document does not fit the buffer or kMaxJsonBytes, leaving an empty string.
 */
int format_json(char *buffer, std::size_t capacity, const view &value);

} // namespace cyberdeck_battery_json
