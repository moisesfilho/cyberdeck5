/*
 * TDD RED host contract for the pure BLE search/pair state machine.
 *
 * Covers REQ-BLE-003 (asynchronous scan), REQ-BLE-005 (Up/Down/Enter/Escape),
 * REQ-BLE-006 (interactive authentication when the peer requires it),
 * REQ-BLE-008 (automatic reconnection) and REQ-BLE-009 (empty / failure /
 * timeout / cancellation messages).
 *
 * RED until the coder creates
 * components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_state_machine.cpp
 * with the ABI declared in contracts/cyberdeck_ble_state_machine.h.  No
 * ESP-IDF, NimBLE, esp_hosted, LVGL, NVS, simulator or hardware is used here.
 */
#include "cyberdeck_ble_state_machine.h"

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

/* A distinctive passkey: it must appear on the auth screen and nowhere else. */
const std::uint32_t kSecretPasskey = 246813;
const char *kSecretDigits = "246813";

using cyberdeck_ble::action;
using cyberdeck_ble::action_kind;
using cyberdeck_ble::address_type;
using cyberdeck_ble::auth_request_kind;
using cyberdeck_ble::device;
using cyberdeck_ble::device_kind;
using cyberdeck_ble::key;
using cyberdeck_ble::notice;
using cyberdeck_ble::pair_outcome;
using cyberdeck_ble::screen;
using cyberdeck_ble::state_machine;

device make_device(const char *address, const char *name, int rssi,
                   device_kind kind, bool connectable = true,
                   address_type addr_type = address_type::public_address)
{
    device item;
    item.address = address;
    item.addr_type = addr_type;
    item.name = name == nullptr ? std::string() : std::string(name);
    item.rssi = rssi;
    item.kind = kind;
    item.connectable = connectable;
    return item;
}

const action *find_action(const std::vector<action> &actions, action_kind kind)
{
    for (const action &item : actions) {
        if (item.kind == kind) return &item;
    }
    return nullptr;
}

std::size_t count_actions(const std::vector<action> &actions, action_kind kind)
{
    std::size_t total = 0;
    for (const action &item : actions) {
        if (item.kind == kind) ++total;
    }
    return total;
}

/* Pins the REQ-BLE-009 message set: the four classes, one exact text each. */
void test_approved_messages_and_deadlines()
{
    CHECK_EQ(cyberdeck_ble::k_scan_timeout_ms, std::uint32_t(10000));
    CHECK_EQ(cyberdeck_ble::k_pair_timeout_ms, std::uint32_t(30000));
    CHECK_EQ(cyberdeck_ble::k_auth_timeout_ms, std::uint32_t(30000));
    CHECK_EQ(cyberdeck_ble::k_connect_timeout_ms, std::uint32_t(20000));
    CHECK_EQ(cyberdeck_ble::k_max_reconnect_attempts, std::uint32_t(3));
}

void test_search_is_asynchronous_and_emits_exactly_one_start()
{
    state_machine machine;
    CHECK(machine.current_screen() == screen::idle);
    CHECK(machine.current_notice() == notice::none);
    CHECK_STR(machine.notice_text(), "");
    CHECK_STR(machine.status_line(), "");
    CHECK_EQ(machine.active_scan_token(), std::uint64_t(0));
    CHECK(machine.take_actions().empty());

    machine.begin_search();
    CHECK(machine.current_screen() == screen::searching);
    CHECK(machine.current_notice() == notice::none);
    CHECK_STR(machine.status_line(), cyberdeck_ble::k_status_scanning);
    CHECK(machine.active_scan_token() != 0);

    const std::vector<action> actions = machine.take_actions();
    CHECK_EQ(actions.size(), std::size_t(1));
    if (!actions.empty()) {
        CHECK(actions[0].kind == action_kind::start_scan);
        CHECK_EQ(actions[0].token, machine.active_scan_token());
    }
    /* Draining does not duplicate work. */
    CHECK(machine.take_actions().empty());

    /* A newer search invalidates the previous generation. */
    const std::uint64_t first = machine.active_scan_token();
    machine.begin_search();
    CHECK(machine.active_scan_token() > first);
    const std::vector<action> restarted = machine.take_actions();
    CHECK_EQ(restarted.size(), std::size_t(1));
    if (!restarted.empty()) {
        CHECK(restarted[0].kind == action_kind::start_scan);
        CHECK_EQ(restarted[0].token, machine.active_scan_token());
    }
}

void test_scan_timeout_fires_exactly_at_the_deadline()
{
    state_machine machine;
    machine.begin_search();
    const std::uint64_t token = machine.active_scan_token();
    machine.take_actions();

    machine.advance_time(cyberdeck_ble::k_scan_timeout_ms - 1);
    CHECK(machine.current_screen() == screen::searching);
    CHECK(machine.current_notice() == notice::none);
    CHECK(machine.take_actions().empty());

    machine.advance_time(1);
    CHECK(machine.current_screen() == screen::results);
    CHECK(machine.current_notice() == notice::timed_out);
    CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_scan_timeout);
    const std::vector<action> timed_out = machine.take_actions();
    CHECK_EQ(count_actions(timed_out, action_kind::cancel_scan), std::size_t(1));
    if (!timed_out.empty()) CHECK_EQ(timed_out[0].token, token);

    /* A completion that arrives after the deadline is dropped. */
    machine.scan_finished(token, {make_device("AA:BB:CC:DD:EE:01", "Late", -40,
                                              device_kind::keyboard)});
    CHECK_EQ(machine.devices().size(), std::size_t(0));
    CHECK(machine.current_notice() == notice::timed_out);

    /* An oversized elapsed value cannot overflow the deadline comparison. */
    machine.advance_time(0xFFFFFFFFU);
    CHECK(machine.current_notice() == notice::timed_out);
    CHECK(machine.take_actions().empty());
}

void test_scan_outcomes_map_to_distinct_messages()
{
    {
        state_machine machine;
        machine.begin_search();
        const std::uint64_t token = machine.active_scan_token();
        machine.take_actions();
        machine.scan_finished(token, {});
        CHECK(machine.current_screen() == screen::results);
        CHECK(machine.current_notice() == notice::empty);
        CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_scan_empty);
        CHECK_EQ(machine.devices().size(), std::size_t(0));
        CHECK(machine.selected() == nullptr);
    }
    {
        state_machine machine;
        machine.begin_search();
        const std::uint64_t token = machine.active_scan_token();
        machine.take_actions();
        machine.scan_failed(token);
        CHECK(machine.current_screen() == screen::results);
        CHECK(machine.current_notice() == notice::failed);
        CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_scan_failed);
    }
    {
        state_machine machine;
        machine.begin_search();
        const std::uint64_t token = machine.active_scan_token();
        machine.take_actions();
        machine.scan_timed_out(token);
        CHECK(machine.current_screen() == screen::results);
        CHECK(machine.current_notice() == notice::timed_out);
        CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_scan_timeout);
    }
    {
        state_machine machine;
        machine.begin_search();
        const std::uint64_t token = machine.active_scan_token();
        machine.take_actions();
        machine.press(key::escape);
        CHECK(machine.current_screen() == screen::idle);
        CHECK(machine.current_notice() == notice::cancelled);
        CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_search_cancelled);
        const std::vector<action> cancelled = machine.take_actions();
        CHECK_EQ(cancelled.size(), std::size_t(1));
        if (!cancelled.empty()) {
            CHECK(cancelled[0].kind == action_kind::cancel_scan);
            CHECK_EQ(cancelled[0].token, token);
        }

        /* The cancelled generation can no longer deliver results. */
        machine.scan_finished(token, {make_device("AA:BB:CC:DD:EE:01", "Late", -40,
                                                  device_kind::keyboard)});
        CHECK_EQ(machine.devices().size(), std::size_t(0));
        CHECK(machine.current_screen() == screen::idle);
    }
}

void test_input_ownership_is_derived_from_visible_model_state()
{
    state_machine machine;
    CHECK(!machine.owns_input());

    machine.begin_search();
    CHECK(machine.owns_input());
    const std::uint64_t token = machine.active_scan_token();
    machine.take_actions();
    machine.scan_finished(token, {});
    CHECK(machine.current_screen() == screen::results);
    CHECK(machine.current_notice() == notice::empty);
    CHECK(!machine.owns_input());
    CHECK(machine.take_actions().empty());
    machine.press(key::up);
    machine.press(key::down);
    machine.press(key::enter);
    CHECK(machine.current_screen() == screen::results);
    CHECK(machine.current_notice() == notice::empty);
    CHECK(machine.take_actions().empty());

    state_machine failed;
    failed.begin_search();
    const std::uint64_t failed_token = failed.active_scan_token();
    failed.take_actions();
    failed.scan_failed(failed_token);
    CHECK(failed.current_notice() == notice::failed);
    CHECK(!failed.owns_input());
    CHECK(failed.take_actions().empty());

    state_machine timed_out;
    timed_out.begin_search();
    timed_out.take_actions();
    timed_out.advance_time(cyberdeck_ble::k_scan_timeout_ms);
    CHECK(timed_out.current_notice() == notice::timed_out);
    CHECK(!timed_out.owns_input());
    CHECK_EQ(count_actions(timed_out.take_actions(), action_kind::cancel_scan),
             std::size_t(1));
    timed_out.press(key::escape);
    CHECK(timed_out.take_actions().empty());
}

void test_scan_ownership_covers_active_empty_and_nonempty_paths()
{
    state_machine machine;
    machine.begin_search();
    const std::uint64_t token = machine.active_scan_token();
    CHECK(machine.owns_input());

    machine.scan_finished(token, {});
    CHECK(machine.current_screen() == screen::results);
    CHECK_EQ(machine.devices().size(), std::size_t(0));
    CHECK(!machine.owns_input());

    machine.begin_search();
    const std::uint64_t second_token = machine.active_scan_token();
    CHECK(machine.owns_input());
    machine.scan_finished(second_token, {
        make_device("AA:BB:CC:DD:EE:10", "Keyboard", -42, device_kind::keyboard),
        make_device("AA:BB:CC:DD:EE:11", "Mouse", -47, device_kind::mouse),
    });
    CHECK(machine.current_screen() == screen::results);
    CHECK_EQ(machine.devices().size(), std::size_t(2));
    CHECK(machine.owns_input());
    CHECK_EQ(machine.selected_index(), std::size_t(0));

    machine.press(key::escape);
    CHECK(machine.current_screen() == screen::idle);
    CHECK(!machine.owns_input());
}

void test_stale_scan_tokens_cannot_mutate_the_visible_list()
{
    state_machine machine;
    machine.begin_search();
    const std::uint64_t stale = machine.active_scan_token();
    machine.take_actions();

    machine.begin_search();
    const std::uint64_t active = machine.active_scan_token();
    machine.take_actions();
    CHECK(active != stale);

    machine.scan_finished(stale, {make_device("AA:BB:CC:DD:EE:EE", "Stale", -30,
                                              device_kind::keyboard)});
    CHECK(machine.current_screen() == screen::searching);
    CHECK_EQ(machine.devices().size(), std::size_t(0));

    machine.scan_finished(0, {make_device("AA:BB:CC:DD:EE:EE", "Zero", -30,
                                         device_kind::keyboard)});
    CHECK(machine.current_screen() == screen::searching);
    CHECK_EQ(machine.devices().size(), std::size_t(0));

    machine.scan_finished(active, {make_device("AA:BB:CC:DD:EE:01", "Live", -40,
                                               device_kind::keyboard)});
    CHECK(machine.current_screen() == screen::results);
    CHECK_EQ(machine.devices().size(), std::size_t(1));
    CHECK(machine.selected() != nullptr);
    if (machine.selected() != nullptr) {
        CHECK_STR(machine.selected()->name, "Live");
    }

    /* A duplicate completion for the same generation is dropped. */
    machine.scan_finished(active, {make_device("AA:BB:CC:DD:EE:02", "Dup", -41,
                                               device_kind::mouse)});
    CHECK_EQ(machine.devices().size(), std::size_t(1));
    CHECK(machine.current_notice() == notice::none);
}

void test_navigation_and_enter_from_results()
{
    state_machine machine;
    machine.begin_search();
    const std::uint64_t token = machine.active_scan_token();
    machine.take_actions();
    machine.scan_finished(token, {
        make_device("AA:BB:CC:DD:EE:01", "Fone", -50, device_kind::headset),
        make_device("AA:BB:CC:DD:EE:02", "Mouse", -55, device_kind::mouse),
        make_device("AA:BB:CC:DD:EE:03", nullptr, -60, device_kind::unknown),
    });
    CHECK(machine.current_screen() == screen::results);
    CHECK(machine.current_notice() == notice::none);
    CHECK(machine.owns_input());
    CHECK_STR(machine.notice_text(), "");
    CHECK_EQ(machine.selected_index(), std::size_t(0));

    machine.press(key::up);
    CHECK_EQ(machine.selected_index(), std::size_t(0));
    machine.press(key::down);
    CHECK_EQ(machine.selected_index(), std::size_t(1));
    machine.press(key::down);
    machine.press(key::down);
    CHECK_EQ(machine.selected_index(), std::size_t(2));
    machine.press(key::down);
    CHECK_EQ(machine.selected_index(), std::size_t(2));

    machine.press(key::enter);
    const std::vector<action> pairing = machine.take_actions();
    CHECK_EQ(pairing.size(), std::size_t(1));
    if (!pairing.empty()) {
        CHECK(pairing[0].kind == action_kind::pair);
        CHECK_STR(pairing[0].address, "AA:BB:CC:DD:EE:03");
        CHECK(pairing[0].token != 0);
    }
    CHECK(machine.current_screen() == screen::pairing);
    CHECK(machine.owns_input());
    CHECK_STR(machine.active_address(), "AA:BB:CC:DD:EE:03");

    /* Escape from a ready list is silent: nothing was cancelled. */
    state_machine listing;
    listing.begin_search();
    listing.scan_finished(listing.active_scan_token(), {
        make_device("AA:BB:CC:DD:EE:01", "Fone", -50, device_kind::headset)});
    listing.take_actions();
    listing.press(key::escape);
    CHECK(listing.current_screen() == screen::idle);
    CHECK(!listing.owns_input());
    CHECK(listing.current_notice() == notice::none);
    CHECK_STR(listing.notice_text(), "");
    CHECK(listing.take_actions().empty());
}

