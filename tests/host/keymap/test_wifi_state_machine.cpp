/* TDD contract tests for the pure Wi-Fi search/saved state machine.
 * No ESP-IDF, LVGL, filesystem, timer, or callback implementation is faked.
 */
#include "features/wifi/cyberdeck_wifi_state_machine.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace cyberdeck_wifi;

namespace {
int failures = 0;
int checks = 0;

#define CHECK(condition) do { \
    ++checks; if (!(condition)) { ++failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } \
} while (0)

#define CHECK_EQ(actual, expected) do { \
    ++checks; if (!((actual) == (expected))) { ++failures; std::printf("FAIL %s:%d\n", __FILE__, __LINE__); } \
} while (0)

bool has_action(const std::vector<action>& actions, action_kind kind, const std::string& ssid,
                const std::string& password = "")
{
    for (const auto& item : actions) {
        if (item.kind == kind && item.ssid == ssid && item.password == password) return true;
    }
    return false;
}

void test_empty_scan_and_ordered_deduplication()
{
    state_machine machine;
    machine.begin_search();
    machine.scan_complete(10, {{"late", -1, true, false}});
    CHECK(machine.search_results().empty());
    machine.scan_complete(1, {});
    CHECK(machine.current_screen() == screen::search);
    CHECK(machine.search_results().empty());

    machine.scan_complete(1, {
        {"weak", -80, false, false}, {"home", -70, false, true},
        {"home", -45, false, true}, {"guest", -60, true, false},
    });
    const auto aps = machine.search_results();
    CHECK_EQ(aps.size(), std::size_t(3));
    if (aps.size() != std::size_t(3)) return;
    CHECK_EQ(aps[0].ssid, "home");
    CHECK_EQ(aps[0].rssi, -45);
    CHECK_EQ(aps[1].ssid, "guest");
    CHECK_EQ(aps[2].ssid, "weak");
}

void test_navigation_enter_and_escape_limits()
{
    state_machine machine;
    machine.begin_search();
    machine.scan_complete(1, {{"a", -30, true, false}, {"b", -40, true, false}});
    machine.press(key::up);
    CHECK_EQ(machine.selected_index(), std::size_t(0));
    machine.press(key::down);
    machine.press(key::down);
    CHECK_EQ(machine.selected_index(), std::size_t(1));
    machine.press(key::escape);
    const auto actions = machine.take_actions();
    CHECK(has_action(actions, action_kind::cancel_scan, ""));
    if (!actions.empty()) {
        CHECK_EQ(actions.front().token, std::uint64_t(1));
    }
    CHECK(machine.current_screen() == screen::idle);
}

void test_open_saved_and_new_protected_selection()
{
    state_machine machine;
    machine.set_saved_networks({"saved"});
    machine.begin_search();
    machine.scan_complete(1, {{"open", -20, true, false}, {"saved", -30, false, true},
                              {"new-secure", -40, false, false}});

    machine.press(key::enter);
    const auto open_connect_actions = machine.take_actions();
    CHECK(has_action(open_connect_actions, action_kind::connect, "open"));
    if (open_connect_actions.empty()) return;
    const auto open_token = open_connect_actions.front().token;
    machine.connection_callback(open_token, connection_event::connected);
    machine.connection_callback(open_token, connection_event::has_ip);
    CHECK(has_action(machine.take_actions(), action_kind::persist, "open"));

    machine.begin_search();
    machine.scan_complete(2, {{"open", -20, true, false}, {"saved", -30, false, true},
                              {"new-secure", -40, false, false}});
    machine.press(key::down);
    machine.press(key::enter);
    const auto saved_connect_actions = machine.take_actions();
    CHECK(has_action(saved_connect_actions, action_kind::connect, "saved"));
    if (saved_connect_actions.empty()) return;
    const auto saved_token = saved_connect_actions.front().token;
    machine.connection_callback(saved_token, connection_event::connected);
    machine.connection_callback(saved_token, connection_event::has_ip);
    CHECK(has_action(machine.take_actions(), action_kind::persist, "saved"));

    machine.begin_search();
    machine.scan_complete(3, {{"open", -20, true, false}, {"saved", -30, false, true},
                              {"new-secure", -40, false, false}});
    machine.press(key::down);
    machine.press(key::down);
    machine.press(key::enter);
    CHECK(machine.current_screen() == screen::password);
    machine.type_password("secret");
    CHECK_EQ(machine.password_display(), "******");
    machine.press(key::enter);
    const auto protected_connect_actions = machine.take_actions();
    CHECK(has_action(protected_connect_actions, action_kind::connect, "new-secure", "secret"));
    if (protected_connect_actions.empty()) return;
    const auto protected_token = protected_connect_actions.front().token;
    machine.connection_callback(protected_token, connection_event::connected);
    machine.connection_callback(protected_token, connection_event::has_ip);
    CHECK(has_action(machine.take_actions(), action_kind::persist, "new-secure", "secret"));
    CHECK(machine.current_screen() == screen::idle);
}

