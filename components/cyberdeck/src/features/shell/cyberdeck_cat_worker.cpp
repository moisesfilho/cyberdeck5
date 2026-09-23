#include "features/shell/cyberdeck_cat_worker.h"

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <cstring>
#include <cstdint>
#include <fcntl.h>
#include <new>
#include <string>

namespace {
constexpr UBaseType_t k_cat_work_queue_capacity = 8;
constexpr size_t k_cat_field_bytes = 512;
constexpr size_t k_cat_worker_stack_size = 6144;
struct cat_request { char cwd[k_cat_field_bytes]; char command[k_cat_field_bytes]; };
struct cat_result { std::string output; bool accepted; std::uint64_t generation; };
QueueHandle_t s_queue = nullptr;
TaskHandle_t s_task = nullptr;
SemaphoreHandle_t s_lifecycle_mutex = nullptr;
SemaphoreHandle_t s_stopped = nullptr;
bool s_closing = false;
char s_host_root[k_cat_field_bytes] = {};
cyberdeck_cat_result_callback s_callback = nullptr;
void *s_context = nullptr;
std::uint64_t s_generation = 0;

void deliver_cat_result(void *data)
{
    cat_result *result = static_cast<cat_result *>(data);
    if (result != nullptr && s_lifecycle_mutex != nullptr &&
        xSemaphoreTake(s_lifecycle_mutex, portMAX_DELAY) == pdTRUE) {
        /* Teardown invalidates already queued LVGL callbacks.  Keep the lock
         * across the call so teardown cannot destroy the callback context
         * while LVGL is delivering it. */
        if (!s_closing && result->generation == s_generation && s_callback != nullptr)
            s_callback(result->output.data(), result->output.size(), result->accepted, s_context);
        xSemaphoreGive(s_lifecycle_mutex);
    }
    delete result;
}

void cat_worker_task(void *)
{
    cat_request request = {};
    for (;;) {
        if (xQueueReceive(s_queue, &request, pdMS_TO_TICKS(100)) != pdTRUE) {
            if (ulTaskNotifyTake(pdTRUE, 0) > 0) break;
            if (s_lifecycle_mutex != nullptr &&
                xSemaphoreTake(s_lifecycle_mutex, portMAX_DELAY) == pdTRUE) {
                const bool closing = s_closing;
                xSemaphoreGive(s_lifecycle_mutex);
                if (closing) break;
            }
            continue;
        }
        if (s_lifecycle_mutex != nullptr &&
            xSemaphoreTake(s_lifecycle_mutex, portMAX_DELAY) == pdTRUE) {
            const bool closing = s_closing;
            xSemaphoreGive(s_lifecycle_mutex);
            if (closing) break;
        }
        const cyberdeck_local_shell_result local =
            cyberdeck_local_shell_cat(s_host_root, request.cwd, request.command);
        cat_result *result = new (std::nothrow) cat_result{local.output,
            local.status == cyberdeck_local_shell_status::handled, s_generation};
        if (result == nullptr) continue;
        if (s_lifecycle_mutex == nullptr ||
            xSemaphoreTake(s_lifecycle_mutex, portMAX_DELAY) != pdTRUE) {
            delete result;
            continue;
        }
        const bool closing = s_closing;
        const lv_result_t scheduled = closing ? LV_RESULT_INVALID : lv_async_call(deliver_cat_result, result);
        xSemaphoreGive(s_lifecycle_mutex);
        if (scheduled != LV_RESULT_OK) delete result;
    }
    if (s_stopped != nullptr) xSemaphoreGive(s_stopped);
}
} // namespace

bool cyberdeck_cat_worker_start(const char *host_root,
                                cyberdeck_cat_result_callback callback,
                                void *context)
{
    if (host_root == nullptr || callback == nullptr) return false;
    if (std::strlen(host_root) >= sizeof(s_host_root)) return false;
    if (s_lifecycle_mutex == nullptr) s_lifecycle_mutex = xSemaphoreCreateMutex();
    if (s_stopped == nullptr) s_stopped = xSemaphoreCreateBinary();
    if (s_lifecycle_mutex == nullptr || s_stopped == nullptr) return false;
    if (xSemaphoreTake(s_lifecycle_mutex, portMAX_DELAY) != pdTRUE) return false;
    if (s_task != nullptr) {
        xSemaphoreGive(s_lifecycle_mutex);
        return true;
    }

    /* A binary semaphore is a generation-local stop notification.  Consume
     * the previous worker's token before publishing a new task handle, or a
     * restart could join without observing the new worker. */
    (void)xSemaphoreTake(s_stopped, 0);
    ++s_generation;
    std::strcpy(s_host_root, host_root);
    s_callback = callback;
    s_context = context;
    s_closing = false;
    s_queue = xQueueCreate(k_cat_work_queue_capacity, sizeof(cat_request));
    if (s_queue == nullptr || xTaskCreate(cat_worker_task, "cat_worker", k_cat_worker_stack_size, nullptr, 4, &s_task) != pdPASS) {
        if (s_queue != nullptr) vQueueDelete(s_queue);
        s_queue = nullptr;
        s_task = nullptr;
        s_closing = true;
        s_callback = nullptr;
        s_context = nullptr;
        std::memset(s_host_root, 0, sizeof(s_host_root));
        xSemaphoreGive(s_lifecycle_mutex);
        return false;
    }
    xSemaphoreGive(s_lifecycle_mutex);
    return true;
}

bool cyberdeck_cat_worker_enqueue(const char *cwd, const char *command)
{
    if (s_lifecycle_mutex == nullptr || cwd == nullptr || command == nullptr ||
        std::strlen(cwd) >= k_cat_field_bytes || std::strlen(command) >= k_cat_field_bytes) return false;
    cat_request request = {};
    std::strcpy(request.cwd, cwd); std::strcpy(request.command, command);
    if (xSemaphoreTake(s_lifecycle_mutex, portMAX_DELAY) != pdTRUE) return false;
    const bool accepted = !s_closing && s_queue != nullptr && xQueueSend(s_queue, &request, 0) == pdTRUE;
    xSemaphoreGive(s_lifecycle_mutex);
    return accepted;
}

void cyberdeck_cat_worker_teardown(void)
{
    if (s_lifecycle_mutex == nullptr || xSemaphoreTake(s_lifecycle_mutex, portMAX_DELAY) != pdTRUE)
        return;
    if (s_task == nullptr) {
        s_closing = true;
        s_callback = nullptr;
        s_context = nullptr;
        if (s_queue != nullptr) vQueueDelete(s_queue);
        s_queue = nullptr;
        std::memset(s_host_root, 0, sizeof(s_host_root));
        xSemaphoreGive(s_lifecycle_mutex);
        return;
    }
    s_closing = true;
    s_callback = nullptr;
    s_context = nullptr;
    TaskHandle_t task = s_task;
    xSemaphoreGive(s_lifecycle_mutex);
    (void)xTaskNotifyGive(task);
    (void)xSemaphoreTake(s_stopped, portMAX_DELAY);
    if (xSemaphoreTake(s_lifecycle_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_queue != nullptr) vQueueDelete(s_queue);
        s_queue = nullptr;
        s_task = nullptr;
        std::memset(s_host_root, 0, sizeof(s_host_root));
        xSemaphoreGive(s_lifecycle_mutex);
    }
}
#else
bool cyberdeck_cat_worker_start(const char *, cyberdeck_cat_result_callback, void *) { return false; }
bool cyberdeck_cat_worker_enqueue(const char *, const char *) { return false; }
void cyberdeck_cat_worker_teardown(void) {}
#endif