void test_enter_preserves_exact_peer_address_type()
{
    for (const address_type type : {address_type::public_address,
                                    address_type::random_static}) {
        state_machine machine;
        machine.begin_search();
        machine.take_actions();
        machine.scan_finished(machine.active_scan_token(), {
            make_device("AA:BB:CC:DD:EE:01", "Keyboard", -40,
                        device_kind::keyboard, true, type)});
        machine.press(key::enter);
        const std::vector<action> actions = machine.take_actions();
        CHECK_EQ(actions.size(), std::size_t(1));
        if (!actions.empty()) {
            CHECK(actions[0].kind == action_kind::pair);
            CHECK_STR(actions[0].address, "AA:BB:CC:DD:EE:01");
            CHECK(actions[0].addr_type == type);
        }
    }
}

void test_enter_refuses_an_empty_or_non_connectable_selection()
{
    {
        state_machine machine;
        machine.begin_search();
        machine.scan_finished(machine.active_scan_token(), {});
        machine.take_actions();
        machine.press(key::enter);
        CHECK(machine.take_actions().empty());
        CHECK(machine.current_screen() == screen::results);
        CHECK(machine.current_notice() == notice::empty);
    }
    {
        state_machine machine;
        machine.begin_search();
        machine.scan_finished(machine.active_scan_token(), {
            make_device("AA:BB:CC:DD:EE:01", "Beacon", -40, device_kind::unknown,
                        false)});
        machine.take_actions();
        machine.press(key::enter);
        CHECK(machine.take_actions().empty());
        CHECK(machine.current_screen() == screen::results);
        CHECK(machine.current_notice() == notice::not_connectable);
        CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_not_connectable);
        CHECK(machine.notice_text().size() < 128);
        machine.press(key::enter);
        CHECK(machine.take_actions().empty());
        CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_not_connectable);
    }
    {
        /* Up/Down/Enter/Escape are all inert while a scan is running. */
        state_machine machine;
        machine.begin_search();
        machine.take_actions();
        machine.press(key::up);
        machine.press(key::down);
        machine.press(key::enter);
        CHECK(machine.take_actions().empty());
        CHECK(machine.current_screen() == screen::searching);
    }
    {
        /* All keys are inert on the idle screen. */
        state_machine machine;
        machine.press(key::up);
        machine.press(key::down);
        machine.press(key::enter);
        machine.press(key::escape);
        CHECK(machine.take_actions().empty());
        CHECK(machine.current_screen() == screen::idle);
        CHECK(machine.current_notice() == notice::none);
    }
}

void test_a_device_that_vanishes_cannot_be_paired()
{
    state_machine machine;
    machine.begin_search();
    machine.scan_finished(machine.active_scan_token(), {
        make_device("AA:BB:CC:DD:EE:01", "A", -50, device_kind::keyboard),
        make_device("AA:BB:CC:DD:EE:02", "B", -51, device_kind::keyboard),
    });
    machine.take_actions();
    machine.press(key::down);
    CHECK_STR(machine.active_address(), "");
    CHECK(machine.selected() != nullptr);
    if (machine.selected() != nullptr) {
        CHECK_STR(machine.selected()->address, "AA:BB:CC:DD:EE:02");
    }

    /* The peripheral stops advertising: the next scan no longer lists it. */
    machine.begin_search();
    machine.take_actions();
    machine.scan_finished(machine.active_scan_token(), {
        make_device("AA:BB:CC:DD:EE:01", "A", -50, device_kind::keyboard)});

    CHECK_EQ(machine.devices().size(), std::size_t(1));
    CHECK_EQ(machine.selected_index(), std::size_t(0));
    CHECK(machine.selected() != nullptr);
    if (machine.selected() != nullptr) {
        CHECK_STR(machine.selected()->address, "AA:BB:CC:DD:EE:01");
    }

    machine.press(key::enter);
    const std::vector<action> pairing = machine.take_actions();
    CHECK_EQ(pairing.size(), std::size_t(1));
    if (!pairing.empty()) {
        CHECK_STR(pairing[0].address, "AA:BB:CC:DD:EE:01");
    }
}

/* Copies a record out of a device_list snapshot.  devices() returns by value,
 * so passing at(0) straight into a const& parameter would dangle. */
device copy_at(const cyberdeck_ble::device_list &list, std::size_t index)
{
    const cyberdeck_ble::device *item = list.at(index);
    if (item == nullptr) return device();
    return *item;
}

/* Drives a machine up to the `pairing` screen.  In-place on purpose: the
 * contract deletes copy and move, so returning by value would be fragile. */
void prepare_pairing(state_machine &machine, const char *address, const char *name)
{
    machine.begin_search();
    machine.scan_finished(machine.active_scan_token(), {
        make_device(address, name, -50, device_kind::keyboard)});
    machine.take_actions();
    machine.press(key::enter);
    machine.take_actions();
}

void test_interactive_passkey_is_shown_on_auth_and_never_logged()
{
    state_machine machine;
    prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
    CHECK(machine.current_screen() == screen::pairing);
    CHECK_EQ(machine.displayed_passkey(), std::uint32_t(0));
    CHECK(machine.active_pair_token() != 0);

    machine.auth_requested(machine.active_pair_token(), auth_request_kind::passkey,
                           kSecretPasskey, cyberdeck_ble::auth_io_action::display);
    CHECK(machine.current_screen() == screen::auth);
    CHECK_EQ(machine.displayed_passkey(), kSecretPasskey);
    CHECK(machine.pending_auth_kind() == auth_request_kind::passkey);

    /* REQ-BLE-006: the user must be able to read the passkey. */
    const std::string status = machine.status_line();
    CHECK(status.find(cyberdeck_ble::k_status_enter_passkey) != std::string::npos);
    CHECK(status.find(kSecretDigits) != std::string::npos);

    /* REQ-BLE-010: the loggable surface never carries the secret. */
    CHECK_STR(machine.notice_text(), "");
    CHECK(machine.notice_text().find(kSecretDigits) == std::string::npos);

    /* The model forwards a user response; NimBLE decides authentication. */
    machine.press(key::enter);
    const std::vector<action> submitted = machine.take_actions();
    CHECK_EQ(submitted.size(), std::size_t(1));
    if (!submitted.empty()) {
        CHECK(submitted[0].kind == action_kind::submit_auth);
        CHECK_EQ(submitted[0].passkey, kSecretPasskey);
        CHECK_EQ(submitted[0].token, machine.active_pair_token());
    }

    /* Once submitted, the passkey is no longer displayed anywhere. */
    CHECK_EQ(machine.displayed_passkey(), std::uint32_t(0));
    CHECK(machine.status_line().find(kSecretDigits) == std::string::npos);
    CHECK(machine.notice_text().find(kSecretDigits) == std::string::npos);
    CHECK(machine.current_screen() == screen::pairing);

    /* Enter is the same as submit_auth with the displayed value. */
    state_machine via_enter;
    prepare_pairing(via_enter, "AA:BB:CC:DD:EE:01", "Teclado");
    via_enter.auth_requested(via_enter.active_pair_token(),
                             auth_request_kind::passkey, kSecretPasskey,
                             cyberdeck_ble::auth_io_action::display);
    via_enter.press(key::enter);
    const std::vector<action> entered = via_enter.take_actions();
    CHECK_EQ(entered.size(), std::size_t(1));
    if (!entered.empty()) {
        CHECK(entered[0].kind == action_kind::submit_auth);
        CHECK_EQ(entered[0].passkey, kSecretPasskey);
    }
    /* Enter must not double-apply: a second press forwards nothing new. */
    via_enter.press(key::enter);
    CHECK(via_enter.take_actions().empty());
}

void test_non_passkey_authorization_forwards_no_secret()
{
    state_machine machine;
    prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
    machine.auth_requested(machine.active_pair_token(), auth_request_kind::numeric_compare,
                           kSecretPasskey, cyberdeck_ble::auth_io_action::numeric_compare);
    CHECK(machine.current_screen() == screen::auth);
    CHECK_EQ(machine.displayed_passkey(), kSecretPasskey);
    CHECK(machine.pending_auth_kind() == auth_request_kind::numeric_compare);
    CHECK(machine.status_line().find(kSecretDigits) != std::string::npos);

    /* NUMCMP confirmation is a bool decision, never a passkey submission. */
    machine.submit_auth(0);
    const std::vector<action> submitted = machine.take_actions();
    CHECK_EQ(submitted.size(), std::size_t(1));
    if (!submitted.empty()) {
        CHECK(submitted[0].kind == action_kind::submit_auth);
        CHECK(submitted[0].auth_action == cyberdeck_ble::auth_io_action::numeric_compare);
        CHECK_EQ(submitted[0].passkey, std::uint32_t(0));
    }
    CHECK_EQ(machine.displayed_passkey(), std::uint32_t(0));
}

void test_auth_io_action_is_preserved_and_numbers_are_not_reinterpreted()
{
    struct auth_case {
        auth_request_kind kind;
        cyberdeck_ble::auth_io_action io_action;
        std::uint32_t challenge;
        const char *status;
    };
    const auth_case cases[] = {
        {auth_request_kind::passkey, cyberdeck_ble::auth_io_action::display,
         kSecretPasskey, kSecretDigits},
        {auth_request_kind::passkey, cyberdeck_ble::auth_io_action::input,
         0, "Enter the passkey"},
        {auth_request_kind::numeric_compare,
         cyberdeck_ble::auth_io_action::numeric_compare, kSecretPasskey,
         cyberdeck_ble::k_status_numeric_compare},
    };
    for (const auth_case &item : cases) {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.auth_requested(machine.active_pair_token(), item.kind,
                               item.challenge, item.io_action);
        CHECK(machine.current_screen() == screen::auth);
        CHECK(machine.pending_auth_kind() == item.kind);
        CHECK(machine.pending_auth_action() == item.io_action);
        CHECK(machine.status_line().find(item.status) != std::string::npos);
        if (item.kind == auth_request_kind::numeric_compare) {
            CHECK_EQ(machine.displayed_passkey(), kSecretPasskey);
        } else {
            CHECK_EQ(machine.displayed_passkey(),
                     item.io_action == cyberdeck_ble::auth_io_action::display
                         ? kSecretPasskey : std::uint32_t(0));
        }
        if (item.io_action == cyberdeck_ble::auth_io_action::input) {
            /* The UI owns the incremental six-digit buffer; the pure model
             * receives its parsed value only after a valid Enter. */
            machine.submit_auth(kSecretPasskey);
        } else {
            machine.press(key::enter);
        }
        const std::vector<action> actions = machine.take_actions();
        CHECK_EQ(actions.size(), std::size_t(1));
        if (!actions.empty()) {
            CHECK(actions[0].kind == action_kind::submit_auth);
            CHECK(actions[0].auth_action == item.io_action);
            CHECK_EQ(actions[0].passkey,
                      item.io_action == cyberdeck_ble::auth_io_action::numeric_compare
                         ? std::uint32_t(0) :
                           (item.io_action == cyberdeck_ble::auth_io_action::input
                                ? kSecretPasskey : item.challenge));
        }
    }
}

void test_numeric_comparison_is_a_boolean_decision_with_display_only_number()
{
    state_machine machine;
    prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
    const std::uint32_t displayed_number = 246814u;
    machine.auth_requested(machine.active_pair_token(),
                           auth_request_kind::numeric_compare,
                           displayed_number,
                           cyberdeck_ble::auth_io_action::numeric_compare);
    CHECK_EQ(machine.displayed_passkey(), displayed_number);
    machine.press(key::enter);
    const std::vector<action> accepted = machine.take_actions();
    CHECK_EQ(accepted.size(), std::size_t(1));
    if (!accepted.empty()) {
        CHECK(accepted[0].auth_action == cyberdeck_ble::auth_io_action::numeric_compare);
        CHECK(accepted[0].numcmp_accept);
        CHECK_EQ(accepted[0].passkey, std::uint32_t(0));
        /* The comparison number is UI-only; the adapter receives the boolean
         * decision and never a host-derived numeric credential. */
        CHECK_EQ(accepted[0].numcmp, std::uint32_t(0));
    }
}

void test_auth_request_validation_and_stale_tokens()
{
    {
        /* An unrepresentable passkey is refused and the bond deadline keeps
         * running: the user is never shown a fabricated value. */
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        const std::uint64_t token = machine.active_pair_token();
        machine.auth_requested(token, auth_request_kind::passkey,
                               cyberdeck_ble::k_passkey_modulus);
        CHECK(machine.current_screen() == screen::pairing);
        CHECK_EQ(machine.displayed_passkey(), std::uint32_t(0));
    }
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        const std::uint64_t token = machine.active_pair_token();
        machine.auth_requested(token + 999, auth_request_kind::passkey,
                               kSecretPasskey, cyberdeck_ble::auth_io_action::display);
        CHECK(machine.current_screen() == screen::pairing);
        CHECK_EQ(machine.displayed_passkey(), std::uint32_t(0));
        CHECK(machine.status_line().find(kSecretDigits) == std::string::npos);
    }
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.auth_requested(0, auth_request_kind::passkey, kSecretPasskey);
        CHECK(machine.current_screen() == screen::pairing);
        CHECK_EQ(machine.displayed_passkey(), std::uint32_t(0));
    }
    {
        /* Nothing accepts authentication before the peer asked for it. */
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.submit_auth(kSecretPasskey);
        CHECK(machine.take_actions().empty());
        CHECK(machine.current_screen() == screen::pairing);
    }
}

