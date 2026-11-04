#include "platform/sensors/cyberdeck_expander_broker.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

int main()
{
    using namespace std::chrono_literals;

    // Exclusion between the app-level owners that share expander B (battery
    // protection, poweroff and, through the BSP, WIFI_EN/USB_EN).
    {
        bool acquired = true;
        {
            cyberdeck_expander_broker::lease owner(10ms);
            assert(owner);
            std::thread contender([&] {
                cyberdeck_expander_broker::lease lease(20ms);
                acquired = static_cast<bool>(lease);
            });
            contender.join();
            assert(!acquired);
        }
        cyberdeck_expander_broker::lease released(10ms);
        assert(released);
    }

    // A failed acquisition is bounded by its timeout and releases nothing: the
    // current owner keeps exclusivity, so consecutive contenders keep failing
    // until that owner's lease ends.
    {
        cyberdeck_expander_broker::lease owner(10ms);
        assert(owner);
        for (int attempt = 0; attempt < 2; ++attempt) {
            bool intruder = true;
            std::thread contender([&] {
                cyberdeck_expander_broker::lease lease(5ms);
                intruder = static_cast<bool>(lease);
            });
            contender.join();
            assert(!intruder);
        }
        bool early_release = false;
        std::thread late([&] {
            cyberdeck_expander_broker::lease lease(5ms);
            early_release = static_cast<bool>(lease);
        });
        late.join();
        assert(!early_release);
    }

    // Ownership returns to the pool only through lease destruction.
    {
        cyberdeck_expander_broker::lease after_release(50ms);
        assert(after_release);
    }

    // Reacquisition from the thread that already owns the lease must fail in a
    // bounded time instead of blocking forever. This is the regression for the
    // battery init path that called write_chg_enable() while holding a lease: on
    // ESP-IDF the non-recursive pthread timed lock reports EBUSY, which maps to
    // ESP_ERR_TIMEOUT, so CHG_EN stayed at the inactive preload for the whole
    // boot. Here the same nested attempt must return false quickly.
    {
        cyberdeck_expander_broker::lease owner(20ms);
        assert(owner);
        const auto begin = std::chrono::steady_clock::now();
        bool nested = true;
        {
            cyberdeck_expander_broker::lease reentrant(100ms);
            nested = static_cast<bool>(reentrant);
        }
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - begin)
                                    .count();
        assert(!nested);
        assert(elapsed_ms < 1000);
        std::printf("INFO reentrant lease from owner thread: acquired=0 wait_ms=%lld\n",
                    static_cast<long long>(elapsed_ms));
    }
    // The owner's lease ended, so the mutex is usable again: a failed nested
    // acquisition must not have left it locked.
    {
        cyberdeck_expander_broker::lease after_reentrant(50ms);
        assert(after_reentrant);
    }

    std::printf("PASS: expander broker exclusion, failed-acquisition release and reentrancy rejection\n");
    return 0;
}
