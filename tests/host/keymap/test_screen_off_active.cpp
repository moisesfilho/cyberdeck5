struct lv_display_t;
using lv_display_t = struct lv_display_t;

#include "platform/display/screen_off.h"

#include <atomic>
#include <cstdio>
#include <thread>

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

void test_active_snapshot_tracks_published_state()
{
    std::atomic<bool> active{false};
    CHECK(!cyberdeck_screen_off_detail::active_snapshot(active));
    cyberdeck_screen_off_detail::publish_active(active, true);
    CHECK(cyberdeck_screen_off_detail::active_snapshot(active));
    cyberdeck_screen_off_detail::publish_active(active, false);
    CHECK(!cyberdeck_screen_off_detail::active_snapshot(active));
}

void test_concurrent_active_access()
{
    std::atomic<bool> active{false};
    std::atomic<unsigned> reads{0};
    std::atomic<unsigned> ready{0};
    std::atomic<bool> start{false};
    std::thread publisher([&] {
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (unsigned i = 0; i < 200000; ++i) {
            cyberdeck_screen_off_detail::publish_active(active, (i & 1U) != 0);
        }
    });
    std::thread observer([&] {
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (unsigned i = 0; i < 200000; ++i) {
            const unsigned weight =
                cyberdeck_screen_off_detail::active_snapshot(active) ? 2U : 1U;
            reads.fetch_add(weight, std::memory_order_relaxed);
        }
    });
    while (ready.load(std::memory_order_acquire) != 2) std::this_thread::yield();
    start.store(true, std::memory_order_release);
    publisher.join();
    observer.join();
    CHECK(reads.load(std::memory_order_relaxed) >= 200000);
    CHECK(reads.load(std::memory_order_relaxed) <= 400000);
    CHECK(cyberdeck_screen_off_detail::active_snapshot(active));
}

} // namespace

int main()
{
    test_active_snapshot_tracks_published_state();
    test_concurrent_active_access();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
