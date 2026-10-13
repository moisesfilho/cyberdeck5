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
    bool self_delete_requested = false;
    bool joined = false;
};
static thread_local host_task *current_task = nullptr;
namespace {
std::mutex log_hook_mutex;
vprintf_like_t current_vprintf = nullptr;
std::mutex ticks_mutex;
TickType_t ticks = 0;
std::mutex waits_mutex;
std::condition_variable waits_cv;
std::vector<event_log_shim::timed_wait *> waits;
uint64_t timed_wait_generation = 0;
std::mutex sync_history_mutex;
std::vector<event_log_shim::sync_event> recorded_sync_events;
std::vector<event_log_shim::file_event> recorded_file_events;
std::vector<event_log_shim::audit_event> recorded_audit_events;
uint64_t audit_sequence = 0;
std::vector<event_log_shim::semaphore_creation> created_semaphores;
std::vector<event_log_shim::task_start_event> started_tasks;
void record_sync(event_log_shim::sync_kind kind, void *handle) {
    const char *name = kind == event_log_shim::sync_kind::semaphore_take ? "semaphore-take" :
        kind == event_log_shim::sync_kind::semaphore_give ? "semaphore-give" :
        kind == event_log_shim::sync_kind::queue_send ? "queue-send" : "queue-receive";
    event_log_shim::record_audit_event(name, handle);
    std::lock_guard<std::mutex> lock(sync_history_mutex);
    recorded_sync_events.push_back({kind, handle, std::this_thread::get_id()});
}
std::mutex task_registry_mutex;
std::vector<host_task *> task_registry;
std::string task_creation_failure;
std::mutex queue_registry_mutex;
std::vector<host_queue *> queue_registry;
void wake_registered_waits() {
    std::lock_guard<std::mutex> lock(waits_mutex);
    for (auto *wait : waits)
        wait->wake();
}
} // namespace
namespace event_log_shim {
void fail_next_task_creation(const std::string &name) {
    std::lock_guard<std::mutex> lock(task_registry_mutex);
    task_creation_failure = name;
}
namespace {
constexpr auto kWaitWatchdog = std::chrono::seconds(5);
}
std::vector<sync_event> sync_history() {
    std::lock_guard<std::mutex> lock(sync_history_mutex);
    return recorded_sync_events;
}
std::vector<semaphore_creation> semaphore_creation_history() {
    std::lock_guard<std::mutex> lock(sync_history_mutex);
    return created_semaphores;
}
std::vector<task_start_event> task_start_history() {
    std::lock_guard<std::mutex> lock(sync_history_mutex);
    return started_tasks;
}
void wait_for_task_starts(size_t expected) {
    std::unique_lock<std::mutex> lock(sync_history_mutex);
    if (!waits_cv.wait_for(lock, kWaitWatchdog, [expected] { return started_tasks.size() >= expected; }))
        throw std::runtime_error("watchdog: expected task starts did not occur within 5s");
}
void record_semaphore_creation(void *handle, bool binary) {
    std::lock_guard<std::mutex> lock(sync_history_mutex);
    created_semaphores.push_back({handle, created_semaphores.size() + 1, binary});
}
uint64_t record_audit_event(const char *kind, void *handle, const char *path) {
    std::lock_guard<std::mutex> lock(sync_history_mutex);
    const auto sequence = ++audit_sequence;
    recorded_audit_events.push_back({sequence, kind, handle, path ? path : "", std::this_thread::get_id()});
    return sequence;
}
std::vector<audit_event> audit_history() {
    std::lock_guard<std::mutex> lock(sync_history_mutex);
    return recorded_audit_events;
}
void clear_audit_history() {
    std::lock_guard<std::mutex> lock(sync_history_mutex);
    recorded_audit_events.clear();
    audit_sequence = 0;
}
void clear_sync_history() {
    std::lock_guard<std::mutex> lock(sync_history_mutex);
    recorded_sync_events.clear();
}
void record_file_event(const char *operation, void *handle, const char *path) {
    record_audit_event(operation, handle, path);
    std::lock_guard<std::mutex> lock(sync_history_mutex);
    recorded_file_events.push_back({operation, std::this_thread::get_id()});
}
std::vector<file_event> file_history() {
    std::lock_guard<std::mutex> lock(sync_history_mutex);
    return recorded_file_events;
}
void clear_file_history() {
    std::lock_guard<std::mutex> lock(sync_history_mutex);
    recorded_file_events.clear();
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
bool wait_for_test_task(TaskHandle_t task, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(task->mutex);
    return task->cv.wait_for(lock, timeout, [&] { return task->completed; });
}
bool join_test_task(TaskHandle_t task) {
    if (!task)
        return false;
    std::lock_guard<std::mutex> lock(task->mutex);
    if (!task->completed)
        return false;
    if (!task->joined) {
        task->thread.join();
        task->joined = true;
    }
    return true;
}
void cleanup_test_tasks() {
    std::vector<host_task *> tasks;
    { std::lock_guard<std::mutex> lock(task_registry_mutex); tasks.swap(task_registry); }
    for (auto *task : tasks) {
        if (!join_test_task(task)) {
            std::lock_guard<std::mutex> lock(task_registry_mutex);
            task_registry.push_back(task);
            continue;
        }
        delete task;
    }
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
    record_sync(event_log_shim::sync_kind::queue_send, q);
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
    record_sync(event_log_shim::sync_kind::queue_receive, q);
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
    auto *semaphore = new host_semaphore{{}, {}, 1, false};
    event_log_shim::record_semaphore_creation(semaphore, false);
    return semaphore;
}
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *) {
    auto *semaphore = new host_semaphore{{}, {}, 0, true};
    event_log_shim::record_semaphore_creation(semaphore, true);
    return semaphore;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait) {
    record_sync(event_log_shim::sync_kind::semaphore_take, s);
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
    record_sync(event_log_shim::sync_kind::semaphore_give, s);
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
BaseType_t xTaskCreate(TaskFunction_t f, const char *name, uint32_t, void *arg, unsigned, TaskHandle_t *out) {
    {
        std::lock_guard<std::mutex> lock(task_registry_mutex);
        if (!task_creation_failure.empty() && name != nullptr && task_creation_failure == name) {
            task_creation_failure.clear();
            if (out != nullptr) *out = nullptr;
            return pdFALSE;
        }
    }
    auto *t = new host_task;
    *out = t;
    { std::lock_guard<std::mutex> lock(task_registry_mutex); task_registry.push_back(t); }
    std::promise<void> published;
    auto ready = published.get_future();
    t->thread = std::thread([=, ready = std::move(ready)]() mutable {
        ready.wait();
        current_task = t;
        {
            std::lock_guard<std::mutex> lock(sync_history_mutex);
            started_tasks.push_back({name ? name : "", std::this_thread::get_id(), ++audit_sequence, t});
        }
        waits_cv.notify_all();
        f(arg);
        {
            std::lock_guard<std::mutex> lock(t->mutex);
            t->completed = true;
        }
        t->cv.notify_all();
    });
    published.set_value();
    return pdPASS;
}
void vTaskDelete(TaskHandle_t task) {
    if (!task) {
        if (current_task) {
            std::lock_guard<std::mutex> lock(current_task->mutex);
            current_task->self_delete_requested = true;
        }
        return;
    }
    // Host tasks are never forcibly terminated; external deletion is modeled
    // only as a request, so their callback must return before join is safe.
    std::lock_guard<std::mutex> lock(task->mutex);
    task->self_delete_requested = true;
}
bool test_task_self_delete_requested(TaskHandle_t task) {
    if (!task) return false;
    std::lock_guard<std::mutex> lock(task->mutex);
    return task->self_delete_requested;
}
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
    std::lock_guard<std::mutex> lock(log_hook_mutex);
    const auto previous = current_vprintf;
    current_vprintf = hook;
    return previous;
}
vprintf_like_t event_log_shim_current_vprintf(void) {
    std::lock_guard<std::mutex> lock(log_hook_mutex);
    return current_vprintf;
}
int esp_log_write(const char *, const char *, ...) {
    return 0;
}
