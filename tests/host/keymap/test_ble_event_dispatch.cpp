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

void test_auth_event_preserves_io_action_and_peer_identity()
{
    using cyberdeck_ble::address_type;
    event_dispatch dispatch;
    const std::uint64_t token = dispatch.begin_pairing("AA:BB:CC:DD:EE:01");
    CHECK(dispatch.publish_auth_request(token, auth_request_kind::numeric_compare,
                                       kSecretPasskey,
                                       cyberdeck_ble::auth_io_action::numeric_compare));
    const std::vector<ble_event> events = drain_all(dispatch);
    CHECK_EQ(events.size(), std::size_t(1));
    if (events.size() == 1) {
        CHECK(events[0].auth_action == cyberdeck_ble::auth_io_action::numeric_compare);
        CHECK_EQ(events[0].passkey, kSecretPasskey);
    }

    const std::uint64_t connection = dispatch.begin_connection(
        41, "AA:BB:CC:DD:EE:01", address_type::random_non_resolvable, false);
    CHECK(dispatch.publish_connected(connection));
    ble_event connected[1];
    CHECK_EQ(dispatch.drain(connected, 1), std::size_t(1));
    CHECK(connected[0].addr_type == address_type::random_non_resolvable);
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

void test_connection_address_type_survives_event_generation()
{
    using cyberdeck_ble::address_type;
    event_dispatch dispatch;
    const std::uint64_t token = dispatch.begin_connection(
        41, "AA:BB:CC:DD:EE:01", address_type::random_resolvable, true);
    CHECK(token != 0);
    CHECK(dispatch.publish_connected(token));
    ble_event event[1];
    CHECK_EQ(dispatch.drain(event, 1), std::size_t(1));
    CHECK(event[0].addr_type == address_type::random_resolvable);
    CHECK(event[0].automatic);
    CHECK_STR(event[0].address, "AA:BB:CC:DD:EE:01");
}

void test_hid_discovery_is_additional_bounded_and_stale_safe()
{
    event_dispatch dispatch;
    const std::uint64_t token = dispatch.begin_connection(
        41, "AA:BB:CC:DD:EE:01", cyberdeck_ble::address_type::random_static, false);

    ble_event::hid_snapshot snapshot;
    snapshot.conn_handle = 41;
    snapshot.success = true;
    snapshot.hid_service = true;
    snapshot.service_start = 10;
    snapshot.service_end = 40;
    snapshot.characteristic_count = 8;
    snapshot.cccd_count = 4;
    snapshot.report_map_handle = 11;
    snapshot.boot_keyboard_input_handle = 12;
    snapshot.boot_keyboard_input_cccd = 13;
    snapshot.report_input_handle = 14;
    snapshot.report_input_cccd = 15;

    /* HID readiness is a second event; CONNECTED remains independently queued. */
    CHECK(dispatch.publish_connected(token));
    CHECK(dispatch.publish_hid_discovery(token, snapshot));
    ble_event events[2];
    CHECK_EQ(dispatch.drain(events, 2), std::size_t(2));
    CHECK(events[0].kind == ble_event_kind::connected);
    CHECK(events[1].kind == ble_event_kind::hid_discovery);
    CHECK(events[1].hid.success && events[1].hid.conn_handle == 41);
    CHECK_EQ(events[1].hid.characteristic_count, std::uint8_t(8));
    CHECK_EQ(events[1].hid.cccd_count, std::uint8_t(4));

    /* A new connection generation invalidates a late HID callback publication. */
    const std::uint64_t next = dispatch.begin_connection(
        42, "AA:BB:CC:DD:EE:02", cyberdeck_ble::address_type::public_address, true);
    CHECK(!dispatch.publish_hid_discovery(token, snapshot));
    CHECK(dispatch.publish_hid_discovery(next, snapshot));

    /* HID events share the same bounded hand-off and overflow fail-closed. */
    dispatch.drain(events, 2);
    for (std::size_t i = 0; i < dispatch.capacity(); ++i) {
        CHECK(dispatch.publish_hid_discovery(next, snapshot));
    }
    CHECK(!dispatch.publish_hid_discovery(next, snapshot));
    CHECK_EQ(dispatch.pending(), dispatch.capacity());
    dispatch.reset();
    CHECK(!dispatch.publish_hid_discovery(next, snapshot));
}

void test_hid_summary_contains_capability_only()
{
    event_dispatch dispatch;
    ble_event event;
    event.kind = ble_event_kind::hid_discovery;
    event.hid.success = true;
    event.hid.report_map_handle = 0x1234;
    const std::string summary = dispatch.event_summary(event);
    CHECK_STR(summary, "BLE HID discovery ready");
    CHECK(summary.find("1234") == std::string::npos);
    CHECK(summary.find("passkey") == std::string::npos);
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

/* REQ-COV-01/TEST-COV-EVENTS: deterministic edge coverage for the real missed
 * gcov branches (zero-token guards, short-circuit right-hand sides, stale
 * generations per kind, partial-drain/overflow edges, every summary shape). */
void test_coverage_zero_token_guards_are_fail_closed()
{
    event_dispatch dispatch;
    /* Zero tokens never activate a generation and never enqueue. */
    CHECK_EQ(dispatch.begin_scan(0), std::uint64_t(0));
    CHECK_EQ(dispatch.begin_pairing(0, "AA:BB:CC:DD:EE:01"), std::uint64_t(0));
    CHECK_EQ(dispatch.begin_connection(0, "AA:BB:CC:DD:EE:01", false),
             std::uint64_t(0));
    CHECK(!dispatch.publish_scan_started(0));
    CHECK(!dispatch.publish_scan_result(0, make_device("AA:BB:CC:DD:EE:01", "Z",
                                                       -40, device_kind::mouse)));
    CHECK(!dispatch.publish_scan_finished(0, notice::empty));
    CHECK(!dispatch.publish_auth_request(0, auth_request_kind::passkey, 1));
    CHECK(!dispatch.publish_pair_finished(0, pair_outcome::bonded));
    CHECK(!dispatch.publish_connected(0));
    CHECK(!dispatch.publish_disconnected(0));
    CHECK(!dispatch.publish_hid_discovery(0, ble_event::hid_snapshot()));
    CHECK_EQ(dispatch.pending(), std::size_t(0));
}

void test_coverage_stale_generations_and_drop_stale_per_kind()
{
    /* Scan generations: previous is kept, older is stale. */
    {
        event_dispatch dispatch;
        const std::uint64_t first = dispatch.begin_scan();
        CHECK(dispatch.publish_scan_result(first, make_device("AA:BB:CC:DD:EE:01",
                                                              "A", -40,
                                                              device_kind::keyboard)));
        const std::uint64_t second = dispatch.begin_scan();
        CHECK(dispatch.publish_scan_result(second, make_device("AA:BB:CC:DD:EE:02",
                                                               "B", -41,
                                                               device_kind::keyboard)));
        CHECK_EQ(dispatch.drop_stale(), std::size_t(0));
        const std::uint64_t third = dispatch.begin_scan();
        /* Queued pair/connect generations are unaffected by the scan sweep. */
        const std::uint64_t pair = dispatch.begin_pairing("AA:BB:CC:DD:EE:09");
        CHECK(dispatch.publish_auth_request(pair, auth_request_kind::passkey, 11));
        const std::uint64_t conn =
            dispatch.begin_connection("AA:BB:CC:DD:EE:08", false);
        CHECK(dispatch.publish_connected(conn));
        /* The oldest scan event is stale; the previous generation, the new
         * pair generation and the new connect generation all survive. */
        CHECK_EQ(dispatch.drop_stale(), std::size_t(1));
        CHECK_EQ(dispatch.pending(), std::size_t(3));
        (void)third;
    }
    /* Pair generations: exhaust stale sweep with a queued survivor. */
    {
        event_dispatch dispatch;
        const std::uint64_t stale_pair = dispatch.begin_pairing("AA:BB:CC:DD:EE:01");
        CHECK(dispatch.publish_auth_request(stale_pair, auth_request_kind::passkey, 1));
        (void)dispatch.begin_pairing("AA:BB:CC:DD:EE:02");
        const std::uint64_t live_pair = dispatch.begin_pairing("AA:BB:CC:DD:EE:03");
        CHECK(dispatch.publish_pair_finished(live_pair, pair_outcome::bonded));
        CHECK_EQ(dispatch.drop_stale(), std::size_t(1));
        CHECK_EQ(dispatch.pending(), std::size_t(1));
    }
    /* Connect generations: compact the queue so a survivor moves forward. */
    {
        event_dispatch dispatch;
        const std::uint64_t stale_conn =
            dispatch.begin_connection("AA:BB:CC:DD:EE:01", false);
        CHECK(dispatch.publish_connected(stale_conn));
        (void)dispatch.begin_connection("AA:BB:CC:DD:EE:02", true);
        const std::uint64_t live_conn =
            dispatch.begin_connection("AA:BB:CC:DD:EE:03", true);
        CHECK(dispatch.publish_disconnected(live_conn));
        CHECK(dispatch.publish_hid_discovery(live_conn, ble_event::hid_snapshot()));
        CHECK_EQ(dispatch.drop_stale(), std::size_t(1));
        CHECK_EQ(dispatch.pending(), std::size_t(2));
        ble_event buffer[2];
        CHECK_EQ(dispatch.drain(buffer, 2), std::size_t(2));
        CHECK(buffer[0].kind == ble_event_kind::disconnected);
        CHECK(buffer[1].kind == ble_event_kind::hid_discovery);
    }
    /* Dequeue from a non-empty queue returns the event; drain into nullptr
     * with a positive limit is safe. */
    {
        event_dispatch dispatch;
        const std::uint64_t token = dispatch.begin_scan();
        CHECK(dispatch.publish_scan_started(token));
        CHECK_EQ(dispatch.drain(nullptr, 4), std::size_t(0));
        CHECK_EQ(dispatch.pending(), std::size_t(1));
    }
}

void test_coverage_every_summary_shape_without_secrets()
{
    event_dispatch dispatch;
    ble_event started;
    started.kind = ble_event_kind::scan_started;
    CHECK_STR(dispatch.event_summary(started), "BLE scan started");

    const notice outcomes[] = {notice::empty, notice::failed, notice::timed_out,
                               notice::cancelled, notice::none};
    const char *fragments[] = {"empty", "failed", "timeout", "cancelled", "none"};
    for (std::size_t i = 0; i < 5; ++i) {
        ble_event finished;
        finished.kind = ble_event_kind::scan_finished;
        finished.scan_outcome = outcomes[i];
        CHECK(dispatch.event_summary(finished).find(fragments[i]) != std::string::npos);
    }

    ble_event numeric;
    numeric.kind = ble_event_kind::auth_request;
    numeric.auth_kind = auth_request_kind::numeric_compare;
    numeric.passkey = kSecretPasskey;
    CHECK_STR(dispatch.event_summary(numeric), "BLE auth request: numeric_compare");

    ble_event unknown_auth;
    unknown_auth.kind = ble_event_kind::auth_request;
    unknown_auth.auth_kind = static_cast<auth_request_kind>(99);
    CHECK_STR(dispatch.event_summary(unknown_auth), "BLE auth request: unknown");

    const pair_outcome results[] = {pair_outcome::bonded, pair_outcome::rejected,
                                    pair_outcome::cancelled, pair_outcome::timed_out,
                                    pair_outcome::failed};
    const char *result_fragments[] = {"bonded", "rejected", "cancelled", "timeout",
                                      "failed"};
    for (std::size_t i = 0; i < 5; ++i) {
        ble_event finished;
        finished.kind = ble_event_kind::pair_finished;
        finished.pair_result = results[i];
        CHECK(dispatch.event_summary(finished).find(result_fragments[i]) !=
              std::string::npos);
    }

    ble_event connected;
    connected.kind = ble_event_kind::connected;
    connected.address = "AA:BB:CC:DD:EE:01";
    connected.automatic = true;
    CHECK(dispatch.event_summary(connected).find("(auto)") != std::string::npos);
    connected.automatic = false;
    CHECK(dispatch.event_summary(connected).find("(auto)") == std::string::npos);

    ble_event disconnected;
    disconnected.kind = ble_event_kind::disconnected;
    disconnected.address = "AA:BB:CC:DD:EE:02";
    disconnected.automatic = true;
    CHECK(dispatch.event_summary(disconnected).find("(auto)") != std::string::npos);
    disconnected.automatic = false;
    CHECK(dispatch.event_summary(disconnected).find("(auto)") == std::string::npos);

    ble_event hid_down;
    hid_down.kind = ble_event_kind::hid_discovery;
    hid_down.hid.success = false;
    CHECK_STR(dispatch.event_summary(hid_down), "BLE HID discovery unavailable");
}

void test_coverage_compaction_order_and_foreign_kind_summary()
{    /* Compaction keeps a leading survivor in place (write == read edge).
     * Two scan generations stay live (active + previous), so the sweep
     * only drops the oldest once a third generation begins. */
    {
        event_dispatch dispatch;
        const std::uint64_t pair = dispatch.begin_pairing("AA:BB:CC:DD:EE:09");
        CHECK(dispatch.publish_auth_request(pair, auth_request_kind::passkey, 11));
        const std::uint64_t scan = dispatch.begin_scan();
        CHECK(dispatch.publish_scan_result(scan, make_device("AA:BB:CC:DD:EE:01",
                                                             "A", -40,
                                                             device_kind::keyboard)));
        (void)dispatch.begin_scan();
        CHECK_EQ(dispatch.drop_stale(), std::size_t(0));
        CHECK_EQ(dispatch.pending(), std::size_t(2));
        (void)dispatch.begin_scan();
        /* Now the first scan event is stale; the older pair event survives. */
        CHECK_EQ(dispatch.drop_stale(), std::size_t(1));
        CHECK_EQ(dispatch.pending(), std::size_t(1));
        ble_event buffer[1];
        CHECK_EQ(dispatch.drain(buffer, 1), std::size_t(1));
        CHECK(buffer[0].kind == ble_event_kind::auth_request);
    }
    /* A foreign event kind yields an empty but bounded summary (the switch
     * has no default arm; the projection stays fail-closed). */
    {
        event_dispatch dispatch;
        ble_event foreign;
        foreign.kind = static_cast<ble_event_kind>(99);
        const std::string line = dispatch.event_summary(foreign);
        CHECK(line.empty());
        CHECK(line.size() < 128);
    }
}

void test_coverage_dequeue_empty_leaves_output_and_drain_edges()
{
    /* dequeue on an empty queue is observable via drain: it returns 0 and
     * never touches the caller's buffer. */
    {
        event_dispatch dispatch;
        ble_event sentinel;
        sentinel.kind = ble_event_kind::connected;
        sentinel.token = 0xA5A5A5A5ULL;
        sentinel.address = "SENTINEL";
        sentinel.automatic = true;
        ble_event buffer[1] = {sentinel};
        CHECK_EQ(dispatch.pending(), std::size_t(0));
        CHECK_EQ(dispatch.drain(buffer, 1), std::size_t(0));
        CHECK(buffer[0].kind == ble_event_kind::connected);
        CHECK_EQ(buffer[0].token, 0xA5A5A5A5ULL);
        CHECK_STR(buffer[0].address, "SENTINEL");
        CHECK(buffer[0].automatic);
        CHECK_EQ(dispatch.pending(), std::size_t(0));
    }
    /* drain limit edges: zero consumes nothing, smaller-than-backlog keeps
     * order, larger-than-backlog returns the backlog. */
    {
        event_dispatch dispatch;
        const std::uint64_t token = dispatch.begin_scan();
        CHECK(dispatch.publish_scan_started(token));
        CHECK(dispatch.publish_scan_result(token, make_device("AA:BB:CC:DD:EE:01",
                                                              "A", -40,
                                                              device_kind::keyboard)));
        CHECK(dispatch.publish_scan_finished(token, notice::empty));
        CHECK_EQ(dispatch.pending(), std::size_t(3));

        ble_event scratch[4];
        CHECK_EQ(dispatch.drain(scratch, 0), std::size_t(0));
        CHECK_EQ(dispatch.pending(), std::size_t(3));

        ble_event first[1];
        CHECK_EQ(dispatch.drain(first, 1), std::size_t(1));
        CHECK(first[0].kind == ble_event_kind::scan_started);
        CHECK_EQ(dispatch.pending(), std::size_t(2));

        ble_event rest[8];
        CHECK_EQ(dispatch.drain(rest, 8), std::size_t(2));
        CHECK(rest[0].kind == ble_event_kind::scan_result);
        CHECK(rest[1].kind == ble_event_kind::scan_finished);
        CHECK_EQ(dispatch.pending(), std::size_t(0));

        /* Draining an empty queue with a huge limit is still a no-op. */
        CHECK_EQ(dispatch.drain(rest, 1000), std::size_t(0));
    }
}

void test_coverage_mismatch_tokens_per_kind_are_fail_closed()
{
    event_dispatch dispatch;
    const std::uint64_t scan = dispatch.begin_scan();
    const std::uint64_t pair =
        dispatch.begin_pairing("AA:BB:CC:DD:EE:09");
    const std::uint64_t conn =
        dispatch.begin_connection("AA:BB:CC:DD:EE:08", false);
    CHECK(scan != 0 && pair != 0 && conn != 0);

    /* Every publish rejects a live token of another generation kind without
     * enqueueing and without touching any active generation. */
    CHECK(!dispatch.publish_scan_started(pair));
    CHECK(!dispatch.publish_scan_started(conn));
    CHECK(!dispatch.publish_scan_result(pair, make_device("AA:BB:CC:DD:EE:01",
                                                          "X", -40,
                                                          device_kind::mouse)));
    CHECK(!dispatch.publish_scan_result(conn, make_device("AA:BB:CC:DD:EE:01",
                                                          "X", -40,
                                                          device_kind::mouse)));
    CHECK(!dispatch.publish_scan_finished(pair, notice::failed));
    CHECK(!dispatch.publish_scan_finished(conn, notice::failed));
    CHECK(!dispatch.publish_auth_request(scan, auth_request_kind::passkey, 7));
    CHECK(!dispatch.publish_auth_request(conn, auth_request_kind::passkey, 7));
    CHECK(!dispatch.publish_pair_finished(scan, pair_outcome::bonded));
    CHECK(!dispatch.publish_pair_finished(conn, pair_outcome::bonded));
    CHECK(!dispatch.publish_connected(scan));
    CHECK(!dispatch.publish_connected(pair));
    CHECK(!dispatch.publish_disconnected(scan));
    CHECK(!dispatch.publish_disconnected(pair));
    CHECK(!dispatch.publish_hid_discovery(scan, ble_event::hid_snapshot()));
    CHECK(!dispatch.publish_hid_discovery(pair, ble_event::hid_snapshot()));
    CHECK_EQ(dispatch.pending(), std::size_t(0));
    CHECK_EQ(dispatch.active_scan_token(), scan);
    CHECK_EQ(dispatch.active_pair_token(), pair);
    CHECK_EQ(dispatch.active_connection_token(), conn);

    /* A never-issued token is also stale for every kind. */
    const std::uint64_t unknown = conn + 1000;
    CHECK(!dispatch.publish_scan_started(unknown));
    CHECK(!dispatch.publish_auth_request(unknown, auth_request_kind::passkey, 7));
    CHECK(!dispatch.publish_pair_finished(unknown, pair_outcome::failed));
    CHECK(!dispatch.publish_connected(unknown));
    CHECK(!dispatch.publish_disconnected(unknown));
    CHECK_EQ(dispatch.pending(), std::size_t(0));

    /* External token activation keeps the shared counter monotonic in both
     * max() directions, using observable begin_scan() results. */
    {
        event_dispatch large;
        CHECK_EQ(large.begin_scan(), std::uint64_t(1));
        CHECK_EQ(large.begin_scan(500), std::uint64_t(500));
        CHECK_EQ(large.begin_scan(), std::uint64_t(501));
    }
    {
        event_dispatch small;
        CHECK_EQ(small.begin_scan(500), std::uint64_t(500));
        CHECK_EQ(small.begin_scan(10), std::uint64_t(10));
        /* The counter kept the high-water mark, so auto allocation continues
         * past it instead of rewinding. */
        CHECK_EQ(small.begin_scan(), std::uint64_t(501));
        CHECK_EQ(small.active_scan_token(), std::uint64_t(501));
    }
}

void test_coverage_full_queue_rejects_every_terminal_kind()
{
    event_dispatch dispatch;
    const std::uint64_t scan = dispatch.begin_scan();
    const std::uint64_t pair =
        dispatch.begin_pairing("AA:BB:CC:DD:EE:09");
    const std::uint64_t conn =
        dispatch.begin_connection("AA:BB:CC:DD:EE:08", true);
    const std::size_t bound = dispatch.capacity();
    CHECK_EQ(bound, std::size_t(8));

    for (std::size_t i = 0; i < bound; ++i) {
        CHECK(dispatch.publish_scan_result(scan, make_device("AA:BB:CC:DD:EE:01",
                                                             "D", -40,
                                                             device_kind::keyboard)));
    }
    CHECK_EQ(dispatch.pending(), bound);

    /* The terminal slot behaves like any other: newest is rejected when full. */
    CHECK(!dispatch.publish_scan_started(scan));
    CHECK(!dispatch.publish_scan_finished(scan, notice::empty));
    CHECK(!dispatch.publish_auth_request(pair, auth_request_kind::passkey, 11));
    CHECK(!dispatch.publish_pair_finished(pair, pair_outcome::bonded));
    CHECK(!dispatch.publish_connected(conn));
    CHECK(!dispatch.publish_disconnected(conn));
    CHECK(!dispatch.publish_hid_discovery(conn, ble_event::hid_snapshot()));
    CHECK_EQ(dispatch.pending(), bound);

    /* Freeing one slot admits exactly one terminal event. */
    ble_event one[1];
    CHECK_EQ(dispatch.drain(one, 1), std::size_t(1));
    CHECK(dispatch.publish_scan_finished(scan, notice::timed_out));
    CHECK_EQ(dispatch.pending(), bound);
    CHECK(!dispatch.publish_pair_finished(pair, pair_outcome::failed));
    CHECK_EQ(dispatch.drain(nullptr, 0), std::size_t(0));
    CHECK_EQ(dispatch.pending(), bound);
}

void test_coverage_reset_clears_generations_and_summary_defaults()
{
    using cyberdeck_ble::address_type;
    event_dispatch dispatch;
    const std::uint64_t scan = dispatch.begin_scan();
    CHECK(dispatch.publish_scan_result(scan, make_device("AA:BB:CC:DD:EE:01",
                                                         "A", -40,
                                                         device_kind::keyboard)));
    const std::uint64_t pair =
        dispatch.begin_pairing("AA:BB:CC:DD:EE:09");
    CHECK(dispatch.publish_auth_request(pair, auth_request_kind::passkey, 11));
    const std::uint64_t conn = dispatch.begin_connection(
        41, "AA:BB:CC:DD:EE:08", address_type::random_resolvable, true);
    CHECK(dispatch.publish_connected(conn));
    CHECK_EQ(dispatch.pending(), std::size_t(3));

    dispatch.reset();
    CHECK_EQ(dispatch.pending(), std::size_t(0));
    CHECK_EQ(dispatch.active_scan_token(), std::uint64_t(0));
    CHECK_EQ(dispatch.active_pair_token(), std::uint64_t(0));
    CHECK_EQ(dispatch.active_connection_token(), std::uint64_t(0));
    CHECK_EQ(dispatch.drop_stale(), std::size_t(0));

    /* Every pre-reset token is stale for its own kind after reset. */
    CHECK(!dispatch.publish_scan_result(scan, make_device("AA:BB:CC:DD:EE:01",
                                                          "A", -40,
                                                          device_kind::keyboard)));
    CHECK(!dispatch.publish_auth_request(pair, auth_request_kind::passkey, 11));
    CHECK(!dispatch.publish_connected(conn));
    CHECK_EQ(dispatch.pending(), std::size_t(0));

    /* A fresh connection generation carries only the new peer identity. */
    const std::uint64_t fresh = dispatch.begin_connection(
        42, "AA:BB:CC:DD:EE:07", address_type::public_address, false);
    CHECK(fresh != 0);
    CHECK(dispatch.publish_connected(fresh));
    ble_event event[1];
    CHECK_EQ(dispatch.drain(event, 1), std::size_t(1));
    CHECK_STR(event[0].address, "AA:BB:CC:DD:EE:07");
    CHECK(event[0].addr_type == address_type::public_address);
    CHECK(!event[0].automatic);

    /* Summary defaults: unknown scan outcomes fall back to "none", the
     * passkey shape is masked, and every shape stays single-line bounded. */
    ble_event not_connectable;
    not_connectable.kind = ble_event_kind::scan_finished;
    not_connectable.scan_outcome = notice::not_connectable;
    CHECK(dispatch.event_summary(not_connectable).find("none") != std::string::npos);

    ble_event unknown_notice;
    unknown_notice.kind = ble_event_kind::scan_finished;
    unknown_notice.scan_outcome = static_cast<notice>(99);
    CHECK(dispatch.event_summary(unknown_notice).find("none") != std::string::npos);

    ble_event passkey;
    passkey.kind = ble_event_kind::auth_request;
    passkey.auth_kind = auth_request_kind::passkey;
    passkey.passkey = kSecretPasskey;
    const std::string masked = dispatch.event_summary(passkey);
    CHECK(masked.find("passkey=******") != std::string::npos);
    CHECK(masked.find(kSecretDigits) == std::string::npos);
    CHECK(masked.size() < 128);

    ble_event unnamed_result;
    unnamed_result.kind = ble_event_kind::scan_result;
    unnamed_result.device_record = make_device("AA:BB:CC:DD:EE:02", "", -60,
                                               device_kind::unknown);
    const std::string unnamed_line = dispatch.event_summary(unnamed_result);
    CHECK(!unnamed_line.empty());
    CHECK(unnamed_line.find("AA:BB:CC:DD:EE:02") != std::string::npos);
    CHECK(unnamed_line.find('\n') == std::string::npos);
    CHECK(unnamed_line.size() < 128);

    ble_event empty_peer;
    empty_peer.kind = ble_event_kind::connected;
    empty_peer.address.clear();
    empty_peer.automatic = false;
    CHECK(!dispatch.event_summary(empty_peer).empty());
    CHECK(dispatch.event_summary(empty_peer).size() < 128);
}

/* REQ-COV-01 cycle 2: short-circuit right-hand sides of the stale guards,
 *Ctor/dtor exception edges are structural (new/delete) and unreachable via
 * the public API; the real behavioral misses are the stale-guard RHS edges:
 * scan_gen==0 but generations differ, gen equal to previous (kept), gen
 * equal to active (kept), pair/connect equivalents, plus a foreign-kind
 * is_stale fallthrough and the pair_finished default (foreign outcome). */
void test_coverage_cycle2_stale_guard_rhs()
{
    /* Scan: queued gen 0 (never stamped) is not stale even when both
     * generations moved on. A default ble_event carries scan_gen==0, so a
     * hand-built scan_result with no generation survives drop_stale. */
    {
        event_dispatch dispatch;
        const std::uint64_t first = dispatch.begin_scan();
        CHECK(dispatch.publish_scan_result(first, make_device("AA:BB:CC:DD:EE:01",
                                                              "A", -40,
                                                              device_kind::keyboard)));
        (void)dispatch.begin_scan();
        (void)dispatch.begin_scan();
        /* Oldest queued event is stale and dropped; craft a gen-0 event by
         * publishing then resetting generations is impossible, so exercise
         * the RHS via previous-generation survival instead. */
        CHECK_EQ(dispatch.drop_stale(), std::size_t(1));
        CHECK_EQ(dispatch.pending(), std::size_t(0));
    }
    /* Scan gen equal to previous generation survives (RHS false path):
     * active + previous both live until a third generation arrives. */
    {
        event_dispatch dispatch;
        const std::uint64_t first = dispatch.begin_scan();
        CHECK(dispatch.publish_scan_result(first, make_device("AA:BB:CC:DD:EE:01",
                                                              "A", -40,
                                                              device_kind::keyboard)));
        const std::uint64_t second = dispatch.begin_scan();
        CHECK(dispatch.publish_scan_result(second, make_device("AA:BB:CC:DD:EE:02",
                                                               "B", -41,
                                                               device_kind::keyboard)));
        /* Both are live (active + previous): nothing stale. */
        CHECK_EQ(dispatch.drop_stale(), std::size_t(0));
        CHECK_EQ(dispatch.pending(), std::size_t(2));
    }
    /* Scan gen equal to active survives trivially; pair gen equal to
     * previous survives a second rotation; connect gen equal to active. */
    {
        event_dispatch dispatch;
        const std::uint64_t pair = dispatch.begin_pairing("AA:BB:CC:DD:EE:09");
        CHECK(dispatch.publish_auth_request(pair, auth_request_kind::passkey, 5));
        (void)dispatch.begin_pairing("AA:BB:CC:DD:EE:0A");
        /* Previous pair generation still live: kept. */
        CHECK_EQ(dispatch.drop_stale(), std::size_t(0));
        CHECK_EQ(dispatch.pending(), std::size_t(1));
    }
    {
        event_dispatch dispatch;
        const std::uint64_t conn =
            dispatch.begin_connection("AA:BB:CC:DD:EE:08", true);
        CHECK(dispatch.publish_connected(conn));
        /* Active connect generation: kept. */
        CHECK_EQ(dispatch.drop_stale(), std::size_t(0));
        CHECK_EQ(dispatch.pending(), std::size_t(1));
        const std::vector<ble_event> events = drain_all(dispatch);
        CHECK_EQ(events.size(), std::size_t(1));
        CHECK(events[0].kind == ble_event_kind::connected);
    }
    /* Foreign event kind hits the switch fallthrough (is_stale default arm
     * returns true): queue a foreign-kind event is impossible via publish,
     * but drain/dequeue of an empty queue and the dequeue-true path pin the
     * remaining dequeue edges. */
    {
        event_dispatch dispatch;
        const std::uint64_t token = dispatch.begin_scan();
        CHECK(dispatch.publish_scan_started(token));
        /* dequeue true path (line 55 miss): drain moves the event out. */
        ble_event out[1];
        CHECK_EQ(dispatch.drain(out, 1), std::size_t(1));
        CHECK(out[0].kind == ble_event_kind::scan_started);
        CHECK_EQ(dispatch.pending(), std::size_t(0));
        /* Empty dequeue leaves the buffer untouched. */
        ble_event sentinel;
        sentinel.kind = ble_event_kind::disconnected;
        sentinel.token = 0xDEADULL;
        ble_event buf[1] = {sentinel};
        CHECK_EQ(dispatch.drain(buf, 1), std::size_t(0));
        CHECK_EQ(buf[0].token, 0xDEADULL);
    }
    /* pair_finished default arm: a foreign pair outcome still formats (the
     * switch has no default but every enumerator is covered; a foreign value
     * falls through to an empty suffix, bounded). */
    {
        event_dispatch dispatch;
        ble_event finished;
        finished.kind = ble_event_kind::pair_finished;
        finished.pair_result = static_cast<pair_outcome>(99);
        const std::string line = dispatch.event_summary(finished);
        CHECK(line.find("BLE pair finished:") != std::string::npos);
        CHECK(line.size() < 128);
    }
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
    test_auth_event_preserves_io_action_and_peer_identity();
    test_connection_generation_distinguishes_manual_from_automatic();
    test_connection_address_type_survives_event_generation();
    test_hid_discovery_is_additional_bounded_and_stale_safe();
    test_hid_summary_contains_capability_only();
    test_drop_stale_discards_superseded_generations();
    test_event_summary_never_carries_a_secret();
    test_bounded_name_in_a_snapshot_cannot_inject_control_characters();
    test_coverage_zero_token_guards_are_fail_closed();
    test_coverage_stale_generations_and_drop_stale_per_kind();
    test_coverage_every_summary_shape_without_secrets();
    test_coverage_compaction_order_and_foreign_kind_summary();
    test_coverage_dequeue_empty_leaves_output_and_drain_edges();
    test_coverage_mismatch_tokens_per_kind_are_fail_closed();
    test_coverage_full_queue_rejects_every_terminal_kind();
    test_coverage_reset_clears_generations_and_summary_defaults();
    test_coverage_cycle2_stale_guard_rhs();

    std::printf("ble event dispatch contract: %d checks, %d failures\n",
                checks, failures);
    return failures == 0 ? 0 : 1;
}
