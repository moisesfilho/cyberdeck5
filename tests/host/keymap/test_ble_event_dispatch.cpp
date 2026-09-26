/*
 * TDD RED host contract for the pure BLE event/token dispatch seam.
 *
 * Covers REQ-BLE-003 (asynchronous scan hand-off), REQ-BLE-008 (automatic
 * reconnection generation) and REQ-BLE-010 (bounded hand-off, no secret in any
 * diagnostic).
 *
 * RED until the coder creates
 * components/cyberdeck/src/features/bluetooth/cyberdeck_ble_event_dispatch.cpp
 * with the ABI declared in contracts/cyberdeck_ble_event_dispatch.h.  No
 * ESP-IDF, NimBLE, esp_hosted, LVGL, NVS, simulator or hardware is used here.
 */
#include "cyberdeck_ble_event_dispatch.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    } \
} while (0)

#define CHECK_EQ(actual, expected) do { \
    ++checks; \
    if (!((actual) == (expected))) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #actual); \
    } \
} while (0)

#define CHECK_STR(actual, expected) do { \
    ++checks; \
    const std::string actual_value = (actual); \
    const std::string expected_value = (expected); \
    if (actual_value != expected_value) { \
        ++failures; \
        std::printf("FAIL %s:%d: expected '%s', actual '%s'\n", \
                    __FILE__, __LINE__, expected_value.c_str(), \
                    actual_value.c_str()); \
    } \
} while (0)

const std::uint32_t kSecretPasskey = 246813;
const char *kSecretDigits = "246813";

using cyberdeck_ble::auth_request_kind;
using cyberdeck_ble::ble_event;
using cyberdeck_ble::ble_event_kind;
using cyberdeck_ble::device;
using cyberdeck_ble::device_kind;
using cyberdeck_ble::event_dispatch;
using cyberdeck_ble::notice;
using cyberdeck_ble::pair_outcome;

device make_device(const char *address, const char *name, int rssi,
                   device_kind kind, bool connectable = true)
{
    device item;
    item.address = address;
    item.name = name == nullptr ? std::string() : std::string(name);
    item.rssi = rssi;
    item.kind = kind;
    item.connectable = connectable;
    return item;
}

std::vector<ble_event> drain_all(event_dispatch &dispatch)
{
    std::vector<ble_event> events;
    ble_event buffer[4];
    for (;;) {
        const std::size_t count = dispatch.drain(buffer, 4);
        if (count == 0) break;
        for (std::size_t i = 0; i < count; ++i) events.push_back(buffer[i]);
    }
    return events;
}

void test_approved_bounds()
{
    CHECK_EQ(cyberdeck_ble::k_max_pending_events, std::size_t(8));

    event_dispatch dispatch;
    CHECK_EQ(dispatch.capacity(), cyberdeck_ble::k_max_pending_events);
    CHECK_EQ(dispatch.pending(), std::size_t(0));
    CHECK_EQ(dispatch.active_scan_token(), std::uint64_t(0));
    CHECK_EQ(dispatch.active_pair_token(), std::uint64_t(0));
    CHECK_EQ(dispatch.active_connection_token(), std::uint64_t(0));
    CHECK(drain_all(dispatch).empty());
}

void test_tokens_are_monotonic_and_non_zero()
{
    event_dispatch dispatch;
    std::uint64_t previous = 0;

    for (int i = 0; i < 5; ++i) {
        const std::uint64_t token = dispatch.begin_scan();
        CHECK(token != 0);
        CHECK(token > previous);
        previous = token;
        CHECK_EQ(dispatch.active_scan_token(), token);
    }

    /* The three generation kinds share one monotonic counter, so a token can
     * never be mistaken for a live generation of another kind. */
    const std::uint64_t pair_token = dispatch.begin_pairing("AA:BB:CC:DD:EE:01");
    CHECK(pair_token > previous);
    previous = pair_token;
    const std::uint64_t connect_token =
        dispatch.begin_connection("AA:BB:CC:DD:EE:01", true);
    CHECK(connect_token > previous);
    previous = connect_token;
    const std::uint64_t manual = dispatch.begin_connection("AA:BB:CC:DD:EE:02", false);
    CHECK(manual > previous);

    dispatch.reset();
    CHECK_EQ(dispatch.active_scan_token(), std::uint64_t(0));
    CHECK_EQ(dispatch.active_pair_token(), std::uint64_t(0));
    CHECK_EQ(dispatch.active_connection_token(), std::uint64_t(0));
    CHECK_EQ(dispatch.pending(), std::size_t(0));

    /* A token handed out before reset() can never become valid again. */
    CHECK(!dispatch.publish_scan_result(connect_token,
                                        make_device("AA:BB:CC:DD:EE:03", "X", -40,
                                                    device_kind::keyboard)));
    CHECK_EQ(dispatch.pending(), std::size_t(0));
}

