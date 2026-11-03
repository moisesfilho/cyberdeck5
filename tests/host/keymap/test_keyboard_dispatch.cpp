#include "platform/input/cyberdeck_keyboard_dispatch.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include <cassert>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if !defined(ESP_PLATFORM)
namespace cyberdeck_keyboard_dispatch {
struct test_snapshot {
    bool queue_empty;
    std::uintptr_t first_identity;
    bool first_in_use;
    bool identity_exhausted;
    bool queue_active;
    std::size_t active_tickets;
};
void seed_ticket_identity_for_test(std::uintptr_t identity);
test_snapshot snapshot_for_test();
}
#endif

struct event { std::string text; std::uint8_t modifier{}; std::uint32_t key{}; };
struct capture { std::vector<event> events; };

struct gated_capture {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<event> events;
    bool entered = false;
    bool release = false;
};

static void receive(const char *text, std::size_t length, std::uint8_t modifier,
                    std::uint32_t key, void *context)
{
    auto &capture = *static_cast<struct capture *>(context);
    capture.events.push_back({std::string(text, length), modifier, key});
}

static void receive_gated(const char *text, std::size_t length, std::uint8_t modifier,
                          std::uint32_t key, void *context)
{
    auto &capture = *static_cast<gated_capture *>(context);
    std::unique_lock<std::mutex> lock(capture.mutex);
    capture.events.push_back({std::string(text, length), modifier, key});
    capture.entered = true;
    capture.changed.notify_all();
    capture.changed.wait(lock, [&] { return capture.release; });
}

static void reset_host()
{
    lv_shim_reset_async();
    lv_shim_reset_wake_count();
}

static void drain_async()
{
    while (lv_shim_run_one_async()) {}
}

static void test_callbacks_are_scheduled_and_fifo()
{
    reset_host();
    capture capture;
    cyberdeck_keyboard_dispatch::dispatcher dispatcher;
    assert(dispatcher.start(receive, &capture));

    dispatcher.submit("one", 3, 1, 0);
    dispatcher.submit("two", 3, 2, 0);
    dispatcher.submit(nullptr, 0, 3, 7);

    assert(capture.events.empty());
    assert(lv_shim_pending_async_calls() == 3);
    assert(lv_shim_wake_count() == 3);
    assert(lv_shim_run_one_async());
    assert(capture.events.size() == 1);
    assert(capture.events[0].text == "one");
    assert(capture.events[0].modifier == 1);
    assert(lv_shim_pending_async_calls() == 2);
    drain_async();
    assert(capture.events.size() == 3);
    assert(capture.events[1].text == "two");
    assert(capture.events[2].key == 7);
    assert(lv_shim_wake_count() == 3);
    dispatcher.stop();
}

static void test_bounded_overflow_drops_without_duplicate_delivery()
{
    reset_host();
    capture capture;
    cyberdeck_keyboard_dispatch::dispatcher dispatcher;
    assert(dispatcher.start(receive, &capture));
    for (std::uint32_t key = 1; key <= 9; ++key)
        dispatcher.submit(nullptr, 0, 0, key);

    assert(capture.events.empty());
    assert(lv_shim_pending_async_calls() == 8);
    assert(lv_shim_wake_count() == 8);
    drain_async();
    assert(capture.events.size() == 8);
    for (std::size_t i = 0; i < capture.events.size(); ++i)
        assert(capture.events[i].key == i + 1);
    dispatcher.stop();
}

static void test_schedule_failure_rolls_back_only_rejected_event()
{
    reset_host();
    capture capture;
    cyberdeck_keyboard_dispatch::dispatcher dispatcher;
    assert(dispatcher.start(receive, &capture));
    dispatcher.submit(nullptr, 0, 0, 11);
    lv_shim_set_next_async_result(LV_RESULT_INVALID);
    dispatcher.submit(nullptr, 0, 0, 12);

    assert(lv_shim_pending_async_calls() == 1);
    assert(lv_shim_wake_count() == 1);
    drain_async();
    assert(capture.events.size() == 1);
    assert(capture.events[0].key == 11);
    dispatcher.stop();
}

static void test_stop_releases_pending_events_once()
{
    reset_host();
    capture capture;
    cyberdeck_keyboard_dispatch::dispatcher dispatcher;
    assert(dispatcher.start(receive, &capture));
    for (std::uint32_t key = 1; key <= 8; ++key)
        dispatcher.submit(nullptr, 0, 0, key);
    dispatcher.stop();
    drain_async();
    assert(capture.events.empty());
}

