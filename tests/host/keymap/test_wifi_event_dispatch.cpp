/* RED contract tests for the IP_EVENT crash fix.
 * No ESP-IDF, hardware, SD, SNTP, UI, or production adapter is faked.
 * The implementation is intentionally absent until the coder consumes this
 * contract. These tests therefore document expected RED failures today.
 */
#include "features/wifi/cyberdeck_wifi_event_dispatch.h"

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

connection_config config(std::uint64_t token, const char *ssid, const char *password)
{
    return {token, ssid, password};
}

void test_got_ip_producer_only_queues_light_snapshot()
{
    event_dispatch dispatch;
    CHECK(dispatch.initialize());
    const got_ip_event_data event{0x01020304};
    CHECK(dispatch.on_got_ip(event, config(7, "home", "secret")));
    CHECK(dispatch.pending() == 1);

    const action queued = dispatch.drain_one();
    CHECK(queued.kind == action_kind::persist);
    CHECK(queued.snapshot.token == 7);
    CHECK(queued.snapshot.ip == 0x01020304);
    CHECK(std::strcmp(queued.snapshot.ssid, "home") == 0);
    CHECK(std::strcmp(queued.snapshot.password, "secret") == 0);
    CHECK(dispatch.acknowledge(queued));
    /* No producer path may perform these side effects. */
    CHECK(dispatch.drain_one().kind == action_kind::none);
}

void test_connected_without_ip_never_persists()
{
    event_dispatch dispatch;
    CHECK(dispatch.initialize());
    dispatch.on_connected(8, "home");
    CHECK(dispatch.pending() == 0);
    CHECK(dispatch.drain_one().kind == action_kind::none);
    CHECK(dispatch.on_got_ip({0x01010101}, config(8, "home", "pw")));
    const action persisted = dispatch.drain_one();
    CHECK(persisted.kind == action_kind::persist);
    CHECK(dispatch.acknowledge(persisted));
}

void test_snapshot_owns_event_data_and_config_independently()
{
    event_dispatch dispatch;
    CHECK(dispatch.initialize());
    got_ip_event_data event{0x0a000001};
    char ssid[] = "mutable-ssid";
    char password[] = "mutable-password";
    CHECK(dispatch.on_got_ip(event, config(11, ssid, password)));

    event.ip = 0xffffffff;
    std::strcpy(ssid, "changed");
    std::strcpy(password, "changed");
    const action queued = dispatch.drain_one();
    CHECK(queued.snapshot.ip == 0x0a000001);
    CHECK(std::strcmp(queued.snapshot.ssid, "mutable-ssid") == 0);
    CHECK(std::strcmp(queued.snapshot.password, "mutable-password") == 0);
    CHECK(dispatch.acknowledge(queued));
}

void test_duplicate_token_and_ssid_are_coalesced()
{
    event_dispatch dispatch;
    CHECK(dispatch.initialize());
    const got_ip_event_data event{1};
    CHECK(dispatch.on_got_ip(event, config(21, "same", "one")));
    CHECK(!dispatch.on_got_ip(event, config(21, "same", "one")));
    CHECK(!dispatch.on_got_ip(event, config(22, "same", "two")));
    CHECK(dispatch.pending() == 1);
    const action queued = dispatch.drain_one();
    CHECK(queued.snapshot.token == 21);
    CHECK(dispatch.acknowledge(queued));
}

void test_stale_events_do_not_persist()
{
    event_dispatch dispatch;
    CHECK(dispatch.initialize());
    const got_ip_event_data event{2};
    CHECK(dispatch.on_got_ip(event, config(30, "active", "pw")));
    CHECK(!dispatch.on_got_ip(event, config(29, "active", "old")));
    const action current = dispatch.drain_one();
    CHECK(current.snapshot.token == 30);
    CHECK(dispatch.acknowledge(current));
    CHECK(dispatch.drain_one().kind == action_kind::none);
}

