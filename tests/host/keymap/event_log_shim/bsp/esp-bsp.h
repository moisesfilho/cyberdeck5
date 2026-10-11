#pragma once
#include "esp_err.h"
void *bsp_sdcard_get_handle(void);
esp_err_t bsp_sdcard_mount(void);