static void test_invalidation_discards_old_generation_and_delivers_new_once()
{
    reset_host();
    capture capture;
    cyberdeck_keyboard_dispatch::dispatcher dispatcher;
    assert(dispatcher.start(receive, &capture));
    dispatcher.submit(nullptr, 0, 0, 13);
    assert(dispatcher.invalidate_pending());
    dispatcher.submit(nullptr, 0, 0, 14);
    drain_async();
    assert(capture.events.size() == 1);
    assert(capture.events[0].key == 14);
    dispatcher.stop();
}

static void test_repeated_invalidation_and_reopen_do_not_retarget_stale_event()
{
    reset_host();
    capture capture;
    cyberdeck_keyboard_dispatch::dispatcher dispatcher;
    assert(dispatcher.start(receive, &capture));
    dispatcher.submit(nullptr, 0, 0, 21);
    assert(dispatcher.invalidate_pending());
    assert(dispatcher.invalidate_pending());
    drain_async();
    dispatcher.submit(nullptr, 0, 0, 22);
    drain_async();
    assert(capture.events.size() == 1);
    assert(capture.events[0].key == 22);
    dispatcher.stop();
}

static void test_callback_from_stopped_generation_cannot_consume_reopened_event()
{
    reset_host();
    capture old_session;
    capture reopened_session;
    cyberdeck_keyboard_dispatch::dispatcher dispatcher;
    assert(dispatcher.start(receive, &old_session));
    dispatcher.submit(nullptr, 0, 0, 31);
    assert(lv_shim_pending_async_calls() == 1);

    dispatcher.stop();
    assert(old_session.events.empty());
    assert(dispatcher.start(receive, &reopened_session));
    dispatcher.submit(nullptr, 0, 0, 32);

    // The first scheduled callback belongs to the stopped lifecycle. It must
    // not consume an event submitted after reopening.
    assert(lv_shim_run_one_async());
    assert(old_session.events.empty());
    assert(reopened_session.events.empty());
    assert(lv_shim_pending_async_calls() == 1);
    assert(lv_shim_run_one_async());
    assert(old_session.events.empty());
    assert(reopened_session.events.size() == 1);
    assert(reopened_session.events[0].key == 32);
    dispatcher.stop();
}

static void test_submit_invalidation_linearization_orders()
{
    // Submit-before-invalidate: the queued event belongs to the old generation
    // and must be discarded, never rerouted to the current handler.
    reset_host();
    capture old_session, reopened_session;
    cyberdeck_keyboard_dispatch::dispatcher dispatcher;
    assert(dispatcher.start(receive, &old_session));
    dispatcher.submit(nullptr, 0, 0, 41);
    assert(dispatcher.invalidate_pending());
    assert(dispatcher.start(receive, &reopened_session));
    dispatcher.submit(nullptr, 0, 0, 42);
    drain_async();
    assert(old_session.events.empty());
    assert(reopened_session.events.size() == 1);
    assert(reopened_session.events[0].key == 42);
    dispatcher.stop();

    // Invalidate-before-submit: the event is stamped with the new generation
    // and is delivered exactly once to its intended route.
    reset_host();
    capture current_session;
    assert(dispatcher.start(receive, &old_session));
    assert(dispatcher.invalidate_pending());
    dispatcher.submit(nullptr, 0, 0, 43);
    drain_async();
    assert(old_session.events.size() == 1);
    assert(old_session.events[0].key == 43);
    assert(current_session.events.empty());
    dispatcher.stop();
}

static void test_claimed_callback_stop_reopen_keeps_session_ownership()
{
    reset_host();
    gated_capture old_session;
    capture reopened_session;
    cyberdeck_keyboard_dispatch::dispatcher dispatcher;
    assert(dispatcher.start(receive_gated, &old_session));
    dispatcher.submit(nullptr, 0, 0, 51);

    std::thread callback([] { assert(lv_shim_run_one_async()); });
    {
        std::unique_lock<std::mutex> lock(old_session.mutex);
        old_session.changed.wait(lock, [&] { return old_session.entered; });
    }
    dispatcher.stop();
    assert(dispatcher.start(receive, &reopened_session));
    dispatcher.submit(nullptr, 0, 0, 52);
    assert(lv_shim_pending_async_calls() == 1);
    {
        std::lock_guard<std::mutex> lock(old_session.mutex);
        old_session.release = true;
    }
    old_session.changed.notify_all();
    callback.join();

    assert(old_session.events.size() == 1);
    assert(old_session.events[0].key == 51);
    assert(reopened_session.events.empty());
    drain_async();
    assert(reopened_session.events.size() == 1);
    assert(reopened_session.events[0].key == 52);
    dispatcher.stop();
}

