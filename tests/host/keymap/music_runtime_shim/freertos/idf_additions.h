#pragma once

#include "freertos/queue.h"

#define MUSIC_TEST_QUEUE_CAPS 0x04U
QueueHandle_t xQueueCreateWithCaps(UBaseType_t capacity, UBaseType_t item_size, UBaseType_t caps);
void vQueueDeleteWithCaps(QueueHandle_t queue);
extern unsigned music_test_last_queue_caps;
extern bool music_test_fail_caps_queue_create;
extern bool music_test_fail_binary_semaphore_create;
extern bool music_test_fail_static_task_create;