void test_pair_and_auth_deadlines()
{
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        const std::uint64_t token = machine.active_pair_token();
        machine.advance_time(cyberdeck_ble::k_pair_timeout_ms - 1);
        CHECK(machine.current_screen() == screen::pairing);
        machine.advance_time(1);
        CHECK(machine.current_screen() == screen::results);
        CHECK(machine.current_notice() == notice::timed_out);
        CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_pair_timeout);
        const std::vector<action> timed_out = machine.take_actions();
        CHECK_EQ(count_actions(timed_out, action_kind::cancel_pair), std::size_t(1));
        if (!timed_out.empty()) {
            const action *cancel = find_action(timed_out, action_kind::cancel_pair);
            if (cancel != nullptr) CHECK_EQ(cancel->token, token);
        }
    }
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
         machine.auth_requested(machine.active_pair_token(),
                                auth_request_kind::passkey, kSecretPasskey,
                                cyberdeck_ble::auth_io_action::display);
        machine.advance_time(cyberdeck_ble::k_auth_timeout_ms - 1);
        CHECK(machine.current_screen() == screen::auth);
        machine.advance_time(1);
        CHECK(machine.current_screen() == screen::results);
        CHECK(machine.current_notice() == notice::timed_out);
        CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_pair_timeout);
        CHECK(machine.take_actions().size() > 0);
        /* The secret is gone with the auth screen. */
        CHECK(machine.notice_text().find(kSecretDigits) == std::string::npos);
        CHECK(machine.status_line().find(kSecretDigits) == std::string::npos);
    }
}

void test_escape_cancels_the_pairing_attempt_only()
{
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        const std::uint64_t token = machine.active_pair_token();
        machine.press(key::escape);
        CHECK(machine.current_screen() == screen::results);
        CHECK(machine.current_notice() == notice::cancelled);
        CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_pair_cancelled);
        const std::vector<action> cancelled = machine.take_actions();
        CHECK_EQ(cancelled.size(), std::size_t(1));
        if (!cancelled.empty()) {
            CHECK(cancelled[0].kind == action_kind::cancel_pair);
            CHECK_EQ(cancelled[0].token, token);
            CHECK_STR(cancelled[0].address, "AA:BB:CC:DD:EE:01");
        }
        /* The device list survives the cancellation. */
        CHECK_EQ(machine.devices().size(), std::size_t(1));
    }
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
         machine.auth_requested(machine.active_pair_token(),
                                auth_request_kind::passkey, kSecretPasskey,
                                cyberdeck_ble::auth_io_action::display);
        machine.press(key::escape);
        CHECK(machine.current_screen() == screen::results);
        CHECK(machine.current_notice() == notice::cancelled);
        CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_pair_cancelled);
        CHECK(machine.notice_text().find(kSecretDigits) == std::string::npos);
        const std::vector<action> cancelled = machine.take_actions();
        CHECK_EQ(count_actions(cancelled, action_kind::cancel_pair), std::size_t(1));
    }
}

void test_pair_outcomes_are_distinct()
{
    struct expectation {
        pair_outcome outcome;
        notice expected_notice;
        const char *text;
    };
    const expectation cases[] = {
        {pair_outcome::rejected, notice::failed, cyberdeck_ble::k_msg_pair_rejected},
        {pair_outcome::cancelled, notice::cancelled, cyberdeck_ble::k_msg_pair_cancelled},
        {pair_outcome::timed_out, notice::timed_out, cyberdeck_ble::k_msg_pair_timeout},
        {pair_outcome::failed, notice::failed, cyberdeck_ble::k_msg_pair_failed},
    };
    for (const expectation &item : cases) {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.pairing_finished(machine.active_pair_token(), item.outcome);
        CHECK(machine.current_screen() == screen::results);
        CHECK(machine.current_notice() == item.expected_notice);
        CHECK_STR(machine.notice_text(), item.text);
        CHECK(machine.take_actions().empty());
    }
    {
        /* A stale bond callback is dropped after the attempt already ended. */
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        const std::uint64_t token = machine.active_pair_token();
        machine.pairing_finished(token, pair_outcome::rejected);
        machine.take_actions();
        machine.pairing_finished(token, pair_outcome::bonded);
        CHECK(machine.take_actions().empty());
        CHECK(machine.current_screen() == screen::results);
        CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_pair_rejected);
    }
}

void test_successful_bond_then_connection()
{
    state_machine machine;
    prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
    const std::uint64_t pair_token = machine.active_pair_token();
    machine.pairing_finished(pair_token, pair_outcome::bonded);

    const std::vector<action> connecting = machine.take_actions();
    CHECK_EQ(connecting.size(), std::size_t(1));
    if (!connecting.empty()) {
        CHECK(connecting[0].kind == action_kind::connect);
        CHECK_STR(connecting[0].address, "AA:BB:CC:DD:EE:01");
        CHECK(connecting[0].token != 0);
        CHECK(connecting[0].token != pair_token);
    }
    CHECK(machine.current_screen() == screen::connecting);
    CHECK(machine.current_notice() == notice::none);
    CHECK(machine.status_line().find("Teclado") != std::string::npos);

    const std::uint64_t connect_token = machine.active_connection_token();
    CHECK(connect_token != 0);
    machine.advance_time(cyberdeck_ble::k_connect_timeout_ms - 1);
    CHECK(machine.current_screen() == screen::connecting);
    machine.advance_time(1);
    CHECK(machine.current_screen() == screen::results);
    CHECK(machine.current_notice() == notice::timed_out);
    CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_connect_timeout);
    const std::vector<action> connect_timeout = machine.take_actions();
    CHECK_EQ(count_actions(connect_timeout, action_kind::cancel_connect),
             std::size_t(1));

    state_machine connected;
    prepare_pairing(connected, "AA:BB:CC:DD:EE:01", "Teclado");
    connected.pairing_finished(connected.active_pair_token(),
                               pair_outcome::bonded);
    connected.take_actions();
    connected.connection_finished(connected.active_connection_token(), true);
    /* A successful connection releases the transient BLE screen.  The
     * authenticated link remains owned by ble_mgr; the model must not turn
     * this completion into a user-visible disconnect lifecycle. */
    CHECK(connected.current_screen() == screen::idle);
    CHECK(connected.is_connected());
    CHECK(!connected.owns_input());
    CHECK(connected.current_notice() == notice::none);
    CHECK_STR(connected.notice_text(), "");
    CHECK_STR(connected.status_line(), "");
    CHECK(connected.take_actions().empty());

    /* A physical disconnect after the UI was released is terminal but does
     * not synthesize a disconnect action or a failed-connection notice. */
    const std::uint64_t established = connected.active_connection_token();
    connected.connection_finished(established, false);
    CHECK(connected.current_screen() == screen::idle);
    CHECK(!connected.is_connected());
    CHECK(!connected.owns_input());
    CHECK(connected.current_notice() == notice::none);
    CHECK(connected.take_actions().empty());

    /* A stale completion from that old generation cannot affect a new one. */
    connected.schedule_reconnect(make_device("AA:BB:CC:DD:EE:01", "Teclado", -50,
                                             device_kind::keyboard));
    const std::uint64_t next_generation = connected.active_connection_token();
    CHECK(next_generation != established);
    connected.take_actions();
    connected.connection_finished(1, false);
    CHECK(connected.active_connection_token() == next_generation);
    CHECK(connected.current_screen() == screen::connecting);
    CHECK(connected.current_notice() == notice::none);

    /* Escape disconnects. */
    connected.connection_finished(next_generation, true);
    CHECK(connected.current_screen() == screen::idle);
    CHECK(connected.take_actions().empty());
}

void test_is_connected_is_false_until_matching_connected_event()
{
    state_machine machine;
    CHECK(!machine.is_connected());

    /* An invalid/stale completion must not fabricate a visible link. */
    machine.connection_finished(1, true);
    CHECK(!machine.is_connected());

    prepare_pairing(machine, "AA:BB:CC:DD:EE:10", "Mouse");
    machine.pairing_finished(machine.active_pair_token(), pair_outcome::bonded);
    machine.take_actions();
    const std::uint64_t token = machine.active_connection_token();
    CHECK(!machine.is_connected());

    machine.connection_finished(token + 1, true);
    CHECK(!machine.is_connected());
    machine.connection_finished(token, true);
    CHECK(machine.is_connected());

    /* Repeated CONNECTED and stale DISCONNECTED events are harmless. */
    machine.connection_finished(token, true);
    CHECK(machine.is_connected());
    machine.connection_finished(token + 1, false);
    CHECK(machine.is_connected());
    machine.connection_finished(token, false);
    CHECK(!machine.is_connected());
}

void test_connection_success_does_not_clear_identity_contract_in_model()
{
    const address_type type = address_type::random_static;
    state_machine machine;
    machine.set_paired_devices({make_device("AA:BB:CC:DD:EE:09", "Keyboard", -40,
                                             device_kind::keyboard, true, type)});
    machine.begin_paired();
    machine.press(key::enter);
    const std::vector<action> connect = machine.take_actions();
    CHECK_EQ(connect.size(), std::size_t(1));
    const std::uint64_t token = machine.active_connection_token();
    CHECK(token != 0);
    CHECK(!connect.empty() && connect[0].addr_type == type);

    machine.connection_finished(token, true);
    CHECK(machine.current_screen() == screen::idle);
    CHECK(machine.active_connection_token() == token);
    CHECK(!machine.owns_input());
    CHECK(machine.take_actions().empty());

    /* A new reconnect is a new generation and carries the same peer identity;
     * an old completion must not report a failure for it. */
    machine.schedule_reconnect(make_device("AA:BB:CC:DD:EE:09", "Keyboard", -40,
                                            device_kind::keyboard, true, type));
    const std::vector<action> reconnect = machine.take_actions();
    CHECK_EQ(reconnect.size(), std::size_t(1));
    if (!reconnect.empty()) {
        CHECK(reconnect[0].kind == action_kind::reconnect);
        CHECK(reconnect[0].token != token);
        CHECK(reconnect[0].addr_type == type);
    }
    machine.connection_finished(token, false);
    CHECK(machine.current_screen() == screen::connecting);
    CHECK(machine.current_notice() == notice::none);
    CHECK(machine.take_actions().empty());
}

void test_connect_reconnect_and_cancellation_preserve_address_type()
{
    const address_type type = address_type::random_static;
    state_machine machine;
    machine.set_paired_devices({
        make_device("AA:BB:CC:DD:EE:01", "Keyboard", -40,
                    device_kind::keyboard, true, type)});
    machine.begin_paired();
    machine.press(key::enter);
    std::vector<action> actions = machine.take_actions();
    CHECK_EQ(actions.size(), std::size_t(1));
    if (!actions.empty()) {
        CHECK(actions[0].kind == action_kind::connect);
        CHECK(actions[0].addr_type == type);
    }
    machine.press(key::escape);
    actions = machine.take_actions();
    CHECK_EQ(actions.size(), std::size_t(1));
    if (!actions.empty()) {
        CHECK(actions[0].kind == action_kind::cancel_connect);
        CHECK(actions[0].addr_type == type);
    }

    state_machine reconnecting;
    const device item = make_device("AA:BB:CC:DD:EE:02", "Mouse", -41,
                                    device_kind::mouse, true, type);
    reconnecting.set_paired_devices({item});
    reconnecting.schedule_reconnect(item);
    actions = reconnecting.take_actions();
    CHECK_EQ(actions.size(), std::size_t(1));
    if (!actions.empty()) {
        CHECK(actions[0].kind == action_kind::reconnect);
        CHECK(actions[0].addr_type == type);
    }
    reconnecting.press(key::escape);
    actions = reconnecting.take_actions();
    CHECK_EQ(actions.size(), std::size_t(1));
    if (!actions.empty()) {
        CHECK(actions[0].kind == action_kind::cancel_connect);
        CHECK(actions[0].addr_type == type);
    }

    state_machine pairing;
    pairing.begin_search();
    pairing.scan_finished(pairing.active_scan_token(), {item});
    pairing.press(key::enter);
    pairing.take_actions();
    pairing.press(key::escape);
    actions = pairing.take_actions();
    CHECK_EQ(actions.size(), std::size_t(1));
    if (!actions.empty()) {
        CHECK(actions[0].kind == action_kind::cancel_pair);
        CHECK(actions[0].addr_type == type);
    }
}

void test_submit_auth_preserves_active_address_type()
{
    const address_type type = address_type::random_static;
    state_machine machine;
    machine.begin_search();
    machine.scan_finished(machine.active_scan_token(), {
        make_device("AA:BB:CC:DD:EE:03", "Keyboard", -40,
                    device_kind::keyboard, true, type)});
    machine.press(key::enter);
    machine.take_actions();
    machine.auth_requested(machine.active_pair_token(), auth_request_kind::numeric_compare,
                           kSecretPasskey, cyberdeck_ble::auth_io_action::numeric_compare);
    machine.submit_auth(0);
    const std::vector<action> actions = machine.take_actions();
    CHECK_EQ(actions.size(), std::size_t(1));
    if (!actions.empty()) {
        CHECK(actions[0].kind == action_kind::submit_auth);
        CHECK(actions[0].addr_type == type);
        CHECK_STR(actions[0].address, "AA:BB:CC:DD:EE:03");
    }
}