void test_stale_publications_are_dropped_without_being_enqueued()
{
    event_dispatch dispatch;
    const std::uint64_t stale = dispatch.begin_scan();
    const std::uint64_t live = dispatch.begin_scan();
    CHECK(stale != live);

    CHECK(!dispatch.publish_scan_started(stale));
    CHECK(!dispatch.publish_scan_result(stale, make_device("AA:BB:CC:DD:EE:EE",
                                                          "Stale", -30,
                                                          device_kind::keyboard)));
    CHECK(!dispatch.publish_scan_finished(stale, notice::none));
    CHECK_EQ(dispatch.pending(), std::size_t(0));

    CHECK(!dispatch.publish_scan_result(0, make_device("AA:BB:CC:DD:EE:00", "Zero",
                                                       -30, device_kind::keyboard)));
    CHECK(!dispatch.publish_scan_finished(0, notice::empty));
    CHECK_EQ(dispatch.pending(), std::size_t(0));

    /* A token belonging to another generation kind is not a scan token. */
    const std::uint64_t pair_token = dispatch.begin_pairing("AA:BB:CC:DD:EE:01");
    CHECK(!dispatch.publish_scan_result(pair_token,
                                        make_device("AA:BB:CC:DD:EE:01", "X", -40,
                                                    device_kind::keyboard)));
    CHECK(!dispatch.publish_auth_request(live, auth_request_kind::passkey,
                                         kSecretPasskey));
    CHECK_EQ(dispatch.pending(), std::size_t(0));

    /* The active generation is still intact after every rejection. */
    CHECK_EQ(dispatch.active_scan_token(), live);
    CHECK(dispatch.publish_scan_result(live, make_device("AA:BB:CC:DD:EE:01", "Live",
                                                        -40,
                                                        device_kind::keyboard)));
    CHECK_EQ(dispatch.pending(), std::size_t(1));
    (void)pair_token;
}

void test_queue_is_bounded_and_overflow_is_fail_closed()
{
    event_dispatch dispatch;
    const std::uint64_t token = dispatch.begin_scan();
    const std::size_t bound = dispatch.capacity();

    for (std::size_t i = 0; i < bound; ++i) {
        CHECK(dispatch.publish_scan_result(token,
                                           make_device("AA:BB:CC:DD:EE:01", "D", -40,
                                                       device_kind::keyboard)));
    }
    CHECK_EQ(dispatch.pending(), bound);

    /* The newest publication is rejected; the queue never grows. */
    CHECK(!dispatch.publish_scan_result(token,
                                        make_device("AA:BB:CC:DD:EE:02", "Overflow",
                                                    -40, device_kind::keyboard)));
    CHECK_EQ(dispatch.pending(), bound);

    /* Draining makes room again. */
    ble_event buffer[8];
    const std::size_t drained = dispatch.drain(buffer, 2);
    CHECK_EQ(drained, std::size_t(2));
    CHECK_EQ(dispatch.pending(), bound - 2);
    CHECK(dispatch.publish_scan_result(token,
                                       make_device("AA:BB:CC:DD:EE:03", "Again", -41,
                                                   device_kind::keyboard)));
    CHECK_EQ(dispatch.pending(), bound - 1);

    /* drain with a zero limit consumes nothing. */
    const std::size_t before = dispatch.pending();
    CHECK_EQ(dispatch.drain(buffer, 0), std::size_t(0));
    CHECK_EQ(dispatch.pending(), before);

    /* drain reports the smaller of the limit and the backlog. */
    CHECK_EQ(dispatch.drain(buffer, 1000), before);
    CHECK_EQ(dispatch.pending(), std::size_t(0));

    /* drain on an empty queue is safe and a null sink with limit 0 is a no-op. */
    CHECK_EQ(dispatch.drain(buffer, 8), std::size_t(0));
    CHECK_EQ(dispatch.drain(nullptr, 0), std::size_t(0));
}

