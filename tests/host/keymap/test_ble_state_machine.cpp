/*
 * TDD RED host contract for the pure BLE search/pair state machine.
 *
 * Covers REQ-BLE-003 (asynchronous scan), REQ-BLE-005 (Up/Down/Enter/Escape),
 * REQ-BLE-006 (interactive authentication when the peer requires it),
 * REQ-BLE-008 (automatic reconnection) and REQ-BLE-009 (empty / failure /
 * timeout / cancellation messages).
 *
 * RED until the coder creates
 * components/cyberdeck/src/features/bluetooth/cyberdeck_ble_state_machine.cpp
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

    std::printf("ble state machine contract: %d checks, %d failures\n",
                checks, failures);
    return failures == 0 ? 0 : 1;
}
