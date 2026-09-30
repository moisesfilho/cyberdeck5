#pragma once
#include "FreeRTOS.h"
struct shim_mutex{}; using SemaphoreHandle_t=shim_mutex*;
inline SemaphoreHandle_t xSemaphoreCreateMutex(){return new shim_mutex;}
inline SemaphoreHandle_t xSemaphoreCreateBinary(){return new shim_mutex;}
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t,TickType_t){return pdTRUE;}
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t){return pdTRUE;}
inline void vSemaphoreDelete(SemaphoreHandle_t s){delete s;}
