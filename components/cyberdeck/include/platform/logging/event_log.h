#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Starts asynchronous persistent logging to the SD card. Concurrent starts
 * during initialization/closing fail with ESP_ERR_INVALID_STATE. */
esp_err_t event_log_init(void);

/* Fences new operations and waits for admitted callers. This intermediate
 * lifecycle milestone deliberately returns ESP_ERR_TIMEOUT after the callers
 * drain: worker tasks and files are not stopped/closed yet. */
esp_err_t event_log_quiesce(uint32_t timeout_ms);
/* Adds a structured event without going through ESP_LOG. */
void event_log_write(char level, const char *tag, const char *message);

/* Persists one event to events.log, including fsync, before returning. The
 * textual projection remains asynchronous and best-effort. ESP_ERR_TIMEOUT
 * means the bounded durable queue accepted the request but its ACK deadline
 * elapsed; ESP_ERR_NO_MEM means no queue slot accepted it and retry is safe. */
esp_err_t event_log_write_durable(char level, const char *tag, const char *message);

typedef void (*event_log_line_callback_t)(const char *line, void *context);

/* Returns recent in-memory records under their mutex; this is not SD I/O and
 * is not fenced by event_log_quiesce. */
size_t event_log_latest(size_t max_events, event_log_line_callback_t callback, void *context);

/* Read-only projection, exposed in bounded chunks for recovery transports. */
size_t event_log_text_size(void);
esp_err_t event_log_text_read(size_t offset, uint8_t *buffer, size_t capacity, size_t *out_read);

#ifdef __cplusplus
}
#endif
