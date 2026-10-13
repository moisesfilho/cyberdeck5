#pragma once
#include "FreeRTOS.h"
#include "../../event_log_shim/freertos/semphr.h"
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return xSemaphoreCreateMutexStatic(nullptr); }
inline SemaphoreHandle_t xSemaphoreCreateBinary() { return xSemaphoreCreateBinaryStatic(nullptr); }
