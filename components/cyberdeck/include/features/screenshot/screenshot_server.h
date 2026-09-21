#pragma once

#include "esp_err.h"
#include "features/wifi/wifi_mgr.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t screenshot_server_init(void);
void screenshot_server_wifi_state(const wifi_status_t *status, bool enabled, void *ctx);

#ifdef __cplusplus
}
#endif
