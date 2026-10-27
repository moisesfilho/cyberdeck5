#pragma once
#include "freertos/FreeRTOS.h"
#include <cstddef>
using TaskHandle_t = void *;
using TaskFunction_t = void (*)(void *);
using StackType_t = std::uint32_t;
struct StaticTask_t { alignas(std::max_align_t) unsigned char storage[512]; };
int xTaskCreate(TaskFunction_t entry, const char *name, unsigned stack_bytes, void *context,
                unsigned priority, TaskHandle_t *handle);
TaskHandle_t xTaskCreateStatic(TaskFunction_t entry, const char *name, unsigned stack_bytes, void *context,
                               unsigned priority, StackType_t *stack, StaticTask_t *control);
void vTaskDelete(TaskHandle_t task);
unsigned music_test_self_delete_count();
unsigned music_test_delete_without_completion_count();
