#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*tab5_keyboard_event_cb_t)(const char *text, size_t length, uint8_t modifier, uint32_t special_key);

void tab5_keyboard_set_callback(tab5_keyboard_event_cb_t callback);
esp_err_t tab5_keyboard_init(void);
esp_err_t tab5_keyboard_deinit(void);
bool tab5_keyboard_is_connected(void);

#ifdef __cplusplus
}
#endif
