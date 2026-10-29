#include "platform/display/cyberdeck_lvgl_dispatch_state.h"

#include <cstdlib>
#include <iostream>

using namespace cyberdeck_lvgl_dispatch_state;

// The packed word spends exactly 3 bits on the state; every enum value must fit
// or two states would silently alias in Job::status.
static_assert(static_cast<unsigned>(State::idle) == 0u, "state packing starts at 0");
static_assert(static_cast<unsigned>(State::cancelled) == 7u,
              "state must stay within the 3 bits reserved by k_state_bits");

namespace {

int checks = 0;

void check(bool condition, const char *message)
{
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

// Mirrors the dispatcher rule: the caller releases admission unless ownership
// was transferred, and the callback releases only when it owns the transfer.
struct Admission {
    int releases = 0;

    void caller_release_if(AdmissionOwner owner)
    {
        if (owner == AdmissionOwner::caller) ++releases;
    }

    void callback_release_if(AdmissionOwner owner)
    {
        if (owner == AdmissionOwner::callback) ++releases;
    }
};

void payload_a(void *ctx)
{
    *static_cast<int *>(ctx) = 1;
}

void payload_b(void *ctx)
{
    *static_cast<int *>(ctx) = 2;
}

// Mirrors dispatch_callback(): claim first, read the payload only afterwards.
bool simulate_callback(Job &job, unsigned token, bool result = true)
{
    if (!start(job, token)) return false;
    const Payload payload = claimed_payload(job);
    if (payload.fn != nullptr) payload.fn(payload.ctx);
    finish(job, result);
    return true;
}

}

int main()
{
    // 1. Cancel while QUEUED, re-arm with a new payload, then let the old
    //    callback run: it must not claim, must not read the new context and
    //    must not run the new fn.
    {
        Job job;
        Admission admission;
        int ctx_a = 0;
        int ctx_b = 0;
        check(queue(job, 1, Payload{payload_a, &ctx_a}), "queue initial job");
        check(cancel(job), "timeout cancels queued job");
        admission.caller_release_if(AdmissionOwner::caller);  // caller released on cancel
        check(queue(job, 2, Payload{payload_b, &ctx_b}), "admission can be reused after cancellation");

        check(!simulate_callback(job, 1), "old callback cannot claim the re-armed job");
        check(ctx_a == 0 && ctx_b == 0, "stale callback never reads nor executes any payload");
        check(admission.releases == 1, "cancel released admission exactly once");
        check(state_of(job) == State::queued && token_of(job) == 2,
              "stale callback left the re-armed job untouched");

        check(simulate_callback(job, 2), "current callback claims its own token");
        check(ctx_b == 2 && ctx_a == 0, "only the new fn runs");
        admission.caller_release_if(AdmissionOwner::caller);
        check(admission.releases == 2, "non-detached completion leaves the release to the caller");
    }

    // 2. Detach while RUNNING then finish: exactly one admission release.
    {
        Job job;
        Admission admission;
        int ctx = 0;
        check(queue(job, 2, Payload{payload_a, &ctx}) && start(job, 2), "callback starts");
        admission.caller_release_if(detach_or_observe_done(job));
        check(state_of(job) == State::detached, "timeout detaches the running callback");
        check(!queue(job, 3, Payload{payload_b, &ctx}), "rearm rejected while detached");
        check(admission.releases == 0, "detach transfers the release to the callback");
        admission.callback_release_if(finish(job, true));
        check(admission.releases == 1, "detached callback released exactly once");
        check(is_done(job) && job.result, "detached callback result published");
    }

    // 3. Duplicate / out-of-order callback: one release, one result.
    {
        Job job;
        Admission admission;
        int ctx = 0;
        check(queue(job, 4, Payload{payload_a, &ctx}) && start(job, 4), "normal callback starts");
        admission.callback_release_if(finish(job, true));
        check(job.result && is_done(job), "normal result published");
        admission.callback_release_if(finish(job, false));
        check(job.result, "duplicate finish cannot rewrite the result");
        check(!start(job, 4), "duplicate callback cannot claim again nor re-execute");
        admission.caller_release_if(detach_or_observe_done(job));
        check(admission.releases == 1, "duplicate callback produced a single release");
        check(queue(job, 5, Payload{payload_b, &ctx}), "rearm after completion");
    }

    // 4. Timeout while RUNNING, rearm rejected; late finish keeps a single owner.
    {
        Job job;
        Admission admission;
        int ctx = 0;
        check(queue(job, 6, Payload{payload_a, &ctx}) && start(job, 6), "busy state setup");
        check(!queue(job, 7, Payload{payload_b, &ctx}), "rearm while running rejected");
        check(!cancel(job), "timeout cannot cancel a running job");
        check(!start(job, 7), "stale callback rejected");
        check(detach_or_observe_done(job) == AdmissionOwner::callback, "timeout detaches a running callback");
        check(!queue(job, 8, Payload{payload_a, &ctx}), "rearm while detached rejected");
        admission.caller_release_if(AdmissionOwner::callback);
        check(admission.releases == 0, "detached callback keeps the only release");
        check(finish(job, false) == AdmissionOwner::callback, "first finish after detach owns the release");
        admission.callback_release_if(AdmissionOwner::callback);
        check(admission.releases == 1, "late finish released admission once");
        check(is_done(job) && !job.result, "detached finish published its own result");
        check(queue(job, 9, Payload{payload_a, &ctx}), "rearm allowed only after completion");
        check(token_of(job) == 9 && state_of(job) == State::queued, "new token and state published");
    }

    // 5. Cancelled slot that is NOT re-armed: the arriving async callback still
    //    carries the matching token, so only the state may reject it. It must not
    //    claim, not run fn, and not release admission (the caller already did).
    {
        Job job;
        Admission admission;
        int ctx_a = 0;
        int ctx_b = 0;
        check(queue(job, 10, Payload{payload_a, &ctx_a}), "queue before cancellation");
        check(cancel(job), "cancel queued job");
        check(state_of(job) == State::cancelled && token_of(job) == 10,
              "cancel keeps the token so only the state can reject the stale claim");
        admission.caller_release_if(AdmissionOwner::caller);

        check(!simulate_callback(job, 10), "stale callback with the matching token cannot claim a cancelled slot");
        check(ctx_a == 0 && ctx_b == 0, "stale callback on a cancelled slot runs no payload");
        check(state_of(job) == State::cancelled, "failed claim leaves the cancelled slot untouched");
        check(admission.releases == 1, "stale callback cannot release admission twice");

        check(queue(job, 11, Payload{payload_b, &ctx_b}), "cancelled slot can be re-armed");
        check(!simulate_callback(job, 10), "old token rejected after re-arm");
        check(ctx_b == 0, "re-armed payload not executed by the stale token");
        check(simulate_callback(job, 11), "current token claims the re-armed job");
        check(ctx_b == 2 && ctx_a == 0, "only the current payload ran");
        admission.caller_release_if(AdmissionOwner::caller);
        check(admission.releases == 2, "each caller holds exactly one release");
    }

    // 6. A second detach on an already detached job must keep the release with
    //    the callback; returning caller ownership twice would double release.
    {
        Job job;
        Admission admission;
        int ctx = 0;
        check(queue(job, 12, Payload{payload_a, &ctx}) && start(job, 12), "callback starts for detach");
        check(detach_or_observe_done(job) == AdmissionOwner::callback, "first detach transfers the release");
        check(detach_or_observe_done(job) == AdmissionOwner::callback,
              "repeated detach must keep the release with the callback");
        check(state_of(job) == State::detached, "repeated detach leaves the state detached");
        admission.caller_release_if(AdmissionOwner::callback);
        admission.callback_release_if(AdmissionOwner::callback);
        check(admission.releases == 1, "repeated detach produced a single release");
        admission.callback_release_if(finish(job, true));
        check(admission.releases == 2, "the detached callback adds exactly one more release");
        check(is_done(job) && job.result, "detached result published once");
    }

    // 7. queue()/start() must agree on token masking (bits above k_token_mask are
    //    dropped), otherwise a legitimate callback could never claim its job.
    {
        Job job;
        int ctx = 0;
        const unsigned wrapped = 0x20000000u + 8u;  // bit 29 set: outside the mask
        check(k_token_mask == 0x1FFFFFFFu, "token mask keeps 29 bits next to the 3 state bits");
        check(queue(job, k_token_mask, Payload{payload_a, &ctx}), "queue with the largest token");
        check(token_of(job) == k_token_mask, "token round-trips at the mask boundary");
        check(start(job, k_token_mask), "start claims the same masked token");
        check(state_of(job) == State::running, "state is packed alongside the token");
        check(finish(job, true) == AdmissionOwner::caller, "masked token completes normally");
        check(queue(job, wrapped, Payload{payload_a, &ctx}), "queue with a token above the mask");
        check(token_of(job) == 8u, "token above the mask is truncated consistently");
        check(start(job, wrapped), "start masks the token exactly like queue");
        check(state_of(job) == State::running, "masked claim reaches running");
        check(finish(job, false) == AdmissionOwner::caller, "masked claim finishes with caller ownership");
        check(is_done(job) && !job.result, "masked claim publishes its result");
    }

    std::cout << "PASS: " << checks << " checks\n";
}
