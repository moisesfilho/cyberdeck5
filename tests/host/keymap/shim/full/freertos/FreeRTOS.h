#pragma once
#include <cstdint>
using TickType_t=std::uint32_t; using BaseType_t=int; using UBaseType_t=unsigned;
using TaskFunction_t=void(*)(void*);
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(x) (x)