void test_connection_lifecycle_timeout_cancel_and_tokens()
{
    state_machine machine;
    machine.begin_search();
    machine.scan_complete(1, {{"new", -10, false, false}});
    machine.press(key::enter);
    machine.type_password("pw");
    machine.press(key::enter);
    CHECK(machine.current_screen() == screen::connecting);

    machine.connection_callback(999, connection_event::has_ip);
    const auto connect_actions = machine.take_actions();
    CHECK(connect_actions.size() == std::size_t(1));
    if (connect_actions.empty()) return;
    CHECK(has_action(connect_actions, action_kind::connect, "new", "pw"));
    const auto token = connect_actions.front().token;
    machine.connection_callback(token, connection_event::connected);
    CHECK(machine.current_screen() == screen::connecting); // connected without IP is not success
    CHECK(machine.take_actions().empty());
    machine.connection_callback(token, connection_event::has_ip);
    CHECK(has_action(machine.take_actions(), action_kind::persist, "new", "pw"));

    machine.begin_search();
    machine.scan_complete(2, {{"slow", -10, false, false}});
    machine.press(key::enter);
    machine.type_password("x");
    machine.press(key::enter);
    machine.advance_time(14999);
    CHECK(machine.current_screen() == screen::connecting);
    machine.advance_time(1);
    CHECK(has_action(machine.take_actions(), action_kind::timeout, "slow"));

    machine.begin_search();
    machine.scan_complete(3, {{"cancelled", -10, true, false}});
    machine.press(key::enter);
    const auto pre_cancel_actions = machine.take_actions();
    CHECK(has_action(pre_cancel_actions, action_kind::connect, "cancelled"));
    machine.press(key::escape);
    const auto cancelled_actions = machine.take_actions();
    CHECK(cancelled_actions.size() == std::size_t(1));
    if (cancelled_actions.empty()) return;
    CHECK(has_action(cancelled_actions, action_kind::cancel_connect, "cancelled"));
    const auto cancelled_token = cancelled_actions.front().token;
    machine.connection_callback(cancelled_token, connection_event::has_ip);
    CHECK(machine.take_actions().empty());
}

void test_persistence_only_after_ip_and_two_enter_forget_with_escape()
{
    state_machine machine;
    machine.set_saved_networks({"first", "second"});
    machine.begin_search();
    machine.scan_complete(1, {{"first", -10, false, true}});
    machine.press(key::enter);
    const auto connect_actions = machine.take_actions();
    CHECK(connect_actions.size() == std::size_t(1));
    if (connect_actions.empty()) return;
    CHECK(has_action(connect_actions, action_kind::connect, "first"));
    const auto token = connect_actions.front().token;
    machine.connection_callback(token, connection_event::connected);
    CHECK(machine.take_actions().empty());
    machine.connection_callback(token, connection_event::has_ip);
    CHECK(has_action(machine.take_actions(), action_kind::persist, "first"));

    machine.begin_saved();
    machine.press(key::enter);
    CHECK(machine.current_screen() == screen::forget_confirmation);
    machine.press(key::escape);
    CHECK(machine.current_screen() == screen::saved);
    CHECK(machine.take_actions().empty());
    machine.press(key::enter);
    machine.press(key::enter);
    CHECK(has_action(machine.take_actions(), action_kind::forget, "first"));
}

