#pragma once

inline void recovery_test_log(const char *, const char *, ...) {}
#define ESP_LOGI(tag, format, ...) recovery_test_log(tag, format, __VA_ARGS__)
#define ESP_LOGW(tag, format, ...) recovery_test_log(tag, format, __VA_ARGS__)
#define ESP_LOGE(tag, format, ...) recovery_test_log(tag, format, __VA_ARGS__)