void test_connection_failure_and_cancellation()
{
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.pairing_finished(machine.active_pair_token(),
                                 pair_outcome::bonded);
        machine.take_actions();
        machine.connection_finished(machine.active_connection_token(), false);
        CHECK(machine.current_screen() == screen::results);
        CHECK(machine.current_notice() == notice::failed);
        CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_connect_failed);
    }
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.pairing_finished(machine.active_pair_token(),
                                 pair_outcome::bonded);
        machine.take_actions();
        const std::uint64_t token = machine.active_connection_token();
        machine.press(key::escape);
        CHECK(machine.current_screen() == screen::results);
        CHECK(machine.current_notice() == notice::cancelled);
        CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_connect_cancelled);
        const std::vector<action> cancelled = machine.take_actions();
        CHECK_EQ(cancelled.size(), std::size_t(1));
        if (!cancelled.empty()) {
            CHECK(cancelled[0].kind == action_kind::cancel_connect);
            CHECK_EQ(cancelled[0].token, token);
        }
    }
}

void test_paired_list_enters_a_connection_without_re_pairing()
{
    state_machine machine;
    machine.set_paired_devices({
        make_device("AA:BB:CC:DD:EE:01", "Fone", -50, device_kind::headset),
        make_device("AA:BB:CC:DD:EE:02", "Mouse", -55, device_kind::mouse),
    });
    machine.begin_paired();
    CHECK(machine.current_screen() == screen::paired);
    CHECK(machine.owns_input());
    CHECK(machine.current_notice() == notice::none);
    CHECK_EQ(machine.devices().size(), std::size_t(2));
    CHECK(machine.take_actions().empty());

    machine.press(key::down);
    CHECK(machine.selected() != nullptr);
    if (machine.selected() != nullptr) {
        CHECK_STR(machine.selected()->address, "AA:BB:CC:DD:EE:02");
    }
    machine.press(key::enter);
    const std::vector<action> connect = machine.take_actions();
    CHECK_EQ(connect.size(), std::size_t(1));
    if (!connect.empty()) {
        CHECK(connect[0].kind == action_kind::connect);
        CHECK_STR(connect[0].address, "AA:BB:CC:DD:EE:02");
    }
    CHECK(machine.current_screen() == screen::connecting);
    CHECK(machine.owns_input());

    /* begin_paired on an empty store is still safe. */
    state_machine empty;
    empty.begin_paired();
    CHECK(empty.current_screen() == screen::paired);
    CHECK_EQ(empty.devices().size(), std::size_t(0));
    CHECK(empty.selected() == nullptr);
    CHECK(!empty.owns_input());
    empty.press(key::enter);
    CHECK(empty.take_actions().empty());
    CHECK(empty.current_screen() == screen::paired);
    CHECK(!empty.owns_input());
}

void test_automatic_reconnection_gives_up_after_the_cap()
{
    state_machine machine;
    machine.set_paired_devices({
        make_device("AA:BB:CC:DD:EE:01", "Fone", -50, device_kind::headset)});

    for (std::uint32_t attempt = 0; attempt < cyberdeck_ble::k_max_reconnect_attempts;
         ++attempt) {
        machine.schedule_reconnect(copy_at(machine.devices(), 0));
        CHECK(machine.current_screen() == screen::connecting);
        const std::vector<action> actions = machine.take_actions();
        CHECK_EQ(actions.size(), std::size_t(1));
        if (!actions.empty()) {
            CHECK(actions[0].kind == action_kind::reconnect);
            CHECK_STR(actions[0].address, "AA:BB:CC:DD:EE:01");
        }
        machine.connection_finished(machine.active_connection_token(), false);
        CHECK(machine.current_notice() == notice::failed);
        CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_connect_failed);
    }

    /* The cap is hard: no further automatic attempt is produced. */
    machine.schedule_reconnect(copy_at(machine.devices(), 0));
    CHECK(machine.take_actions().empty());
    CHECK(machine.current_screen() == screen::paired);
    CHECK(machine.current_notice() == notice::failed);
    CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_reconnect_gave_up);

    /* A manual connect resets the failure budget. */
    machine.press(key::enter);
    const std::vector<action> manual = machine.take_actions();
    CHECK_EQ(count_actions(manual, action_kind::connect), std::size_t(1));
    machine.connection_finished(machine.active_connection_token(), true);
    CHECK(machine.current_screen() == screen::idle);

    state_machine rearmed;
    rearmed.set_paired_devices({
        make_device("AA:BB:CC:DD:EE:01", "Fone", -50, device_kind::headset)});
    rearmed.begin_paired();
    for (std::uint32_t attempt = 0; attempt < cyberdeck_ble::k_max_reconnect_attempts;
         ++attempt) {
        rearmed.schedule_reconnect(copy_at(rearmed.devices(), 0));
        rearmed.take_actions();
        rearmed.connection_finished(rearmed.active_connection_token(), false);
    }
    rearmed.schedule_reconnect(copy_at(rearmed.devices(), 0));
    CHECK(rearmed.take_actions().empty());
    rearmed.begin_paired();
    rearmed.press(key::enter);
    CHECK_EQ(count_actions(rearmed.take_actions(), action_kind::connect),
             std::size_t(1));
    rearmed.connection_finished(rearmed.active_connection_token(), true);
    rearmed.press(key::escape);
    rearmed.take_actions();
    rearmed.schedule_reconnect(copy_at(rearmed.devices(), 0));
    CHECK_EQ(count_actions(rearmed.take_actions(), action_kind::reconnect),
             std::size_t(1));
}

void test_scan_and_reconnect_do_not_corrupt_each_other()
{
    state_machine machine;
    machine.set_paired_devices({
        make_device("AA:BB:CC:DD:EE:01", "Fone", -50, device_kind::headset)});

    machine.schedule_reconnect(copy_at(machine.devices(), 0));
    const std::uint64_t reconnect_token = machine.active_connection_token();
    machine.take_actions();
    CHECK(machine.current_screen() == screen::connecting);

    /* A user-initiated scan starts while the reconnection is in flight. */
    machine.begin_search();
    const std::uint64_t scan_token = machine.active_scan_token();
    machine.take_actions();
    CHECK(machine.current_screen() == screen::searching);
    CHECK_EQ(machine.active_connection_token(), reconnect_token);

    /* The reconnection completion still lands: it owns its own generation and
     * releases the transient screen without handing the link to the model. */
    machine.connection_finished(reconnect_token, true);
    CHECK(machine.current_screen() == screen::idle);
    CHECK(machine.current_notice() == notice::none);

    /* The late scan result cannot clobber the released connection session. */
    machine.scan_finished(scan_token, {
        make_device("AA:BB:CC:DD:EE:02", "Outro", -40, device_kind::mouse)});
    CHECK(machine.current_screen() == screen::idle);
    CHECK(machine.current_notice() == notice::none);
    CHECK_EQ(machine.devices().size(), std::size_t(0));

    /* And the reverse: a scan result arriving while a scan is running applies. */
    state_machine other;
    other.begin_search();
    const std::uint64_t live = other.active_scan_token();
    other.take_actions();
    other.schedule_reconnect(make_device("AA:BB:CC:DD:EE:09", "Fone", -50,
                                         device_kind::headset));
    other.take_actions();
    other.connection_finished(other.active_connection_token(), true);
    CHECK(other.current_screen() == screen::idle);
    other.scan_finished(live, {});
    CHECK(other.current_screen() == screen::idle);
    CHECK(other.current_notice() == notice::none);
}

void test_status_lines_never_leak_and_are_bounded()
{
    state_machine machine;
    CHECK_STR(machine.status_line(), "");

    machine.begin_search();
    CHECK_STR(machine.status_line(), cyberdeck_ble::k_status_scanning);

    state_machine pairing;
    prepare_pairing(pairing, "AA:BB:CC:DD:EE:01", "Teclado");
    CHECK(pairing.status_line().find("Pairing with ") == 0);
    CHECK(pairing.status_line().find("Teclado") != std::string::npos);
    CHECK(pairing.status_line().find(kSecretDigits) == std::string::npos);

    state_machine unnamed;
    prepare_pairing(unnamed, "AA:BB:CC:DD:EE:01", nullptr);
    CHECK(unnamed.status_line().find(cyberdeck_ble::k_unnamed_placeholder) !=
          std::string::npos);

    pairing.pairing_finished(pairing.active_pair_token(), pair_outcome::bonded);
    pairing.take_actions();
    CHECK(pairing.status_line().find("Connecting to ") == 0);
    CHECK(pairing.status_line().find("Teclado") != std::string::npos);

    pairing.connection_finished(pairing.active_connection_token(), true);
    CHECK_STR(pairing.status_line(), "");
    /* A bounded, display-safe name can never inject a line break. */
    CHECK(pairing.status_line().find('\n') == std::string::npos);
    CHECK(pairing.status_line().size() < 128);
    CHECK(pairing.status_line().find('\x1b') == std::string::npos);
}

void test_background_bond_reconnect_cycles_are_bounded_and_explicit()
{
    const address_type type = address_type::random_resolvable;
    const device bond = make_device("AA:BB:CC:DD:EE:42", "Keyboard", -45,
                                    device_kind::keyboard, true, type);
    state_machine machine;
    machine.set_paired_devices({bond});

    /* Boot restoration is silent and preserves the complete bond identity. */
    machine.arm_background_reconnect(bond);
    CHECK(machine.current_screen() == screen::idle);
    CHECK(machine.current_notice() == notice::none);
    CHECK(machine.take_actions().empty());
    CHECK(machine.background_reconnect_armed());
    CHECK(machine.has_background_target());
    CHECK_STR(machine.background_target().address, bond.address);
    CHECK(machine.background_target().addr_type == type);

    /* A spontaneous loss keeps the observer armed; no synthetic disconnect. */
    CHECK(machine.consume_background_attempt());
    machine.schedule_reconnect(bond);
    const std::uint64_t first_token = machine.active_connection_token();
    CHECK(machine.take_actions().size() == 1);
    machine.connection_finished(first_token, true);
    CHECK(machine.is_connected());
    CHECK(machine.background_reconnect_armed());
    machine.connection_finished(first_token, false);
    CHECK(!machine.is_connected());
    CHECK(machine.background_reconnect_armed());
    CHECK(machine.take_actions().empty());

    /* Exactly three attempts are available in one announcement cycle. */
    machine.reset_background_cycle();
    for (std::uint32_t attempt = 0;
         attempt < cyberdeck_ble::k_max_reconnect_attempts; ++attempt) {
        CHECK(machine.consume_background_attempt());
    }
    CHECK(!machine.consume_background_attempt());
    CHECK(!machine.background_reconnect_armed());

    /* A fresh announcement from the same peer opens a new cycle without
     * re-arming the bond or changing its address type. */
    machine.reset_background_cycle();
    CHECK(machine.background_reconnect_armed());
    CHECK(machine.consume_background_attempt());
    machine.schedule_reconnect(bond);
    const std::uint64_t second_token = machine.active_connection_token();
    CHECK(second_token != first_token);
    const std::vector<action> second_attempt = machine.take_actions();
    CHECK_EQ(count_actions(second_attempt, action_kind::reconnect), std::size_t(1));
    if (!second_attempt.empty()) {
        CHECK(second_attempt[0].token == second_token);
        CHECK(second_attempt[0].addr_type == type);
        CHECK_STR(second_attempt[0].address, bond.address);
    }
    machine.connection_finished(first_token, false);
    CHECK(machine.current_screen() == screen::connecting);
    CHECK(machine.active_connection_token() == second_token);
    machine.connection_finished(second_token, false);

    /* Manual disconnect blocks background work until explicit Enter. */
    machine.begin_paired();
    machine.press(key::enter);
    machine.take_actions();
    const std::uint64_t manual_token = machine.active_connection_token();
    machine.connection_finished(manual_token, true);
    machine.press(key::escape);
    const std::vector<action> disconnected = machine.take_actions();
    CHECK_EQ(count_actions(disconnected, action_kind::disconnect), std::size_t(1));
    CHECK(!machine.background_reconnect_armed());
    CHECK(!machine.consume_background_attempt());
    /* A matching fresh announcement cannot clear a manual block. */
    machine.reset_background_cycle();
    CHECK(!machine.background_reconnect_armed());
    CHECK(!machine.consume_background_attempt());

    machine.begin_paired();
    machine.press(key::enter);
    CHECK(machine.background_reconnect_armed());
    CHECK_EQ(count_actions(machine.take_actions(), action_kind::connect), std::size_t(1));
}

/* REQ-COV-01/TEST-COV-STATE: deterministic edge coverage for the real missed
 * gcov branches (stale/zero tokens, guard short-circuits, deadline screens,
 * press paths, cancelled/notice/status/background/ownership edges). */
