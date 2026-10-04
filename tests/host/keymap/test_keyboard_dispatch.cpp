#include "platform/input/cyberdeck_keyboard_dispatch.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

struct event { std::string text; std::uint8_t modifier{}; std::uint32_t key{}; };
struct capture { std::vector<event> events; };

static void receive(const char *text, std::size_t length, std::uint8_t modifier,
                    std::uint32_t key, void *context)
{
    auto &capture = *static_cast<struct capture *>(context);
    capture.events.push_back({std::string(text, length), modifier, key});
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

int main()
{
    test_callbacks_are_scheduled_and_fifo();
    test_bounded_overflow_drops_without_duplicate_delivery();
    test_schedule_failure_rolls_back_only_rejected_event();
    test_stop_releases_pending_events_once();
    std::cout << "keyboard dispatch latency tests passed\n";
}
