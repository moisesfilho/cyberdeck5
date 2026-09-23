/* RED TDD contract for the passive/local Wi-Fi association audit. */
#include "features/wifi/cyberdeck_wifi_audit.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace cyberdeck_wifi_audit_test;

namespace {
int failures = 0;
int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

void test_commands_are_exact_and_confirmation_is_explicit()
{
    CHECK(parse_command("wifi audit").kind == command::audit);
    CHECK(!parse_command("wifi audit").confirmed);
    CHECK(parse_command("wifi audit export").kind == command::export_audit);
    CHECK(!parse_command("wifi audit export").confirmed);
    CHECK(parse_command("wifi audit export confirm").confirmed);
    CHECK(parse_command("wifi audit export --confirm").confirmed);
    CHECK(parse_command("wifi").kind == command::invalid);
    CHECK(parse_command("wifi audit now").kind == command::invalid);
    CHECK(parse_command("wifi audit export secret").kind == command::invalid);
    CHECK(parse_command("wifi scan").kind == command::invalid);
}

void test_unavailable_and_ready_snapshots_are_versioned_and_bounded()
{
    audit_controller audit;
    CHECK(audit.initialize());
    const snapshot initial = audit.snapshot_view();
    CHECK(initial.version == snapshot_version);
    CHECK(initial.status == state::unavailable);
    CHECK(initial.item_count <= snapshot_capacity);

    const std::string long_ssid(256, 's');
    const std::string long_bssid(256, 'b');
    const std::uint64_t token = audit.begin({true, long_ssid, long_bssid, "192.0.2.1"});
    CHECK(token != 0);
    CHECK(audit.status() == state::collecting);
    CHECK(audit.complete(token, {true, long_ssid, long_bssid, "192.0.2.1"}) == result::accepted);
    const snapshot ready = audit.snapshot_view();
    CHECK(ready.version == snapshot_version);
    CHECK(ready.status == state::ready);
    CHECK(ready.item_count <= snapshot_capacity);
    CHECK(ready.ssid[sizeof(ready.ssid) - 1] == '\0');
    CHECK(ready.bssid[sizeof(ready.bssid) - 1] == '\0');
    CHECK(ready.ip[sizeof(ready.ip) - 1] == '\0');
}

void test_tokens_are_monotonic_and_stale_future_or_cancelled_work_is_inert()
{
    audit_controller audit;
    CHECK(audit.initialize());
    const auto first = audit.begin({true, "one", "aa:bb:cc:dd:ee:ff", "192.0.2.2"});
    CHECK(audit.complete(first - 1, {true, "stale", "", ""}) == result::ignored);
    CHECK(audit.complete(first + 1, {true, "future", "", ""}) == result::ignored);
    CHECK(audit.cancel(first));
    CHECK(audit.status() != state::ready);
    const auto second = audit.begin({true, "two", "", ""});
    CHECK(second > first);
    CHECK(audit.complete(first, {true, "old", "", ""}) == result::ignored);
    CHECK(audit.complete(second, {true, "two", "", ""}) == result::accepted);
    CHECK(audit.snapshot_view().token == second);
}

void test_errors_do_not_publish_partial_data_and_teardown_drops_queue()
{
    audit_controller audit;
    CHECK(audit.initialize());
    const auto token = audit.begin({true, "private", "", ""});
    CHECK(audit.complete(token, {true, "private", "", ""}, true) == result::failed);
    CHECK(audit.status() == state::error);
    CHECK(audit.snapshot_view().item_count == 0);
    CHECK(!audit.enqueue_export(token, "/sdcard/../escape", true));
    audit.teardown();
    CHECK(!audit.initialized());
    CHECK(audit.pending() == 0);
    CHECK(!audit.enqueue_export(token, "/sdcard/audit.txt", true));
}

void test_sanitization_is_applied_before_ui_log_and_export()
{
    audit_controller audit;
    CHECK(audit.initialize());
    const auto token = audit.begin({true, "office", "aa:bb:cc:dd:ee:ff", "198.51.100.7"});
    CHECK(audit.complete(token, {true, "office", "aa:bb:cc:dd:ee:ff", "198.51.100.7"}) == result::accepted);
    const auto value = audit.snapshot_view();
    const std::string ui = render_ui(value);
    const std::string log = render_log(value);
    CHECK(!contains_secret(ui));
    CHECK(!contains_secret(log));
    CHECK(ui.find("office") == std::string::npos);
    CHECK(log.find("aa:bb:cc:dd:ee:ff") == std::string::npos);
    CHECK(audit.enqueue_export(token, "/sdcard/wifi-audit.txt", true));
    const auto exported = audit.drain_export();
    CHECK(exported.ok);
    CHECK(exported.bytes <= export_capacity);
    CHECK(std::strlen(exported.data) == exported.bytes);
    CHECK(!contains_secret(std::string(exported.data, exported.bytes)));
}

void test_export_requires_confirmation_is_confined_bounded_and_single_shot()
{
    audit_controller audit;
    CHECK(audit.initialize());
    const auto token = audit.begin({true, "safe", "", ""});
    CHECK(audit.complete(token, {true, "safe", "", ""}) == result::accepted);
    CHECK(!audit.enqueue_export(token, "/sdcard/audit.txt", false));
    CHECK(!audit.enqueue_export(token, "/tmp/audit.txt", true));
    CHECK(!audit.enqueue_export(token, "/sdcard/../audit.txt", true));
    CHECK(audit.enqueue_export(token, "/sdcard/audit.txt", true));
    CHECK(!audit.enqueue_export(token, "/sdcard/second.txt", true));
    const auto result = audit.drain_export();
    CHECK(result.ok);
    CHECK(result.bytes <= export_capacity);
    CHECK(audit.drain_export().ok == false);
}
} // namespace

int main()
{
    test_commands_are_exact_and_confirmation_is_explicit();
    test_unavailable_and_ready_snapshots_are_versioned_and_bounded();
    test_tokens_are_monotonic_and_stale_future_or_cancelled_work_is_inert();
    test_errors_do_not_publish_partial_data_and_teardown_drops_queue();
    test_sanitization_is_applied_before_ui_log_and_export();
    test_export_requires_confirmation_is_confined_bounded_and_single_shot();
    std::printf("wifi audit: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
