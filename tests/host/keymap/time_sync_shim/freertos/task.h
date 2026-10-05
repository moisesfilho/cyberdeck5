#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
extern "C" {
BaseType_t xTaskCreateWithCaps(TaskFunction_t, const char *, uint32_t, void *, unsigned, TaskHandle_t *, uint32_t);
void vTaskDelete(TaskHandle_t);
void vTaskDeleteWithCaps(TaskHandle_t);
}
