#pragma once
#include <stdarg.h>
typedef int (*vprintf_like_t)(const char *, va_list);
vprintf_like_t esp_log_set_vprintf(vprintf_like_t);
int esp_log_write(const char *, const char *, ...);
#define ESP_LOGW(tag, ...) esp_log_write("W", tag, __VA_ARGS__)
