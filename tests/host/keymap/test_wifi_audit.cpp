/* RED TDD contract for the passive/local Wi-Fi association audit. */
#include "features/wifi/cyberdeck_wifi_audit.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

using namespace cyberdeck_wifi_audit_test;

namespace {
int failures = 0;
int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

// Export contract: bounded, LF-delimited key=value records.  Values are
// compared from the complete text; parsing a pre-rendered object or a generic
// status message cannot satisfy these assertions.
std::optional<std::string> line_value(const std::string &text, std::string_view key)
{
    const std::string prefix = std::string(key) + "=";
    std::size_t search_from = 0;
    for (;;) {
        const std::size_t marker = text.find(prefix, search_from);
        if (marker == std::string::npos) return std::nullopt;
        search_from = marker + 1;
        if (marker != 0 && text[marker - 1] != '\n') continue;
        const std::size_t value_begin = marker + prefix.size();
        const std::size_t value_end = text.find('\n', value_begin);
        return text.substr(value_begin, value_end == std::string::npos ?
                                   std::string::npos : value_end - value_begin);
    }
}

void check_line(const std::string &text, std::string_view key, const std::string &expected)
{
    const auto actual = line_value(text, key);
    CHECK(actual.has_value());
    if (actual) CHECK(*actual == expected);
}

std::size_t count_occurrences(std::string_view text, std::string_view needle)
{
    std::size_t count = 0;
    std::size_t from = 0;
    while (from < text.size()) {
        const std::size_t at = text.find(needle, from);
        if (at == std::string_view::npos) return count;
        ++count;
        from = at + needle.size();
    }
    return count;
}

void check_missing_line(const std::string &text, std::string_view key)
{
    const auto actual = line_value(text, key);
    CHECK(actual.has_value());
    if (actual) {
        // An absent value must be explicit; an empty record line is not a
        // valid representation of a missing field.
        CHECK(*actual == "<missing>");
    }
}

constexpr const char *export_keys[] = {
    "version", "token", "status", "ssid", "bssid", "ip",
};
constexpr std::size_t export_key_count = sizeof(export_keys) / sizeof(export_keys[0]);
static_assert(export_key_count == 6, "Wi-Fi audit export must have exactly six keys");

std::size_t key_line_count(const std::string &text, std::string_view key)
{
    const std::string prefix = std::string(key) + "=";
    std::size_t count = 0;
    std::size_t search_from = 0;
    for (;;) {
        const std::size_t marker = text.find(prefix, search_from);
        if (marker == std::string::npos) return count;
        if (marker == 0 || text[marker - 1] == '\n') ++count;
        search_from = marker + prefix.size();
    }
}

void check_export_shape(const std::string &text)
{
    CHECK(!text.empty());
    if (text.empty()) return;

    // The export is a deterministic six-line LF-delimited record.  Do not
    // accept CRLF, a missing final LF, extra lines, or reordered/duplicate
    // keys just because individual key/value lookups happen to succeed.
    CHECK(text.back() == '\n');
    CHECK(text.find('\r') == std::string::npos);

    std::size_t line_start = 0;
    std::size_t line_count = 0;
    while (line_start < text.size()) {
        const std::size_t line_end = text.find('\n', line_start);
        CHECK(line_end != std::string::npos);
        if (line_end == std::string::npos) break;

        CHECK(line_count < export_key_count);
        if (line_count < export_key_count) {
            const std::string prefix = std::string(export_keys[line_count]) + "=";
            const std::string line = text.substr(line_start, line_end - line_start);
            CHECK(line.compare(0, prefix.size(), prefix) == 0);
            CHECK(line.size() > prefix.size());
        }
        ++line_count;
        line_start = line_end + 1;
    }
    CHECK(line_count == export_key_count);

    for (std::size_t index = 0; index < export_key_count; ++index) {
        CHECK(key_line_count(text, export_keys[index]) == 1);
    }
}

void test_commands_are_exact_and_save_is_the_only_explicit_persistence_command()
{
    CHECK(parse_command("wifi audit").kind == command::audit);
    CHECK(!parse_command("wifi audit").confirmed);

    // The save verb is deliberately explicit and does not inherit the retired
    // export confirmation syntax.
    const command_line save = parse_command("wifi audit save");
    CHECK(save.kind != command::invalid);
    CHECK(save.kind != command::audit);
    CHECK(!save.confirmed);
    CHECK(parse_command("wifi audit save confirm").kind == command::invalid);
    CHECK(parse_command("wifi audit save --confirm").kind == command::invalid);
    CHECK(parse_command("wifi audit export").kind == command::invalid);
    CHECK(parse_command("wifi audit export confirm").kind == command::invalid);
    CHECK(parse_command("wifi audit export --confirm").kind == command::invalid);
    CHECK(parse_command("wifi").kind == command::invalid);
    CHECK(parse_command("wifi audit now").kind == command::invalid);
    CHECK(parse_command("wifi audit savex").kind == command::invalid);
    CHECK(parse_command("wifi scan").kind == command::invalid);
}

void test_ui_state_contract_is_quiet_for_collecting_and_terminal_for_final_states()
{
    audit_controller audit;
    CHECK(audit.initialize());

    const snapshot initial = audit.snapshot_view();
    CHECK(initial.status == state::unavailable);

    const std::uint64_t token = audit.begin({true, "", "", ""});
    CHECK(token != 0);
    const snapshot collecting = audit.snapshot_view();
    CHECK(collecting.status == state::collecting);
    CHECK(render_ui(collecting).empty());
    CHECK(collecting.token == token);
    CHECK(collecting.ssid[0] == '\0');
    CHECK(collecting.bssid[0] == '\0');
    CHECK(collecting.ip[0] == '\0');
    snapshot collecting_with_residue = collecting;
    std::snprintf(collecting_with_residue.ssid, sizeof(collecting_with_residue.ssid),
                  "%s", "stale-ssid");
    std::snprintf(collecting_with_residue.bssid, sizeof(collecting_with_residue.bssid),
                  "%s", "stale-bssid");
    std::snprintf(collecting_with_residue.ip, sizeof(collecting_with_residue.ip),
                  "%s", "192.0.2.99");
    CHECK(render_ui(collecting_with_residue).empty());

    CHECK(audit.complete(token, {true, "Lab-Room", "aa:bb:cc:dd:ee:01", "198.51.100.7"}) == result::accepted);
    const snapshot ready = audit.snapshot_view();
    const std::string ready_rendered = render_ui(ready);
    CHECK(ready_rendered ==
          "wifi audit: status=ready\n"
          "ssid: Lab-Room\n"
          "bssid: aa:bb:cc:dd:ee:01\n"
          "ip: 198.51.100.7\n");
    CHECK(count_occurrences(ready_rendered, "wifi audit: status=ready\n") == 1);
    CHECK(count_occurrences(ready_rendered, "<missing>") == 0);
    CHECK(count_occurrences(ready_rendered, "\nssid: ") == 1);
    CHECK(count_occurrences(ready_rendered, "\nbssid: ") == 1);
    CHECK(count_occurrences(ready_rendered, "\nip: ") == 1);

    const std::uint64_t partial_token = audit.begin({true, "", "02:00:00:00:00:02", ""});
    CHECK(partial_token != 0);
    CHECK(audit.complete(partial_token, {true, "", "02:00:00:00:00:02", ""}) == result::accepted);
    const std::string partial_rendered = render_ui(audit.snapshot_view());
    CHECK(partial_rendered ==
          "wifi audit: status=ready\n"
          "ssid: <missing>\n"
          "bssid: 02:00:00:00:00:02\n"
          "ip: <missing>\n");
    CHECK(count_occurrences(partial_rendered, "<missing>") == 2);
    CHECK(partial_rendered.find("Lab-Room") == std::string::npos);
    CHECK(partial_rendered.find("198.51.100.7") == std::string::npos);

    audit.teardown();
    CHECK(audit.initialize());
    const std::uint64_t failed_token = audit.begin({true, "stale", "stale-bssid", "192.0.2.99"});
    CHECK(failed_token != 0);
    CHECK(audit.complete(failed_token, {true, "ignored", "ignored-bssid", "192.0.2.100"}, true) ==
          result::failed);
    const snapshot error = audit.snapshot_view();
    CHECK(error.status == state::error);
    const std::string error_rendered = render_ui(error);
    CHECK(error_rendered == "wifi audit: status=error\n");
    CHECK(count_occurrences(error_rendered, "\n") == 1);
    CHECK(error_rendered.find("ssid") == std::string::npos);
    CHECK(error_rendered.find("bssid") == std::string::npos);
    CHECK(error_rendered.find("ip") == std::string::npos);
    CHECK(error_rendered.find("<missing>") == std::string::npos);
    CHECK(error_rendered.find("stale") == std::string::npos);
    CHECK(error_rendered.find("192.0.2.99") == std::string::npos);
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

void test_export_contains_the_real_snapshot_and_fixed_target()
{
    audit_controller audit;
    CHECK(audit.initialize());
    const std::string ssid = "Office WiFi";
    const std::string bssid = "aa:bb:cc:dd:ee:ff";
    const std::string ip = "198.51.100.7";
    const auto token = audit.begin({true, ssid, bssid, ip});
    CHECK(token != 0);
    CHECK(audit.complete(token, {true, ssid, bssid, ip}) == result::accepted);
    const snapshot value = audit.snapshot_view();
    CHECK(audit.enqueue_export(token, "/sdcard/wifi-audit/wifi-audit-20260924-000405.txt", true));

    const export_result exported = audit.drain_export();
    CHECK(exported.ok);
    CHECK(exported.bytes < export_capacity);
    CHECK(std::strlen(exported.data) == exported.bytes);
    CHECK(std::string_view(exported.path) == "/sdcard/wifi-audit/wifi-audit-20260924-000405.txt");

    const std::string text(exported.data, exported.bytes);
    check_export_shape(text);
    CHECK(text.find("wifi audit snapshot updated") == std::string::npos);
    check_line(text, "version", std::to_string(snapshot_version));
    check_line(text, "token", std::to_string(value.token));
    check_line(text, "status", "ready");
    check_line(text, "ssid", value.ssid);
    check_line(text, "bssid", value.bssid);
    check_line(text, "ip", value.ip);
}

void test_missing_snapshot_fields_are_explicit_and_never_stale()
{
    audit_controller audit;
    CHECK(audit.initialize());

    const auto first = audit.begin({true, "first", "aa:bb:cc:dd:ee:01", "192.0.2.1"});
    CHECK(audit.complete(first, {true, "first", "aa:bb:cc:dd:ee:01", "192.0.2.1"}) == result::accepted);
    CHECK(audit.enqueue_export(first, "/sdcard/wifi-audit/wifi-audit-20260924-000405.txt", true));
    const auto first_export = audit.drain_export();
    CHECK(first_export.ok);
    const std::string first_text(first_export.data, first_export.bytes);
    check_export_shape(first_text);
    check_line(first_text, "bssid", "aa:bb:cc:dd:ee:01");
    check_line(first_text, "ip", "192.0.2.1");

    const auto second = audit.begin({true, "ssid-only", "", ""});
    CHECK(audit.complete(second, {true, "ssid-only", "", ""}) == result::accepted);
    CHECK(audit.enqueue_export(second, "/sdcard/wifi-audit/wifi-audit-20260924-000405.txt", true));
    const auto second_export = audit.drain_export();
    CHECK(second_export.ok);
    const std::string second_text(second_export.data, second_export.bytes);
    check_export_shape(second_text);
    check_line(second_text, "token", std::to_string(second));
    check_line(second_text, "ssid", "ssid-only");
    check_missing_line(second_text, "bssid");
    check_missing_line(second_text, "ip");
    CHECK(second_text.find("aa:bb:cc:dd:ee:01") == std::string::npos);
    CHECK(second_text.find("192.0.2.1") == std::string::npos);

    const auto third = audit.begin({true, "", "02:00:00:00:00:02", "198.51.100.9"});
    CHECK(audit.complete(third, {true, "", "02:00:00:00:00:02", "198.51.100.9"}) == result::accepted);
    CHECK(audit.enqueue_export(third, "/sdcard/wifi-audit/wifi-audit-20260924-000405.txt", true));
    const auto third_export = audit.drain_export();
    CHECK(third_export.ok);
    const std::string third_text(third_export.data, third_export.bytes);
    check_export_shape(third_text);
    check_missing_line(third_text, "ssid");
    check_line(third_text, "bssid", "02:00:00:00:00:02");
    check_line(third_text, "ip", "198.51.100.9");

    const auto fourth = audit.begin({true, "", "", ""});
    CHECK(audit.complete(fourth, {true, "", "", ""}) == result::accepted);
    CHECK(audit.enqueue_export(fourth, "/sdcard/wifi-audit/wifi-audit-20260924-000405.txt", true));
    const auto fourth_export = audit.drain_export();
    CHECK(fourth_export.ok);
    const std::string fourth_text(fourth_export.data, fourth_export.bytes);
    check_export_shape(fourth_text);
    check_missing_line(fourth_text, "ssid");
    check_missing_line(fourth_text, "bssid");
    check_missing_line(fourth_text, "ip");
}

void test_export_sanitization_and_line_escaping_stay_bounded()
{
    audit_controller audit;
    CHECK(audit.initialize());
    const std::string ssid = "Lab\nINJECT\r\t\x7f";
    const std::string bssid(256, 'b');
    const std::string ip = "192.0.2.1\nspoof";
    const auto token = audit.begin({true, ssid, bssid, ip});
    CHECK(audit.complete(token, {true, ssid, bssid, ip}) == result::accepted);
    CHECK(audit.enqueue_export(token, "/sdcard/wifi-audit/wifi-audit-20260924-000405.txt", true));

    const export_result exported = audit.drain_export();
    CHECK(exported.ok);
    CHECK(exported.bytes < export_capacity);
    CHECK(std::strlen(exported.data) == exported.bytes);
    const std::string text(exported.data, exported.bytes);
    check_export_shape(text);
    check_line(text, "ssid", "Lab?INJECT???");
    check_line(text, "ip", "192.0.2.1?spoof");
    const auto bounded_bssid = line_value(text, "bssid");
    CHECK(bounded_bssid.has_value());
    if (bounded_bssid) {
        CHECK(bounded_bssid->size() == field_capacity - 1);
        CHECK(bounded_bssid->find_first_not_of('b') == std::string::npos);
    }

    bool printable_record_lines = true;
    for (const unsigned char byte : text) {
        if ((byte < 0x20 && byte != '\n') || byte == 0x7f || byte >= 0x80) {
            printable_record_lines = false;
        }
    }
    CHECK(printable_record_lines);
    CHECK(text.find("Lab\nINJECT") == std::string::npos);
    CHECK(text.find("192.0.2.1\nspoof") == std::string::npos);
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
    CHECK(ui.find("office") != std::string::npos);
    CHECK(ui.find("aa:bb:cc:dd:ee:ff") != std::string::npos);
    CHECK(ui.find("198.51.100.7") != std::string::npos);
    CHECK(log.find("aa:bb:cc:dd:ee:ff") == std::string::npos);
    CHECK(audit.enqueue_export(token, "/sdcard/wifi-audit/wifi-audit-20260924-000405.txt", true));
    const auto exported = audit.drain_export();
    CHECK(exported.ok);
    CHECK(exported.bytes <= export_capacity);
    CHECK(std::strlen(exported.data) == exported.bytes);
    const std::string text(exported.data, exported.bytes);
    check_export_shape(text);
    CHECK(!contains_secret(text));
}

void test_save_is_confined_bounded_and_single_shot()
{
    audit_controller audit;
    CHECK(audit.initialize());
    const auto token = audit.begin({true, "safe", "", ""});
    CHECK(audit.complete(token, {true, "safe", "", ""}) == result::accepted);
    CHECK(!audit.enqueue_export(token, "/tmp/audit.txt", true));
    CHECK(!audit.enqueue_export(token, "/sdcard/../audit.txt", true));
    CHECK(audit.enqueue_export(token, "/sdcard/wifi-audit/wifi-audit-20260924-000405.txt", true));
    CHECK(!audit.enqueue_export(token, "/sdcard/wifi-audit/wifi-audit-20260924-000406.txt", true));
    const auto result = audit.drain_export();
    CHECK(result.ok);
    CHECK(result.bytes <= export_capacity);
    CHECK(audit.drain_export().ok == false);
}
} // namespace

int main()
{
    test_commands_are_exact_and_save_is_the_only_explicit_persistence_command();
    test_ui_state_contract_is_quiet_for_collecting_and_terminal_for_final_states();
    test_unavailable_and_ready_snapshots_are_versioned_and_bounded();
    test_export_contains_the_real_snapshot_and_fixed_target();
    test_missing_snapshot_fields_are_explicit_and_never_stale();
    test_export_sanitization_and_line_escaping_stay_bounded();
    test_tokens_are_monotonic_and_stale_future_or_cancelled_work_is_inert();
    test_errors_do_not_publish_partial_data_and_teardown_drops_queue();
    test_sanitization_is_applied_before_ui_log_and_export();
    test_save_is_confined_bounded_and_single_shot();
    std::printf("wifi audit: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
