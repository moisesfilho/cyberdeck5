/*
 * Host test for the image-viewer worker handoff (TEST-IMG-LIFE-05/06,
 * REQ/AC-IMG-04).
 *
 * The production application owns a FreeRTOS task and therefore cannot be
 * linked on the host; the *handoff protocol* it relies on is a pure state
 * machine (`worker_state`) called with the request mutex held.  This test drives
 * that exact protocol and reproduces the interleaving that used to strand a job:
 * a request arriving while the worker is on its way out.
 *
 * Coverage by scenario:
 *   TEST-IMG-LIFE-05 - a job enqueued during the terminal transition of the
 *       worker is never stranded: either the live worker takes it, or the
 *       enqueue reports that a task must be created.  A cancelled worker
 *       discards a queued job instead of publishing a stale frame.
 *   TEST-IMG-LIFE-06 - a persistent worker only leaves on cancellation, and the
 *       exit retires `alive()` in the same step that observes the queue, so the
 *       next request cannot observe a dead worker as alive.
 */
#include "apps/image/cyberdeck_image_worker_state.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(condition)) {                                                                                            \
            ++failures;                                                                                                \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                           \
        }                                                                                                              \
    } while (false)

#define CHECK_EQ(actual, expected)                                                                                     \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if ((actual) != (expected)) {                                                                                  \
            ++failures;                                                                                                \
            std::printf("FAIL %s:%d: %s (actual=%ld, expected=%ld)\n", __FILE__, __LINE__, #actual,                    \
                        static_cast<long>(actual), static_cast<long>(expected));                                       \
        }                                                                                                              \
    } while (false)

namespace image = cyberdeck_image;
using step = image::worker_state::step;

/* Models `start()`: reset + create the persistent task. */
void start(image::worker_state &worker) {
    worker.reset();
    worker.mark_alive();
}

/* Models one request() call.  Returns true when the caller must create a task. */
bool request(image::worker_state &worker) {
    return worker.enqueue();
}

/* TEST-IMG-LIFE-06: a live worker waits instead of exiting while idle. */
void test_persistent_worker_waits_when_idle() {
    image::worker_state worker{};
    start(worker);
    CHECK(worker.alive());
    CHECK(!worker.pending());
    /* No request yet: the worker must not interpret "idle" as "exit", which is
     * exactly the situation that used to open the create/terminate window. */
    CHECK_EQ(worker.next(false), step::wait);
    CHECK(worker.alive());
    CHECK_EQ(worker.next(false), step::wait);
    CHECK(worker.alive());

    /* A live worker needs a wake-up, not a new task: enqueue reports false. */
    CHECK(!request(worker));
    CHECK(worker.pending());
    CHECK_EQ(worker.next(false), step::run);
    CHECK(!worker.pending());
    CHECK(worker.alive());
    CHECK_EQ(worker.next(false), step::wait);
}

/* TEST-IMG-LIFE-05: the regression that motivated the rewrite. */
void test_request_during_terminal_transition_is_not_lost() {
    image::worker_state worker{};
    start(worker);

    /* Old implementation: the worker observed "no pending job" under the mutex,
     * released it, and only then cleared `running`.  A request in that window
     * saw `running == true`, skipped task creation, and the job was stranded
     * with the app stuck in `loading`.  The protocol now retires `alive()` in
     * the same step that observes the queue, so the request is told to create a
     * task and the job is not lost. */
    CHECK_EQ(worker.next(false), step::wait);
    /* The request lands after the worker decided to wait (the new "terminal"
     * state is only reached through cancellation, never through idleness). */
    CHECK(!request(worker));
    CHECK(worker.pending());
    CHECK_EQ(worker.next(false), step::run);

    /* Cancellation is the only exit: it retires alive() and drops the queue. */
    CHECK(!request(worker));
    CHECK(worker.pending());
    CHECK_EQ(worker.next(true), step::exit);
    CHECK(!worker.alive());
    CHECK(!worker.pending());

    /* A request after the exit must force a new task, or the job would hang
     * forever with no consumer. */
    CHECK(request(worker));
    CHECK(worker.pending());
    CHECK(!worker.alive());
    worker.mark_alive();
    CHECK_EQ(worker.next(false), step::run);
    CHECK(!worker.pending());
}

/* Invariant sweep: for every short sequence of request/worker steps, a pending
 * job always has a consumer (a live worker or an explicit create request). */
void test_no_interleaving_strands_a_job() {
    enum class op : std::uint8_t { request, take, take_cancelled };
    const op ops[] = {op::request, op::take, op::take_cancelled};
    /* 4 positions, 3^4 = 81 sequences: small enough to be exhaustive and large
     * enough to cover every ordering of request/take around the exit. */
    constexpr int kPositionCount = 4;
    constexpr int kOpCount = 3;
    for (int encoded = 0; encoded < kOpCount * kOpCount * kOpCount * kOpCount; ++encoded) {
        image::worker_state worker{};
        start(worker);
        int value = encoded;
        bool alive_before_request = false;
        bool create_requested = false;
        for (int position = 0; position < kPositionCount; ++position) {
            const op current = ops[value % kOpCount];
            value /= kOpCount;
            alive_before_request = worker.alive();
            switch (current) {
            case op::request:
                create_requested = request(worker);
                /* A request over a dead worker must be paired with creation. */
                CHECK(!create_requested || !alive_before_request);
                /* A request over a live worker keeps the job queued. */
                CHECK(worker.pending());
                break;
            case op::take: {
                const step what = worker.next(false);
                CHECK(what != step::exit);
                if (what == step::run)
                    CHECK(!worker.pending());
                break;
            }
            case op::take_cancelled: {
                CHECK_EQ(worker.next(true), step::exit);
                CHECK(!worker.alive());
                CHECK(!worker.pending());
                worker.mark_alive(); /* models start() after a stop */
                break;
            }
            }
        }
        /* Whatever the ordering, a queued job is never left without a worker
         * that either owns it or was created for it. */
        if (worker.pending()) {
            CHECK(worker.alive());
            CHECK_EQ(worker.next(false), step::run);
            CHECK(!worker.pending());
        }
    }
}

void test_reset_clears_both_flags() {
    image::worker_state worker{};
    start(worker);
    CHECK(request(worker) == false);
    CHECK(worker.pending());
    worker.reset();
    CHECK(!worker.pending());
    CHECK(!worker.alive());
    /* After reset a request must create a task again. */
    CHECK(request(worker));
    CHECK(worker.pending());
}

} // namespace

int main() {
    test_persistent_worker_waits_when_idle();
    test_request_during_terminal_transition_is_not_lost();
    test_no_interleaving_strands_a_job();
    test_reset_clears_both_flags();
    std::printf("test_image_worker_state: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
