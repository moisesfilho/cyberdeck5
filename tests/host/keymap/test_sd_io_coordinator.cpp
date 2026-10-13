#include "platform/storage/cyberdeck_sd_io_coordinator.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using namespace std::chrono_literals;
using cyberdeck_sd_io::access;
using cyberdeck_sd_io::coordinator;
using cyberdeck_sd_io::drain_result;
using cyberdeck_sd_io::gate_state;

namespace {
int failures = 0;
#define CHECK(condition) do { if (!(condition)) { ++failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (false)

static_assert(!std::is_copy_constructible_v<cyberdeck_sd_io::lease>);
static_assert(!std::is_copy_assignable_v<cyberdeck_sd_io::lease>);
static_assert(std::is_move_constructible_v<cyberdeck_sd_io::lease>);
static_assert(std::is_move_assignable_v<cyberdeck_sd_io::lease>);

void test_move_assignment_transfers_and_releases_exactly_once()
{
    coordinator gate;
    auto source = gate.acquire(access::read);
    cyberdeck_sd_io::lease empty;
    CHECK(source);
    CHECK(!empty);
    CHECK(gate.active_leases() == 1);

    empty = std::move(source);
    CHECK(!source);
    CHECK(empty);
    CHECK(gate.active_leases() == 1);

    auto replacement = gate.acquire(access::write);
    CHECK(replacement);
    CHECK(gate.active_leases() == 2);
    empty = std::move(replacement);
    CHECK(!replacement);
    CHECK(empty);
    CHECK(gate.active_leases() == 1);

    auto &self = empty;
    empty = std::move(self);
    CHECK(empty);
    CHECK(gate.active_leases() == 1);
    empty.release();
    CHECK(gate.active_leases() == 0);
}

void test_raii_move_and_idempotent_release()
{
    coordinator gate;
    {
        auto original = gate.acquire(access::read);
        CHECK(original);
        CHECK(gate.active_leases() == 1);
        auto moved = std::move(original);
        CHECK(!original);
        CHECK(moved);
        moved.release();
        moved.release();
        CHECK(!moved);
        CHECK(gate.active_leases() == 0);
    }
    auto writer = gate.acquire(access::write);
    CHECK(writer);
    CHECK(gate.active_leases() == 1);
}

void test_close_blocks_admission_and_owner_controls_reopen()
{
    coordinator gate;
    auto lease = gate.acquire(access::write);
    const auto owner = gate.begin_close();
    CHECK(owner != 0);
    CHECK(gate.state() == gate_state::closing);
    CHECK(!gate.acquire(access::read));
    CHECK(gate.drain(owner + 1, std::chrono::steady_clock::now()) == drain_result::not_owner);
    CHECK(!gate.reopen(owner + 1));
    CHECK(gate.reopen(owner));
    CHECK(gate.state() == gate_state::open);
    CHECK(gate.acquire(access::read));
}

void test_drain_deadline_and_timeout_preserve_active_lease()
{
    coordinator gate;
    auto lease = gate.acquire(access::read);
    const auto owner = gate.begin_close();
    const auto started = std::chrono::steady_clock::now();
    CHECK(gate.drain(owner, started + 25ms) == drain_result::timed_out);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    CHECK(elapsed < 500ms);
    CHECK(gate.state() == gate_state::closing);
    CHECK(gate.active_leases() == 1);
    CHECK(lease);
    CHECK(!gate.acquire(access::write));
    lease.release();
    CHECK(gate.drain(owner, std::chrono::steady_clock::now() + 100ms) == drain_result::drained);
    CHECK(gate.state() == gate_state::closed);
    CHECK(!gate.acquire(access::read));
    CHECK(!gate.reopen(owner));
}

void test_drain_waits_for_release_and_wrong_owner_is_rejected()
{
    coordinator gate;
    auto lease = gate.acquire(access::write);
    const auto owner = gate.begin_close();
    std::atomic<drain_result> result{drain_result::not_owner};
    std::thread waiter([&] { result.store(gate.drain(owner, std::chrono::steady_clock::now() + 1s)); });
    std::this_thread::sleep_for(10ms);
    CHECK(result.load() == drain_result::not_owner);
    CHECK(!gate.reopen(owner + 1));
    lease.release();
    waiter.join();
    CHECK(result.load() == drain_result::drained);
    CHECK(gate.active_leases() == 0);
}

void test_concurrent_read_and_write_leases_account_for_all_users()
{
    coordinator gate;
    auto reader = gate.acquire(access::read);
    auto writer_while_reading = gate.acquire(access::write);
    CHECK(reader);
    CHECK(writer_while_reading);
    CHECK(gate.active_leases() == 2);

    const auto owner = gate.begin_close();
    CHECK(owner != 0);
    CHECK(!gate.acquire(access::read));
    CHECK(!gate.acquire(access::write));

    std::atomic<bool> drain_finished{false};
    std::atomic<drain_result> drain_status{drain_result::not_owner};
    std::thread drain_waiter([&] {
        drain_status.store(gate.drain(owner, std::chrono::steady_clock::now() + 1s));
        drain_finished.store(true);
    });
    std::this_thread::sleep_for(10ms);
    CHECK(!drain_finished.load());
    writer_while_reading.release();
    std::this_thread::sleep_for(10ms);
    CHECK(!drain_finished.load());
    CHECK(gate.active_leases() == 1);
    reader.release();
    drain_waiter.join();
    CHECK(drain_finished.load());
    CHECK(drain_status.load() == drain_result::drained);
    CHECK(gate.active_leases() == 0);
    CHECK(gate.state() == gate_state::closed);

    coordinator concurrent_gate;
    std::atomic<bool> start{false};
    std::atomic<int> acquired{0};
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i) {
        workers.emplace_back([&, i] {
            while (!start.load()) std::this_thread::yield();
            auto lease = concurrent_gate.acquire(i % 2 ? access::write : access::read);
            if (lease) ++acquired;
            std::this_thread::yield();
        });
    }
    start = true;
    for (auto &worker : workers) worker.join();
    CHECK(acquired == 8);
    CHECK(concurrent_gate.active_leases() == 0);
    const auto concurrent_owner = concurrent_gate.begin_close();
    CHECK(concurrent_gate.drain(concurrent_owner, std::chrono::steady_clock::now() + 50ms) == drain_result::drained);
}
}

