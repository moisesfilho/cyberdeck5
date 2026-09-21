#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cyberdeck_clock_time {
    int16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
} cyberdeck_clock_time_t;

#define CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN (-180)

bool cyberdeck_clock_from_utc(const cyberdeck_clock_time_t *utc,
                              int32_t offset_minutes,
                              cyberdeck_clock_time_t *out);
size_t cyberdeck_format_clock(char *buf, size_t cap,
                              const cyberdeck_clock_time_t *clock);

#ifdef __cplusplus
}
#endif
