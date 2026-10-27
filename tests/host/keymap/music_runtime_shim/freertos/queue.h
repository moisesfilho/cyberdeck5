#pragma once
#include "freertos/FreeRTOS.h"
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>
struct music_test_queue {
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::vector<unsigned char>> items;
    std::size_t capacity{};
    std::size_t item_size{};
    unsigned memory_caps{};
};
using QueueHandle_t = music_test_queue *;
QueueHandle_t xQueueCreate(std::size_t capacity, std::size_t item_size);
QueueHandle_t xQueueCreateWithCaps(UBaseType_t capacity, UBaseType_t item_size, UBaseType_t caps);
void vQueueDeleteWithCaps(QueueHandle_t queue);
int xQueueSend(QueueHandle_t queue, const void *item, TickType_t timeout);
int xQueueReceive(QueueHandle_t queue, void *item, TickType_t timeout);
void xQueueReset(QueueHandle_t queue);
unsigned music_test_queue_reset_count();
void vQueueDelete(QueueHandle_t queue);
void vQueueDeleteWithCaps(QueueHandle_t queue);
void vQueueDeleteWithCaps(QueueHandle_t queue);