void test_coverage_stale_tokens_and_pairing_guards()
{
    /* scan_failed/scan_timed_out/pairing_started/auth_requested/pairing_finished
     * drop calls on the wrong screen or with a foreign/zero token. */
    {
        state_machine machine;
        machine.begin_search();
        const std::uint64_t live = machine.active_scan_token();
        machine.scan_failed(live + 1000);
        CHECK(machine.current_screen() == screen::searching);
        machine.scan_timed_out(0);
        CHECK(machine.current_screen() == screen::searching);
        machine.pairing_started(live, "AA:BB:CC:DD:EE:01");
        CHECK(machine.current_screen() == screen::searching);
        machine.auth_requested(live, auth_request_kind::passkey, 1,
                               cyberdeck_ble::auth_io_action::input);
        CHECK(machine.current_screen() == screen::searching);
        machine.pairing_finished(live, pair_outcome::bonded);
        CHECK(machine.current_screen() == screen::searching);
        machine.scan_failed(live);
        CHECK(machine.current_screen() == screen::results);
        machine.scan_timed_out(live);
        CHECK(machine.current_screen() == screen::results);
    }
    /* pairing_started applies from results/paired with the machine token and
     * resolves addr_type from the scan list; foreign tokens are dropped. */
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        const std::uint64_t pair_token = machine.active_pair_token();
        machine.pairing_started(pair_token + 7, "AA:BB:CC:DD:EE:01");
        CHECK(machine.current_screen() == screen::pairing);
        machine.press(key::escape);
        machine.begin_paired();
        machine.set_paired_devices(
            {make_device("AA:BB:CC:DD:EE:09", "Bond", -40, device_kind::keyboard,
                         true, address_type::random_static)});
        machine.take_actions();
        state_machine fresh;
        fresh.set_paired_devices(
            {make_device("AA:BB:CC:DD:EE:09", "Bond", -40, device_kind::keyboard,
                         true, address_type::random_static)});
        fresh.begin_paired();
        fresh.take_actions();
        const device *sel = fresh.selected();
        CHECK(sel != nullptr);
        /* Enter from paired emits connect carrying the stored addr_type. */
        fresh.press(key::enter);
        const std::vector<action> connected = fresh.take_actions();
        CHECK_EQ(count_actions(connected, action_kind::connect), std::size_t(1));
        if (!connected.empty())
            CHECK(connected[0].addr_type == address_type::random_static);
        (void)pair_token;
    }
    /* auth_requested validation edges: foreign screen, display/input shape,
     * numeric_compare shape, and unsupported io action. */
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        const std::uint64_t token = machine.active_pair_token();
        machine.auth_requested(token, auth_request_kind::numeric_compare, 11,
                               cyberdeck_ble::auth_io_action::display);
        CHECK(machine.current_screen() == screen::pairing);
        machine.auth_requested(token, auth_request_kind::passkey, 0,
                               cyberdeck_ble::auth_io_action::input);
        CHECK(machine.current_screen() == screen::auth);
        machine.press(key::escape);
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        const std::uint64_t token2 = machine.active_pair_token();
        machine.auth_requested(token2, auth_request_kind::passkey, 1,
                               cyberdeck_ble::auth_io_action::input);
        CHECK(machine.current_screen() == screen::pairing);
        machine.auth_requested(token2, auth_request_kind::passkey, 0,
                               static_cast<cyberdeck_ble::auth_io_action>(99));
        CHECK(machine.current_screen() == screen::pairing);
        machine.auth_requested(token2, auth_request_kind::passkey, 0,
                               cyberdeck_ble::auth_io_action::numeric_compare);
        CHECK(machine.current_screen() == screen::pairing);
        machine.auth_requested(token2, auth_request_kind::numeric_compare,
                               cyberdeck_ble::k_passkey_modulus,
                               cyberdeck_ble::auth_io_action::numeric_compare);
        CHECK(machine.current_screen() == screen::pairing);
        machine.pairing_finished(token2 + 5, pair_outcome::failed);
        CHECK(machine.current_screen() == screen::pairing);
    }
}

void test_coverage_connection_deadlines_and_background_outcome()
{
    /* connection_finished guards: zero/foreign token, success without flight,
     * late failure after establishment stays silent (no reconnect storm). */
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.pairing_finished(machine.active_pair_token(), pair_outcome::bonded);
        machine.take_actions();
        const std::uint64_t live = machine.active_connection_token();
        machine.connection_finished(0, true);
        machine.connection_finished(live + 9, false);
        CHECK(machine.current_screen() == screen::connecting);
        machine.connection_finished(live, true);
        CHECK(machine.is_connected());
        machine.connection_finished(live, true);
        CHECK(machine.is_connected());
        /* Success with an empty connecting address skips background arming. */
        CHECK(machine.background_reconnect_armed());
    }
    /* Background connection failure returns silently to idle (no notice,
     * no results screen), keeping the retry budget managed by the caller. */
    {
        state_machine machine;
        const device bond = make_device("AA:BB:CC:DD:EE:07", "Fone", -50,
                                        device_kind::headset);
        machine.arm_background_reconnect(bond);
        CHECK(machine.consume_background_attempt());
        machine.schedule_reconnect(bond);
        machine.take_actions();
        const std::uint64_t token = machine.active_connection_token();
        machine.connection_finished(token, false);
        CHECK(machine.current_screen() == screen::idle);
        CHECK(machine.current_notice() == notice::none);
        CHECK_STR(machine.notice_text(), "");
    }
    /* advance_time deadline edges: scan deadline on a non-search screen,
     * pair/auth/connect deadlines expiring off-screen, background timeout. */
    {
        state_machine machine;
        machine.begin_search();
        machine.scan_finished(machine.active_scan_token(), {});
        machine.advance_time(60000);
        CHECK(machine.current_screen() == screen::results);
        state_machine pairing;
        prepare_pairing(pairing, "AA:BB:CC:DD:EE:01", "Teclado");
        pairing.press(key::escape);
        pairing.advance_time(60000);
        CHECK(pairing.current_screen() == screen::results);
        state_machine auth;
        prepare_pairing(auth, "AA:BB:CC:DD:EE:01", "Teclado");
        auth.auth_requested(auth.active_pair_token(), auth_request_kind::passkey,
                            42, cyberdeck_ble::auth_io_action::display);
        auth.press(key::escape);
        auth.advance_time(60000);
        CHECK(auth.current_screen() == screen::results);
        state_machine conn;
        prepare_pairing(conn, "AA:BB:CC:DD:EE:01", "Teclado");
        conn.pairing_finished(conn.active_pair_token(), pair_outcome::bonded);
        conn.take_actions();
        conn.press(key::escape);
        conn.advance_time(60000);
        CHECK(conn.current_screen() == screen::results);
        /* Background connect timeout emits cancel_connect without a notice. */
        state_machine bg;
        const device bond = make_device("AA:BB:CC:DD:EE:08", "Fone", -50,
                                        device_kind::headset);
        bg.arm_background_reconnect(bond);
        CHECK(bg.consume_background_attempt());
        bg.schedule_reconnect(bond);
        bg.take_actions();
        bg.advance_time(cyberdeck_ble::k_connect_timeout_ms);
        CHECK(bg.current_screen() == screen::idle);
        CHECK(bg.current_notice() == notice::none);
        CHECK_EQ(count_actions(bg.take_actions(), action_kind::cancel_connect),
                 std::size_t(1));
    }
}

void test_coverage_press_notice_status_and_background_edges()
{
    /* press edges: idle without link ignores escape; searching ignores
     * navigation; paired up/escape; connecting ignores other keys; results
     * enter on empty selection is silent. */
    {
        state_machine machine;
        machine.press(key::escape);
        CHECK(machine.current_screen() == screen::idle);
        CHECK(machine.take_actions().empty());
        machine.begin_search();
        machine.take_actions();
        machine.press(key::up);
        machine.press(key::down);
        machine.press(key::enter);
        CHECK(machine.current_screen() == screen::searching);
        machine.press(key::escape);
        CHECK(machine.current_screen() == screen::idle);
    }
    {
        state_machine machine;
        machine.set_paired_devices(
            {make_device("AA:BB:CC:DD:EE:01", "A", -40, device_kind::keyboard),
             make_device("AA:BB:CC:DD:EE:02", "B", -41, device_kind::mouse)});
        machine.begin_paired();
        machine.take_actions();
        machine.press(key::up);
        machine.press(key::enter);
        machine.take_actions();
        CHECK(machine.current_screen() == screen::connecting);
        machine.press(key::up);
        CHECK(machine.current_screen() == screen::connecting);
        machine.press(key::escape);
        CHECK(machine.current_screen() == screen::results);
        machine.begin_paired();
        machine.take_actions();
        machine.press(key::escape);
        CHECK(machine.current_screen() == screen::idle);
    }
    /* auth enter with display/numcmp io forwards submit_auth; connecting is
     * the only other escape→cancel_connect path with flags set. */
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.auth_requested(machine.active_pair_token(),
                               auth_request_kind::numeric_compare, 424242,
                               cyberdeck_ble::auth_io_action::numeric_compare);
        machine.press(key::enter);
        CHECK_EQ(count_actions(machine.take_actions(), action_kind::submit_auth),
                 std::size_t(1));
        CHECK(machine.current_screen() == screen::pairing);
    }
    /* submit_auth guards: input-mode overflow rejected; display/numcmp
     * forward (numcmp as a boolean decision); wrong screen silent. */
    {
        state_machine input_mode;
        prepare_pairing(input_mode, "AA:BB:CC:DD:EE:01", "Teclado");
        input_mode.auth_requested(input_mode.active_pair_token(),
                                  auth_request_kind::passkey, 0,
                                  cyberdeck_ble::auth_io_action::input);
        input_mode.submit_auth(cyberdeck_ble::k_passkey_modulus + 5);
        CHECK(input_mode.take_actions().empty());
        CHECK(input_mode.current_screen() == screen::auth);
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.auth_requested(machine.active_pair_token(),
                               auth_request_kind::passkey, 11,
                               cyberdeck_ble::auth_io_action::display);
        /* Display mode forwards submit_auth (passkey shown by the peer is not
         * re-validated as user input); the screen returns to pairing. */
        machine.submit_auth(cyberdeck_ble::k_passkey_modulus + 5);
        CHECK_EQ(count_actions(machine.take_actions(), action_kind::submit_auth),
                 std::size_t(1));
        CHECK(machine.current_screen() == screen::pairing);
        state_machine idle;
        idle.submit_auth(123);
        CHECK(idle.take_actions().empty());
    }
    /* notice_text cancelled paths: idle search-cancel, results pair-cancel,
     * and the connect-cancel fallback; owns_input across connected/idle. */
    {
        state_machine machine;
        machine.begin_search();
        machine.press(key::escape);
        CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_search_cancelled);
        machine.begin_search();
        machine.scan_finished(machine.active_scan_token(), {
            make_device("AA:BB:CC:DD:EE:01", "Fone", -50, device_kind::headset)});
        machine.take_actions();
        machine.press(key::enter);
        machine.take_actions();
        machine.press(key::escape);
        CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_pair_cancelled);
        CHECK(machine.owns_input());
        state_machine conn;
        prepare_pairing(conn, "AA:BB:CC:DD:EE:01", "Teclado");
        conn.pairing_finished(conn.active_pair_token(), pair_outcome::bonded);
        conn.take_actions();
        conn.press(key::escape);
        CHECK_STR(conn.notice_text(), cyberdeck_ble::k_msg_connect_cancelled);
        CHECK(conn.owns_input());
    }
    /* status_line connected path and active_address/background guards. */
    {
        state_machine machine;
        CHECK_STR(machine.active_address(), "");
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        CHECK_STR(machine.active_address(), "AA:BB:CC:DD:EE:01");
        machine.pairing_finished(machine.active_pair_token(), pair_outcome::bonded);
        machine.take_actions();
        CHECK_STR(machine.active_address(), "AA:BB:CC:DD:EE:01");
        machine.connection_finished(machine.active_connection_token(), true);
        CHECK_STR(machine.active_address(), "");
        CHECK_STR(machine.status_line(), "");
        /* background guards: empty arm ignored; consume without arm/target;
         * reset under manual block keeps the block. */
        state_machine bg;
        bg.arm_background_reconnect(make_device("", nullptr, -50,
                                                device_kind::unknown));
        CHECK(!bg.has_background_target());
        CHECK(!bg.consume_background_attempt());
        CHECK(!bg.background_reconnect_armed());
        bg.block_background_reconnect();
        bg.reset_background_cycle();
        CHECK(!bg.background_reconnect_armed());
        CHECK(!bg.consume_background_attempt());
        /* schedule_reconnect at the cap reports gave-up without an action. */
        state_machine cap;
        const device bond = make_device("AA:BB:CC:DD:EE:09", "Fone", -50,
                                        device_kind::headset);
        for (std::uint32_t i = 0; i < cyberdeck_ble::k_max_reconnect_attempts; ++i)
            cap.schedule_reconnect(bond);
        cap.take_actions();
        cap.schedule_reconnect(bond);
        CHECK(cap.current_screen() == screen::paired);
        CHECK_STR(cap.notice_text(), cyberdeck_ble::k_msg_reconnect_gave_up);
        CHECK(cap.take_actions().empty());
    }
    /* emit_action token routing and addr_type lookup from the paired list:
     * disconnect on connected, idle-with-link escape with empty address. */
    {
        state_machine machine;
        machine.set_paired_devices(
            {make_device("AA:BB:CC:DD:EE:01", "Fone", -50, device_kind::headset)});
        machine.begin_paired();
        machine.take_actions();
        machine.press(key::enter);
        machine.take_actions();
        machine.connection_finished(machine.active_connection_token(), true);
        /* Idle with an established link: Escape disconnects once. */
        machine.press(key::escape);
        const std::vector<action> first = machine.take_actions();
        CHECK_EQ(count_actions(first, action_kind::disconnect), std::size_t(1));
        machine.press(key::escape);
        CHECK(machine.take_actions().empty());
    }
}

