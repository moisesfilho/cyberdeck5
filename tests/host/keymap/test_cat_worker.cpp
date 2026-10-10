#include "apps/shell/cyberdeck_cat_worker.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <chrono>
#include <future>

int main()
{
    cyberdeck_sd_io::coordinator coordinator;
    int calls = 0;
    auto normal_callback = [&] {
        ++calls;
        assert(coordinator.active_leases() == 1);
    };
    const bool completed = cyberdeck_cat_worker::with_read_lease(coordinator, normal_callback);
    assert(completed && calls == 1);
    assert(coordinator.active_leases() == 0);

    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false;
    bool release_callback = false;
    std::thread operation([&] {
        auto blocked_callback = [&] {
            std::unique_lock<std::mutex> lock(mutex);
            entered = true;
            cv.notify_all();
            cv.wait(lock, [&] { return release_callback; });
        };
        assert(cyberdeck_cat_worker::with_read_lease(coordinator, blocked_callback));
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return entered; });
    }
    const auto owner = coordinator.begin_close();
    assert(coordinator.active_leases() == 1);
    auto drain = std::async(std::launch::async, [&] {
        return coordinator.drain(owner, std::chrono::steady_clock::now() + std::chrono::seconds(5));
    });
    assert(drain.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout);
    {
        std::lock_guard<std::mutex> lock(mutex);
        release_callback = true;
    }
    cv.notify_all();
    operation.join();
    assert(drain.get() == cyberdeck_sd_io::drain_result::drained);
    assert(coordinator.active_leases() == 0);

    int denied_calls = 0;
    assert(coordinator.state() == cyberdeck_sd_io::gate_state::closed);
    auto denied_callback = [&] { ++denied_calls; };
    assert(!cyberdeck_cat_worker::with_read_lease(coordinator, denied_callback));
    assert(denied_calls == 0 && coordinator.active_leases() == 0);
    cyberdeck_sd_io::coordinator failure_coordinator;
    try {
        auto throwing_callback = [] { throw 7; };
        cyberdeck_cat_worker::with_read_lease(failure_coordinator, throwing_callback);
        assert(false);
    } catch (int) {
        assert(failure_coordinator.active_leases() == 0);
    }

    const auto root = std::filesystem::temp_directory_path() / "cyberdeck-cat-worker-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "nested");
    std::ofstream(root / "hello.txt") << "hello\nworld\n";
    std::ofstream(root / "nested/value.txt") << "nested\n";
    std::ofstream(root / "empty.txt", std::ios::binary);
    std::ofstream oversized(root / "oversized.txt", std::ios::binary);
    oversized << std::string(12289, 'x');

    const auto read = cyberdeck_cat_worker_process_request(root.c_str(), "/", "cat hello.txt");
    assert(read.status == cyberdeck_local_shell_status::handled);
    assert(read.output == "hello\nworld\n");
    const auto nested = cyberdeck_cat_worker_process_request(root.c_str(), "/nested", "cat value.txt");
    assert(nested.status == cyberdeck_local_shell_status::handled);
    assert(nested.output == "nested\n");
    const auto rejected = cyberdeck_cat_worker_process_request(root.c_str(), "/", "cat ../hello.txt");
    assert(rejected.status != cyberdeck_local_shell_status::handled);
    const auto missing = cyberdeck_cat_worker_process_request(root.c_str(), "/", "cat missing.txt");
    assert(missing.status != cyberdeck_local_shell_status::handled);

    cyberdeck_sd_io::coordinator cat_coordinator;
    cyberdeck_local_shell_result empty{};
    auto empty_request = [&] {
        assert(cat_coordinator.active_leases() == 1);
        empty = cyberdeck_cat_worker_process_request(root.c_str(), "/", "cat empty.txt");
    };
    assert(cyberdeck_cat_worker::with_read_lease(cat_coordinator, empty_request));
    assert(empty.status == cyberdeck_local_shell_status::handled);
    assert(empty.output.empty());
    assert(cat_coordinator.active_leases() == 0);

    cyberdeck_local_shell_result too_large{};
    auto oversized_request = [&] {
        assert(cat_coordinator.active_leases() == 1);
        too_large = cyberdeck_cat_worker_process_request(root.c_str(), "/", "cat oversized.txt");
    };
    assert(cyberdeck_cat_worker::with_read_lease(cat_coordinator, oversized_request));
    assert(too_large.status == cyberdeck_local_shell_status::rejected);
    assert(too_large.output == "cat: file is too large\n");
    assert(cat_coordinator.active_leases() == 0);
    std::filesystem::remove_all(root);
    std::cout << "cat worker seam tests passed\n";
}
