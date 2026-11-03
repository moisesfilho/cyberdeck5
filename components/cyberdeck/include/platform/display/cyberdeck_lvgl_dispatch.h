#pragma once

#include <cstdint>

namespace cyberdeck_lvgl_dispatch {

// Bounded display-lock acquisition performed before queuing the async call.
inline constexpr std::uint32_t k_display_lock_timeout_ms = 1000;
// Extra wait granted after timeout_ms expired with the callback still running,
// only to return its real result. Correctness does not depend on it: when it
// expires the running callback is detached and releases admission itself.
inline constexpr std::uint32_t k_detach_grace_ms = 250;

// ctx must remain valid until the callback completes, including after run() times out.
// runs fn on the LVGL task. timeout_ms is the single budget shared by the
// admission wait and the completion wait, so one call blocks the caller for at
// most timeout_ms + k_detach_grace_ms of waiting plus at most
// k_display_lock_timeout_ms for the display lock (3250 ms with timeout_ms at
// 2000 ms). A call therefore never blocks the HTTP (503) or Serial-JTAG
// ("display busy") task for two full timeouts plus an unbounded grace.
//
// run() must NOT be called from the LVGL task itself. The callback is queued
// through lv_async_call and only runs on that same task, so a caller that owns
// the LVGL task can never observe completion: it blocks for the whole budget,
// detaches the job and returns false. Callers must live on a worker/HTTP/Serial
// task and hand work to LVGL through this function.
bool run(void (*fn)(void *), void *ctx, std::uint32_t timeout_ms);

}