void test_drain_preserves_publication_order_and_ownership()
{
    event_dispatch dispatch;
    const std::uint64_t token = dispatch.begin_scan();
    CHECK(dispatch.publish_scan_started(token));
    CHECK(dispatch.publish_scan_result(token, make_device("AA:BB:CC:DD:EE:01",
                                                          "Fone", -50,
                                                          device_kind::headset)));
    CHECK(dispatch.publish_scan_result(token, make_device("AA:BB:CC:DD:EE:02",
                                                          "Mouse", -60,
                                                          device_kind::mouse,
                                                          false)));
    CHECK(dispatch.publish_scan_finished(token, notice::empty));

    ble_event buffer[4];
    CHECK_EQ(dispatch.drain(buffer, 4), std::size_t(4));

    CHECK(buffer[0].kind == ble_event_kind::scan_started);
    CHECK_EQ(buffer[0].token, token);

    CHECK(buffer[1].kind == ble_event_kind::scan_result);
    CHECK_STR(buffer[1].device_record.address, "AA:BB:CC:DD:EE:01");
    CHECK_STR(buffer[1].device_record.name, "Fone");
    CHECK(buffer[1].device_record.kind == device_kind::headset);
    CHECK_EQ(buffer[1].device_record.rssi, -50);

    CHECK(buffer[2].kind == ble_event_kind::scan_result);
    CHECK(!buffer[2].device_record.connectable);

    CHECK(buffer[3].kind == ble_event_kind::scan_finished);
    CHECK(buffer[3].scan_outcome == notice::empty);

    /* The snapshot owns its data: the source device may go out of scope. */
    CHECK_STR(buffer[1].device_record.address, "AA:BB:CC:DD:EE:01");
    CHECK_EQ(dispatch.pending(), std::size_t(0));
}

void test_pairing_generation_and_auth_request()
{
    event_dispatch dispatch;
    const std::uint64_t stale_pair = dispatch.begin_pairing("AA:BB:CC:DD:EE:01");
    const std::uint64_t live_pair = dispatch.begin_pairing("AA:BB:CC:DD:EE:02");
    CHECK(stale_pair != live_pair);

    CHECK(!dispatch.publish_auth_request(stale_pair, auth_request_kind::passkey,
                                         kSecretPasskey));
    CHECK(!dispatch.publish_pair_finished(stale_pair, pair_outcome::bonded));
    CHECK_EQ(dispatch.pending(), std::size_t(0));

    CHECK(dispatch.publish_auth_request(live_pair, auth_request_kind::passkey,
                                        kSecretPasskey));
    CHECK(dispatch.publish_pair_finished(live_pair, pair_outcome::bonded));
    CHECK_EQ(dispatch.pending(), std::size_t(2));

    const std::vector<ble_event> events = drain_all(dispatch);
    CHECK_EQ(events.size(), std::size_t(2));
    if (events.size() == 2) {
        CHECK(events[0].kind == ble_event_kind::auth_request);
        CHECK(events[0].auth_kind == auth_request_kind::passkey);
        CHECK_EQ(events[0].passkey, kSecretPasskey);
        CHECK_EQ(events[0].token, live_pair);
        CHECK(events[1].kind == ble_event_kind::pair_finished);
        CHECK(events[1].pair_result == pair_outcome::bonded);
    }
}

void test_connection_generation_distinguishes_manual_from_automatic()
{
    event_dispatch dispatch;
    const std::uint64_t manual = dispatch.begin_connection("AA:BB:CC:DD:EE:01", false);
    const std::uint64_t automatic =
        dispatch.begin_connection("AA:BB:CC:DD:EE:02", true);
    CHECK(manual != automatic);

    /* The manual generation is already superseded: its callbacks are dropped. */
    CHECK(!dispatch.publish_connected(manual));
    CHECK(!dispatch.publish_disconnected(manual));
    CHECK_EQ(dispatch.pending(), std::size_t(0));

    CHECK(dispatch.publish_connected(automatic));
    const std::vector<ble_event> events = drain_all(dispatch);
    CHECK_EQ(events.size(), std::size_t(1));
    if (!events.empty()) {
        CHECK(events[0].kind == ble_event_kind::connected);
        CHECK(events[0].automatic);
    }

    /* A new connection generation invalidates the previous one. */
    const std::uint64_t next = dispatch.begin_connection("AA:BB:CC:DD:EE:02", true);
    CHECK(!dispatch.publish_connected(automatic));
    CHECK(dispatch.publish_disconnected(next));
    CHECK_EQ(dispatch.pending(), std::size_t(1));
}

void test_drop_stale_discards_superseded_generations()
{
    event_dispatch dispatch;
    const std::uint64_t first = dispatch.begin_scan();
    CHECK(dispatch.publish_scan_result(first, make_device("AA:BB:CC:DD:EE:01", "A",
                                                          -40,
                                                          device_kind::keyboard)));
    CHECK(dispatch.publish_scan_result(first, make_device("AA:BB:CC:DD:EE:02", "B",
                                                          -41,
                                                          device_kind::keyboard)));

    const std::uint64_t second = dispatch.begin_scan();
    /* Nothing is stale yet: both events belong to the active generation. */
    CHECK_EQ(dispatch.drop_stale(), std::size_t(0));
    CHECK_EQ(dispatch.pending(), std::size_t(2));

    const std::uint64_t third = dispatch.begin_scan();
    CHECK_EQ(dispatch.drop_stale(), std::size_t(2));
    CHECK_EQ(dispatch.pending(), std::size_t(0));

    CHECK(dispatch.publish_scan_result(third, make_device("AA:BB:CC:DD:EE:03", "C",
                                                          -42,
                                                          device_kind::keyboard)));
    CHECK_EQ(dispatch.drop_stale(), std::size_t(0));
    CHECK_EQ(dispatch.pending(), std::size_t(1));
    CHECK_STR(drain_all(dispatch).at(0).device_record.address,
              "AA:BB:CC:DD:EE:03");
    (void)second;

    /* A live scan is never swept by a new pairing generation. */
    const std::uint64_t scan = dispatch.begin_scan();
    CHECK(dispatch.publish_scan_result(scan, make_device("AA:BB:CC:DD:EE:04", "D",
                                                         -43,
                                                         device_kind::keyboard)));
    dispatch.begin_pairing("AA:BB:CC:DD:EE:04");
    CHECK_EQ(dispatch.drop_stale(), std::size_t(0));
    CHECK_EQ(dispatch.pending(), std::size_t(1));
}

