#pragma once

#include <atomic>
#include <cstdint>

namespace cyberdeck_lvgl_dispatch_state {

// Kept contiguous 0..7 on purpose: Job::status packs the state in 3 bits, so a
// new state must be inserted before `cancelled` (the production cpp asserts
// that span) instead of appended after it.
enum class State : unsigned char { idle, queued, running, detached, finishing, finishing_detached, done, cancelled };

// Callback payload. It is published by the caller inside queue() (before the
// release CAS that stores State::queued) and is read by the LVGL callback only
// after start() claimed the matching token with an acquiring CAS. A stale
// callback therefore never observes the payload of a newer operation.
struct Payload {
    void (*fn)(void *) = nullptr;
    void *ctx = nullptr;
};

// Who must release the admission slot of the current transition.
enum class AdmissionOwner : unsigned char { caller, callback };

// Tokens are stored in bits 3..31 of Job::status; 0 is reserved as "invalid".
inline constexpr unsigned k_token_mask = 0x1FFFFFFFu;

struct Job {
    // Single atomic word: State in the low 3 bits, token in bits 3..31. Keeping
    // both in one CAS-able word is what makes a stale callback's claim fail
    // atomically once the slot was cancelled and re-armed with a new token.
    std::atomic<std::uint32_t> status{0};
    std::atomic<bool> result{false};
    Payload payload{};
};

State state_of(const Job &job);
unsigned token_of(const Job &job);

// Publishes payload and token for a new operation. Accepted only from a
// terminal state (idle/done/cancelled), i.e. when the caller holds admission.
bool queue(Job &job, unsigned token, const Payload &payload);
// Claims the job for the LVGL callback. The atomic claim compares state+token
// as a single word, so a stale token can never take over a re-armed job.
bool start(Job &job, unsigned token);
// Payload published for the current operation. Must be read only after
// start(job, token) returned true for the token being executed; a callback that
// lost the claim reads nothing.
Payload claimed_payload(const Job &job);
// Cancels a still queued job. The caller keeps admission ownership.
bool cancel(Job &job);
// Publishes the callback result. Returns AdmissionOwner::caller when the caller
// still owns the release (callback must not release) and
// AdmissionOwner::callback when the callback must release admission itself.
AdmissionOwner finish(Job &job, bool result);
// Called by the caller after its completion budget expired. Returns
// AdmissionOwner::callback when the running callback was detached and will
// release admission, AdmissionOwner::caller when the caller keeps the release.
// A job already in State::detached also reports AdmissionOwner::callback: the
// first detach transferred the release, so a repeated detach must never reclaim
// it or admission would be released both here and by the callback.
AdmissionOwner detach_or_observe_done(Job &job);
bool is_done(const Job &job);

}
