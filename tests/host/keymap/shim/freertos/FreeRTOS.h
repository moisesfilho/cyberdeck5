#pragma once
/* Host shim for the FreeRTOS types pulled in by ble_mgr.h and queue.h. */

#include <stddef.h>
#include <stdint.h>

typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef void (*TaskFunction_t)(void *);

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