void test_failed_connection_returns_to_idle()
{
    state_machine machine;
    machine.begin_search();
    machine.scan_complete(1, {{"failed", -10, false, false}});
    machine.press(key::enter);
    machine.type_password("bad");
    machine.press(key::enter);

    const auto connect_actions = machine.take_actions();
    CHECK(connect_actions.size() == std::size_t(1));
    if (connect_actions.empty()) return;
    CHECK(has_action(connect_actions, action_kind::connect, "failed", "bad"));
    const auto token = connect_actions.front().token;

    machine.connection_callback(token, connection_event::failed);
    CHECK(machine.current_screen() == screen::idle);
    CHECK(machine.take_actions().empty());
    machine.connection_callback(token, connection_event::has_ip);
    CHECK(machine.take_actions().empty());
}

void test_empty_saved_list_is_safe()
{
    state_machine machine;
    machine.set_saved_networks({});
    machine.begin_saved();
    CHECK(machine.current_screen() == screen::saved);
    CHECK(machine.saved_networks().empty());
    CHECK(machine.selected_index() == std::size_t(0));
    machine.press(key::enter);
    CHECK(machine.current_screen() == screen::saved);
    CHECK(machine.take_actions().empty());
}

void test_selection_resets_between_searches_and_transitions()
{
    state_machine machine;
    machine.set_saved_networks({"saved"});
    machine.begin_search();
    machine.scan_complete(1, {{"one", -10, true, false}, {"two", -20, true, false}});
    machine.press(key::down);
    CHECK(machine.selected_index() == std::size_t(1));

    machine.begin_search();
    CHECK(machine.selected_index() == std::size_t(0));
    machine.scan_complete(2, {{"one", -10, true, false}, {"two", -20, true, false}});
    CHECK(machine.selected_index() == std::size_t(0));

    machine.begin_saved();
    CHECK(machine.selected_index() == std::size_t(0));
    machine.begin_search();
    CHECK(machine.selected_index() == std::size_t(0));
}

void test_password_backspace_empty_and_reentrant_search_rejected()
{
    state_machine machine;
    machine.begin_search();
    machine.scan_complete(1, {{"secure", -10, false, false}});
    machine.press(key::enter);
    CHECK(machine.current_screen() == screen::password);

    machine.begin_search();
    CHECK(machine.current_screen() == screen::password);
    CHECK(machine.take_actions().empty());

    machine.type_password("pw");
    machine.press(key::backspace);
    CHECK(machine.password_display() == "*");
    machine.press(key::backspace);
    CHECK(machine.password_display().empty());
    machine.press(key::backspace);
    CHECK(machine.password_display().empty());
    machine.press(key::enter);
    const auto empty_password_actions = machine.take_actions();
    CHECK(has_action(empty_password_actions, action_kind::connect, "secure", ""));
    CHECK(machine.current_screen() == screen::connecting);

    machine.begin_search();
    CHECK(machine.current_screen() == screen::connecting);
    CHECK(machine.take_actions().empty());
}

void test_escape_password_clears_model_and_allows_new_search()
{
    state_machine machine;
    machine.begin_search();
    machine.scan_complete(1, {{"protected", -10, false, false}});
    machine.press(key::enter);
    machine.type_password("secret");
    CHECK(machine.current_screen() == screen::password);
    CHECK_EQ(machine.password_display(), "******");

    machine.press(key::escape);
    CHECK(machine.current_screen() == screen::idle);
    CHECK(machine.password_display().empty());
    CHECK(machine.active_scan_token() == 0);
    CHECK(machine.active_connection_token() == 0);
    CHECK(machine.take_actions().empty());

    // Escape must not leave the model/UI stuck: a subsequent search gets a
    // fresh generation and accepts its completion.
    machine.begin_search();
    CHECK(machine.current_screen() == screen::search);
    const auto token = machine.active_scan_token();
    CHECK(token > 1);
    machine.scan_complete(token, {{"fresh", -5, true, false}});
    CHECK(machine.search_results().size() == std::size_t(1));
    CHECK_EQ(machine.search_results().front().ssid, "fresh");
}

