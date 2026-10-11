#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace event_log_shim {
struct queue_snapshot {
    QueueHandle_t handle;
    size_t capacity;
    size_t item_size;
    size_t depth;
};
queue_snapshot snapshot_queue(QueueHandle_t handle);
std::vector<queue_snapshot> snapshot_queues();
void wait_for_queue_depth(QueueHandle_t handle, size_t expected);
void reset_ticks();
void advance_ticks(TickType_t ticks);
void wake_waiters();
void wait_for_test_task(TaskHandle_t task);
bool wait_for_test_task(TaskHandle_t task, std::chrono::milliseconds timeout);
bool join_test_task(TaskHandle_t task);
void cleanup_test_tasks();
void wait_for_notification_wait(TaskHandle_t task);
void wait_for_timed_wait_registration();
void wait_for_timed_wait_registration(std::thread::id owner);
uint64_t timed_wait_registration_generation();
uint64_t wait_for_next_timed_wait_registration(std::thread::id owner, uint64_t after_generation);
class timed_wait {
  public:
    timed_wait();
    ~timed_wait();
    timed_wait(const timed_wait &) = delete;
    timed_wait &operator=(const timed_wait &) = delete;
    bool wait_until(TickType_t deadline);
    void wake();
    void notify_tick_advanced();

  private:
    friend void wake_waiters();
    friend void wait_for_timed_wait_registration();
    friend void wait_for_timed_wait_registration(std::thread::id owner);
    friend uint64_t timed_wait_registration_generation();
    friend uint64_t wait_for_next_timed_wait_registration(std::thread::id owner, uint64_t after_generation);
    std::mutex mutex_;
    std::condition_variable cv_;
    bool woken_ = false;
    bool waiting_ = false;
    std::thread::id owner_;
    uint64_t registration_generation_ = 0;
};
} // namespace event_log_shim
