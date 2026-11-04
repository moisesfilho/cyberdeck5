#pragma once

#include <cstdint>

#include "esp_err.h"
#include "platform/sensors/battery_status.h"
#include "platform/sensors/cyberdeck_battery_protection.h"
#include "platform/sensors/ina226_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t battery_protection_init(void);
esp_err_t battery_protection_start(void);
bool battery_protection_started(void);
bool battery_protection_get_snapshot(cyberdeck_battery::snapshot *out_snapshot);
/* Pure policy snapshot (state, charger signal, availability, percentage) used
 * by the header view.  It is the only battery input the UI is allowed to
 * consume, so no charger/I2C knowledge leaks into the LVGL layer. */
bool battery_protection_get_policy_snapshot(cyberdeck_battery_protection::snapshot *out_snapshot);
/* Cached policy state for the explicit shell status command; never samples or observes. */
bool battery_protection_get_cached_policy_snapshot(cyberdeck_battery_protection::snapshot *out_snapshot);
/* Timestamp (microseconds) of the cached policy sample; 0 means never sampled.
 * Takes only the snapshot mutex, so it is safe to call after the getter above. */
bool battery_protection_get_cached_policy_sampled_at_us(std::int64_t *out_sampled_at_us);
bool battery_protection_set_enabled(bool enabled);
bool battery_protection_is_enabled(void);
bool battery_protection_is_active(void);
bool battery_protection_charger_enabled(void);
bool battery_protection_get_hardware_diagnostics(cyberdeck_battery_protection::hardware_diagnostics *out);

#ifdef __cplusplus
}
#endif
