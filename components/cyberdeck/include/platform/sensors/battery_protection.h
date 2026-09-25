#pragma once

#include "esp_err.h"
#include "platform/sensors/battery_status.h"
#include "platform/sensors/ina226_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t battery_protection_init(void);
esp_err_t battery_protection_start(void);
bool battery_protection_started(void);
bool battery_protection_get_snapshot(cyberdeck_battery::snapshot *out_snapshot);
bool battery_protection_set_enabled(bool enabled);
bool battery_protection_is_enabled(void);
bool battery_protection_is_active(void);
bool battery_protection_charger_enabled(void);

#ifdef __cplusplus
}
#endif