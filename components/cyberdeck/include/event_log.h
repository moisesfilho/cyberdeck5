#pragma once

#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Starts asynchronous persistent logging to the SD card. */
esp_err_t event_log_init(void);

/* Adds a structured event without going through ESP_LOG. */
void event_log_write(char level, const char *tag, const char *message);

typedef void (*event_log_line_callback_t)(const char *line, void *context);

/* Returns the most recent records already processed by the logger task. */
size_t event_log_latest(size_t max_events, event_log_line_callback_t callback, void *context);

#ifdef __cplusplus
}
#endif
