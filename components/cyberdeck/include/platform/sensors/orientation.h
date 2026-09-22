#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void orientation_reset(void);
void orientation_set_current(lv_display_rotation_t rotation);
lv_display_rotation_t orientation_from_accel(float ax, float ay, float az, lv_display_rotation_t fallback);
lv_display_rotation_t orientation_update(float ax, float ay, float az);

#ifdef __cplusplus
}
#endif