void test_lost_ip_invalidates_only_active_connection_and_reconnects()
{
    event_dispatch dispatch;
    CHECK(dispatch.initialize());
    CHECK(dispatch.on_got_ip({3}, config(40, "home", "pw")));
    const action persisted = dispatch.drain_one();
    CHECK(persisted.kind == action_kind::persist);
    CHECK(dispatch.acknowledge(persisted));
    dispatch.on_lost_ip(40, "home");
    const action lost = dispatch.drain_one();
    CHECK(lost.kind == action_kind::lost_ip);
    CHECK(dispatch.acknowledge(lost));
    CHECK(dispatch.on_got_ip({4}, config(41, "home", "new-pw")));
    const action next = dispatch.drain_one();
    CHECK(next.snapshot.token == 41);
    CHECK(dispatch.acknowledge(next));
}

void test_password_is_not_retained_after_snapshot_is_drained()
{
    event_dispatch dispatch;
    CHECK(dispatch.initialize());
    CHECK(dispatch.on_got_ip({5}, config(50, "secure", "one-time")));
    const action first = dispatch.drain_one();
    CHECK(std::strcmp(first.snapshot.password, "one-time") == 0);
    CHECK(dispatch.acknowledge(first));
    CHECK(dispatch.drain_one().kind == action_kind::none);
    /* GOT_IP is not a reconnect primitive: a new generation must first be
     * opened by CONNECTED. */
    CHECK(!dispatch.on_got_ip({6}, config(51, "next", "")));
    dispatch.on_connected(51, "next");
    CHECK(dispatch.on_got_ip({6}, config(51, "next", "")));
    const action next = dispatch.drain_one();
    CHECK(next.snapshot.password[0] == '\0');
    CHECK(dispatch.acknowledge(next));
}

void test_initialization_is_idempotent()
{
    event_dispatch dispatch;
    CHECK(dispatch.initialize());
    CHECK(dispatch.initialize());
    CHECK(dispatch.initialized());
    CHECK(dispatch.on_got_ip({6}, config(60, "once", "pw")));
    CHECK(dispatch.pending() == 1);
}

void test_snapshot_inputs_are_bounded_and_null_safe()
{
    event_dispatch dispatch;
    CHECK(dispatch.initialize());
    const std::string long_ssid(128, 's');
    const std::string long_password(256, 'p');
    CHECK(dispatch.on_got_ip({0x01020304}, config(61, long_ssid.c_str(), long_password.c_str())));
    const action bounded = dispatch.drain_one();
    CHECK(bounded.kind == action_kind::persist);
    CHECK(bounded.snapshot.ssid[sizeof(bounded.snapshot.ssid) - 1] == '\0');
    CHECK(bounded.snapshot.password[sizeof(bounded.snapshot.password) - 1] == '\0');
    CHECK(std::strlen(bounded.snapshot.ssid) == sizeof(bounded.snapshot.ssid) - 1);
    CHECK(std::strlen(bounded.snapshot.password) == sizeof(bounded.snapshot.password) - 1);
    CHECK(dispatch.acknowledge(bounded));

    dispatch.on_connected(62, nullptr);
    CHECK(dispatch.on_got_ip({0x05060708}, config(62, nullptr, nullptr)));
    const action empty = dispatch.drain_one();
    CHECK(empty.snapshot.ssid[0] == '\0');
    CHECK(empty.snapshot.password[0] == '\0');
    CHECK(dispatch.acknowledge(empty));
}

void test_retry_does_not_duplicate_or_exceed_capacity()
{
    event_dispatch dispatch;
    CHECK(dispatch.initialize());
    for (std::uint64_t token = 70; token < 70 + event_dispatch::queue_capacity; ++token) {
        char ssid[16];
        std::snprintf(ssid, sizeof(ssid), "ssid-%llu",
                      static_cast<unsigned long long>(token));
        dispatch.on_connected(token, ssid);
        CHECK(dispatch.on_got_ip({static_cast<std::uint32_t>(token)}, config(token, ssid, "pw")));
    }
    CHECK(dispatch.pending() == event_dispatch::queue_capacity);
    CHECK(dispatch.retry_pending() == false);
    CHECK(dispatch.retry_pending() == false);
    CHECK(dispatch.pending() <= event_dispatch::queue_capacity);
}

