/* RED contract tests for bounded persistence delivery and retry.
 * The queue must never drop an accepted snapshot when its consumer is busy.
 */
#include "features/wifi/cyberdeck_wifi_persistence_queue.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace cyberdeck_wifi_test;

namespace {
int failures = 0;
int checks = 0;
#define CHECK(condition) do { \
    ++checks; if (!(condition)) { ++failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } \
} while (0)

ip_snapshot snapshot(std::uint64_t token, const char *ssid, const char *password)
{
    ip_snapshot result{};
    result.token = token;
    std::snprintf(result.ssid, sizeof(result.ssid), "%s", ssid ? ssid : "");
    std::snprintf(result.password, sizeof(result.password), "%s", password ? password : "");
    return result;
}

void test_full_queue_preserves_backlog_for_retry_without_loss()
{
    persistence_queue queue;
    CHECK(queue.initialize());
    for (std::uint64_t token = 1; token <= persistence_queue::capacity; ++token) {
        char ssid[16];
        std::snprintf(ssid, sizeof(ssid), "ssid-%llu",
                      static_cast<unsigned long long>(token));
        CHECK(queue.enqueue(snapshot(token, ssid, "pw")));
    }
    CHECK(queue.pending() == persistence_queue::capacity);
    const ip_snapshot overflow = snapshot(99, "overflow", "pw");
    CHECK(!queue.enqueue(overflow));
    /* Retry is bounded: a busy consumer must not turn one failed item into an
     * unbounded duplicate backlog.  The implementation may reject the retry
     * while full, or coalesce it, but it must never grow beyond capacity. */
    const bool retried = queue.retry(overflow);
    CHECK(queue.pending() <= persistence_queue::capacity);

    for (std::uint64_t token = 1; token <= persistence_queue::capacity; ++token) {
        const action item = queue.take();
        if (token == 1) {
            CHECK(item.kind == action_kind::persist);
            CHECK(item.snapshot.token == token);
            CHECK(queue.take().kind == action_kind::none);
            CHECK(queue.acknowledge(item.snapshot));
        } else {
            CHECK(item.kind == action_kind::persist);
            CHECK(item.snapshot.token == token);
            CHECK(queue.acknowledge(item.snapshot));
        }
    }
    const action retried_item = queue.take();
    if (retried) {
        CHECK(retried_item.kind == action_kind::persist);
        CHECK(retried_item.snapshot.token == 99);
    } else {
        CHECK(retried_item.kind == action_kind::none);
    }
    CHECK(queue.take().kind == action_kind::none);
}

void test_take_remains_in_flight_until_explicit_ack()
{
    persistence_queue queue;
    CHECK(queue.initialize());
    const ip_snapshot item = snapshot(200, "crash-safe", "pw");
    CHECK(queue.enqueue(item));
    const action first = queue.take();
    CHECK(first.kind == action_kind::persist);
    CHECK(queue.pending() == 1);
    CHECK(queue.take().kind == action_kind::none);

    /* A worker/storage failure makes the owned item eligible for re-delivery. */
    CHECK(queue.retry(item));
    CHECK(queue.pending() == 1);
    const action redelivered = queue.take();
    CHECK(redelivered.kind == action_kind::persist);
    CHECK(redelivered.snapshot.token == first.snapshot.token);
    CHECK(queue.acknowledge(redelivered.snapshot));
    CHECK(queue.pending() == 0);
    CHECK(!queue.acknowledge(first.snapshot));
}

void test_duplicate_token_and_ssid_do_not_grow_backlog()
{
    persistence_queue queue;
    CHECK(queue.initialize());
    CHECK(queue.enqueue(snapshot(1, "same", "pw")));
    CHECK(!queue.enqueue(snapshot(1, "same", "pw")));
    CHECK(!queue.enqueue(snapshot(2, "same", "new")));
    CHECK(queue.pending() == 1);
}

void test_old_token_is_ignored_after_reconnect()
{
    /* The old test only checked FIFO, so it could pass while an old callback
     * was still persisted after a reconnect.  Drive the producer contract
     * through a real reconnect and prove that a late old event is inert. */
    event_dispatch dispatch;
    CHECK(dispatch.initialize());
    CHECK(dispatch.on_got_ip({0x0a000001}, {10, "home", "old"}));
    dispatch.on_lost_ip(10, "home");
    const action old_persist = dispatch.drain_one();
    CHECK(old_persist.kind == action_kind::persist);
    CHECK(dispatch.acknowledge(old_persist));
    const action old_lost = dispatch.drain_one();
    CHECK(old_lost.kind == action_kind::lost_ip);
    CHECK(dispatch.acknowledge(old_lost));

    CHECK(dispatch.on_got_ip({0x0a000002}, {11, "home", "new"}));
    CHECK(!dispatch.on_got_ip({0x0a000003}, {10, "home", "old"}));
    const action current = dispatch.drain_one();
    CHECK(current.kind == action_kind::persist);
    CHECK(current.snapshot.token == 11);
    CHECK(current.snapshot.ip == 0x0a000002);
    CHECK(dispatch.acknowledge(current));
    CHECK(dispatch.drain_one().kind == action_kind::none);
}

