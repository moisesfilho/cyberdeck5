#include "apps/music/cyberdeck_music_trace.h"

#include <algorithm>

namespace cyberdeck_music {
namespace {
constexpr std::uint32_t k_ticket_limit = 0x3fffffffU;
#if defined(CYBERDECK_MUSIC_TRACE_TESTING)
trace_recorder::test_hook s_test_hook = nullptr;
void *s_test_context = nullptr;
void run_test_hook(trace_recorder::test_stage stage) {
    if (s_test_hook != nullptr) s_test_hook(stage, s_test_context);
}
#endif
}

#if defined(CYBERDECK_MUSIC_TRACE_TESTING)
void trace_recorder::set_test_hook(test_hook hook, void *context) {
    s_test_context = context;
    s_test_hook = hook;
}
#endif

void trace_recorder::record(std::uint64_t timestamp_us, std::uint32_t generation, trace_kind kind,
                            std::int32_t result, std::uint32_t duration_us, std::uint32_t bytes) {
    const std::uint32_t ticket = written_.load(std::memory_order_seq_cst);
    if (ticket >= k_ticket_limit) {
        const auto lost = lost_.load(std::memory_order_seq_cst);
        if (lost != UINT32_MAX) lost_.store(lost + 1U, std::memory_order_seq_cst);
        return;
    }
    written_.store(ticket + 1U, std::memory_order_seq_cst);
    if (ticket >= k_trace_capacity) {
        const auto lost = lost_.load(std::memory_order_seq_cst);
        if (lost != UINT32_MAX) lost_.store(lost + 1U, std::memory_order_seq_cst);
    }
    slot &target = slots_[ticket % k_trace_capacity];
    const std::uint32_t committed = ticket * 2U + 2U;
    target.publication.store(committed - 1U, std::memory_order_seq_cst);
#if defined(CYBERDECK_MUSIC_TRACE_TESTING)
    run_test_hook(test_stage::publication_started);
#endif
    target.time_low.store(static_cast<std::uint32_t>(timestamp_us), std::memory_order_seq_cst);
    target.time_high.store(static_cast<std::uint32_t>(timestamp_us >> 32U), std::memory_order_seq_cst);
    target.generation.store(generation, std::memory_order_seq_cst);
    target.kind.store(static_cast<std::uint32_t>(kind), std::memory_order_seq_cst);
    target.result.store(static_cast<std::uint32_t>(result), std::memory_order_seq_cst);
    target.duration_us.store(duration_us, std::memory_order_seq_cst);
    target.bytes.store(bytes, std::memory_order_seq_cst);
#if defined(CYBERDECK_MUSIC_TRACE_TESTING)
    run_test_hook(test_stage::fields_written);
#endif
    target.publication.store(committed, std::memory_order_seq_cst);
#if defined(CYBERDECK_MUSIC_TRACE_TESTING)
    run_test_hook(test_stage::publication_finished);
#endif
}

void trace_recorder::snapshot(std::uint32_t cursor, std::uint32_t limit, trace_page &out) const {
    out = {};
    limit = std::min<std::uint32_t>(limit, k_trace_page_capacity);
    const std::uint32_t end = written_.load(std::memory_order_seq_cst);
    const std::uint32_t oldest = end > k_trace_capacity ? end - k_trace_capacity : 0;
    if (cursor < oldest) {
        cursor = oldest;
        out.truncated = true;
    }
    if (cursor > end) cursor = end;
    out.cursor = cursor;
    const std::uint32_t stop = cursor + std::min(limit, end - cursor);
    for (std::uint32_t ticket = cursor; ticket != stop; ++ticket) {
        const slot &source = slots_[ticket % k_trace_capacity];
        const std::uint32_t expected = ticket * 2U + 2U;
        const auto before = source.publication.load(std::memory_order_seq_cst);
        if (before != expected) {
            ++out.lost;
            out.truncated = true;
            continue;
        }
        trace_event event{};
        event.sequence = ticket;
        const auto low = source.time_low.load(std::memory_order_seq_cst);
        const auto high = source.time_high.load(std::memory_order_seq_cst);
        event.timestamp_us = (std::uint64_t(high) << 32U) | low;
        event.generation = source.generation.load(std::memory_order_seq_cst);
        event.kind = static_cast<trace_kind>(source.kind.load(std::memory_order_seq_cst));
        event.result = static_cast<std::int32_t>(source.result.load(std::memory_order_seq_cst));
        event.duration_us = source.duration_us.load(std::memory_order_seq_cst);
        event.bytes = source.bytes.load(std::memory_order_seq_cst);
        if (source.publication.load(std::memory_order_seq_cst) != before) {
            ++out.lost;
            out.truncated = true;
            continue;
        }
        out.events[out.count++] = event;
    }
    out.next = stop;
    const auto overwritten = lost_.load(std::memory_order_seq_cst);
    out.lost = out.lost > UINT32_MAX - overwritten ? UINT32_MAX : overwritten + out.lost;
}
}
