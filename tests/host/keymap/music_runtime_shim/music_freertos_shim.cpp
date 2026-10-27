#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"

#include <chrono>
#include <atomic>
#include <cstring>
#include <thread>

extern bool music_test_fail_caps_queue_create;
unsigned music_test_last_queue_caps = 0;
bool music_test_fail_caps_queue_create = false;
bool music_test_fail_binary_semaphore_create = false;
bool music_test_fail_static_task_create = false;

namespace {
thread_local bool completion_signaled_in_task = false;
std::atomic<unsigned> self_delete_count{0};
std::atomic<unsigned> delete_without_completion_count{0};
std::atomic<unsigned> queue_reset_count{0};
}

TickType_t xTaskGetTickCount() {
    using namespace std::chrono;
    static const auto epoch = steady_clock::now();
    return static_cast<TickType_t>(duration_cast<milliseconds>(steady_clock::now() - epoch).count());
}

void vTaskDelay(TickType_t ticks) { std::this_thread::sleep_for(std::chrono::milliseconds(ticks)); }

SemaphoreHandle_t xSemaphoreCreateBinary() {
    if (music_test_fail_binary_semaphore_create) return nullptr;
    return new music_test_semaphore{};
}
int xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout) {
    if (semaphore == nullptr) return 0;
    std::unique_lock<std::mutex> lock(semaphore->mutex);
    const auto ready = [&] { return semaphore->given; };
    if (timeout == portMAX_DELAY) semaphore->changed.wait(lock, ready);
    else if (!semaphore->changed.wait_for(lock, std::chrono::milliseconds(timeout), ready)) return 0;
    semaphore->given = false;
    return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t semaphore) {
    if (semaphore == nullptr) return 0;
    completion_signaled_in_task = true;
    { std::lock_guard<std::mutex> lock(semaphore->mutex); semaphore->given = true; }
    semaphore->changed.notify_one();
    return pdTRUE;
}
void vSemaphoreDelete(SemaphoreHandle_t semaphore) { delete semaphore; }

QueueHandle_t xQueueCreate(std::size_t capacity, std::size_t item_size) {
    auto *queue = new music_test_queue{};
    queue->capacity = capacity; queue->item_size = item_size;
    return queue;
}
QueueHandle_t xQueueCreateWithCaps(UBaseType_t capacity, UBaseType_t item_size, UBaseType_t caps) {
    music_test_last_queue_caps = caps;
    if (music_test_fail_caps_queue_create) return nullptr;
    auto *queue = new music_test_queue{};
    queue->capacity = capacity;
    queue->item_size = item_size;
    if (queue != nullptr) queue->memory_caps = caps;
    return queue;
}
int xQueueSend(QueueHandle_t queue, const void *item, TickType_t) {
    if (queue == nullptr || item == nullptr) return 0;
    { std::lock_guard<std::mutex> lock(queue->mutex);
      if (queue->items.size() >= queue->capacity) return 0;
      std::vector<unsigned char> bytes(queue->item_size);
      std::memcpy(bytes.data(), item, queue->item_size);
      queue->items.push_back(std::move(bytes)); }
    queue->ready.notify_one();
    return pdTRUE;
}
int xQueueReceive(QueueHandle_t queue, void *item, TickType_t timeout) {
    if (queue == nullptr || item == nullptr) return 0;
    std::unique_lock<std::mutex> lock(queue->mutex);
    const auto ready = [&] { return !queue->items.empty(); };
    if (timeout == portMAX_DELAY) queue->ready.wait(lock, ready);
    else if (!queue->ready.wait_for(lock, std::chrono::milliseconds(timeout), ready)) return 0;
    auto bytes = std::move(queue->items.front()); queue->items.pop_front();
    std::memcpy(item, bytes.data(), queue->item_size);
    return pdTRUE;
}
void xQueueReset(QueueHandle_t queue) {
    if (queue == nullptr) return;
    queue_reset_count.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(queue->mutex); queue->items.clear();
}
unsigned music_test_queue_reset_count() { return queue_reset_count.load(std::memory_order_relaxed); }
void vQueueDelete(QueueHandle_t queue) { delete queue; }
void vQueueDeleteWithCaps(QueueHandle_t queue) { vQueueDelete(queue); }

int xTaskCreate(TaskFunction_t entry, const char *, unsigned, void *context, unsigned, TaskHandle_t *handle) {
    if (entry == nullptr) return 0;
    if (handle != nullptr) *handle = nullptr;
    std::thread([entry, context] { completion_signaled_in_task = false; entry(context); }).detach();
    return pdPASS;
}
TaskHandle_t xTaskCreateStatic(TaskFunction_t entry, const char *name, unsigned, void *context,
                               unsigned priority, StackType_t *, StaticTask_t *) {
    if (music_test_fail_static_task_create) return nullptr;
    if (entry == nullptr) return nullptr;
    TaskHandle_t handle = reinterpret_cast<void *>(1);
    std::thread([entry, context] { completion_signaled_in_task = false; entry(context); }).detach();
    (void)name;
    (void)priority;
    return handle;
}
void vTaskDelete(TaskHandle_t task) {
    if (task != nullptr) return;
    if (completion_signaled_in_task) self_delete_count.fetch_add(1, std::memory_order_relaxed);
    else delete_without_completion_count.fetch_add(1, std::memory_order_relaxed);
}
unsigned music_test_self_delete_count() { return self_delete_count.load(std::memory_order_relaxed); }
unsigned music_test_delete_without_completion_count() { return delete_without_completion_count.load(std::memory_order_relaxed); }
