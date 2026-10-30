#pragma once
#include "freertos/FreeRTOS.h"
#include <cstddef>
#include <string>
#include <vector>
using TaskHandle_t = void *;
using TaskFunction_t = void (*)(void *);
using StackType_t = std::uint32_t;
struct StaticTask_t { alignas(std::max_align_t) unsigned char storage[512]; };
int xTaskCreate(TaskFunction_t entry, const char *name, unsigned stack_bytes, void *context,
                unsigned priority, TaskHandle_t *handle);
TaskHandle_t xTaskCreateStatic(TaskFunction_t entry, const char *name, unsigned stack_bytes, void *context,
                               unsigned priority, StackType_t *stack, StaticTask_t *control);
/* Host threads have no FreeRTOS stack accounting; the shim reports a fixed
 * value so code that only logs the real peak stays compilable and silent. */
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task);
void vTaskDelete(TaskHandle_t task);
void vTaskSuspend(TaskHandle_t task);
unsigned music_test_self_delete_count();
unsigned music_test_delete_without_completion_count();
/* Counts tasks whose entry point returned without deleting itself.  The IDF
 * build enables CONFIG_FREERTOS_TASK_FUNCTION_WRAPPER, so the same condition
 * aborts the device; the shim only records it so tests can fail on it. */
unsigned music_test_entry_returned_without_delete_count();
unsigned music_test_park_count();
/* Ordered lifecycle events ("park", "semaphore_take", "task_delete_with_caps",
 * "queue_delete", "semaphore_delete", ...) so tests can prove no handle is
 * released before the worker signalled completion. */
std::vector<std::string> music_test_teardown_events();
void music_test_clear_teardown_events();
