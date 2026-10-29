#include "platform/display/cyberdeck_lvgl_dispatch_state.h"

namespace cyberdeck_lvgl_dispatch_state {
namespace {

constexpr std::uint32_t k_state_bits = 3u;
constexpr std::uint32_t k_state_mask = (1u << k_state_bits) - 1u;
// Every State must fit in the 3 bits reserved by k_state_bits, otherwise two
// states would alias inside Job::status. Packing starts at zero, so the highest
// enumerator is the one that bounds the range.
static_assert(static_cast<std::uint32_t>(State::idle) == 0u,
              "state packing starts at 0");
// Mirrors the host contract: the eight states must span exactly 0..7. A new
// state is appended by inserting it BEFORE `cancelled` and updating this assert,
// otherwise values above k_state_mask would silently alias in Job::status.
static_assert(static_cast<std::uint32_t>(State::cancelled) == k_state_mask,
              "cancelled must stay the last state that fits in k_state_bits");
// Bounded spin while the callback publishes State::done; the callback only
// performs two stores, so a caller never stays here for long.
constexpr unsigned k_max_settle_spins = 4096u;

std::uint32_t pack(State state, unsigned token)
{
    return ((token & k_token_mask) << k_state_bits) |
           (static_cast<std::uint32_t>(state) & k_state_mask);
}

State unpack_state(std::uint32_t status)
{
    return static_cast<State>(status & k_state_mask);
}

unsigned unpack_token(std::uint32_t status)
{
    return (status >> k_state_bits) & k_token_mask;
}

bool is_terminal(State state)
{
    return state == State::idle || state == State::done || state == State::cancelled;
}

}

State state_of(const Job &job)
{
    return unpack_state(job.status.load(std::memory_order_acquire));
}

unsigned token_of(const Job &job)
{
    return unpack_token(job.status.load(std::memory_order_acquire));
}

bool queue(Job &job, unsigned token, const Payload &payload)
{
    const std::uint32_t next = pack(State::queued, token);
    std::uint32_t current = job.status.load(std::memory_order_acquire);
    for (;;) {
        if (!is_terminal(unpack_state(current))) return false;
        // Written before the release CAS below, so the acquiring CAS in start()
        // publishes the payload to the callback.
        job.result.store(false, std::memory_order_relaxed);
        job.payload = payload;
        if (job.status.compare_exchange_strong(current, next, std::memory_order_release,
                                               std::memory_order_acquire)) return true;
    }
}

bool start(Job &job, unsigned token)
{
    std::uint32_t current = pack(State::queued, token);
    return job.status.compare_exchange_strong(current, pack(State::running, token),
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire);
}

Payload claimed_payload(const Job &job)
{
    return job.payload;
}

bool cancel(Job &job)
{
    std::uint32_t current = job.status.load(std::memory_order_acquire);
    for (;;) {
        if (unpack_state(current) != State::queued) return false;
        if (job.status.compare_exchange_strong(current,
                                               pack(State::cancelled, unpack_token(current)),
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire)) return true;
    }
}

AdmissionOwner finish(Job &job, bool result)
{
    std::uint32_t current = job.status.load(std::memory_order_acquire);
    for (;;) {
        const State state = unpack_state(current);
        const unsigned token = unpack_token(current);
        if (state == State::running) {
            if (job.status.compare_exchange_strong(current, pack(State::finishing, token),
                                                   std::memory_order_acq_rel,
                                                   std::memory_order_acquire)) {
                job.result.store(result, std::memory_order_relaxed);
                job.status.store(pack(State::done, token), std::memory_order_release);
                return AdmissionOwner::caller;
            }
            continue;
        }
        if (state == State::detached) {
            if (job.status.compare_exchange_strong(current, pack(State::finishing_detached, token),
                                                   std::memory_order_acq_rel,
                                                   std::memory_order_acquire)) {
                job.result.store(result, std::memory_order_relaxed);
                job.status.store(pack(State::done, token), std::memory_order_release);
                return AdmissionOwner::callback;
            }
            continue;
        }
        // Already settled (duplicate or out-of-order finish): the record is
        // immutable from here and the callback must not release admission.
        return AdmissionOwner::caller;
    }
}

AdmissionOwner detach_or_observe_done(Job &job)
{
    unsigned spins = 0;
    for (;;) {
        const std::uint32_t current = job.status.load(std::memory_order_acquire);
        const State state = unpack_state(current);
        if (state == State::running) {
            std::uint32_t expected = current;
            if (job.status.compare_exchange_strong(expected, pack(State::detached, unpack_token(current)),
                                                   std::memory_order_acq_rel,
                                                   std::memory_order_acquire)) return AdmissionOwner::callback;
            continue;
        }
        if (state == State::finishing) {
            // The callback committed to State::done with caller ownership; it
            // will never release, so bailing out here cannot double release.
            if (++spins > k_max_settle_spins) return AdmissionOwner::caller;
            continue;
        }
        // A job already detached keeps the release with the callback: the first
        // detach transferred ownership, so answering caller here would release
        // admission twice (once here, once when the callback finishes).
        if (state == State::detached) return AdmissionOwner::callback;
        if (state == State::finishing_detached) return AdmissionOwner::callback;
        return AdmissionOwner::caller;
    }
}

bool is_done(const Job &job)
{
    return unpack_state(job.status.load(std::memory_order_acquire)) == State::done;
}

}