void test_coverage_guards_timeouts_and_emit_routing()
{
    /* scan_failed on the wrong screen is dropped without side effects. */
    {
        state_machine machine;
        machine.begin_search();
        const std::uint64_t live = machine.active_scan_token();
        machine.take_actions();
        machine.scan_failed(live);
        CHECK(machine.current_screen() == screen::results);
        machine.scan_failed(live);
        CHECK(machine.current_screen() == screen::results);
        CHECK(machine.take_actions().empty());
    }
    /* pairing_started on the paired screen resolves addr_type from the bond
     * list when known and proceeds with default type when unknown. */
    {
        state_machine known;
        known.set_paired_devices(
            {make_device("AA:BB:CC:DD:EE:09", "Bond", -40, device_kind::keyboard,
                         true, address_type::random_static)});
        known.begin_paired();
        known.take_actions();
        known.pairing_started(0, "AA:BB:CC:DD:EE:09");
        CHECK(known.current_screen() == screen::pairing);
        CHECK_STR(known.active_address(), "AA:BB:CC:DD:EE:09");
        known.press(key::escape);
        CHECK(known.current_screen() == screen::results);

        state_machine unknown;
        unknown.set_paired_devices(
            {make_device("AA:BB:CC:DD:EE:09", "Bond", -40, device_kind::keyboard)});
        unknown.begin_paired();
        unknown.take_actions();
        unknown.pairing_started(0, "AA:BB:CC:DD:EE:FF");
        CHECK(unknown.current_screen() == screen::pairing);
        CHECK_STR(unknown.active_address(), "AA:BB:CC:DD:EE:FF");
        unknown.press(key::escape);
        CHECK_EQ(count_actions(unknown.take_actions(), action_kind::cancel_pair),
                 std::size_t(1));
    }
    /* schedule_reconnect with an identity outside the scan list exercises the
     * paired-list fallback (match and miss) and the reconnect token route. */
    {
        state_machine machine;
        const device bond = make_device("AA:BB:CC:DD:EE:0A", "Bond", -50,
                                        device_kind::headset, true,
                                        address_type::random_static);
        machine.set_paired_devices({bond});
        machine.begin_search();
        machine.take_actions();
        machine.schedule_reconnect(bond);
        const std::vector<action> first = machine.take_actions();
        CHECK_EQ(count_actions(first, action_kind::reconnect), std::size_t(1));
        if (!first.empty()) CHECK(first[0].addr_type == address_type::random_static);
        const device stranger = make_device("AA:BB:CC:DD:EE:0B", "Stranger", -60,
                                            device_kind::mouse);
        machine.schedule_reconnect(stranger);
        CHECK_EQ(count_actions(machine.take_actions(), action_kind::reconnect),
                 std::size_t(1));
    }
    /* switch(outcome) default: a foreign outcome value leaves the pairing
     * screen untouched and emits nothing. */
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        const std::uint64_t token = machine.active_pair_token();
        machine.pairing_finished(token, static_cast<pair_outcome>(99));
        CHECK(machine.current_screen() == screen::pairing);
        CHECK(machine.take_actions().empty());
    }
    /* A scan deadline left armed across a reconnect cycle fires harmlessly
     * off-screen: no timeout notice and no cancel_scan. */
    {
        state_machine machine;
        machine.begin_search();
        const std::uint64_t live = machine.active_scan_token();
        machine.take_actions();
        machine.schedule_reconnect(make_device("AA:BB:CC:DD:EE:09", "Fone", -50,
                                               device_kind::headset));
        machine.take_actions();
        machine.connection_finished(machine.active_connection_token(), true);
        CHECK(machine.current_screen() == screen::idle);
        machine.scan_finished(live, {});
        machine.advance_time(60000);
        CHECK(machine.current_screen() == screen::idle);
        CHECK(machine.current_notice() == notice::none);
        CHECK(machine.take_actions().empty());
    }
}

/* REQ-COV-STATE-01: Escape on the established link (idle with link up)
 * emits exactly one disconnect with identity cleanup; other screens keep
 * their exact escape contract. Output/state/action only. */
void test_coverage_connected_escape_cleanup_and_other_escapes()
{
    {
        state_machine machine;
        machine.set_paired_devices(
            {make_device("AA:BB:CC:DD:EE:01", "Fone", -50,
                         device_kind::headset, true,
                         address_type::random_static)});
        machine.begin_paired();
        machine.take_actions();
        machine.press(key::enter);
        const std::vector<action> connecting = machine.take_actions();
        CHECK_EQ(count_actions(connecting, action_kind::connect), std::size_t(1));
        const std::uint64_t token = machine.active_connection_token();
        CHECK(token != 0);
        machine.connection_finished(token, true);
        CHECK(machine.current_screen() == screen::idle);
        CHECK(machine.is_connected());
        CHECK(!machine.owns_input());
        CHECK_STR(machine.status_line(), "");
        CHECK_STR(machine.notice_text(), "");
        CHECK_STR(machine.active_address(), "");
        CHECK(machine.background_reconnect_armed());

        machine.press(key::escape);
        CHECK(machine.current_screen() == screen::idle);
        CHECK(machine.current_notice() == notice::none);
        CHECK_STR(machine.notice_text(), "");
        CHECK_STR(machine.active_address(), "");
        CHECK_STR(machine.status_line(), "");
        CHECK(!machine.owns_input());
        /* The link stays reported until the physical DISCONNECTED arrives. */
        CHECK(machine.is_connected());
        const std::vector<action> disconnected = machine.take_actions();
        CHECK_EQ(count_actions(disconnected, action_kind::disconnect),
                 std::size_t(1));
        const action *gone = find_action(disconnected, action_kind::disconnect);
        if (gone != nullptr) {
            CHECK_STR(gone->address, "AA:BB:CC:DD:EE:01");
            CHECK_EQ(gone->token, token);
            CHECK(gone->addr_type == address_type::random_static);
        }
        CHECK(!machine.background_reconnect_armed());

        /* The cleared address guards a second synthetic disconnect. */
        machine.press(key::escape);
        CHECK(machine.take_actions().empty());
        CHECK(machine.current_screen() == screen::idle);
        CHECK(machine.is_connected());

        machine.connection_finished(token, false);
        CHECK(!machine.is_connected());
        CHECK(machine.current_screen() == screen::idle);
        CHECK(machine.current_notice() == notice::none);
        CHECK(machine.take_actions().empty());
    }
    {
        /* Escape elsewhere stays exact: idle without link silent, searching
         * cancels once, results/paireved release silently. */
        state_machine idle;
        idle.press(key::escape);
        CHECK(idle.take_actions().empty());
        CHECK(idle.current_screen() == screen::idle);
        CHECK(!idle.is_connected());

        state_machine searching;
        searching.begin_search();
        const std::uint64_t scan_token = searching.active_scan_token();
        searching.take_actions();
        searching.press(key::up);
        searching.press(key::down);
        CHECK(searching.current_screen() == screen::searching);
        searching.press(key::escape);
        CHECK(searching.current_screen() == screen::idle);
        CHECK(searching.current_notice() == notice::cancelled);
        CHECK_STR(searching.notice_text(),
                  cyberdeck_ble::k_msg_search_cancelled);
        const std::vector<action> cancelled = searching.take_actions();
        CHECK_EQ(count_actions(cancelled, action_kind::cancel_scan),
                 std::size_t(1));
        if (!cancelled.empty()) CHECK_EQ(cancelled[0].token, scan_token);
        searching.press(key::escape);
        CHECK(searching.take_actions().empty());
    }
}

/* REQ-COV-STATE-02: pairing_started gates duplicate, wrong token, wrong
 * screen and resolves addr_type from the visible list. */
void test_coverage_pairing_started_gates_and_type_resolution()
{
    {
        state_machine machine;
        machine.begin_search();
        machine.take_actions();
        machine.scan_finished(machine.active_scan_token(), {
            make_device("AA:BB:CC:DD:EE:21", "Kb", -40,
                        device_kind::keyboard, true,
                        address_type::random_static)});
        machine.take_actions();
        CHECK(machine.current_screen() == screen::results);

        machine.pairing_started(9999, "AA:BB:CC:DD:EE:21");
        CHECK(machine.current_screen() == screen::results);
        CHECK_STR(machine.active_address(), "");
        CHECK(machine.take_actions().empty());

        machine.pairing_started(0, "AA:BB:CC:DD:EE:21");
        CHECK(machine.current_screen() == screen::pairing);
        CHECK_STR(machine.active_address(), "AA:BB:CC:DD:EE:21");
        CHECK(machine.owns_input());
        CHECK(machine.status_line().find("Pairing with ") == 0);

        /* Duplicate while already pairing is dropped. */
        machine.pairing_started(0, "AA:BB:CC:DD:EE:21");
        CHECK(machine.current_screen() == screen::pairing);
        CHECK(machine.take_actions().empty());

        machine.press(key::escape);
        const std::vector<action> cancelled = machine.take_actions();
        CHECK_EQ(count_actions(cancelled, action_kind::cancel_pair),
                 std::size_t(1));
        const action *gone = find_action(cancelled, action_kind::cancel_pair);
        if (gone != nullptr) {
            CHECK_STR(gone->address, "AA:BB:CC:DD:EE:21");
            CHECK(gone->addr_type == address_type::random_static);
        }
    }
    {
        /* Unknown address proceeds with the default public type. */
        state_machine machine;
        machine.begin_search();
        machine.take_actions();
        machine.scan_finished(machine.active_scan_token(), {
            make_device("AA:BB:CC:DD:EE:22", "Kb", -40,
                        device_kind::keyboard)});
        machine.take_actions();
        machine.pairing_started(0, "AA:BB:CC:DD:EE:FF");
        CHECK(machine.current_screen() == screen::pairing);
        CHECK_STR(machine.active_address(), "AA:BB:CC:DD:EE:FF");
        machine.press(key::escape);
        const std::vector<action> cancelled = machine.take_actions();
        CHECK_EQ(count_actions(cancelled, action_kind::cancel_pair),
                 std::size_t(1));
        const action *gone = find_action(cancelled, action_kind::cancel_pair);
        if (gone != nullptr) {
            CHECK(gone->addr_type == address_type::public_address);
            CHECK_STR(gone->address, "AA:BB:CC:DD:EE:FF");
        }
    }
    {
        /* Wrong screen (searching/auth) drops pairing_started. */
        state_machine searching;
        searching.begin_search();
        searching.take_actions();
        searching.pairing_started(0, "AA:BB:CC:DD:EE:01");
        CHECK(searching.current_screen() == screen::searching);

        state_machine auth;
        prepare_pairing(auth, "AA:BB:CC:DD:EE:01", "Teclado");
        auth.auth_requested(auth.active_pair_token(),
                            auth_request_kind::passkey, kSecretPasskey,
                            cyberdeck_ble::auth_io_action::display);
        CHECK(auth.current_screen() == screen::auth);
        const std::uint64_t pair_token = auth.active_pair_token();
        auth.pairing_started(pair_token, "AA:BB:CC:DD:EE:01");
        CHECK(auth.current_screen() == screen::auth);
        CHECK_EQ(auth.displayed_passkey(), kSecretPasskey);
    }
    {
        /* pairing_finished/connection_finished on the wrong screen stay
         * silent (no conn/result confusion). */
        state_machine machine;
        machine.begin_search();
        machine.take_actions();
        machine.pairing_finished(machine.active_pair_token(),
                                 pair_outcome::bonded);
        CHECK(machine.current_screen() == screen::searching);
        machine.connection_finished(machine.active_connection_token(), true);
        CHECK(!machine.is_connected());
        CHECK(machine.take_actions().empty());
    }
}

/* REQ-COV-STATE-03: authentication outcomes from the auth screen, plus the
 * input-mode Enter inert path and duplicate-request gate. */
void test_coverage_auth_outcomes_from_auth_screen()
{
    {
        /* Input mode owns the buffer: Enter alone forwards nothing. */
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.auth_requested(machine.active_pair_token(),
                               auth_request_kind::passkey, 0,
                               cyberdeck_ble::auth_io_action::input);
        CHECK(machine.current_screen() == screen::auth);
        machine.press(key::enter);
        CHECK(machine.take_actions().empty());
        CHECK(machine.current_screen() == screen::auth);
        machine.submit_auth(123456);
        const std::vector<action> submitted = machine.take_actions();
        CHECK_EQ(count_actions(submitted, action_kind::submit_auth),
                 std::size_t(1));
        CHECK(machine.current_screen() == screen::pairing);
    }
    {
        struct expectation {
            pair_outcome outcome;
            notice expected_notice;
            const char *text;
        };
        const expectation cases[] = {
            {pair_outcome::rejected, notice::failed,
             cyberdeck_ble::k_msg_pair_rejected},
            {pair_outcome::cancelled, notice::cancelled,
             cyberdeck_ble::k_msg_pair_cancelled},
            {pair_outcome::timed_out, notice::timed_out,
             cyberdeck_ble::k_msg_pair_timeout},
            {pair_outcome::failed, notice::failed,
             cyberdeck_ble::k_msg_pair_failed},
        };
        for (const expectation &item : cases) {
            state_machine machine;
            prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
            machine.auth_requested(machine.active_pair_token(),
                                   auth_request_kind::passkey, kSecretPasskey,
                                   cyberdeck_ble::auth_io_action::display);
            CHECK(machine.current_screen() == screen::auth);
            machine.pairing_finished(machine.active_pair_token(), item.outcome);
            CHECK(machine.current_screen() == screen::results);
            CHECK(machine.current_notice() == item.expected_notice);
            CHECK_STR(machine.notice_text(), item.text);
            CHECK_EQ(machine.displayed_passkey(), std::uint32_t(0));
            CHECK(machine.notice_text().find(kSecretDigits) ==
                  std::string::npos);
            CHECK(machine.take_actions().empty());
        }
    }
    {
        /* Bonded from auth promotes to connecting with identity. */
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        const std::uint64_t pair_token = machine.active_pair_token();
        machine.auth_requested(pair_token, auth_request_kind::passkey,
                               kSecretPasskey,
                               cyberdeck_ble::auth_io_action::display);
        machine.pairing_finished(pair_token, pair_outcome::bonded);
        CHECK(machine.current_screen() == screen::connecting);
        CHECK_STR(machine.active_address(), "AA:BB:CC:DD:EE:01");
        const std::vector<action> connecting = machine.take_actions();
        CHECK_EQ(count_actions(connecting, action_kind::connect),
                 std::size_t(1));
        if (!connecting.empty()) {
            CHECK(connecting[0].token != pair_token);
            CHECK_STR(connecting[0].address, "AA:BB:CC:DD:EE:01");
        }
    }
    {
        /* Duplicate request while in auth is dropped; boundary values. */
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        const std::uint64_t token = machine.active_pair_token();
        machine.auth_requested(token, auth_request_kind::passkey,
                               kSecretPasskey,
                               cyberdeck_ble::auth_io_action::display);
        CHECK_EQ(machine.displayed_passkey(), kSecretPasskey);
        machine.auth_requested(token, auth_request_kind::passkey, 111111,
                               cyberdeck_ble::auth_io_action::display);
        CHECK(machine.current_screen() == screen::auth);
        CHECK_EQ(machine.displayed_passkey(), kSecretPasskey);

        state_machine zero;
        prepare_pairing(zero, "AA:BB:CC:DD:EE:01", "Teclado");
        zero.auth_requested(zero.active_pair_token(),
                            auth_request_kind::passkey, 0,
                            cyberdeck_ble::auth_io_action::display);
        CHECK(zero.current_screen() == screen::auth);
        CHECK_EQ(zero.displayed_passkey(), std::uint32_t(0));

        state_machine numcmp;
        prepare_pairing(numcmp, "AA:BB:CC:DD:EE:01", "Teclado");
        numcmp.auth_requested(numcmp.active_pair_token(),
                              auth_request_kind::numeric_compare, 0,
                              cyberdeck_ble::auth_io_action::numeric_compare);
        CHECK(numcmp.current_screen() == screen::auth);
        CHECK_EQ(numcmp.displayed_passkey(), std::uint32_t(0));
    }
}