void test_wifi_connect_failure_restores_state_and_permits_retry()
{
    state_machine machine;
    machine.begin_connection("failed", "transient-secret");
    const auto first = machine.take_actions();
    CHECK(first.size() == std::size_t(1));
    if (first.empty()) return;
    CHECK(first.front().kind == action_kind::connect);
    const auto old_token = first.front().token;
    CHECK(machine.current_screen() == screen::connecting);

    // This models wifi_mgr_connect/connection failure delivery through the
    // existing pure adapter contract, without faking the production manager.
    machine.connection_callback(old_token, connection_event::failed);
    CHECK(machine.current_screen() == screen::idle);
    CHECK(machine.active_connection_token() == 0);
    CHECK(machine.password_display().empty());
    CHECK(machine.take_actions().empty());

    machine.begin_connection("retry", "new-secret");
    const auto retry = machine.take_actions();
    CHECK(retry.size() == std::size_t(1));
    if (retry.empty()) return;
    CHECK(retry.front().kind == action_kind::connect);
    CHECK_EQ(retry.front().ssid, "retry");
    CHECK_EQ(retry.front().password, "new-secret");
    CHECK(retry.front().token != old_token);
}

void test_secret_actions_are_drained_and_not_reexposed()
{
    state_machine machine;
    machine.begin_connection("secret-net", "one-time-secret");
    const auto connect = machine.take_actions();
    CHECK(connect.size() == std::size_t(1));
    if (connect.empty()) return;
    CHECK_EQ(connect.front().password, "one-time-secret");

    // Draining the action queue must also clear the model-held secret.  The
    // returned value is intentionally owned by the caller and is not reused
    // as an internal observation.
    CHECK(machine.take_actions().empty());
    machine.connection_callback(connect.front().token, connection_event::failed);
    const auto after_failure = machine.take_actions();
    CHECK(after_failure.empty());
    CHECK(machine.password_display().empty());

    // A later operation must not inherit the previous password.
    machine.begin_connection("next-net", "");
    const auto next = machine.take_actions();
    CHECK(next.size() == std::size_t(1));
    if (next.empty()) return;
    CHECK(next.front().password.empty());
}

void test_secret_is_not_reexposed_after_successful_ip_completion()
{
    state_machine machine;
    machine.begin_connection("secure-net", "one-time-secret");
    const auto connect = machine.take_actions();
    CHECK(connect.size() == std::size_t(1));
    if (connect.empty()) return;

    machine.connection_callback(connect.front().token, connection_event::connected);
    CHECK(machine.take_actions().empty());
    machine.connection_callback(connect.front().token, connection_event::has_ip);
    const auto persisted = machine.take_actions();
    CHECK(persisted.size() == std::size_t(1));
    if (persisted.empty()) return;
    CHECK_EQ(persisted.front().password, "one-time-secret");
    CHECK(machine.password_display().empty());
    CHECK(machine.active_connection_token() == 0);

    /* Repeated late callbacks are idempotent and cannot recreate the secret. */
    machine.connection_callback(connect.front().token, connection_event::has_ip);
    machine.connection_callback(connect.front().token, connection_event::failed);
    CHECK(machine.take_actions().empty());
    machine.begin_connection("next-net", "");
    const auto next = machine.take_actions();
    CHECK(next.size() == std::size_t(1));
    if (!next.empty()) CHECK(next.front().password.empty());
}

void test_cancel_teardown_clears_secret_and_ignores_late_callbacks()
{
    state_machine machine;
    machine.begin_connection("secure-net", "rollback-secret");
    const auto connect = machine.take_actions();
    CHECK(connect.size() == std::size_t(1));
    if (connect.empty()) return;
    const auto token = connect.front().token;

    machine.cancel_connection();
    const auto cancelled = machine.take_actions();
    CHECK(has_action(cancelled, action_kind::cancel_connect, "secure-net"));
    CHECK(machine.current_screen() == screen::idle);
    CHECK(machine.password_display().empty());
    CHECK(machine.active_connection_token() == 0);

    /* Cancellation and the old callback are intentionally interleaved through
     * the public host seam; no callback mock or production adapter is needed. */
    machine.connection_callback(token, connection_event::connected);
    machine.connection_callback(token, connection_event::has_ip);
    CHECK(machine.take_actions().empty());

    machine.begin_connection("next-net", "");
    const auto next = machine.take_actions();
    CHECK(next.size() == std::size_t(1));
    if (!next.empty()) CHECK(next.front().password.empty());
}

