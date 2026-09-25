#pragma once

#include "esp_err.h"

#include "platform/sensors/battery_status.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ina226_reader_init(void);
esp_err_t ina226_reader_start(void);
bool ina226_reader_started(void);
bool ina226_reader_get_snapshot(cyberdeck_battery::snapshot *out_snapshot);
bool ina226_reader_get_raw_sample(cyberdeck_battery::sample *out_sample);

#ifdef __cplusplus
}
#endif
