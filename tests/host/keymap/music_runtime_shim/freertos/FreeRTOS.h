#pragma once
#include <cstdint>
using TickType_t = std::uint32_t;
using UBaseType_t = unsigned;
inline constexpr TickType_t portTICK_PERIOD_MS = 1;
inline constexpr TickType_t portMAX_DELAY = 0xffffffffU;
inline constexpr int pdTRUE = 1;
inline constexpr int pdPASS = 1;
inline TickType_t pdMS_TO_TICKS(std::uint32_t ms) { return ms; }
TickType_t xTaskGetTickCount();
void vTaskDelay(TickType_t ticks);
