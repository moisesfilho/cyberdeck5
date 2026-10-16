#pragma once
#include "FreeRTOS.h"
BaseType_t xTaskCreateWithCaps(void (*)(void *), const char *, uint32_t, void *, unsigned, void **, unsigned);
void vTaskDelete(void *);
void vTaskDelay(TickType_t);
