#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"

#include <chrono>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

unsigned music_test_last_queue_caps = 0;
unsigned music_test_last_task_caps = 0;
unsigned music_test_last_task_stack_bytes = 0;
unsigned music_test_delete_with_caps_count = 0;
unsigned music_test_invalid_task_delete_count = 0;
bool music_test_fail_caps_queue_create = false;
bool music_test_fail_binary_semaphore_create = false;
bool music_test_fail_static_task_create = false;
bool music_test_fail_task_create_with_caps = false;

struct music_test_task_state;

namespace {
thread_local bool completion_signaled_in_task = false;
thread_local music_test_task_state *current_task_state = nullptr;
std::atomic<unsigned> self_delete_count{0};
std::atomic<unsigned> delete_without_completion_count{0};
std::atomic<unsigned> queue_reset_count{0};
std::atomic<unsigned> queue_delete_count{0};
std::atomic<unsigned> semaphore_delete_count{0};
std::atomic<unsigned> entry_returned_without_delete_count{0};
std::atomic<unsigned> park_count{0};

/* Raised by vTaskSuspend/vTaskDelay to unwind a managed task whose deletion was
 * requested; it never escapes the host thread wrapper. */
struct task_terminated {};

/* Registry and event log of the host-backed tasks.  They cannot be ordinary
 * namespace-scope statics: spawn_task detaches its std::thread, so a worker may
 * still be inside retire_task()/record() while the process exits and destroys
 * static objects, which would crash on an already destroyed mutex/container.
 * The state is therefore allocated once and intentionally never destroyed; it
 * stays reachable through the pointer, so LSan does not report it as a leak. */
struct shared_state {
    std::mutex registry_mutex;
    std::unordered_set<music_test_task_state *> live_tasks;
    std::mutex events_mutex;
    std::vector<std::string> teardown_events;
};

shared_state &shared() {
    static shared_state *state = new shared_state();
    return *state;
}

void record(const char *event) {
    shared_state &s = shared();
    std::lock_guard<std::mutex> lock(s.events_mutex);
    s.teardown_events.emplace_back(event);
}
} // namespace

/* State of a host-backed FreeRTOS task.  The shim cannot kill a std::thread,
 * so deletion is cooperative: vTaskSuspend parks the thread, vTaskDelete*
 * releases it, and the wrapper exits without ever returning into the entry
 * point's caller. */
struct music_test_task_state {
    std::mutex mutex;
    std::condition_variable changed;
    bool parked = false;
    bool deleted = false;
    bool self_deleted = false;
};

namespace {
void retire_task(music_test_task_state *state) {
    shared_state &s = shared();
    std::lock_guard<std::mutex> lock(s.registry_mutex);
    s.live_tasks.erase(state);
}

void run_managed(TaskFunction_t entry, void *context, music_test_task_state *state) {
    current_task_state = state;
    try {
        entry(context);
    } catch (const task_terminated &) {
        retire_task(state);
        return;
    }
    bool self_deleted = false;
    bool parked = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        self_deleted = state->self_deleted;
        parked = state->parked;
    }
    if (!self_deleted && !parked) {
        entry_returned_without_delete_count.fetch_add(1, std::memory_order_relaxed);
        record("entry_returned_without_delete");
    }
    retire_task(state);
}

music_test_task_state *spawn_task(TaskFunction_t entry, void *context) {
    if (entry == nullptr) return nullptr;
    auto *state = new music_test_task_state{};
    {
        shared_state &s = shared();
        std::lock_guard<std::mutex> lock(s.registry_mutex);
        s.live_tasks.insert(state);
    }
    std::thread([entry, context, state] { run_managed(entry, context, state); }).detach();
    return state;
}

void delete_task_handle(TaskHandle_t handle, bool with_caps) {
    auto *state = static_cast<music_test_task_state *>(handle);
    {
        shared_state &s = shared();
        std::lock_guard<std::mutex> lock(s.registry_mutex);
        if (s.live_tasks.count(state) == 0) {
            ++music_test_invalid_task_delete_count;
            record("invalid_task_delete");
            return;
        }
    }
    if (with_caps) {
        ++music_test_delete_with_caps_count;
        record("task_delete_with_caps");
    } else {
        record("task_delete");
    }
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->deleted = true;
    }
    state->changed.notify_all();
}
} // namespace

TickType_t xTaskGetTickCount() {
    using namespace std::chrono;
    static const auto epoch = steady_clock::now();
    return static_cast<TickType_t>(duration_cast<milliseconds>(steady_clock::now() - epoch).count());
}

