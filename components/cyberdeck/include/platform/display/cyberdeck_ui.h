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
esp_err_t cyberdeck_ui_term_dump(char *buffer, size_t capacity, size_t *out_bytes, int *out_truncated);

#ifdef __cplusplus
}
#endif
