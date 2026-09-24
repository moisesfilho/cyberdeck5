#pragma once

#include "esp_err.h"
#include "lvgl.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t screen_off_init(lv_display_t *display, int active_brightness);
void screen_off_turn_on(void);
void screen_off_turn_off(void);
esp_err_t screen_off_set_timeout_minutes(uint16_t minutes);

#ifdef __cplusplus
}
#endif
