#pragma once

#include "esp_err.h"
#include "apps/wifi/wifi_mgr.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int width;
    int height;
    size_t stride;
    uint8_t *data;
} screenshot_frame_t;

typedef esp_err_t (*screenshot_capture_fn)(void *context, screenshot_frame_t *out);
typedef void (*screenshot_release_fn)(void *context, screenshot_frame_t *frame);

esp_err_t screenshot_server_init(void);
esp_err_t screenshot_server_set_display_port(screenshot_capture_fn capture,
                                              screenshot_release_fn release,
                                              void *context);
void screenshot_server_wifi_state(const wifi_status_t *status, bool enabled, void *ctx);

#ifdef __cplusplus
}
#endif