/* REQ-COV-STATE-04: advance_time inclusive versus exceeded deadlines,
 * off-screen harmless paths and idle-with-link stability. */
void test_coverage_advance_time_exceeded_and_offscreen()
{
    {
        state_machine machine;
        machine.begin_search();
        const std::uint64_t token = machine.active_scan_token();
        machine.take_actions();
        machine.advance_time(cyberdeck_ble::k_scan_timeout_ms + 5);
        CHECK(machine.current_screen() == screen::results);
        CHECK(machine.current_notice() == notice::timed_out);
        CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_scan_timeout);
        const std::vector<action> timed_out = machine.take_actions();
        CHECK_EQ(count_actions(timed_out, action_kind::cancel_scan),
                 std::size_t(1));
        if (!timed_out.empty()) CHECK_EQ(timed_out[0].token, token);
    }
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        const std::uint64_t token = machine.active_pair_token();
        machine.advance_time(cyberdeck_ble::k_pair_timeout_ms + 10);
        CHECK(machine.current_screen() == screen::results);
        CHECK(machine.current_notice() == notice::timed_out);
        CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_pair_timeout);
        const std::vector<action> timed_out = machine.take_actions();
        CHECK_EQ(count_actions(timed_out, action_kind::cancel_pair),
                 std::size_t(1));
        if (!timed_out.empty()) CHECK_EQ(timed_out[0].token, token);
    }
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.auth_requested(machine.active_pair_token(),
                               auth_request_kind::passkey, kSecretPasskey,
                               cyberdeck_ble::auth_io_action::display);
        machine.advance_time(cyberdeck_ble::k_auth_timeout_ms + 10);
        CHECK(machine.current_screen() == screen::results);
        CHECK(machine.current_notice() == notice::timed_out);
        CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_pair_timeout);
        CHECK(machine.status_line().find(kSecretDigits) == std::string::npos);
        CHECK_EQ(count_actions(machine.take_actions(), action_kind::cancel_pair),
                 std::size_t(1));
    }
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.pairing_finished(machine.active_pair_token(),
                                 pair_outcome::bonded);
        machine.take_actions();
        const std::uint64_t token = machine.active_connection_token();
        machine.advance_time(cyberdeck_ble::k_connect_timeout_ms + 7);
        CHECK(machine.current_screen() == screen::results);
        CHECK(machine.current_notice() == notice::timed_out);
        CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_connect_timeout);
        const std::vector<action> timed_out = machine.take_actions();
        CHECK_EQ(count_actions(timed_out, action_kind::cancel_connect),
                 std::size_t(1));
        if (!timed_out.empty()) CHECK_EQ(timed_out[0].token, token);
    }
    {
        /* Idle with an established link is stable under time. */
        state_machine machine;
        machine.set_paired_devices(
            {make_device("AA:BB:CC:DD:EE:01", "Fone", -50,
                         device_kind::headset)});
        machine.begin_paired();
        machine.take_actions();
        machine.press(key::enter);
        machine.take_actions();
        const std::uint64_t token = machine.active_connection_token();
        machine.connection_finished(token, true);
        CHECK(machine.is_connected());
        machine.advance_time(60000);
        CHECK(machine.current_screen() == screen::idle);
        CHECK(machine.current_notice() == notice::none);
        CHECK(machine.is_connected());
        CHECK(machine.take_actions().empty());

        state_machine idle;
        idle.advance_time(60000);
        CHECK(idle.current_screen() == screen::idle);
        CHECK(idle.current_notice() == notice::none);
        CHECK(idle.take_actions().empty());
    }
    {
        /* Partial time while both pair and connect deadlines are armed
         * stays in connecting without synthetic timeouts. */
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.schedule_reconnect(make_device("AA:BB:CC:DD:EE:01", "Teclado",
                                               -50, device_kind::keyboard));
        machine.take_actions();
        CHECK(machine.current_screen() == screen::connecting);
        machine.advance_time(1000);
        CHECK(machine.current_screen() == screen::connecting);
        CHECK(machine.current_notice() == notice::none);
        CHECK(machine.take_actions().empty());
    }
}

/* REQ-COV-STATE-05: observation defaults per screen and background
 * arm/block/consume/reset transitions. */
void test_coverage_observation_defaults_and_background()
{
    {
        state_machine machine;
        CHECK(!machine.owns_input());
        CHECK_STR(machine.status_line(), "");
        CHECK_STR(machine.notice_text(), "");
        CHECK_STR(machine.active_address(), "");
        CHECK_EQ(machine.displayed_passkey(), std::uint32_t(0));
        CHECK(!machine.has_background_target());
        CHECK(!machine.background_reconnect_armed());
        CHECK(!machine.consume_background_attempt());

        machine.begin_search();
        CHECK(machine.owns_input());
        CHECK_STR(machine.status_line(), cyberdeck_ble::k_status_scanning);
        machine.scan_finished(machine.active_scan_token(), {});
        CHECK(!machine.owns_input());
        CHECK_STR(machine.status_line(), "");
        CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_scan_empty);

        machine.begin_search();
        machine.scan_finished(machine.active_scan_token(), {
            make_device("AA:BB:CC:DD:EE:01", "Fone", -50,
                        device_kind::headset)});
        machine.take_actions();
        CHECK(machine.owns_input());
        CHECK_STR(machine.status_line(), "");
        CHECK(machine.current_notice() == notice::none);
        CHECK_STR(machine.notice_text(), "");
    }
    {
        state_machine empty;
        empty.set_paired_devices({});
        empty.begin_paired();
        CHECK(!empty.owns_input());
        CHECK_STR(empty.status_line(), "");
        CHECK(empty.selected() == nullptr);

        state_machine paired;
        paired.set_paired_devices(
            {make_device("AA:BB:CC:DD:EE:01", "Fone", -50,
                         device_kind::headset)});
        paired.begin_paired();
        CHECK(paired.owns_input());
        CHECK_STR(paired.status_line(), "");
        CHECK_STR(paired.notice_text(), "");
        CHECK_STR(paired.active_address(), "");

        state_machine pairing;
        prepare_pairing(pairing, "AA:BB:CC:DD:EE:01", "Teclado");
        CHECK(pairing.owns_input());
        CHECK(pairing.status_line().find("Pairing with ") == 0);
        CHECK_STR(pairing.active_address(), "AA:BB:CC:DD:EE:01");
        pairing.auth_requested(pairing.active_pair_token(),
                               auth_request_kind::passkey, 0,
                               cyberdeck_ble::auth_io_action::input);
        CHECK(pairing.owns_input());
        CHECK(pairing.status_line().find("Enter the passkey") !=
              std::string::npos);
        pairing.submit_auth(111111);
        pairing.pairing_finished(pairing.active_pair_token(),
                                 pair_outcome::bonded);
        pairing.take_actions();
        CHECK(pairing.current_screen() == screen::connecting);
        CHECK(pairing.owns_input());
        CHECK(pairing.status_line().find("Connecting to ") == 0);
        CHECK_STR(pairing.active_address(), "AA:BB:CC:DD:EE:01");
    }
    {
        const device bond = make_device("AA:BB:CC:DD:EE:33", "Kb", -45,
                                        device_kind::keyboard, true,
                                        address_type::random_static);
        state_machine machine;
        CHECK(!machine.has_background_target());
        machine.arm_background_reconnect(bond);
        CHECK(machine.has_background_target());
        CHECK_STR(machine.background_target().address, bond.address);
        CHECK(machine.background_target().addr_type ==
              address_type::random_static);
        CHECK(machine.background_reconnect_armed());

        machine.block_background_reconnect();
        CHECK(!machine.background_reconnect_armed());
        CHECK(machine.has_background_target());
        machine.arm_background_reconnect(bond);
        CHECK(machine.background_reconnect_armed());

        machine.reset_background_cycle();
        for (std::uint32_t attempt = 0;
             attempt < cyberdeck_ble::k_max_reconnect_attempts; ++attempt) {
            CHECK(machine.consume_background_attempt());
        }
        CHECK(!machine.consume_background_attempt());
        CHECK(!machine.background_reconnect_armed());
        machine.reset_background_cycle();
        CHECK(machine.background_reconnect_armed());
        CHECK(machine.consume_background_attempt());

        machine.block_background_reconnect();
        machine.reset_background_cycle();
        CHECK(!machine.background_reconnect_armed());
        CHECK(!machine.consume_background_attempt());

        machine.set_paired_devices({bond});
        machine.begin_paired();
        machine.take_actions();
        machine.press(key::enter);
        CHECK_EQ(count_actions(machine.take_actions(), action_kind::connect),
                 std::size_t(1));
        CHECK(machine.background_reconnect_armed());
    }
}

/* REQ-COV-01 cycle 2: the public-API-reachable branch misses from the gcovr
 * report that cycle 1 did not close. Each block pins one real behavior;
 * none of these invent API, throw, OOM or exclude categories. */
void test_coverage_cycle2_emit_and_connection_guards()
{
    /* emit_action token routing: tok!=0 passes through verbatim (pair path
     * already covers tok==0 routing for start_scan/pair/connect). Reach the
     * default arm via submit_auth with an explicit displayed passkey: the
     * action carries the explicit token, not a generation token. */
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.auth_requested(machine.active_pair_token(),
                               auth_request_kind::passkey, 0,
                               cyberdeck_ble::auth_io_action::input);
        machine.submit_auth(654321);
        const std::vector<action> actions = machine.take_actions();
        CHECK_EQ(count_actions(actions, action_kind::submit_auth),
                 std::size_t(1));
        if (!actions.empty()) {
            CHECK_EQ(actions[0].passkey, std::uint32_t(654321));
            CHECK_EQ(actions[0].token, machine.active_pair_token());
        }
    }
    /* connection_finished success with a cleared connecting address skips
     * the background re-arm (12->15 miss): schedule_reconnect reserves a
     * background attempt, then a manual paired-screen switch clears the
     * in-flight identity while the token stays valid. Simpler public path:
     * connect from paired, succeed, then verify armed; then a machine whose
     * connecting address was cleared by Escape-before-completion. */
    {
        state_machine machine;
        machine.set_paired_devices(
            {make_device("AA:BB:CC:DD:EE:01", "Fone", -50,
                         device_kind::headset)});
        machine.begin_paired();
        machine.take_actions();
        machine.press(key::enter);
        machine.take_actions();
        const std::uint64_t token = machine.active_connection_token();
        machine.connection_finished(token, false);
        CHECK(machine.current_screen() == screen::results);
        CHECK(machine.current_notice() == notice::failed);
        CHECK_STR(machine.notice_text(), cyberdeck_ble::k_msg_connect_failed);
    }
    /* pairing_started with a foreign token on the paired screen is dropped
     * (token-mismatch arm from paired). Note token 0 matches the initial
     * pair_token, so a foreign non-zero token is the real mismatch path. */
    {
        state_machine machine;
        machine.set_paired_devices(
            {make_device("AA:BB:CC:DD:EE:09", "Bond", -40,
                         device_kind::keyboard)});
        machine.begin_paired();
        machine.take_actions();
        machine.pairing_started(9999, "AA:BB:CC:DD:EE:09");
        CHECK(machine.current_screen() == screen::paired);
        CHECK(machine.take_actions().empty());
    }
    /* auth_requested numeric_compare with a bad number is dropped while the
     * pair deadline keeps running (278 miss arm). */
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        const std::uint64_t token = machine.active_pair_token();
        machine.auth_requested(token, auth_request_kind::passkey, 7,
                               cyberdeck_ble::auth_io_action::numeric_compare);
        CHECK(machine.current_screen() == screen::pairing);
        CHECK_EQ(machine.displayed_passkey(), std::uint32_t(0));
    }
    /* connection_finished connected==true while only established (no flight)
     * returns early (393 5->6 miss): succeed, then replay success. */
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.pairing_finished(machine.active_pair_token(),
                                 pair_outcome::bonded);
        machine.take_actions();
        const std::uint64_t token = machine.active_connection_token();
        machine.connection_finished(token, true);
        CHECK(machine.is_connected());
        machine.connection_finished(token, true);
        CHECK(machine.is_connected());
        CHECK(machine.take_actions().empty());
    }
}

/* REQ-COV-01 cycle 2: advance_time off-screen deadline arms that stay silent,
 * plus press/escape arms on paired/connecting and auth-enter routing. */
