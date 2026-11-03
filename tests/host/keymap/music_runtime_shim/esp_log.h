#pragma once
#include <cstdio>
#define ESP_LOGI(tag, format, ...) do { if (false) std::fprintf(stderr, format, ##__VA_ARGS__); } while (false)
#define ESP_LOGE(tag, format, ...) do { if (false) std::fprintf(stderr, format, ##__VA_ARGS__); } while (false)
#define ESP_LOGW(tag, format, ...) ((void)0)
