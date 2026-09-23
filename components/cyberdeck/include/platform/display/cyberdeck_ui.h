#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cyberdeck_ui_init(void);
void cyberdeck_ui_deinit(void);
void cyberdeck_keyboard_input(const char *text, size_t length, uint8_t modifier, uint32_t special_key);

#ifdef __cplusplus
}
#endif
