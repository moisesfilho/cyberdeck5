#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace cyberdeck_music {
constexpr std::size_t k_trace_capacity = 64;
constexpr std::size_t k_trace_page_capacity = 16;

enum class trace_kind : std::uint32_t { request_start, request_retire, sd_read, decode, codec_start, output_write, failure };

struct trace_event {
    std::uint32_t sequence = 0;
    std::uint64_t timestamp_us = 0;
    std::uint32_t generation = 0;
    trace_kind kind = trace_kind::request_start;
    std::int32_t result = 0;
    std::uint32_t duration_us = 0;
    std::uint32_t bytes = 0;
};

struct trace_page {
    trace_event events[k_trace_page_capacity]{};
    std::uint32_t count = 0;
    std::uint32_t cursor = 0;
    std::uint32_t next = 0;
    std::uint32_t lost = 0;
    bool truncated = false;
};

class trace_recorder {
  public:
    void record(std::uint64_t timestamp_us, std::uint32_t generation, trace_kind kind,
                std::int32_t result = 0, std::uint32_t duration_us = 0, std::uint32_t bytes = 0);
    void snapshot(std::uint32_t cursor, std::uint32_t limit, trace_page &out) const;
#if defined(CYBERDECK_MUSIC_TRACE_TESTING)
    enum class test_stage { publication_started, fields_written, publication_finished };
    using test_hook = void (*)(test_stage, void *);
    static void set_test_hook(test_hook hook, void *context);
#endif
  private:
    struct slot {
        std::atomic<std::uint32_t> publication{0};
        std::atomic<std::uint32_t> time_low{0}, time_high{0}, generation{0}, kind{0};
        std::atomic<std::uint32_t> result{0}, duration_us{0}, bytes{0};
    };
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "audio trace requires lock-free 32-bit atomics");
    slot slots_[k_trace_capacity]{};
    std::atomic<std::uint32_t> written_{0};
    std::atomic<std::uint32_t> lost_{0};
};
static_assert(sizeof(trace_recorder) <= 4096, "audio trace recorder exceeds its memory budget");
static_assert(sizeof(trace_page) <= 4096, "audio trace page exceeds its memory budget");
}
