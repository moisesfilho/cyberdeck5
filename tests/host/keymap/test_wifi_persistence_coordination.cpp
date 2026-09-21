/* RED contract tests for the public Wi-Fi persistence worker seam.
 *
 * These tests deliberately do not inspect wifi_mgr.cpp's local
 * producer_pending/persist_pending variables.  A real storage sink and a
 * deterministic retry scheduler are explicit test doubles for the public
 * interfaces required by the coordinator.
 */
#include "features/wifi/cyberdeck_wifi_persistence_coordinator.h"

#include <cstdio>

using namespace cyberdeck_wifi_test;

namespace {
int failures = 0;
int checks = 0;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    } \
} while (0)

class scripted_storage final : public storage_sink {
public:
    bool next_result = true;
    int calls = 0;
    ip_snapshot last{};

    bool persist(const ip_snapshot &item) override
    {
        ++calls;
        last = item;
        return next_result;
    }
};

class recording_scheduler final : public retry_scheduler {
public:
    bool armed = false;
    int arm_calls = 0;
    int cancel_calls = 0;

    void arm() override { armed = true; ++arm_calls; }
    void cancel() override { armed = false; ++cancel_calls; }
};

void test_producer_and_persist_slots_are_publicly_coordinated()
{
    event_dispatch producer;
    persistence_queue persistence;
    scripted_storage storage;
    recording_scheduler scheduler;
    persistence_coordinator coordinator(producer, persistence, storage, scheduler);

    CHECK(producer.initialize());
    CHECK(persistence.initialize());
    CHECK(producer.on_got_ip({0x0a000001}, {7, "home", "secret"}));
    CHECK(coordinator.producer_pending());
    CHECK(!coordinator.persist_pending());

    CHECK(coordinator.pump_producer());
    CHECK(!coordinator.producer_pending());
    CHECK(coordinator.persist_pending());
    CHECK(persistence.pending() == 1);

    CHECK(coordinator.pump_persistence());
    CHECK(!coordinator.producer_pending());
    CHECK(!coordinator.persist_pending());
    CHECK(storage.calls == 1);
    CHECK(storage.last.token == 7);
    CHECK(persistence.pending() == 0);
}

void test_rollback_clears_producer_and_persist_pending_without_storage_call()
{
    event_dispatch producer;
    persistence_queue persistence;
    scripted_storage storage;
    recording_scheduler scheduler;
    persistence_coordinator coordinator(producer, persistence, storage, scheduler);
    CHECK(producer.initialize());
    CHECK(persistence.initialize());

    CHECK(producer.on_got_ip({1}, {10, "rollback", "secret"}));
    CHECK(coordinator.producer_pending());
    coordinator.rollback();

    CHECK(!coordinator.producer_pending());
    CHECK(!coordinator.persist_pending());
    CHECK(!coordinator.retry_pending());
    CHECK(!scheduler.armed);
    CHECK(persistence.pending() == 0);
    CHECK(storage.calls == 0);
    CHECK(!coordinator.pump_producer());
    CHECK(!coordinator.pump_persistence());
}

void test_disconnect_clears_in_flight_and_invalidates_old_token()
{
    event_dispatch producer;
    persistence_queue persistence;
    scripted_storage storage;
    recording_scheduler scheduler;
    persistence_coordinator coordinator(producer, persistence, storage, scheduler);
    CHECK(producer.initialize());
    CHECK(persistence.initialize());

    CHECK(producer.on_got_ip({2}, {20, "disconnect", "secret"}));
    CHECK(coordinator.pump_producer());
    CHECK(coordinator.pump_persistence());
    CHECK(!coordinator.persist_pending());

    coordinator.disconnect(20);
    CHECK(!coordinator.producer_pending());
    CHECK(!coordinator.persist_pending());
    CHECK(!coordinator.retry_pending());
    CHECK(persistence.pending() == 0);
    producer.on_connected(21, "reconnected");
    CHECK(!producer.on_got_ip({3}, {20, "disconnect", "late-old"}));
    CHECK(producer.on_got_ip({4}, {21, "reconnected", "new-secret"}));
    CHECK(!coordinator.pump_persistence());
    CHECK(storage.calls == 1);
}

void test_storage_failure_retries_once_after_backoff_and_acknowledges_on_success()
{
    event_dispatch producer;
    persistence_queue persistence;
    scripted_storage storage;
    recording_scheduler scheduler;
    persistence_coordinator coordinator(producer, persistence, storage, scheduler);
    CHECK(producer.initialize());
    CHECK(persistence.initialize());

    CHECK(producer.on_got_ip({4}, {30, "retry", "secret"}));
    CHECK(coordinator.pump_producer());
    storage.next_result = false;
    CHECK(!coordinator.pump_persistence());
    CHECK(coordinator.persist_pending());
    CHECK(coordinator.retry_pending());
    CHECK(scheduler.armed);
    CHECK(scheduler.arm_calls == 1);
    CHECK(persistence.pending() == 1);

    storage.next_result = true;
    coordinator.on_retry_timer();
    CHECK(!coordinator.retry_pending());
    CHECK(coordinator.persist_pending());
    CHECK(coordinator.pump_persistence());
    CHECK(storage.calls == 2);
    CHECK(persistence.pending() == 0);
    CHECK(!coordinator.persist_pending());
}

void test_teardown_during_backoff_cancels_retry_and_never_reappears()
{
    event_dispatch producer;
    persistence_queue persistence;
    scripted_storage storage;
    recording_scheduler scheduler;
    persistence_coordinator coordinator(producer, persistence, storage, scheduler);
    CHECK(producer.initialize());
    CHECK(persistence.initialize());
    CHECK(producer.on_got_ip({5}, {40, "teardown", "secret"}));
    CHECK(coordinator.pump_producer());

    storage.next_result = false;
    CHECK(!coordinator.pump_persistence());
    CHECK(coordinator.retry_pending());
    const int calls_before_teardown = storage.calls;

    coordinator.teardown();
    CHECK(!coordinator.producer_pending());
    CHECK(!coordinator.persist_pending());
    CHECK(!coordinator.retry_pending());
    CHECK(!scheduler.armed);
    CHECK(scheduler.cancel_calls == 1);
    CHECK(persistence.pending() == 0);

    /* Simulate a stale timer callback after teardown. */
    coordinator.on_retry_timer();
    CHECK(!coordinator.retry_pending());
    CHECK(!coordinator.persist_pending());
    CHECK(!coordinator.pump_persistence());
    CHECK(storage.calls == calls_before_teardown);
}

} // namespace

int main()
{
    test_producer_and_persist_slots_are_publicly_coordinated();
    test_rollback_clears_producer_and_persist_pending_without_storage_call();
    test_disconnect_clears_in_flight_and_invalidates_old_token();
    test_storage_failure_retries_once_after_backoff_and_acknowledges_on_success();
    test_teardown_during_backoff_cancels_retry_and_never_reappears();
    std::printf("wifi persistence coordination: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
