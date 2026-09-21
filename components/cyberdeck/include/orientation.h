#pragma once

#include "lvgl.h"

void orientation_reset(void);
void orientation_set_current(lv_display_rotation_t rotation);
lv_display_rotation_t orientation_update(float ax, float ay, float az);
