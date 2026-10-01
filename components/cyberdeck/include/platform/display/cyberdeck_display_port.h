#pragma once

#include "apps/screenshot/screenshot_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cyberdeck_display_capture(void *context, screenshot_frame_t *out);
void cyberdeck_display_release(void *context, screenshot_frame_t *frame);

#ifdef __cplusplus
}
#endif