void test_event_summary_never_carries_a_secret()
{
    event_dispatch dispatch;
    const std::uint64_t pair_token = dispatch.begin_pairing("AA:BB:CC:DD:EE:01");
    CHECK(dispatch.publish_auth_request(pair_token, auth_request_kind::passkey,
                                        kSecretPasskey));

    ble_event buffer[4];
    CHECK_EQ(dispatch.drain(buffer, 4), std::size_t(1));

    const std::string summary = dispatch.event_summary(buffer[0]);
    CHECK(!summary.empty());
    /* Not a single digit of the passkey may survive the projection, and the
     * masked form is what the log carries. */
    CHECK(summary.find(kSecretDigits) == std::string::npos);
    CHECK(summary.find("******") != std::string::npos);
    CHECK(summary.size() < 128);
    CHECK(summary.find('\n') == std::string::npos);
    CHECK(summary.find('\x1b') == std::string::npos);

    /* A summary of any other event kind is also secret free. */
    const std::uint64_t scan_token = dispatch.begin_scan();
    CHECK(dispatch.publish_scan_result(scan_token,
                                       make_device("AA:BB:CC:DD:EE:02", "Fone", -50,
                                                   device_kind::headset)));
    CHECK(dispatch.publish_scan_finished(scan_token, notice::timed_out));
    CHECK(dispatch.publish_pair_finished(pair_token, pair_outcome::bonded));
    const std::vector<ble_event> events = drain_all(dispatch);
    CHECK_EQ(events.size(), std::size_t(3));
    for (const ble_event &event : events) {
        const std::string line = dispatch.event_summary(event);
        CHECK(!line.empty());
        CHECK(line.find(kSecretDigits) == std::string::npos);
        CHECK(line.find('\n') == std::string::npos);
        CHECK(line.size() < 128);
    }

    /* A scan result summary names the device and its type, nothing more. */
    const ble_event scan_result = events[0];
    const std::string scan_line = dispatch.event_summary(scan_result);
    CHECK(scan_line.find("AA:BB:CC:DD:EE:02") != std::string::npos);
    CHECK(scan_line.find("Headset") != std::string::npos);

    /* A hostile/zeroed event must not crash or produce a huge string. */
    const ble_event hostile = ble_event();
    CHECK(!dispatch.event_summary(hostile).empty());
    CHECK(dispatch.event_summary(hostile).size() < 128);
}

void test_bounded_name_in_a_snapshot_cannot_inject_control_characters()
{
    event_dispatch dispatch;
    const std::uint64_t token = dispatch.begin_scan();
    device hostile = make_device("AA:BB:CC:DD:EE:01", "Bad\nName\x1b[31m", -40,
                                 device_kind::keyboard);
    CHECK(dispatch.publish_scan_result(token, hostile));
    /* The seam forwards the record as-is; the UI-facing projection is where the
     * sanitizer applies, and the summary must already be terminal safe. */
    ble_event buffer[2];
    CHECK_EQ(dispatch.drain(buffer, 2), std::size_t(1));
    const std::string line = dispatch.event_summary(buffer[0]);
    CHECK(line.find('\n') == std::string::npos);
    CHECK(line.find('\x1b') == std::string::npos);
    CHECK(line.find("Bad") != std::string::npos);
}

} // namespace

int main()
{
    test_approved_bounds();
    test_tokens_are_monotonic_and_non_zero();
    test_stale_publications_are_dropped_without_being_enqueued();
    test_queue_is_bounded_and_overflow_is_fail_closed();
    test_drain_preserves_publication_order_and_ownership();
    test_pairing_generation_and_auth_request();
    test_connection_generation_distinguishes_manual_from_automatic();
    test_drop_stale_discards_superseded_generations();
    test_event_summary_never_carries_a_secret();
    test_bounded_name_in_a_snapshot_cannot_inject_control_characters();

    std::printf("ble event dispatch contract: %d checks, %d failures\n",
                checks, failures);
    return failures == 0 ? 0 : 1;
}