void vTaskDelay(TickType_t ticks) {
    if (ticks == portMAX_DELAY && current_task_state != nullptr) {
        music_test_task_state *state = current_task_state;
        std::unique_lock<std::mutex> lock(state->mutex);
        state->changed.wait(lock, [state] { return state->deleted; });
        throw task_terminated{};
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(ticks));
}

void vTaskSuspend(TaskHandle_t task) {
    if (task != nullptr) return;
    music_test_task_state *state = current_task_state;
    if (state == nullptr) return;
    park_count.fetch_add(1, std::memory_order_relaxed);
    record("park");
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->parked = true;
    }
    state->changed.notify_all();
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        state->changed.wait(lock, [state] { return state->deleted; });
    }
    throw task_terminated{};
}

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
    record("semaphore_take");
    return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t semaphore) {
    if (semaphore == nullptr) return 0;
    completion_signaled_in_task = true;
    { std::lock_guard<std::mutex> lock(semaphore->mutex); semaphore->given = true; }
    semaphore->changed.notify_one();
    return pdTRUE;
}
void vSemaphoreDelete(SemaphoreHandle_t semaphore) {
    semaphore_delete_count.fetch_add(1, std::memory_order_relaxed);
    record("semaphore_delete");
    delete semaphore;
}

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
unsigned music_test_queue_delete_count() { return queue_delete_count.load(std::memory_order_relaxed); }
void vQueueDelete(QueueHandle_t queue) {
    queue_delete_count.fetch_add(1, std::memory_order_relaxed);
    record("queue_delete");
    delete queue;
}
void vQueueDeleteWithCaps(QueueHandle_t queue) { vQueueDelete(queue); }

int xTaskCreate(TaskFunction_t entry, const char *, unsigned, void *context, unsigned, TaskHandle_t *handle) {
    if (entry == nullptr) {
        if (handle != nullptr) *handle = nullptr;
        return pdFAIL;
    }
    music_test_task_state *created = spawn_task(entry, context);
    if (handle != nullptr) *handle = created;
    return created == nullptr ? pdFAIL : pdPASS;
}
TaskHandle_t xTaskCreateStatic(TaskFunction_t entry, const char *name, unsigned, void *context,
                               unsigned priority, StackType_t *, StaticTask_t *) {
    if (music_test_fail_static_task_create) return nullptr;
    if (entry == nullptr) return nullptr;
    (void)name;
    (void)priority;
    return spawn_task(entry, context);
}
BaseType_t xTaskCreateWithCaps(TaskFunction_t entry, const char *name, unsigned stack_bytes, void *context,
                               UBaseType_t priority, TaskHandle_t *handle, UBaseType_t caps) {
    music_test_last_task_caps = caps;
    music_test_last_task_stack_bytes = stack_bytes;
    if (handle != nullptr) *handle = nullptr;
    if (music_test_fail_task_create_with_caps || entry == nullptr) return pdFAIL;
    (void)name;
    (void)priority;
    music_test_task_state *created = spawn_task(entry, context);
    if (created == nullptr) return pdFAIL;
    if (handle != nullptr) *handle = created;
    record("task_create_with_caps");
    return pdPASS;
}
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t) { return 512; }
void vTaskDelete(TaskHandle_t task) {
    if (task != nullptr) {
        delete_task_handle(task, false);
        return;
    }
    if (completion_signaled_in_task) self_delete_count.fetch_add(1, std::memory_order_relaxed);
    else delete_without_completion_count.fetch_add(1, std::memory_order_relaxed);
    record("task_self_delete");
    if (current_task_state != nullptr) {
        std::lock_guard<std::mutex> lock(current_task_state->mutex);
        current_task_state->self_deleted = true;
    }
}
void vTaskDeleteWithCaps(TaskHandle_t task) {
    if (task == nullptr) {
        vTaskDelete(nullptr);
        return;
    }
    delete_task_handle(task, true);
}
unsigned music_test_self_delete_count() { return self_delete_count.load(std::memory_order_relaxed); }
unsigned music_test_delete_without_completion_count() {
    return delete_without_completion_count.load(std::memory_order_relaxed);
}
unsigned music_test_entry_returned_without_delete_count() {
    return entry_returned_without_delete_count.load(std::memory_order_relaxed);
}
unsigned music_test_park_count() { return park_count.load(std::memory_order_relaxed); }
unsigned music_test_semaphore_delete_count() { return semaphore_delete_count.load(std::memory_order_relaxed); }
std::vector<std::string> music_test_teardown_events() {
    shared_state &s = shared();
    std::lock_guard<std::mutex> lock(s.events_mutex);
    return s.teardown_events;
}
void music_test_clear_teardown_events() {
    shared_state &s = shared();
    std::lock_guard<std::mutex> lock(s.events_mutex);
    s.teardown_events.clear();
}