void test_repeated_retry_is_coalesced_and_bounded()
{
    persistence_queue queue;
    CHECK(queue.initialize());
    const ip_snapshot item = snapshot(30, "retry", "pw");
    CHECK(queue.enqueue(item));
    CHECK(!queue.retry(item));
    CHECK(!queue.retry(item));
    CHECK(!queue.retry(item));
    CHECK(queue.pending() == 1);
    const action taken = queue.take();
    CHECK(taken.snapshot.token == 30);
    CHECK(queue.take().kind == action_kind::none);
    CHECK(queue.acknowledge(taken.snapshot));
    CHECK(queue.take().kind == action_kind::none);
}

void test_snapshot_bounds_and_null_inputs_are_safe()
{
    persistence_queue queue;
    CHECK(queue.initialize());
    const std::string long_ssid(128, 's');
    const std::string long_password(256, 'p');
    CHECK(queue.enqueue(snapshot(31, long_ssid.c_str(), long_password.c_str())));
    const action bounded = queue.take();
    CHECK(bounded.kind == action_kind::persist);
    CHECK(bounded.snapshot.ssid[sizeof(bounded.snapshot.ssid) - 1] == '\0');
    CHECK(bounded.snapshot.password[sizeof(bounded.snapshot.password) - 1] == '\0');
    CHECK(std::strlen(bounded.snapshot.ssid) == sizeof(bounded.snapshot.ssid) - 1);
    CHECK(std::strlen(bounded.snapshot.password) == sizeof(bounded.snapshot.password) - 1);

    CHECK(queue.enqueue(snapshot(32, nullptr, nullptr)));
    CHECK(queue.acknowledge(bounded.snapshot));
    const action empty = queue.take();
    CHECK(empty.kind == action_kind::persist);
    CHECK(empty.snapshot.ssid[0] == '\0');
    CHECK(empty.snapshot.password[0] == '\0');
    CHECK(queue.acknowledge(empty.snapshot));
}

void test_queue_initialization_is_idempotent_and_password_is_cleared()
{
    persistence_queue queue;
    CHECK(queue.initialize());
    CHECK(queue.initialize());
    CHECK(queue.initialized());
    CHECK(queue.enqueue(snapshot(20, "secret", "temporary")));
    const action item = queue.take();
    CHECK(std::strcmp(item.snapshot.password, "temporary") == 0);
    CHECK(queue.take().kind == action_kind::none);
    CHECK(queue.acknowledge(item.snapshot));
    CHECK(queue.enqueue(snapshot(21, "next", "")));
    const action next = queue.take();
    CHECK(next.snapshot.password[0] == '\0');
    CHECK(queue.acknowledge(next.snapshot));
}

void test_worker_crash_retry_reprocesses_owned_snapshot_once_before_ack()
{
    persistence_queue queue;
    CHECK(queue.initialize());

    const ip_snapshot original = snapshot(40, "retry-after-crash", "unchanged");
    CHECK(queue.enqueue(original));

    /* Delivery transfers ownership to the worker.  Losing the worker after
     * take() must not release or recreate the snapshot. */
    const action first_attempt = queue.take();
    CHECK(first_attempt.kind == action_kind::persist);
    CHECK(first_attempt.snapshot.token == original.token);
    CHECK(std::strcmp(first_attempt.snapshot.ssid, original.ssid) == 0);
    CHECK(std::strcmp(first_attempt.snapshot.password, original.password) == 0);
    CHECK(queue.pending() == 1);

    /* A retry requested after the worker's backoff reuses the owned item. */
    CHECK(queue.retry(first_attempt.snapshot));
    const action retry_attempt = queue.take();
    CHECK(retry_attempt.kind == action_kind::persist);
    CHECK(retry_attempt.snapshot.token == first_attempt.snapshot.token);
    CHECK(std::strcmp(retry_attempt.snapshot.ssid, first_attempt.snapshot.ssid) == 0);
    CHECK(std::strcmp(retry_attempt.snapshot.password, first_attempt.snapshot.password) == 0);
    CHECK(queue.pending() == 1);

    /* Persistence succeeded: only now may the consumer acknowledge it. */
    CHECK(queue.acknowledge(retry_attempt.snapshot));
    CHECK(queue.pending() == 0);
    CHECK(queue.take().kind == action_kind::none);
}

