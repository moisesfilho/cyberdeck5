#include "platform/power/cyberdeck_poweroff.h"
#include <array>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <sys/stat.h>
#include <vector>
#include <unistd.h>

using namespace cyberdeck_poweroff;

namespace {
constexpr std::array<stage, 15> sequence{stage::ui, stage::fence, stage::serial, stage::screen_nvs, stage::cat, stage::audit,
    stage::time_sync, stage::ssh, stage::http, stage::ble, stage::wifi, stage::drain, stage::log, stage::storage, stage::signal};

struct fake_ports final : ports {
    std::uint64_t clock = 0;
    stage fail = stage::idle;
    stage delayed = stage::idle;
    stage cancellation = stage::idle;
    controller *owner = nullptr;
    std::vector<stage> calls;
    std::vector<std::uint32_t> budgets;
    std::uint64_t now_ms() const override { return clock; }
    bool run(stage step, std::uint32_t remaining) override {
        calls.push_back(step);
        budgets.push_back(remaining);
        clock += step == delayed ? 1000 : 1;
        if (step == cancellation && owner != nullptr) assert(owner->expire());
        return step != fail;
    }
};

struct fake_signal final : signal_ports {
    bool configured = true;
    int failure = -1;
    std::vector<bool> levels;
    std::vector<std::uint32_t> delays;
    bool configure() override { return configured; }
    bool set_level(bool high) override {
        levels.push_back(high);
        return static_cast<int>(levels.size()) != failure;
    }
    void delay_ms(std::uint32_t ms) override { delays.push_back(ms); }
};

bool fail_write = false, fail_sync = false, fail_close = false;
std::vector<std::string> file_calls;
}

extern "C" ssize_t __real_write(int, const void *, size_t);
extern "C" int __real_fsync(int);
extern "C" int __real_close(int);
extern "C" ssize_t __wrap_write(int fd, const void *data, size_t count) {
    file_calls.emplace_back("write");
    if (fail_write) { errno = EIO; return -1; }
    return __real_write(fd, data, count);
}
extern "C" int __wrap_fsync(int fd) {
    file_calls.emplace_back("fsync");
    if (fail_sync) { errno = EIO; return -1; }
    return __real_fsync(fd);
}
extern "C" int __wrap_close(int fd) {
    file_calls.emplace_back("close");
    const int closed = __real_close(fd);
    if (fail_close) { errno = EIO; return -1; }
    return closed;
}

int main() {
    {
        unsigned probes = 0;
        unsigned records = 0;
        task_create_heap_snapshot observed{};
        observe_task_create_failure(
            [&] {
                ++probes;
                 return task_create_heap_snapshot{123456, 32768, 65432, 16384};
            },
            [&](task_create_heap_snapshot snapshot) {
                ++records;
                observed = snapshot;
            });
        assert(probes == 1);
        assert(records == 1);
        assert(observed.free_bytes == 123456);
        assert(observed.largest_block_bytes == 32768);
        assert(observed.internal_free_bytes == 65432);
        assert(observed.internal_largest_block_bytes == 16384);
    }
    {
        controller operation;
        fake_ports backend;
        assert(operation.request());
        assert(operation.expire());
        assert(operation.status() == state::failed);
        assert(operation.last_stage() == stage::idle);
        assert(!operation.request());
        assert(!operation.execute(backend, 45000));
        assert(backend.calls.empty());
    }
    assert(mkdir("/tmp/opencode", 0700) == 0 || errno == EEXIST);
    {
        controller operation;
        fake_ports backend;
        assert(!operation.execute(backend, 100));
        assert(operation.request());
        assert(!operation.request());
        assert(operation.execute(backend, 100));
        assert(operation.status() == state::signal_sent);
        assert(backend.calls == std::vector<stage>(sequence.begin(), sequence.end()));
        for (size_t i = 0; i < sequence.size(); ++i) {
            assert(backend.budgets[i] == 100 - i);
            assert(std::string(stage_name(sequence[i])) != "invalid");
        }
        assert(!operation.execute(backend, 100));
        assert(!operation.request());
    }
    for (size_t index = 0; index < sequence.size(); ++index) {
        controller operation;
        fake_ports backend;
        backend.fail = sequence[index];
        assert(operation.request());
        assert(!operation.execute(backend, 100));
        assert(operation.status() == state::failed);
        assert(operation.last_stage() == sequence[index]);
        assert(backend.calls.size() == index + 1);
        assert(!operation.request());
    }
    for (auto step : sequence) {
        if (step == stage::signal) continue;
        controller operation;
        fake_ports backend;
        backend.delayed = step;
        assert(operation.request());
        assert(!operation.execute(backend, 100));
        assert(operation.last_stage() == step);
        assert(backend.calls.back() != stage::signal);
    }
    for (auto step : sequence) {
        if (step == stage::signal) continue;
        controller operation;
        fake_ports backend;
        backend.cancellation = step;
        backend.owner = &operation;
        assert(operation.request());
        assert(!operation.execute(backend, 100));
        assert(backend.calls.back() == step);
        assert(operation.status() == state::failed);
    }
    {
        controller operation;
        fake_ports backend;
        assert(operation.request());
        assert(!operation.execute(backend, 0));
        assert(backend.calls.empty());
    }
    {
        controller operation;
        fake_ports backend;
        assert(!operation.expire());
        assert(operation.request());
        assert(operation.expire());
        assert(!operation.expire());
        assert(!operation.execute(backend, 100));
        assert(backend.calls.empty());
        assert(!operation.request());
    }
    {
        controller operation;
        std::atomic<unsigned> winners{0};
        std::vector<std::thread> callers;
        for (unsigned i = 0; i < 8; ++i) callers.emplace_back([&] { if (operation.request()) ++winners; });
        for (auto &caller : callers) caller.join();
        assert(winners == 1);
    }
    {
        fake_signal backend;
        assert(emit_factory_signal(backend));
        assert(backend.levels == std::vector<bool>({true, false, true, false, true, false}));
        assert(backend.delays == std::vector<std::uint32_t>(6, 100));
        backend = {};
        backend.configured = false;
        assert(!emit_factory_signal(backend));
        assert(backend.levels.empty());
        for (int i = 1; i <= 6; ++i) {
            backend = {};
            backend.failure = i;
            assert(!emit_factory_signal(backend));
            assert(backend.levels.size() == static_cast<size_t>(i));
            assert(backend.delays.size() == static_cast<size_t>(i - 1));
        }
    }
    char root[] = "/tmp/opencode/poweroff-test-XXXXXX";
    assert(mkdtemp(root) != nullptr);
    const std::string path = std::string(root) + "/.poweroff-sync";
    for (unsigned failure = 0; failure < 4; ++failure) {
        file_calls.clear();
        fail_write = failure == 1;
        fail_sync = failure == 2;
        fail_close = failure == 3;
        assert(flush_metadata(path.c_str()) == (failure == 0));
        assert(file_calls == std::vector<std::string>({"write", "fsync", "close"}));
    }
    fail_write = fail_sync = fail_close = false;
    assert(!flush_metadata(nullptr));
    assert(!flush_metadata((path + "/nonexistent").c_str()));
    assert(unlink(path.c_str()) == 0);
    assert(rmdir(root) == 0);
    std::puts("PASS: poweroff order, per-stage errors/deadlines, one-shot admission, metadata barrier and factory pulse");
}