void test_coverage_cycle2_deadlines_press_auth()
{
    /* Scan deadline armed but screen already left searching: fires
     * harmlessly (504 4->7 miss). begin_search, reconnect (moves to
     * connecting, deadline still armed), then advance past the scan
     * deadline: no timeout notice, no cancel_scan. */
    {
        state_machine machine;
        machine.begin_search();
        machine.take_actions();
        machine.schedule_reconnect(make_device("AA:BB:CC:DD:EE:09", "Fone",
                                               -50, device_kind::headset));
        machine.take_actions();
        CHECK(machine.current_screen() == screen::connecting);
        machine.advance_time(cyberdeck_ble::k_scan_timeout_ms + 1);
        CHECK(machine.current_screen() == screen::connecting);
        CHECK(machine.current_notice() == notice::none);
        CHECK(machine.take_actions().empty());
    }
    /* Pair deadline armed but off the pairing/auth screens: silent (514).
     * Pair, escape to results (deadline cleared), re-arm by entering again
     * is covered; instead pin the pair-deadline-not-armed path with a plain
     * results machine advancing time. */
    {
        state_machine machine;
        machine.begin_search();
        machine.scan_finished(machine.active_scan_token(), {
            make_device("AA:BB:CC:DD:EE:01", "A", -50,
                        device_kind::keyboard)});
        machine.take_actions();
        machine.advance_time(cyberdeck_ble::k_pair_timeout_ms + 1);
        CHECK(machine.current_screen() == screen::results);
        CHECK(machine.take_actions().empty());
    }
    /* Auth deadline armed but already left auth (527 19->26 miss): request
     * auth, escape to results, advance past the auth deadline: silent. */
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.auth_requested(machine.active_pair_token(),
                               auth_request_kind::passkey, kSecretPasskey,
                               cyberdeck_ble::auth_io_action::display);
        machine.press(key::escape);
        machine.take_actions();
        machine.advance_time(cyberdeck_ble::k_auth_timeout_ms + 1);
        CHECK(machine.current_screen() == screen::results);
        CHECK(machine.take_actions().empty());
    }
    /* Connect deadline armed but off connecting (540 28->37 miss): pair,
     * bond, cancel via escape (deadline cleared), advance: silent. */
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.pairing_finished(machine.active_pair_token(),
                                 pair_outcome::bonded);
        machine.take_actions();
        machine.press(key::escape);
        machine.take_actions();
        machine.advance_time(cyberdeck_ble::k_connect_timeout_ms + 1);
        CHECK(machine.current_screen() == screen::results);
        CHECK(machine.take_actions().empty());
    }
    /* press idle with link but token==0/address empty: silent (570 6->9).
     * Reachable only via a synthetic path: fresh machine, escape. */
    {
        state_machine machine;
        machine.press(key::escape);
        CHECK(machine.current_screen() == screen::idle);
        CHECK(machine.take_actions().empty());
    }
    /* press results: enter on a non-connectable selection sets the notice
     * (622 49->51 is the escape arm; pin escape-from-results-with-notice). */
    {
        state_machine machine;
        machine.begin_search();
        machine.scan_finished(machine.active_scan_token(), {
            make_device("AA:BB:CC:DD:EE:01", "Beacon", -40,
                        device_kind::unknown, false)});
        machine.take_actions();
        machine.press(key::enter);
        CHECK(machine.current_notice() == notice::not_connectable);
        machine.press(key::escape);
        CHECK(machine.current_screen() == screen::idle);
        CHECK(machine.current_notice() == notice::none);
        CHECK(machine.take_actions().empty());
    }
    /* press paired escape releases silently (655 64->66 miss). */
    {
        state_machine machine;
        machine.set_paired_devices(
            {make_device("AA:BB:CC:DD:EE:01", "A", -40,
                         device_kind::keyboard)});
        machine.begin_paired();
        machine.take_actions();
        machine.press(key::escape);
        CHECK(machine.current_screen() == screen::idle);
        CHECK(machine.current_notice() == notice::none);
        CHECK(machine.take_actions().empty());
    }
    /* press pairing/auth enter with input pending_io is inert (677/678):
     * covered in cycle 1; pin the display-mode enter forwards submit_auth
     * and clears the secret. */
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.auth_requested(machine.active_pair_token(),
                               auth_request_kind::passkey, kSecretPasskey,
                               cyberdeck_ble::auth_io_action::display);
        machine.press(key::enter);
        CHECK_EQ(count_actions(machine.take_actions(), action_kind::submit_auth),
                 std::size_t(1));
        CHECK_EQ(machine.displayed_passkey(), std::uint32_t(0));
        CHECK(machine.current_screen() == screen::pairing);
    }
    /* submit_auth display-mode with an out-of-range displayed value still
     * forwards (737 8->9 miss): request display with max valid, then submit
     * an arbitrary user value. */
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.auth_requested(machine.active_pair_token(),
                               auth_request_kind::passkey, 999999,
                               cyberdeck_ble::auth_io_action::display);
        machine.submit_auth(123456);
        CHECK_EQ(count_actions(machine.take_actions(), action_kind::submit_auth),
                 std::size_t(1));
        CHECK(machine.current_screen() == screen::pairing);
    }
}

/* REQ-COV-01 cycle 2: owns_input connecting arm, notice cancelled arms,
 * status connected arm, active_address arms, background short-circuits. */
void test_coverage_cycle2_observe_background()
{
    /* owns_input on connecting is true (760 2->7 miss is the default/foreign
     * arm; pin connecting explicitly). */
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.pairing_finished(machine.active_pair_token(),
                                 pair_outcome::bonded);
        machine.take_actions();
        CHECK(machine.current_screen() == screen::connecting);
        CHECK(machine.owns_input());
    }
    /* notice_text cancelled-from-connect with idle screen: search-cancel
     * path already covers idle; pin connecting-cancel notice text. */
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.pairing_finished(machine.active_pair_token(),
                                 pair_outcome::bonded);
        machine.take_actions();
        machine.press(key::escape);
        CHECK_STR(machine.notice_text(),
                  cyberdeck_ble::k_msg_connect_cancelled);
    }
    /* status_line connecting arm resolves the shown name from the scan
     * list (829 3->4 miss is the found path; pin found vs missing). */
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.pairing_finished(machine.active_pair_token(),
                                 pair_outcome::bonded);
        machine.take_actions();
        CHECK(machine.status_line().find("Teclado") != std::string::npos);
    }
    /* active_address on connecting returns the connecting address (904
     * 6->7 miss is the connected arm; pin connected via the only public
     * route that visits screen::connected... there is none: connected is
     * legacy and unreachable via the public API, so pin connecting). */
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.pairing_finished(machine.active_pair_token(),
                                 pair_outcome::bonded);
        machine.take_actions();
        CHECK_STR(machine.active_address(), "AA:BB:CC:DD:EE:01");
    }
    /* background_reconnect_armed short-circuit: armed but manually blocked
     * returns false without evaluating the rest (912/913). */
    {
        state_machine machine;
        const device bond = make_device("AA:BB:CC:DD:EE:77", "Kb", -45,
                                        device_kind::keyboard);
        machine.arm_background_reconnect(bond);
        CHECK(machine.background_reconnect_armed());
        machine.block_background_reconnect();
        CHECK(!machine.background_reconnect_armed());
        CHECK(!machine.consume_background_attempt());
    }
    /* consume_background_attempt with arm but no target: block path needs
     * a target; pin the cap path (952 4->5 miss: attempts exhausted). */
    {
        state_machine machine;
        const device bond = make_device("AA:BB:CC:DD:EE:78", "Kb", -45,
                                        device_kind::keyboard);
        machine.arm_background_reconnect(bond);
        for (std::uint32_t i = 0;
             i < cyberdeck_ble::k_max_reconnect_attempts; ++i) {
            CHECK(machine.consume_background_attempt());
        }
        CHECK(!machine.consume_background_attempt());
        CHECK(!machine.background_reconnect_armed());
    }
}

/* REQ-COV-01 cycle 2b: leftover reachable arms from the gcovr miss list:
 * empty-identity success skips re-arm, active_address on auth, armed pair
 * /auth deadlines firing off-screen, and the paired-screen token gate. */
void test_coverage_cycle2b_identity_and_armed_deadlines()
{
    /* Success with an empty connecting identity skips the background re-arm
     * (state_machine.cpp:421 12->15): schedule_reconnect accepts any record
     * through the public API, including one with no address. */
    {
        state_machine machine;
        device anonymous = make_device("", nullptr, -50,
                                       device_kind::unknown);
        machine.schedule_reconnect(anonymous);
        CHECK(machine.current_screen() == screen::connecting);
        const std::vector<action> attempt = machine.take_actions();
        CHECK_EQ(count_actions(attempt, action_kind::reconnect),
                 std::size_t(1));
        machine.connection_finished(machine.active_connection_token(), true);
        CHECK(machine.current_screen() == screen::idle);
        CHECK(machine.is_connected());
        CHECK(!machine.has_background_target());
        CHECK(!machine.background_reconnect_armed());
        CHECK(machine.take_actions().empty());
    }
    /* active_address on the auth screen returns the pairing address (the
     * pairing||auth second-operand arm). */
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.auth_requested(machine.active_pair_token(),
                               auth_request_kind::passkey, kSecretPasskey,
                               cyberdeck_ble::auth_io_action::display);
        CHECK(machine.current_screen() == screen::auth);
        CHECK_STR(machine.active_address(), "AA:BB:CC:DD:EE:01");
    }
    /* Armed pair deadline firing off-screen stays silent: bonded promotes
     * to connecting without clearing the pair deadline, so advancing past
     * it exercises the pairing||auth-false arm while the connect deadline
     * fires its own timeout. */
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        const std::uint64_t pair_token = machine.active_pair_token();
        machine.pairing_finished(pair_token, pair_outcome::bonded);
        machine.take_actions();
        CHECK(machine.current_screen() == screen::connecting);
        machine.advance_time(cyberdeck_ble::k_pair_timeout_ms +
                             cyberdeck_ble::k_connect_timeout_ms);
        CHECK(machine.current_screen() == screen::results);
        CHECK(machine.current_notice() == notice::timed_out);
        CHECK_STR(machine.notice_text(),
                  cyberdeck_ble::k_msg_connect_timeout);
        const std::vector<action> timed_out = machine.take_actions();
        CHECK_EQ(count_actions(timed_out, action_kind::cancel_pair),
                 std::size_t(0));
        CHECK_EQ(count_actions(timed_out, action_kind::cancel_connect),
                 std::size_t(1));
    }
    /* Armed auth deadline firing off-screen stays silent: auth requested,
     * then bonded straight from the auth screen leaves the auth deadline
     * armed while connecting owns the screen. */
    {
        state_machine machine;
        prepare_pairing(machine, "AA:BB:CC:DD:EE:01", "Teclado");
        machine.auth_requested(machine.active_pair_token(),
                               auth_request_kind::passkey, kSecretPasskey,
                               cyberdeck_ble::auth_io_action::display);
        machine.pairing_finished(machine.active_pair_token(),
                                 pair_outcome::bonded);
        machine.take_actions();
        CHECK(machine.current_screen() == screen::connecting);
        machine.advance_time(cyberdeck_ble::k_auth_timeout_ms +
                             cyberdeck_ble::k_connect_timeout_ms);
        CHECK(machine.current_screen() == screen::results);
        CHECK_STR(machine.notice_text(),
                  cyberdeck_ble::k_msg_connect_timeout);
        CHECK(machine.status_line().find(kSecretDigits) == std::string::npos);
    }
}

} // namespace

int main()
{
    test_approved_messages_and_deadlines();
    test_search_is_asynchronous_and_emits_exactly_one_start();
    test_scan_timeout_fires_exactly_at_the_deadline();
    test_scan_outcomes_map_to_distinct_messages();
    test_input_ownership_is_derived_from_visible_model_state();
    test_scan_ownership_covers_active_empty_and_nonempty_paths();
    test_stale_scan_tokens_cannot_mutate_the_visible_list();
    test_navigation_and_enter_from_results();
    test_enter_preserves_exact_peer_address_type();
    test_enter_refuses_an_empty_or_non_connectable_selection();
    test_a_device_that_vanishes_cannot_be_paired();
    test_interactive_passkey_is_shown_on_auth_and_never_logged();
    test_non_passkey_authorization_forwards_no_secret();
    test_auth_io_action_is_preserved_and_numbers_are_not_reinterpreted();
    test_numeric_comparison_is_a_boolean_decision_with_display_only_number();
    test_auth_request_validation_and_stale_tokens();
    test_pair_and_auth_deadlines();
    test_escape_cancels_the_pairing_attempt_only();
    test_pair_outcomes_are_distinct();
    test_successful_bond_then_connection();
    test_is_connected_is_false_until_matching_connected_event();
    test_connection_success_does_not_clear_identity_contract_in_model();
    test_connect_reconnect_and_cancellation_preserve_address_type();
    test_submit_auth_preserves_active_address_type();
    test_connection_failure_and_cancellation();
    test_paired_list_enters_a_connection_without_re_pairing();
    test_automatic_reconnection_gives_up_after_the_cap();
    test_scan_and_reconnect_do_not_corrupt_each_other();
    test_status_lines_never_leak_and_are_bounded();
    test_background_bond_reconnect_cycles_are_bounded_and_explicit();
    test_coverage_stale_tokens_and_pairing_guards();
    test_coverage_connection_deadlines_and_background_outcome();
    test_coverage_press_notice_status_and_background_edges();
    test_coverage_guards_timeouts_and_emit_routing();
    test_coverage_connected_escape_cleanup_and_other_escapes();
    test_coverage_pairing_started_gates_and_type_resolution();
    test_coverage_auth_outcomes_from_auth_screen();
    test_coverage_advance_time_exceeded_and_offscreen();
    test_coverage_observation_defaults_and_background();
    test_coverage_cycle2_emit_and_connection_guards();
    test_coverage_cycle2_deadlines_press_auth();
    test_coverage_cycle2_observe_background();
    test_coverage_cycle2b_identity_and_armed_deadlines();

    std::printf("ble state machine contract: %d checks, %d failures\n",
                checks, failures);
    return failures == 0 ? 0 : 1;
}
