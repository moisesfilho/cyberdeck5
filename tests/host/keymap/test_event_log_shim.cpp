#include "event_log_shim/event_log_shim.h"
#include "event_log_shim/freertos/queue.h"
#include "event_log_shim/freertos/semphr.h"
#include "event_log_shim/freertos/task.h"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>

struct notify_case {
    std::promise<void> started;
    std::promise<uint32_t> result;
    BaseType_t clear;
    TickType_t wait;
    std::shared_future<void> gate;
};
static void notify_task(void *argument) {
    auto *test = static_cast<notify_case *>(argument);
    test->started.set_value();
    if (test->gate.valid()) test->gate.wait();
    test->result.set_value(ulTaskNotifyTake(test->clear, test->wait));
}

int main() {
    event_log_shim::reset_ticks();
    StaticQueue_t durable_storage{};
    uint8_t durable_bytes[4 * sizeof(void *)]{};
    auto durable_queue = xQueueCreateStatic(4, sizeof(void *), durable_bytes, &durable_storage);
    int payloads[] = {101, 202, 303, 404};
    for (size_t index = 0; index < 4; ++index) {
        void *payload = &payloads[index];
        assert(xQueueSend(durable_queue, &payload, 0) == pdTRUE);
        const auto snapshot = event_log_shim::snapshot_queue(durable_queue);
        assert(snapshot.handle == durable_queue && snapshot.capacity == 4);
        assert(snapshot.item_size == sizeof(void *) && snapshot.depth == index + 1);
    }
    void *rejected = &payloads[0];
    assert(xQueueSend(durable_queue, &rejected, 0) == pdFALSE);
    assert(event_log_shim::snapshot_queue(durable_queue).depth == 4);
    void *blocked_payload = &payloads[0];
    auto blocked_sender = std::async(std::launch::async, [&] {
        return xQueueSend(durable_queue, &blocked_payload, portMAX_DELAY);
    });
    event_log_shim::wait_for_timed_wait_registration();
    assert(blocked_sender.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout);
    void *received_payload = nullptr;
    assert(xQueueReceive(durable_queue, &received_payload, 0) == pdTRUE);
    assert(received_payload == &payloads[0]);
    event_log_shim::wait_for_queue_depth(durable_queue, 4);
    assert(blocked_sender.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    assert(blocked_sender.get() == pdTRUE);
    for (size_t index = 1; index < 4; ++index) {
        received_payload = nullptr;
        assert(xQueueReceive(durable_queue, &received_payload, 0) == pdTRUE);
        assert(received_payload == &payloads[index]);
    }
    assert(xQueueReceive(durable_queue, &received_payload, 0) == pdTRUE);
    assert(received_payload == &payloads[0]);
    assert(event_log_shim::snapshot_queue(durable_queue).depth == 0);

    StaticQueue_t storage{};
    uint8_t queue_bytes[2 * sizeof(int)]{};
    auto queue = xQueueCreateStatic(2, sizeof(int), queue_bytes, &storage);
    int original = 11, next = 22, received = 0;
    assert(xQueueSend(queue, &original, 0) == pdTRUE);
    original = 99;
    assert(xQueueSend(queue, &next, 0) == pdTRUE);
    assert(xQueueSend(queue, &next, 0) == pdFALSE);
    assert(xQueueReceive(queue, &received, 0) == pdTRUE && received == 11);
    assert(xQueueSend(queue, &next, 0) == pdTRUE);
    assert(xQueueReceive(queue, &received, 0) == pdTRUE && received == 22);
    assert(xQueueReceive(queue, &received, 0) == pdTRUE && received == 22);
    std::promise<void> receive_started;
    auto receive_result = std::async(std::launch::async, [&] {
        receive_started.set_value();
        return xQueueReceive(queue, &received, 5);
    });
    receive_started.get_future().wait();
    event_log_shim::wait_for_timed_wait_registration();
    assert(receive_result.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout);
    event_log_shim::advance_ticks(5);
    assert(receive_result.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    assert(receive_result.get() == pdFALSE);
    std::promise<void> receive_wake_started;
    auto receive_wake = std::async(std::launch::async, [&] {
        receive_wake_started.set_value();
        return xQueueReceive(queue, &received, 10);
    });
    receive_wake_started.get_future().wait();
    assert(receive_wake.wait_for(std::chrono::seconds(2)) == std::future_status::timeout);
    int delivered = 44;
    assert(xQueueSend(queue, &delivered, 0) == pdTRUE);
    assert(receive_wake.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    assert(receive_wake.get() == pdTRUE && received == delivered);
    int full_a = 31, full_b = 32;
    int blocked_value = 55;
    assert(xQueueSend(queue, &full_a, 0) == pdTRUE && xQueueSend(queue, &full_b, 0) == pdTRUE);
    std::promise<void> send_started;
    auto send_result = std::async(std::launch::async, [&] {
        send_started.set_value();
        return xQueueSend(queue, &blocked_value, portMAX_DELAY);
    });
    send_started.get_future().wait();
    event_log_shim::wait_for_timed_wait_registration();
    assert(send_result.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout);
    assert(xQueueReceive(queue, &received, 0) == pdTRUE && received == 31);
    assert(send_result.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    assert(send_result.get() == pdTRUE);
    assert(xQueueReceive(queue, &received, 0) == pdTRUE && received == 32);
    assert(xQueueReceive(queue, &received, 0) == pdTRUE && received == blocked_value);
    assert(xQueueSend(queue, &full_a, 0) == pdTRUE && xQueueSend(queue, &full_b, 0) == pdTRUE);
    std::promise<void> send_timeout_started;
    auto send_timeout = std::async(std::launch::async, [&] {
        send_timeout_started.set_value();
        return xQueueSend(queue, &blocked_value, 7);
    });
    send_timeout_started.get_future().wait();
    event_log_shim::wait_for_timed_wait_registration();
    assert(send_timeout.wait_for(std::chrono::seconds(2)) == std::future_status::timeout);
    event_log_shim::advance_ticks(7);
    assert(send_timeout.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    assert(send_timeout.get() == pdFALSE);
    StaticSemaphore_t binary_storage{}, mutex_storage{};
    auto binary = xSemaphoreCreateBinaryStatic(&binary_storage);
    auto mutex = xSemaphoreCreateMutexStatic(&mutex_storage);
    assert(xSemaphoreTake(binary, 0) == pdFALSE);
    assert(xSemaphoreGive(binary) == pdTRUE && xSemaphoreTake(binary, 0) == pdTRUE);
    assert(xSemaphoreTake(binary, 0) == pdFALSE);
    assert(xSemaphoreTake(mutex, 0) == pdTRUE && xSemaphoreTake(mutex, 0) == pdFALSE);
    assert(xSemaphoreGive(mutex) == pdTRUE && xSemaphoreTake(mutex, 0) == pdTRUE);
    event_log_shim::reset_ticks();
    auto timed = xSemaphoreCreateBinaryStatic(&binary_storage);
    std::promise<void> timed_started;
    auto timed_result = std::async(std::launch::async, [&] {
        timed_started.set_value();
        return xSemaphoreTake(timed, 10);
    });
    timed_started.get_future().wait();
    event_log_shim::wait_for_timed_wait_registration();
    assert(timed_result.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout);
    event_log_shim::advance_ticks(9);
    assert(timed_result.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout);
    event_log_shim::advance_ticks(1);
    assert(timed_result.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    assert(timed_result.get() == pdFALSE);
    assert(xSemaphoreTake(timed, 0) == pdFALSE);

    event_log_shim::reset_ticks();
    std::promise<void> give_started;
    auto give_result = std::async(std::launch::async, [&] {
        give_started.set_value();
        return xSemaphoreTake(timed, 10);
    });
    give_started.get_future().wait();
    event_log_shim::wait_for_timed_wait_registration();
    assert(give_result.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout);
    assert(xSemaphoreGive(timed) == pdTRUE);
    assert(give_result.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    assert(give_result.get() == pdTRUE);
    assert(xSemaphoreTake(timed, 0) == pdFALSE);
    assert(xSemaphoreGive(timed) == pdTRUE);
    assert(xSemaphoreTake(timed, 0) == pdTRUE);
    event_log_shim::reset_ticks();
    notify_case notify_timeout{{}, {}, pdTRUE, 9, {}};
    TaskHandle_t timeout_task = nullptr;
    assert(xTaskCreate(notify_task, "notify-timeout", 0, &notify_timeout, 0, &timeout_task) == pdPASS);
    auto timeout_started = notify_timeout.started.get_future();
    auto timeout_value = notify_timeout.result.get_future();
    timeout_started.wait();
    event_log_shim::wait_for_notification_wait(timeout_task);
    assert(timeout_value.wait_for(std::chrono::seconds(2)) == std::future_status::timeout);
    event_log_shim::advance_ticks(9);
    assert(timeout_value.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    assert(timeout_value.get() == 0);
    event_log_shim::wait_for_test_task(timeout_task);

    event_log_shim::reset_ticks();
    notify_case notify_wake{{}, {}, pdFALSE, 20, {}};
    TaskHandle_t wake_task = nullptr;
    assert(xTaskCreate(notify_task, "notify-wake", 0, &notify_wake, 0, &wake_task) == pdPASS);
    auto wake_started = notify_wake.started.get_future();
    auto wake_value = notify_wake.result.get_future();
    wake_started.wait();
    event_log_shim::wait_for_notification_wait(wake_task);
    assert(wake_value.wait_for(std::chrono::seconds(2)) == std::future_status::timeout);
    xTaskNotifyGive(wake_task);
    assert(wake_value.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    assert(wake_value.get() == 1);
    event_log_shim::wait_for_test_task(wake_task);

    std::promise<void> release_clear;
    notify_case notify_clear{{}, {}, pdTRUE, portMAX_DELAY, release_clear.get_future().share()};
    TaskHandle_t clear_task = nullptr;
    assert(xTaskCreate(notify_task, "notify-clear", 0, &notify_clear, 0, &clear_task) == pdPASS);
    auto clear_started = notify_clear.started.get_future();
    auto clear_value = notify_clear.result.get_future();
    clear_started.wait();
    xTaskNotifyGive(clear_task);
    xTaskNotifyGive(clear_task);
    release_clear.set_value();
    assert(clear_value.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    assert(clear_value.get() == 2);
    event_log_shim::wait_for_test_task(clear_task);

    std::promise<void> release_count;
    notify_case notify_count{{}, {}, pdFALSE, portMAX_DELAY, release_count.get_future().share()};
    TaskHandle_t count_task = nullptr;
    assert(xTaskCreate(notify_task, "notify-count", 0, &notify_count, 0, &count_task) == pdPASS);
    auto count_started = notify_count.started.get_future();
    auto count_value = notify_count.result.get_future();
    count_started.wait();
    xTaskNotifyGive(count_task);
    xTaskNotifyGive(count_task);
    release_count.set_value();
    assert(count_value.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    assert(count_value.get() == 2);
    event_log_shim::wait_for_test_task(count_task);
    event_log_shim::reset_ticks();
    event_log_shim::timed_wait wait;
    bool woke = false;
    std::thread waiter([&] { woke = wait.wait_until(8); });
    event_log_shim::advance_ticks(7);
    event_log_shim::wake_waiters();
    assert(!woke);
    event_log_shim::advance_ticks(1);
    event_log_shim::wake_waiters();
    waiter.join();
    assert(!woke);
    event_log_shim::reset_ticks();
    event_log_shim::timed_wait early;
    std::thread early_waiter([&] { woke = early.wait_until(20); });
    early.wake();
    early_waiter.join();
    assert(woke);
}