void test_connected_generation_and_lost_ip_use_token_not_mutable_ssid()
{
    event_dispatch dispatch;
    CHECK(dispatch.initialize());
    dispatch.on_connected(100, "original");
    CHECK(!dispatch.on_got_ip({1}, config(99, "original", "old")));
    CHECK(dispatch.on_got_ip({2}, config(100, "original", "pw")));
    const action persisted = dispatch.drain_one();
    CHECK(persisted.kind == action_kind::persist);
    CHECK(dispatch.acknowledge(persisted));

    /* The SSID supplied by a later driver callback is not identity. */
    dispatch.on_lost_ip(100, "renamed-by-driver");
    const action lost = dispatch.drain_one();
    CHECK(lost.kind == action_kind::lost_ip);
    CHECK(lost.snapshot.token == 100);
    CHECK(std::strcmp(lost.snapshot.ssid, "original") == 0);
    CHECK(dispatch.acknowledge(lost));
}

void test_acknowledge_removes_secret_snapshot_from_delivery_contract()
{
    event_dispatch dispatch;
    CHECK(dispatch.initialize());
    CHECK(dispatch.on_got_ip({7}, config(110, "secret-net", "one-shot-password")));
    const action first = dispatch.drain_one();
    CHECK(first.kind == action_kind::persist);
    CHECK(std::strcmp(first.snapshot.password, "one-shot-password") == 0);
    CHECK(dispatch.acknowledge(first));

    /* The only observable cleanup guarantee is that the acknowledged snapshot
     * can never be delivered again, including after a new generation starts. */
    CHECK(dispatch.drain_one().kind == action_kind::none);
    dispatch.on_connected(111, "next-net");
    CHECK(dispatch.on_got_ip({8}, config(111, "next-net", "next-password")));
    const action next = dispatch.drain_one();
    CHECK(next.kind == action_kind::persist);
    CHECK(next.snapshot.token == 111);
    CHECK(std::strcmp(next.snapshot.password, "next-password") == 0);
    CHECK(std::strcmp(next.snapshot.password, "one-shot-password") != 0);
    CHECK(dispatch.acknowledge(next));
}

void test_disconnect_rolls_back_unacknowledged_secret_snapshot()
{
    event_dispatch dispatch;
    CHECK(dispatch.initialize());
    CHECK(dispatch.on_got_ip({9}, config(120, "rollback-net", "rollback-password")));

    /* Disconnect is a rollback boundary: the owner discards the invalidated
     * generation through the public wipe seam. */
    dispatch.on_lost_ip(120, "rollback-net");
    dispatch.wipe();
    CHECK(dispatch.drain_one().kind == action_kind::none);
    CHECK(dispatch.pending() == 0);
}

void test_dispatch_teardown_does_not_retain_snapshot_across_instances()
{
    event_dispatch dispatch;
    CHECK(dispatch.initialize());
    CHECK(dispatch.on_got_ip({10}, config(130, "teardown-net", "teardown-password")));
    CHECK(dispatch.drain_one().kind == action_kind::persist);
    dispatch.teardown();
    CHECK(!dispatch.initialized());
    CHECK(dispatch.pending() == 0);
    CHECK(dispatch.drain_one().kind == action_kind::none);
    CHECK(dispatch.initialize());
    CHECK(dispatch.pending() == 0);
}

} // namespace

int main()
{
    test_got_ip_producer_only_queues_light_snapshot();
    test_connected_without_ip_never_persists();
    test_snapshot_owns_event_data_and_config_independently();
    test_duplicate_token_and_ssid_are_coalesced();
    test_stale_events_do_not_persist();
    test_lost_ip_invalidates_only_active_connection_and_reconnects();
    test_password_is_not_retained_after_snapshot_is_drained();
    test_initialization_is_idempotent();
    test_snapshot_inputs_are_bounded_and_null_safe();
    test_retry_does_not_duplicate_or_exceed_capacity();
    test_connected_generation_and_lost_ip_use_token_not_mutable_ssid();
    test_acknowledge_removes_secret_snapshot_from_delivery_contract();
    test_disconnect_rolls_back_unacknowledged_secret_snapshot();
    test_dispatch_teardown_does_not_retain_snapshot_across_instances();
    std::printf("wifi event dispatch: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
