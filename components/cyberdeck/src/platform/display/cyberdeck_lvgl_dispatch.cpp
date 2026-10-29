#include "platform/display/cyberdeck_lvgl_dispatch.h"
#include "platform/display/cyberdeck_lvgl_dispatch_state.h"

#include "bsp/esp-bsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"

#include <atomic>
#include <mutex>

namespace cyberdeck_lvgl_dispatch {
namespace {

using cyberdeck_lvgl_dispatch_state::AdmissionOwner;

StaticSemaphore_t s_admission_storage;
StaticSemaphore_t s_done_storage;
SemaphoreHandle_t s_admission = nullptr;
SemaphoreHandle_t s_done = nullptr;
std::once_flag s_once;
cyberdeck_lvgl_dispatch_state::Job s_job;
std::atomic<std::uint32_t> s_generation{0};

bool initialize()
{
    std::call_once(s_once, [] {
        s_admission = xSemaphoreCreateBinaryStatic(&s_admission_storage);
        if (s_admission != nullptr) xSemaphoreGive(s_admission);
        s_done = xSemaphoreCreateBinaryStatic(&s_done_storage);
    });
    return s_admission != nullptr && s_done != nullptr;
}

unsigned next_token()
{
    unsigned token = static_cast<unsigned>(s_generation.fetch_add(1, std::memory_order_acq_rel)) + 1u;
    while ((token & cyberdeck_lvgl_dispatch_state::k_token_mask) == 0u) {
        token = static_cast<unsigned>(s_generation.fetch_add(1, std::memory_order_acq_rel)) + 1u;
    }
    return token;
}

// Runs on the LVGL task. The payload is read only after start() claims the
// matching token, so a stale callback never observes (nor runs) the payload of
// a newer operation, even when the cancelled slot was re-armed meanwhile.
void dispatch_callback(void *token_ptr)
{
    const auto token = static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(token_ptr));
    if (token == 0u) return;
    if (!cyberdeck_lvgl_dispatch_state::start(s_job, token)) return;
    const cyberdeck_lvgl_dispatch_state::Payload payload =
        cyberdeck_lvgl_dispatch_state::claimed_payload(s_job);
    bool completed = false;
    if (payload.fn != nullptr) {
        payload.fn(payload.ctx);
        completed = true;
    }
    const AdmissionOwner owner = cyberdeck_lvgl_dispatch_state::finish(s_job, completed);
    xSemaphoreGive(s_done);
    if (owner == AdmissionOwner::callback) xSemaphoreGive(s_admission);
}

// Releases admission exactly once for a slot this call could not dispatch.
// owned_job is false when queue() itself was rejected, which means the slot
// belongs to another operation (and its owner performs the only release).
void abandon(bool owned_job)
{
    if (owned_job && cyberdeck_lvgl_dispatch_state::cancel(s_job)) {
        xSemaphoreGive(s_admission);
        return;
    }
    if (cyberdeck_lvgl_dispatch_state::detach_or_observe_done(s_job) == AdmissionOwner::caller) {
        xSemaphoreGive(s_admission);
    }
}

}

bool run(void (*fn)(void *), void *ctx, std::uint32_t timeout_ms)
{
    if (fn == nullptr || ctx == nullptr || timeout_ms == 0 || !initialize()) return false;
    const TickType_t call_started = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(timeout_ms);
    const TickType_t grace_timeout = pdMS_TO_TICKS(timeout_ms + k_detach_grace_ms);
    const auto remaining = [call_started](TickType_t budget) -> TickType_t {
        const TickType_t used = xTaskGetTickCount() - call_started;
        return used >= budget ? 0 : budget - used;
    };

    if (xSemaphoreTake(s_admission, timeout) != pdTRUE) return false;
    while (xSemaphoreTake(s_done, 0) == pdTRUE) {}

    const unsigned token = next_token();
    const cyberdeck_lvgl_dispatch_state::Payload payload{fn, ctx};
    if (!cyberdeck_lvgl_dispatch_state::queue(s_job, token, payload)) {
        abandon(false);
        return false;
    }

    // k_display_lock_timeout_ms, kept literal so the concurrency contract can
    // assert the bounded acquisition.
    if (!bsp_display_lock(pdMS_TO_TICKS(1000))) {
        abandon(true);
        return false;
    }
    const lv_result_t dispatched = lv_async_call(dispatch_callback,
        reinterpret_cast<void *>(static_cast<std::uintptr_t>(token)));
    bsp_display_unlock();
    if (dispatched != LV_RESULT_OK) {
        abandon(true);
        return false;
    }

    while (!cyberdeck_lvgl_dispatch_state::is_done(s_job)) {
        const TickType_t left = remaining(timeout);
        if (left == 0) break;
        if (xSemaphoreTake(s_done, left) != pdTRUE) break;
    }
    if (!cyberdeck_lvgl_dispatch_state::is_done(s_job)) {
        if (cyberdeck_lvgl_dispatch_state::cancel(s_job)) {
            xSemaphoreGive(s_admission);
            return false;
        }
        while (!cyberdeck_lvgl_dispatch_state::is_done(s_job)) {
            const TickType_t left = remaining(grace_timeout);
            if (left == 0) break;
            if (xSemaphoreTake(s_done, left) != pdTRUE) break;
        }
        // Exactly one owner releases admission: either the detached callback
        // does it when it finishes, or the caller does it right below.
        if (cyberdeck_lvgl_dispatch_state::detach_or_observe_done(s_job) == AdmissionOwner::callback) {
            return false;
        }
        const bool success = cyberdeck_lvgl_dispatch_state::is_done(s_job) &&
                             s_job.result.load(std::memory_order_acquire);
        xSemaphoreGive(s_admission);
        return success;
    }
    const bool success = s_job.result.load(std::memory_order_acquire);
    xSemaphoreGive(s_admission);
    return success;
}

}
