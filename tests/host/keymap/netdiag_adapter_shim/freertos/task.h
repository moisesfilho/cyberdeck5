#pragma once
#include "FreeRTOS.h"
#include "freertos/FreeRTOS.h"
BaseType_t xTaskCreate(TaskFunction_t, const char *, unsigned, void *, UBaseType_t, void *);
void vTaskDelete(void *);