/* REGRESSAO (final): a senha digitada passa pelo contrato puro como payload
 * pertencente exclusivamente a action::connect.  A verificacao observa a
 * API (sem ler stack/heap depois de lifetime, portanto sem UB): o payload
 * entregue e exato, a mascara interna e limpa ao iniciar a conexao, e uma
 * conexao posterior nao herda bytes da tentativa anterior. */
void test_password_copy_contract_is_clean_without_memory_observation()
{
    state_machine machine;
    machine.begin_search();
    machine.scan_complete(1, {{"protected", -10, false, false}});
    machine.press(key::enter);

    const std::string secret(64, 'P');
    machine.type_password(secret);
    CHECK_EQ(machine.password_display(), std::string(64, '*'));
    machine.press(key::enter);

    const auto connect = machine.take_actions();
    CHECK(connect.size() == std::size_t(1));
    if (connect.empty()) return;
    CHECK(connect.front().kind == action_kind::connect);
    CHECK_EQ(connect.front().password, secret);
    CHECK(machine.password_display().empty());

    machine.connection_callback(connect.front().token, connection_event::failed);
    CHECK(machine.password_display().empty());
    CHECK(machine.take_actions().empty());

    machine.begin_connection("next", "");
    const auto next = machine.take_actions();
    CHECK(next.size() == std::size_t(1));
    if (next.empty()) return;
    CHECK(next.front().password.empty());
}

void test_forget_then_search_and_old_callbacks_remain_inert()
{
    state_machine machine;
    machine.set_saved_networks({"old", "keep"});
    machine.begin_saved();
    machine.press(key::enter);
    machine.press(key::enter);
    const auto forget = machine.take_actions();
    CHECK(forget.size() == std::size_t(1));
    if (forget.empty()) return;
    CHECK(forget.front().kind == action_kind::forget);
    CHECK(machine.saved_networks().size() == std::size_t(1));

    const auto old_operation_token = forget.front().token;
    machine.begin_search();
    const auto scan_token = machine.active_scan_token();
    CHECK(scan_token > 0);
    // A callback from the forgotten operation must not affect the new screen.
    machine.connection_callback(old_operation_token, connection_event::has_ip);
    CHECK(machine.take_actions().empty());
    machine.scan_complete(scan_token, {{"after-forget", -1, true, false}});
    CHECK(machine.search_results().size() == std::size_t(1));
}

void test_review_scan_future_and_stale_tokens_rejected()
{
    state_machine machine;
    machine.begin_search(); // active scan token: 1
    // Future token rejected
    machine.scan_complete(9999, {{"future", -20, true, false}});
    CHECK(machine.search_results().empty());

    // Valid active token accepted
    machine.scan_complete(1, {{"valid", -30, true, false}});
    CHECK_EQ(machine.search_results().size(), std::size_t(1));

    // Stale token 0 or mismatch rejected
    machine.scan_complete(0, {{"zero", -10, true, false}});
    CHECK_EQ(machine.search_results().front().ssid, "valid");

    // Advance search to token 2
    machine.begin_search();
    // Old token 1 rejected now
    machine.scan_complete(1, {{"stale1", -10, true, false}});
    CHECK(machine.search_results().empty());
    // Active token 2 accepted
    machine.scan_complete(2, {{"fresh2", -15, true, false}});
    CHECK_EQ(machine.search_results().size(), std::size_t(1));
    CHECK_EQ(machine.search_results().front().ssid, "fresh2");
}

