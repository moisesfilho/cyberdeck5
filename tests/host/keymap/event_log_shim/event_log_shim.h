#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

namespace event_log_shim {
enum class sync_kind { semaphore_take, semaphore_give, queue_send, queue_receive };
struct sync_event { sync_kind kind; void *handle; std::thread::id thread; };
struct file_event { std::string operation; std::thread::id thread; };
struct audit_event { uint64_t sequence; std::string kind; void *handle; std::string path; std::thread::id owner; };
struct semaphore_creation { void *handle; uint64_t ordinal; bool binary; };
struct task_start_event { std::string name; std::thread::id owner; uint64_t sequence; TaskHandle_t handle; };
void fail_next_task_creation(const std::string &name);
std::vector<task_start_event> task_start_history();
void wait_for_task_starts(size_t expected);
std::vector<semaphore_creation> semaphore_creation_history();
void record_semaphore_creation(void *handle, bool binary);
uint64_t record_audit_event(const char *kind, void *handle = nullptr, const char *path = nullptr);
std::vector<audit_event> audit_history();
void clear_audit_history();
std::vector<sync_event> sync_history();
void clear_sync_history();
void record_file_event(const char *operation, void *handle = nullptr, const char *path = nullptr);
std::vector<file_event> file_history();
void clear_file_history();
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