static void test_duplicate_callback_cannot_claim_a_reused_ticket()
{
    reset_host();
    capture capture;
    cyberdeck_keyboard_dispatch::dispatcher dispatcher;
    assert(dispatcher.start(receive, &capture));
    dispatcher.submit(nullptr, 0, 0, 61);
    auto &calls = lv_shim_async_calls();
    assert(calls.size() == 1);
    const auto stale = calls.front();
    assert(lv_shim_run_one_async());

    dispatcher.submit(nullptr, 0, 0, 62);
    assert(lv_shim_pending_async_calls() == 1);
    // Simulate duplicate invocation of the already completed LVGL callback.
    stale.callback(stale.parameter);
    assert(capture.events.size() == 1);
    assert(capture.events[0].key == 61);
    assert(lv_shim_pending_async_calls() == 1);
    assert(lv_shim_run_one_async());
    assert(capture.events.size() == 2);
    assert(capture.events[1].key == 62);
    dispatcher.stop();
}

static void test_ticket_exhaustion_fails_closed_until_stale_callbacks_retire()
{
    reset_host();
    capture capture;
    cyberdeck_keyboard_dispatch::dispatcher dispatcher;
    assert(dispatcher.start(receive, &capture));
    for (std::uint32_t key = 1; key <= 8; ++key)
        dispatcher.submit(nullptr, 0, 0, key);
    assert(lv_shim_pending_async_calls() == 8);
    dispatcher.stop();

    assert(dispatcher.start(receive, &capture));
    dispatcher.submit(nullptr, 0, 0, 99);
    assert(lv_shim_pending_async_calls() == 8);
    assert(capture.events.empty());
    drain_async();
    dispatcher.submit(nullptr, 0, 0, 100);
    assert(lv_shim_pending_async_calls() == 1);
    drain_async();
    assert(capture.events.size() == 1);
    assert(capture.events[0].key == 100);
    dispatcher.stop();
}

static void test_ticket_identity_uintptr_limit_fails_closed_without_reuse()
{
    reset_host();
    capture capture;
    cyberdeck_keyboard_dispatch::dispatcher dispatcher;
    cyberdeck_keyboard_dispatch::seed_ticket_identity_for_test(UINTPTR_MAX);
    assert(dispatcher.start(receive, &capture));

    dispatcher.submit(nullptr, 0, 0, 71);
    assert(lv_shim_pending_async_calls() == 1);
    auto snapshot = cyberdeck_keyboard_dispatch::snapshot_for_test();
    assert(snapshot.first_in_use);
    assert(snapshot.first_identity == UINTPTR_MAX);
    assert(snapshot.identity_exhausted);
    assert(snapshot.queue_active);
    assert(!snapshot.queue_empty);

    auto &calls = lv_shim_async_calls();
    const auto last_admissible = calls.front();
    dispatcher.submit(nullptr, 0, 0, 72);
    assert(lv_shim_pending_async_calls() == 1);
    snapshot = cyberdeck_keyboard_dispatch::snapshot_for_test();
    assert(!snapshot.queue_empty);
    assert(snapshot.first_identity == UINTPTR_MAX);

    assert(lv_shim_run_one_async());
    assert(capture.events.size() == 1 && capture.events[0].key == 71);
    snapshot = cyberdeck_keyboard_dispatch::snapshot_for_test();
    assert(!snapshot.first_in_use);
    assert(snapshot.queue_empty);

    dispatcher.submit(nullptr, 0, 0, 73);
    assert(lv_shim_pending_async_calls() == 0);
    snapshot = cyberdeck_keyboard_dispatch::snapshot_for_test();
    assert(snapshot.queue_empty);
    assert(snapshot.active_tickets == 0);
    last_admissible.callback(last_admissible.parameter);
    assert(capture.events.size() == 1);
    assert(cyberdeck_keyboard_dispatch::snapshot_for_test().queue_empty);
    dispatcher.stop();
    assert(!cyberdeck_keyboard_dispatch::snapshot_for_test().queue_active);
}

int main()
{
    test_callbacks_are_scheduled_and_fifo();
    test_bounded_overflow_drops_without_duplicate_delivery();
    test_schedule_failure_rolls_back_only_rejected_event();
    test_stop_releases_pending_events_once();
    test_invalidation_discards_old_generation_and_delivers_new_once();
    test_repeated_invalidation_and_reopen_do_not_retarget_stale_event();
    test_callback_from_stopped_generation_cannot_consume_reopened_event();
    test_submit_invalidation_linearization_orders();
    test_claimed_callback_stop_reopen_keeps_session_ownership();
    test_duplicate_callback_cannot_claim_a_reused_ticket();
    test_ticket_exhaustion_fails_closed_until_stale_callbacks_retire();
    test_ticket_identity_uintptr_limit_fails_closed_without_reuse();
    std::cout << "keyboard dispatch latency tests passed\n";
}
