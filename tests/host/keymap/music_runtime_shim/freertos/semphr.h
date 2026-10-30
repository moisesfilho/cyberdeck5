#pragma once
#include "freertos/FreeRTOS.h"
#include <condition_variable>
#include <mutex>
struct music_test_semaphore { std::mutex mutex; std::condition_variable changed; bool given{}; };
using SemaphoreHandle_t = music_test_semaphore *;
SemaphoreHandle_t xSemaphoreCreateBinary();
int xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout);
int xSemaphoreGive(SemaphoreHandle_t semaphore);
void vSemaphoreDelete(SemaphoreHandle_t semaphore);
unsigned music_test_semaphore_delete_count();
