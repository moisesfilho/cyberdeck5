#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TAB5_CHAR_EVENT_MAX_TEXT 16

typedef struct {
    uint8_t modifier;
    uint8_t length;
    char text[TAB5_CHAR_EVENT_MAX_TEXT + 1];
} tab5_char_event_t;

#ifdef __cplusplus
extern "C" {
#endif

bool tab5_char_event_parse(const uint8_t *raw, size_t raw_len,
                           tab5_char_event_t *out);

#ifdef __cplusplus
}
#endif
