#pragma once

#include "freertos/queue.h"
#include "freertos/task.h"

#define MUSIC_TEST_QUEUE_CAPS 0x04U
QueueHandle_t xQueueCreateWithCaps(UBaseType_t capacity, UBaseType_t item_size, UBaseType_t caps);
void vQueueDeleteWithCaps(QueueHandle_t queue);
BaseType_t xTaskCreateWithCaps(TaskFunction_t entry, const char *name, unsigned stack_bytes, void *context,
                               UBaseType_t priority, TaskHandle_t *handle, UBaseType_t caps);
void vTaskDeleteWithCaps(TaskHandle_t task);
extern unsigned music_test_last_queue_caps;
extern unsigned music_test_last_task_caps;
extern unsigned music_test_last_task_stack_bytes;
extern unsigned music_test_delete_with_caps_count;
/* A delete of a handle that is not a live managed task, i.e. a double free or
 * a stale handle. */
extern unsigned music_test_invalid_task_delete_count;
extern bool music_test_fail_caps_queue_create;
extern bool music_test_fail_binary_semaphore_create;
extern bool music_test_fail_static_task_create;
extern bool music_test_fail_task_create_with_caps;