void test_review_scan_callback_after_escape_and_saved_rejected()
{
    state_machine machine;
    machine.begin_search(); // token 1
    machine.press(key::escape);
    const auto escape_actions = machine.take_actions();
    CHECK(has_action(escape_actions, action_kind::cancel_scan, ""));
    if (!escape_actions.empty()) {
        CHECK_EQ(escape_actions.front().token, std::uint64_t(1));
    }
    // Callback arriving after escape cancelled the scan must be ignored
    machine.scan_complete(1, {{"late_ap", -20, true, false}});
    CHECK(machine.search_results().empty());

    // Transition to saved cancels active scan
    machine.begin_search(); // token 2
    machine.begin_saved();
    const auto saved_actions = machine.take_actions();
    CHECK(has_action(saved_actions, action_kind::cancel_scan, ""));
    if (!saved_actions.empty()) {
        CHECK_EQ(saved_actions.front().token, std::uint64_t(2));
    }
    // Callback arriving after transition to saved must be ignored
    machine.scan_complete(2, {{"saved_late", -20, true, false}});
    CHECK(machine.search_results().empty());
}

void test_scan_cancel_race_rejects_stale_generation_after_restart()
{
    state_machine machine;
    machine.begin_search();
    const auto cancelled_token = machine.active_scan_token();
    machine.press(key::escape);
    CHECK(machine.active_scan_token() == 0);
    CHECK(has_action(machine.take_actions(), action_kind::cancel_scan, ""));

    machine.begin_search();
    const auto current_token = machine.active_scan_token();
    CHECK(current_token != cancelled_token);

    /* Model the callback/cancel race with real public calls: a late callback
     * from the cancelled generation must not populate the new scan. */
    machine.scan_complete(cancelled_token, {{"stale", -10, true, false}});
    CHECK(machine.search_results().empty());
    machine.scan_complete(current_token, {{"current", -20, true, false}});
    CHECK(machine.search_results().size() == std::size_t(1));
    CHECK(machine.search_results().front().ssid == "current");
}

void test_review_begin_search_inert_in_password_and_connecting()
{
    state_machine machine;
    machine.begin_search(); // token 1
    machine.scan_complete(1, {{"secured", -10, false, false}});
    machine.press(key::enter);
    CHECK(machine.current_screen() == screen::password);
    machine.type_password("my_secret");

    // begin_search in password screen must be completely inert
    machine.begin_search();
    CHECK(machine.current_screen() == screen::password);
    CHECK_EQ(machine.password_display(), "*********");
    CHECK(machine.take_actions().empty());

    machine.press(key::enter);
    CHECK(machine.current_screen() == screen::connecting);
    const auto connect_actions = machine.take_actions();
    CHECK(connect_actions.size() == std::size_t(1));
    const auto conn_token = connect_actions.front().token;

    // begin_search in connecting screen must be completely inert
    machine.begin_search();
    CHECK(machine.current_screen() == screen::connecting);
    CHECK(machine.take_actions().empty());

    // Connection completes successfully
    machine.connection_callback(conn_token, connection_event::connected);
    machine.connection_callback(conn_token, connection_event::has_ip);
    const auto persist_actions = machine.take_actions();
    CHECK(has_action(persist_actions, action_kind::persist, "secured", "my_secret"));
}

void test_review_connection_future_and_stale_tokens_rejected()
{
    state_machine machine;
    machine.begin_search();
    machine.scan_complete(1, {{"ap1", -20, true, false}});
    machine.press(key::enter);
    const auto conn_actions = machine.take_actions();
    CHECK(conn_actions.size() == std::size_t(1));
    const auto conn_token = conn_actions.front().token;

    // Future token rejected
    machine.connection_callback(conn_token + 100, connection_event::connected);
    machine.connection_callback(conn_token + 100, connection_event::has_ip);
    CHECK(machine.take_actions().empty());
    CHECK(machine.current_screen() == screen::connecting);

    // Stale token 0 rejected
    machine.connection_callback(0, connection_event::connected);
    CHECK(machine.take_actions().empty());

    // Valid token finishes
    machine.connection_callback(conn_token, connection_event::connected);
    machine.connection_callback(conn_token, connection_event::has_ip);
    CHECK(has_action(machine.take_actions(), action_kind::persist, "ap1"));
    CHECK(machine.current_screen() == screen::idle);

    // Callback after completion rejected
    machine.connection_callback(conn_token, connection_event::has_ip);
    CHECK(machine.take_actions().empty());
}