int main()
{
    {
        CHECK(!cyberdeck_sd_io::acquire_global(access::read));
        CHECK(cyberdeck_sd_io::global_fenced());
        cyberdeck_sd_io::record_global_persistence_failure();
        static coordinator global;
        cyberdeck_sd_io::bind_global(global);
        CHECK(!cyberdeck_sd_io::global_fenced());
        CHECK(!global.persistence_failed());
        auto admitted = cyberdeck_sd_io::acquire_global(access::write);
        CHECK(admitted);
        const auto owner = global.begin_close();
        CHECK(cyberdeck_sd_io::global_fenced());
        CHECK(!cyberdeck_sd_io::acquire_global(access::read));
        cyberdeck_sd_io::record_global_persistence_failure();
        CHECK(global.persistence_failed());
        admitted.release();
        CHECK(global.drain(owner, std::chrono::steady_clock::now()) == drain_result::drained);
        CHECK(global.persistence_failed());
        static coordinator replacement;
        cyberdeck_sd_io::bind_global(replacement);
        CHECK(!cyberdeck_sd_io::acquire_global(access::write));
        CHECK(replacement.state() == gate_state::open);
    }
    test_raii_move_and_idempotent_release();
    test_move_assignment_transfers_and_releases_exactly_once();
    test_close_blocks_admission_and_owner_controls_reopen();
    test_drain_deadline_and_timeout_preserve_active_lease();
    test_drain_waits_for_release_and_wrong_owner_is_rejected();
    test_concurrent_read_and_write_leases_account_for_all_users();
    if (failures) return 1;
    std::puts("PASS: cyberdeck_sd_io_coordinator");
    return 0;
}
