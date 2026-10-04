#include "platform/input/cyberdeck_keyboard_dispatch.h"

#include <cstdlib>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

namespace cyberdeck_keyboard_dispatch {
namespace {

constexpr UBaseType_t k_queue_capacity = 8;

struct event {
    std::size_t length;
    std::uint8_t modifier;
    std::uint32_t special_key;
    char text[];
};

QueueHandle_t s_queue = nullptr;
SemaphoreHandle_t s_mutex = nullptr;
callback s_handler = nullptr;
void *s_context = nullptr;

void discard_from_queue(event *rejected)
{
    event *pending[k_queue_capacity]{};
    UBaseType_t pending_count = 0;
    while (pending_count < k_queue_capacity &&
           xQueueReceive(s_queue, &pending[pending_count], 0) == pdTRUE) {
        ++pending_count;
    }
    for (UBaseType_t i = 0; i < pending_count; ++i) {
        if (pending[i] == rejected) continue;
        (void)xQueueSend(s_queue, &pending[i], 0);
    }
}

void process_async(void *)
{
    event *pending = nullptr;
    if (s_queue == nullptr || s_mutex == nullptr) return;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return;
    const BaseType_t received = xQueueReceive(s_queue, &pending, 0);
    xSemaphoreGive(s_mutex);
    if (received != pdTRUE || pending == nullptr) return;

    callback handler = s_handler;
    void *context = s_context;
    if (handler != nullptr) {
        handler(pending->text, pending->length, pending->modifier,
                pending->special_key, context);
    }
    free(pending);
}

} // namespace

bool dispatcher::start(callback handler, void *context)
{
    stop();
    if (handler == nullptr) return false;
    s_queue = xQueueCreate(k_queue_capacity, sizeof(event *));
    s_mutex = xSemaphoreCreateMutex();
    if (s_queue == nullptr || s_mutex == nullptr) {
        stop();
        return false;
    }
    s_handler = handler;
    s_context = context;
    handler_ = handler;
    context_ = context;
    return true;
}

void dispatcher::stop()
{
    if (s_mutex != nullptr && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_queue != nullptr) {
            event *pending = nullptr;
            while (xQueueReceive(s_queue, &pending, 0) == pdTRUE) free(pending);
        }
        s_handler = nullptr;
        s_context = nullptr;
        xSemaphoreGive(s_mutex);
    }
    if (s_mutex != nullptr) {
        vSemaphoreDelete(s_mutex);
        s_mutex = nullptr;
    }
    if (s_queue != nullptr) {
        vQueueDelete(s_queue);
        s_queue = nullptr;
    }
    handler_ = nullptr;
    context_ = nullptr;
}

void dispatcher::submit(const char *text, std::size_t length, std::uint8_t modifier,
                        std::uint32_t special_key)
{
    if ((text == nullptr || length == 0) && special_key == 0) return;
    event *pending = static_cast<event *>(std::malloc(sizeof(event) + length + 1));
    if (pending == nullptr) return;
    pending->length = text != nullptr ? length : 0;
    pending->modifier = modifier;
    pending->special_key = special_key;
    if (pending->length != 0) std::memcpy(pending->text, text, pending->length);
    pending->text[pending->length] = '\0';

    if (s_mutex == nullptr || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        free(pending);
        return;
    }
    if (s_queue == nullptr || xQueueSend(s_queue, &pending, 0) != pdTRUE) {
        xSemaphoreGive(s_mutex);
        free(pending);
        return;
    }
    const lv_result_t result = lv_async_call(process_async, nullptr);
    if (result != LV_RESULT_OK) {
        discard_from_queue(pending);
        free(pending);
    } else {
        // lv_async_call queues the callback but does not wake the port task.
        // Wake it explicitly so the configured timer period remains unchanged.
        (void)lvgl_port_task_wake(LVGL_PORT_EVENT_USER, nullptr);
    }
    xSemaphoreGive(s_mutex);
}

} // namespace cyberdeck_keyboard_dispatch
