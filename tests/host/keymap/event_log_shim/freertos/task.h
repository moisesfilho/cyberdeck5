#pragma once
#include "FreeRTOS.h"
typedef struct host_task *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
BaseType_t xTaskCreate(TaskFunction_t, const char *, uint32_t, void *, unsigned, TaskHandle_t *);
void vTaskDelete(TaskHandle_t);
TickType_t xTaskGetTickCount(void);
void xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t);
void vTaskDelay(TickType_t);