void test_timeout_invalidates_old_connection_and_allows_new_scan()
{
    state_machine machine;
    machine.begin_search();
    machine.scan_complete(1, {{"slow", -20, true, false}});
    machine.press(key::enter);
    const auto first_connect = machine.take_actions();
    CHECK(first_connect.size() == std::size_t(1));
    if (first_connect.empty()) return;
    const auto old_token = first_connect.front().token;

    machine.advance_time(15000);
    const auto timeout_actions = machine.take_actions();
    CHECK(machine.current_screen() == screen::idle);
    CHECK(has_action(timeout_actions, action_kind::timeout, "slow"));
    CHECK(has_action(timeout_actions, action_kind::cancel_connect, "slow"));

    // A real late callback from the timed-out connection must not resurrect it.
    machine.connection_callback(old_token, connection_event::connected);
    machine.connection_callback(old_token, connection_event::has_ip);
    CHECK(machine.current_screen() == screen::idle);
    CHECK(machine.take_actions().empty());

    // The coordinator/UI model must remain usable after timeout.
    machine.begin_search();
    machine.scan_complete(2, {{"fresh", -10, true, false}});
    CHECK(machine.search_results().size() == std::size_t(1));
    CHECK(machine.search_results().front().ssid == "fresh");
}

void test_real_connection_callbacks_old_and_future_tokens_are_ignored()
{
    state_machine machine;
    machine.begin_search();
    machine.scan_complete(1, {{"first", -20, true, false}});
    machine.press(key::enter);
    const auto first_actions = machine.take_actions();
    CHECK(first_actions.size() == std::size_t(1));
    if (first_actions.empty()) return;
    const auto old_token = first_actions.front().token;

    machine.press(key::escape);
    const auto cancel_actions = machine.take_actions();
    CHECK(has_action(cancel_actions, action_kind::cancel_connect, "first"));

    machine.begin_search();
    machine.scan_complete(2, {{"second", -10, true, false}});
    machine.press(key::enter);
    const auto second_actions = machine.take_actions();
    CHECK(second_actions.size() == std::size_t(1));
    if (second_actions.empty()) return;
    const auto current_token = second_actions.front().token;
    CHECK(current_token != old_token);

    // Both callback directions are exercised with a stale and a future token.
    machine.connection_callback(old_token, connection_event::connected);
    machine.connection_callback(old_token, connection_event::has_ip);
    machine.connection_callback(current_token + 100, connection_event::connected);
    machine.connection_callback(current_token + 100, connection_event::has_ip);
    CHECK(machine.current_screen() == screen::connecting);
    CHECK(machine.take_actions().empty());

    machine.connection_callback(current_token, connection_event::connected);
    CHECK(machine.take_actions().empty());
    machine.connection_callback(current_token, connection_event::has_ip);
    CHECK(has_action(machine.take_actions(), action_kind::persist, "second"));
}

void test_cancel_and_timeout_return_to_idle_and_permit_new_connection()
{
    state_machine machine;
    machine.begin_search();
    machine.scan_complete(1, {{"cancel-me", -20, true, false}});
    machine.press(key::enter);
    const auto cancel_connect = machine.take_actions();
    CHECK(cancel_connect.size() == std::size_t(1));
    machine.press(key::escape);
    CHECK(machine.current_screen() == screen::idle);
    CHECK(has_action(machine.take_actions(), action_kind::cancel_connect, "cancel-me"));

    machine.begin_search();
    machine.scan_complete(2, {{"timeout-me", -15, true, false}});
    machine.press(key::enter);
    (void)machine.take_actions();
    machine.advance_time(15000);
    CHECK(machine.current_screen() == screen::idle);
    CHECK(has_action(machine.take_actions(), action_kind::timeout, "timeout-me"));

    machine.begin_search();
    machine.scan_complete(3, {{"after-timeout", -5, true, false}});
    machine.press(key::enter);
    const auto reconnect = machine.take_actions();
    CHECK(has_action(reconnect, action_kind::connect, "after-timeout"));
    CHECK(machine.current_screen() == screen::connecting);
}

