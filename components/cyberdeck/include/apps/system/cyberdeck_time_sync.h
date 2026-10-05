#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cyberdeck_time_sync_start(void);
esp_err_t cyberdeck_time_sync_stop(uint32_t timeout_ms);
void cyberdeck_time_sync_boot_ready(void);

#ifdef __cplusplus
}
#endif
