#pragma once
#include "FreeRTOS.h"
typedef struct { void *opaque[8]; } StaticSemaphore_t;
typedef struct host_semaphore *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *);
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *);
BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t);
BaseType_t xSemaphoreGive(SemaphoreHandle_t);
