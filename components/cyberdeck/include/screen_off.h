#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t screen_off_init(lv_display_t *display, int active_brightness);

#ifdef __cplusplus
}
#endif
