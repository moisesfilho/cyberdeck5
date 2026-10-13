#pragma once
#include "FreeRTOS.h"
#include "../../event_log_shim/freertos/queue.h"
inline QueueHandle_t xQueueCreate(size_t capacity, size_t item_size) {
    return xQueueCreateStatic(capacity, item_size, nullptr, nullptr);
}
void vQueueDelete(QueueHandle_t queue);
