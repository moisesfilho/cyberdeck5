#include "event_log_shim.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

struct host_queue {
    size_t length, item_size;
    std::deque<std::vector<unsigned char>> values;
    std::mutex mutex;
    std::condition_variable cv;
};
struct host_semaphore {
    std::mutex mutex;
    std::condition_variable cv;
    unsigned count;
    bool binary;
};
struct host_task {
    std::thread thread;
    std::mutex mutex;
    std::condition_variable cv;
    unsigned notifications = 0;
    bool completed = false;
    bool notification_wait_registered = false;
};
static thread_local host_task *current_task = nullptr;
namespace {
std::mutex ticks_mutex;
TickType_t ticks = 0;
std::mutex waits_mutex;
std::condition_variable waits_cv;
std::vector<event_log_shim::timed_wait *> waits;
uint64_t timed_wait_generation = 0;
std::mutex queue_registry_mutex;
std::vector<host_queue *> queue_registry;
void wake_registered_waits() {
    std::lock_guard<std::mutex> lock(waits_mutex);
    for (auto *wait : waits)
        wait->wake();
}
} // namespace
namespace event_log_shim {
namespace {
constexpr auto kWaitWatchdog = std::chrono::seconds(5);
}
queue_snapshot snapshot_queue(QueueHandle_t handle) {
    std::lock_guard<std::mutex> lock(handle->mutex);
    return {handle, handle->length, handle->item_size, handle->values.size()};
}
std::vector<queue_snapshot> snapshot_queues() {
    std::vector<host_queue *> handles;
    {
        std::lock_guard<std::mutex> lock(queue_registry_mutex);
        handles = queue_registry;
    }
    std::vector<queue_snapshot> snapshots;
    snapshots.reserve(handles.size());
    for (auto *handle : handles)
        snapshots.push_back(snapshot_queue(handle));
    return snapshots;
}
void wait_for_queue_depth(QueueHandle_t handle, size_t expected) {
    std::unique_lock<std::mutex> lock(handle->mutex);
    if (!handle->cv.wait_for(lock, kWaitWatchdog, [&] { return handle->values.size() == expected; }))
        throw std::runtime_error("watchdog: queue depth did not reach expected value within 5s");
}
void wait_for_test_task(TaskHandle_t task) {
    std::unique_lock<std::mutex> lock(task->mutex);
    if (!task->cv.wait_for(lock, kWaitWatchdog, [&] { return task->completed; }))
        throw std::runtime_error("watchdog: test task did not complete within 5s");
}
void wait_for_notification_wait(TaskHandle_t task) {
    std::unique_lock<std::mutex> lock(task->mutex);
    if (!task->cv.wait_for(lock, kWaitWatchdog, [&] { return task->notification_wait_registered || task->completed; }))
        throw std::runtime_error("watchdog: notification wait was not registered within 5s");
}
void wait_for_timed_wait_registration() {
    std::unique_lock<std::mutex> lock(waits_mutex);
    if (!waits_cv.wait_for(lock, kWaitWatchdog, [] {
            return std::any_of(waits.begin(), waits.end(), [](auto *wait) { return wait->waiting_; });
        }))
        throw std::runtime_error("watchdog: timed wait was not registered within 5s");
}
void wait_for_timed_wait_registration(std::thread::id owner) {
    std::unique_lock<std::mutex> lock(waits_mutex);
    if (!waits_cv.wait_for(lock, kWaitWatchdog, [owner] {
            return std::any_of(waits.begin(), waits.end(),
                               [owner](auto *wait) { return wait->waiting_ && wait->owner_ == owner; });
        }))
        throw std::runtime_error("watchdog: caller timed wait was not registered within 5s");
}
uint64_t timed_wait_registration_generation() {
    std::lock_guard<std::mutex> lock(waits_mutex);
    return timed_wait_generation;
}
uint64_t wait_for_next_timed_wait_registration(std::thread::id owner, uint64_t after_generation) {
    std::unique_lock<std::mutex> lock(waits_mutex);
    if (!waits_cv.wait_for(lock, kWaitWatchdog, [owner, after_generation] {
            return std::any_of(waits.begin(), waits.end(), [owner, after_generation](auto *wait) {
                return wait->waiting_ && wait->owner_ == owner && wait->registration_generation_ > after_generation;
            });
        }))
        throw std::runtime_error("watchdog: next caller timed wait was not registered within 5s");
    const auto found = std::find_if(waits.begin(), waits.end(), [owner, after_generation](auto *wait) {
        return wait->waiting_ && wait->owner_ == owner && wait->registration_generation_ > after_generation;
    });
    return (*found)->registration_generation_;
}
void reset_ticks() {
    std::lock_guard<std::mutex> lock(ticks_mutex);
    ticks = 0;
}
void advance_ticks(TickType_t amount) {
    {
        std::lock_guard<std::mutex> lock(ticks_mutex);
        ticks += amount;
    }
    wake_waiters();
}
void wake_waiters() {
    std::lock_guard<std::mutex> lock(waits_mutex);
    for (auto *wait : waits)
        wait->notify_tick_advanced();
}
timed_wait::timed_wait() {
    std::lock_guard<std::mutex> lock(waits_mutex);
    waits.push_back(this);
}
timed_wait::~timed_wait() {
    std::lock_guard<std::mutex> lock(waits_mutex);
    waits.erase(std::remove(waits.begin(), waits.end(), this), waits.end());
}
bool timed_wait::wait_until(TickType_t deadline) {
    {
        std::lock_guard<std::mutex> registry_lock(waits_mutex);
        owner_ = std::this_thread::get_id();
        waiting_ = true;
        registration_generation_ = ++timed_wait_generation;
        waits_cv.notify_all();
    }
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [&] {
        std::lock_guard<std::mutex> tick_lock(ticks_mutex);
        return woken_ || ticks >= deadline;
    });
    const bool woke = woken_;
    woken_ = false;
    lock.unlock();
    {
        std::lock_guard<std::mutex> registry_lock(waits_mutex);
        waiting_ = false;
    }
    return woke;
}
void timed_wait::wake() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        woken_ = true;
    }
    cv_.notify_all();
}
void timed_wait::notify_tick_advanced() {
    std::lock_guard<std::mutex> lock(mutex_);
    cv_.notify_all();
}
} // namespace event_log_shim
QueueHandle_t xQueueCreateStatic(size_t n, size_t s, uint8_t *, StaticQueue_t *) {
    auto *q = new host_queue;
    q->length = n;
    q->item_size = s;
    {
        std::lock_guard<std::mutex> lock(queue_registry_mutex);
        queue_registry.push_back(q);
    }
    return q;
}
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait) {
    event_log_shim::timed_wait registered_wait;
    const auto deadline = wait == portMAX_DELAY ? portMAX_DELAY : xTaskGetTickCount() + wait;
    std::unique_lock<std::mutex> l(q->mutex);
    while (q->values.size() == q->length) {
        if (!wait || xTaskGetTickCount() >= deadline)
            return pdFALSE;
        l.unlock();
        registered_wait.wait_until(deadline);
        l.lock();
    }
    std::vector<unsigned char> v(q->item_size);
    std::memcpy(v.data(), item, q->item_size);
    q->values.push_back(std::move(v));
    l.unlock();
    q->cv.notify_all();
    wake_registered_waits();
    return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q, void *out, TickType_t wait) {
    event_log_shim::timed_wait registered_wait;
    const auto deadline = wait == portMAX_DELAY ? portMAX_DELAY : xTaskGetTickCount() + wait;
    std::unique_lock<std::mutex> l(q->mutex);
    while (q->values.empty()) {
        if (!wait || xTaskGetTickCount() >= deadline)
            return pdFALSE;
        l.unlock();
        registered_wait.wait_until(deadline);
        l.lock();
    }
    auto v = std::move(q->values.front());
    q->values.pop_front();
    std::memcpy(out, v.data(), q->item_size);
    l.unlock();
    q->cv.notify_all();
    wake_registered_waits();
    return pdTRUE;
}
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *) {
    return new host_semaphore{{}, {}, 1, false};
}
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *) {
    return new host_semaphore{{}, {}, 0, true};
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait) {
    if (!wait) {
        std::lock_guard<std::mutex> lock(s->mutex);
        if (!s->count)
            return pdFALSE;
        --s->count;
        return pdTRUE;
    }
    event_log_shim::timed_wait registered_wait;
    const TickType_t deadline = wait == portMAX_DELAY ? portMAX_DELAY : xTaskGetTickCount() + wait;
    std::unique_lock<std::mutex> l(s->mutex);
    while (!s->count) {
        l.unlock();
        const bool woke = registered_wait.wait_until(deadline);
        l.lock();
        if (!s->count && !woke && xTaskGetTickCount() >= deadline)
            return pdFALSE;
    }
    --s->count;
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t s) {
    {
        std::lock_guard<std::mutex> l(s->mutex);
        if (s->binary)
            s->count = 1;
        else if (s->count == 0)
            s->count = 1;
        else
            return pdFALSE;
    }
    s->cv.notify_one();
    wake_registered_waits();
    return pdTRUE;
}
BaseType_t xTaskCreate(TaskFunction_t f, const char *, uint32_t, void *arg, unsigned, TaskHandle_t *out) {
    auto *t = new host_task;
    *out = t;
    std::promise<void> published;
    auto ready = published.get_future();
    t->thread = std::thread([=, ready = std::move(ready)]() mutable {
        ready.wait();
        current_task = t;
        f(arg);
        {
            std::lock_guard<std::mutex> lock(t->mutex);
            t->completed = true;
        }
        t->cv.notify_all();
    });
    t->thread.detach();
    published.set_value();
    return pdPASS;
}
void vTaskDelete(TaskHandle_t) {}
TickType_t xTaskGetTickCount(void) {
    std::lock_guard<std::mutex> lock(ticks_mutex);
    return ticks;
}
void xTaskNotifyGive(TaskHandle_t t) {
    if (!t)
        return;
    {
        std::lock_guard<std::mutex> l(t->mutex);
        ++t->notifications;
    }
    t->cv.notify_one();
    wake_registered_waits();
}
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait) {
    if (!current_task)
        return 0;
    auto *task = current_task;
    event_log_shim::timed_wait registered_wait;
    const auto deadline = wait == portMAX_DELAY ? portMAX_DELAY : xTaskGetTickCount() + wait;
    std::unique_lock<std::mutex> l(task->mutex);
    while (!task->notifications) {
        if (!wait || xTaskGetTickCount() >= deadline)
            return 0;
        task->notification_wait_registered = true;
        task->cv.notify_all();
        l.unlock();
        registered_wait.wait_until(deadline);
        l.lock();
    }
    unsigned n = task->notifications;
    if (clear)
        task->notifications = 0;
    else if (n)
        --task->notifications;
    return n;
}
void vTaskDelay(TickType_t ms) {
    if (!ms)
        return;
    event_log_shim::timed_wait registered_wait;
    registered_wait.wait_until(ms == portMAX_DELAY ? portMAX_DELAY : xTaskGetTickCount() + ms);
}
int64_t esp_timer_get_time(void) {
    return static_cast<int64_t>(xTaskGetTickCount()) * 1000;
}
void *bsp_sdcard_get_handle(void) {
    return reinterpret_cast<void *>(1);
}
esp_err_t bsp_sdcard_mount(void) {
    return ESP_OK;
}
vprintf_like_t esp_log_set_vprintf(vprintf_like_t hook) {
    return hook;
}
int esp_log_write(const char *, const char *, ...) {
    return 0;
}