void test_worker_retry_is_deduplicated_and_bounded()
{
    persistence_queue queue;
    CHECK(queue.initialize());
    const ip_snapshot original = snapshot(41, "bounded-retry", "pw");
    CHECK(queue.enqueue(original));

    const action first_attempt = queue.take();
    CHECK(first_attempt.kind == action_kind::persist);
    CHECK(queue.retry(first_attempt.snapshot));
    CHECK(queue.retry(first_attempt.snapshot));
    CHECK(queue.pending() == 1);

    const action retry_attempt = queue.take();
    CHECK(retry_attempt.kind == action_kind::persist);
    CHECK(retry_attempt.snapshot.token == original.token);
    CHECK(queue.take().kind == action_kind::none);
    CHECK(queue.acknowledge(retry_attempt.snapshot));
    CHECK(queue.pending() == 0);
    CHECK(queue.take().kind == action_kind::none);
}

void test_storage_failure_retry_keeps_one_in_flight_secret_until_ack()
{
    persistence_queue queue;
    CHECK(queue.initialize());
    const ip_snapshot secret = snapshot(50, "mount-failure", "storage-secret");
    CHECK(queue.enqueue(secret));

    const action first = queue.take();
    CHECK(first.kind == action_kind::persist);
    CHECK(std::strcmp(first.snapshot.password, "storage-secret") == 0);
    CHECK(queue.pending() == 1);

    /* A failed mount/storage attempt retains exactly one owned retry item. */
    CHECK(queue.retry(first.snapshot));
    CHECK(queue.retry(first.snapshot));
    CHECK(queue.pending() == 1);
    const action retry_attempt = queue.take();
    CHECK(retry_attempt.kind == action_kind::persist);
    CHECK(retry_attempt.snapshot.token == first.snapshot.token);
    CHECK(std::strcmp(retry_attempt.snapshot.password, "storage-secret") == 0);

    CHECK(queue.acknowledge(retry_attempt.snapshot));
    CHECK(queue.pending() == 0);
    CHECK(queue.take().kind == action_kind::none);
    CHECK(!queue.retry(first.snapshot));
    CHECK(!queue.acknowledge(first.snapshot));
}

void test_retry_pending_before_delivery_cannot_create_secret_in_flight()
{
    persistence_queue queue;
    CHECK(queue.initialize());
    const ip_snapshot pending = snapshot(51, "pending-retry", "pending-secret");

    /* A retry callback arriving before take() is stale and must not promote a
     * pending queue entry into an in-flight delivery. */
    CHECK(queue.enqueue(pending));
    CHECK(!queue.retry(pending));
    CHECK(queue.pending() == 1);
    const action delivered = queue.take();
    CHECK(delivered.kind == action_kind::persist);
    CHECK(delivered.snapshot.token == pending.token);
    CHECK(queue.acknowledge(delivered.snapshot));
    CHECK(queue.pending() == 0);
}

void test_queue_teardown_starts_without_pending_or_in_flight_snapshot()
{
    persistence_queue queue;
    CHECK(queue.initialize());
    CHECK(queue.enqueue(snapshot(52, "teardown", "teardown-secret")));
    CHECK(queue.take().kind == action_kind::persist);
    queue.teardown();
    CHECK(!queue.initialized());
    CHECK(queue.pending() == 0);
    CHECK(queue.take().kind == action_kind::none);
    CHECK(queue.initialize());
    CHECK(queue.pending() == 0);
}

void test_mount_failure_rollback_discards_pending_and_in_flight_snapshots()
{
    persistence_queue queue;
    CHECK(queue.initialize());
    CHECK(queue.enqueue(snapshot(53, "rollback-storage", "rollback-secret")));
    CHECK(queue.enqueue(snapshot(54, "other-storage", "other-secret")));
    const action in_flight = queue.take();
    CHECK(in_flight.kind == action_kind::persist);
    queue.wipe();
    CHECK(queue.pending() == 0);
    CHECK(queue.take().kind == action_kind::none);
    CHECK(!queue.retry(in_flight.snapshot));
    CHECK(!queue.acknowledge(in_flight.snapshot));
}

} // namespace

int main()
{
    test_full_queue_preserves_backlog_for_retry_without_loss();
    test_duplicate_token_and_ssid_do_not_grow_backlog();
    test_take_remains_in_flight_until_explicit_ack();
    test_old_token_is_ignored_after_reconnect();
    test_repeated_retry_is_coalesced_and_bounded();
    test_snapshot_bounds_and_null_inputs_are_safe();
    test_queue_initialization_is_idempotent_and_password_is_cleared();
    test_worker_crash_retry_reprocesses_owned_snapshot_once_before_ack();
    test_worker_retry_is_deduplicated_and_bounded();
    test_storage_failure_retry_keeps_one_in_flight_secret_until_ack();
    test_retry_pending_before_delivery_cannot_create_secret_in_flight();
    test_queue_teardown_starts_without_pending_or_in_flight_snapshot();
    test_mount_failure_rollback_discards_pending_and_in_flight_snapshots();
    std::printf("wifi persistence queue: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
