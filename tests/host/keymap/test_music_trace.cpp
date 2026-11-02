#include "apps/music/cyberdeck_music_trace.h"

#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>

using namespace cyberdeck_music;

static_assert(k_trace_capacity == 64);
static_assert(sizeof(trace_recorder) <= 4096);
static_assert(sizeof(trace_page) <= 4096);

struct publication_gate {
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false;
    bool released = false;
};

static void pause_before_publish(trace_recorder::test_stage stage, void *context) {
    if (stage != trace_recorder::test_stage::fields_written) return;
    auto &gate = *static_cast<publication_gate *>(context);
    std::unique_lock<std::mutex> lock(gate.mutex);
    gate.entered = true;
    gate.cv.notify_all();
    gate.cv.wait(lock, [&] { return gate.released; });
}

int main() {
    trace_recorder recorder;
    assert(sizeof(recorder) <= 4096);
    trace_page page{};
    recorder.snapshot(0, 16, page);
    assert(page.count == 0 && page.next == 0 && !page.truncated);
    for (std::uint32_t i = 0; i < 70; ++i)
        recorder.record(0x100000000ULL + i, i / 3, trace_kind::decode, static_cast<std::int32_t>(i), i + 1, i * 2);
    recorder.snapshot(0, 16, page);
    assert(page.count == 16 && page.events[0].sequence == 6 && page.events[0].timestamp_us == 0x100000006ULL);
    assert(page.events[0].generation == 2 && page.events[0].result == 6 && page.events[0].bytes == 12);
    assert(page.next == 22 && page.truncated && page.lost == 6);
    recorder.snapshot(page.next, 16, page);
    assert(page.count == 16 && page.events[0].sequence == 22 && page.next == 38);
    recorder.snapshot(UINT32_MAX, UINT32_MAX, page);
    assert(page.count == 0 && page.cursor == 70 && page.next == 70);
    recorder.snapshot(70, 0, page);
    assert(page.count == 0 && page.cursor == 70 && page.next == 70);

    trace_recorder interleaved;
    publication_gate gate;
    trace_recorder::set_test_hook(pause_before_publish, &gate);
    std::thread blocked_writer([&] {
        interleaved.record(0x1122334455667788ULL, 0xAABBCCDD,
                           trace_kind::codec_start, -1234, 9876, 54321);
    });
    {
        std::unique_lock<std::mutex> lock(gate.mutex);
        gate.cv.wait(lock, [&] { return gate.entered; });
    }
    interleaved.snapshot(0, 16, page);
    assert(page.count == 0 && page.lost == 1 && page.next == 1);
    {
        std::lock_guard<std::mutex> lock(gate.mutex);
        gate.released = true;
    }
    gate.cv.notify_all();
    blocked_writer.join();
    trace_recorder::set_test_hook(nullptr, nullptr);
    interleaved.snapshot(0, 16, page);
    assert(page.count == 1 && page.events[0].sequence == 0);
    const auto &whole = page.events[0];
    assert(whole.timestamp_us == 0x1122334455667788ULL && whole.generation == 0xAABBCCDD);
    assert(whole.kind == trace_kind::codec_start && whole.result == -1234);
    assert(whole.duration_us == 9876 && whole.bytes == 54321);

    trace_recorder concurrent;
    std::thread producer([&] {
        for (std::uint32_t i = 0; i < 100000; ++i)
            concurrent.record(i, i, trace_kind::sd_read, static_cast<std::int32_t>(i), i, i ^ 0xA5A5A5A5U);
    });
    for (unsigned i = 0; i < 5000; ++i) {
        concurrent.snapshot(0, 64, page);
        for (std::uint32_t j = 0; j < page.count; ++j) {
            const auto &e = page.events[j];
            assert(e.generation == e.timestamp_us);
            assert(e.result == static_cast<std::int32_t>(e.generation));
            assert(e.duration_us == e.generation);
            assert(e.bytes == (e.generation ^ 0xA5A5A5A5U));
        }
    }
    producer.join();
    std::cout << "test_music_trace: PASS (recorder=" << sizeof(recorder)
              << " B, page=" << sizeof(trace_page) << " B)\n";
}
