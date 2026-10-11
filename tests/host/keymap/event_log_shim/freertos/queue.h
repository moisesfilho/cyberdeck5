#pragma once
#include "FreeRTOS.h"
#include <stddef.h>
typedef struct { void *opaque[8]; } StaticQueue_t;
typedef struct host_queue *QueueHandle_t;
QueueHandle_t xQueueCreateStatic(size_t, size_t, uint8_t *, StaticQueue_t *);
BaseType_t xQueueSend(QueueHandle_t, const void *, TickType_t);
BaseType_t xQueueReceive(QueueHandle_t, void *, TickType_t);
