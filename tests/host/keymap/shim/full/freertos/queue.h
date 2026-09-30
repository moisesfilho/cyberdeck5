#pragma once
#include "FreeRTOS.h"
#include <deque>
struct shim_queue { std::deque<void*> values; unsigned capacity{}; };
using QueueHandle_t=shim_queue*;
inline QueueHandle_t xQueueCreate(UBaseType_t n,std::size_t){return new shim_queue{{},n};}
inline BaseType_t xQueueSend(QueueHandle_t q,void *p,TickType_t){if(!q||q->values.size()>=q->capacity)return pdFALSE;q->values.push_back(*static_cast<void**>(p));return pdTRUE;}
inline BaseType_t xQueueReceive(QueueHandle_t q,void *p,TickType_t){if(!q||q->values.empty())return pdFALSE;*static_cast<void**>(p)=q->values.front();q->values.pop_front();return pdTRUE;}
inline void vQueueDelete(QueueHandle_t q){delete q;}