void test_persist_requires_ip_not_only_connected()
{
    state_machine machine;
    machine.begin_search();
    machine.scan_complete(1, {{"no-ip", -10, true, false}});
    machine.press(key::enter);
    const auto connect_actions = machine.take_actions();
    CHECK(connect_actions.size() == std::size_t(1));
    if (connect_actions.empty()) return;
    const auto token = connect_actions.front().token;

    machine.connection_callback(token, connection_event::connected);
    CHECK(machine.current_screen() == screen::connecting);
    CHECK(machine.take_actions().empty());

    machine.connection_callback(token, connection_event::has_ip);
    const auto persisted = machine.take_actions();
    CHECK(persisted.size() == std::size_t(1));
    CHECK(has_action(persisted, action_kind::persist, "no-ip"));
    CHECK(machine.current_screen() == screen::idle);
}

void test_review_forget_domain_token_isolated()
{
    state_machine machine;
    machine.set_saved_networks({"net_a", "net_b"});
    machine.begin_saved();
    machine.press(key::enter); // forget confirmation
    machine.press(key::enter); // confirm forget
    const auto forget_actions = machine.take_actions();
    CHECK(has_action(forget_actions, action_kind::forget, "net_a"));
    if (forget_actions.empty()) return;
    const auto forget_token = forget_actions.front().token;
    CHECK(forget_token > 0);

    // Starting search after forget gets its own isolated scan token sequence
    machine.begin_search();
    machine.scan_complete(1, {{"net_x", -30, true, false}});
    CHECK_EQ(machine.search_results().size(), std::size_t(1));
}

void test_review_moved_from_safety()
{
    state_machine a;
    a.begin_search();
    a.scan_complete(1, {{"test_ap", -40, true, false}});
    state_machine b = std::move(a);

    // Moved-to object b has results
    CHECK(b.current_screen() == screen::search);
    CHECK_EQ(b.search_results().size(), std::size_t(1));

    // Moved-from object a must not crash when querying
    CHECK(a.current_screen() == screen::idle);
    CHECK_EQ(a.selected_index(), std::size_t(0));
    CHECK(a.search_results().empty());
    CHECK(a.saved_networks().empty());
    CHECK(a.password_display().empty());
    CHECK(a.take_actions().empty());
}
} // namespace

int main()
{
    test_empty_scan_and_ordered_deduplication();
    test_navigation_enter_and_escape_limits();
    test_open_saved_and_new_protected_selection();
    test_connection_lifecycle_timeout_cancel_and_tokens();
    test_persistence_only_after_ip_and_two_enter_forget_with_escape();
    test_failed_connection_returns_to_idle();
    test_empty_saved_list_is_safe();
    test_selection_resets_between_searches_and_transitions();
    test_password_backspace_empty_and_reentrant_search_rejected();
    test_escape_password_clears_model_and_allows_new_search();
    test_wifi_connect_failure_restores_state_and_permits_retry();
    test_secret_actions_are_drained_and_not_reexposed();
    test_secret_is_not_reexposed_after_successful_ip_completion();
    test_cancel_teardown_clears_secret_and_ignores_late_callbacks();
    test_password_copy_contract_is_clean_without_memory_observation();
    test_forget_then_search_and_old_callbacks_remain_inert();
    test_review_scan_future_and_stale_tokens_rejected();
    test_review_scan_callback_after_escape_and_saved_rejected();
    test_scan_cancel_race_rejects_stale_generation_after_restart();
    test_review_begin_search_inert_in_password_and_connecting();
    test_review_connection_future_and_stale_tokens_rejected();
    test_timeout_invalidates_old_connection_and_allows_new_scan();
    test_real_connection_callbacks_old_and_future_tokens_are_ignored();
    test_cancel_and_timeout_return_to_idle_and_permit_new_connection();
    test_persist_requires_ip_not_only_connected();
    test_review_forget_domain_token_isolated();
    test_review_moved_from_safety();
    std::printf("%s: %d checks\n", failures == 0 ? "PASS" : "FAIL", checks);
    return failures == 0 ? 0 : 1;
}
