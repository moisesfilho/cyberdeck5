#include "apps/wifi/cyberdeck_wifi_survey.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace cyberdeck_wifi_survey;
int checks = 0;
int failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (false)

observation ap(const char *ssid, const char *bssid, int rssi, unsigned channel = 1,
              unsigned security = 3, bool known = true)
{
    observation value{};
    std::snprintf(value.ssid, sizeof(value.ssid), "%s", ssid);
    value.ssid_known = known;
    std::snprintf(value.bssid, sizeof(value.bssid), "%s", bssid);
    value.rssi = static_cast<std::int8_t>(rssi);
    value.channel = static_cast<std::uint8_t>(channel);
    value.security = static_cast<std::uint8_t>(security);
    return value;
}

void start(session &s, std::uint32_t duration = default_duration_seconds)
{
    CHECK(s.start(duration, 1000));
}

void test_duration_and_single_session_boundaries()
{
    session s;
    CHECK(!s.start(9, 0));
    CHECK(s.start(default_duration_seconds, 0));
    CHECK(s.duration_seconds() == 30u);
    CHECK(s.scan_count() == 0);
    CHECK(!s.start(10, 0));
    CHECK(!s.start(120, 0));
    CHECK(!s.start(121, 0));
    CHECK(s.expire(30000));
    CHECK(s.start(10, 30000));
    CHECK(s.expire(40000));
    CHECK(s.start(120, 40000));
    CHECK(s.expire(160000));
    CHECK(!s.start(30, UINT64_MAX - 29999));
}

void test_scan_schedule_sequential_interval_and_max_count()
{
    for (const auto duration : {10u, 30u, 120u}) {
        session s;
        start(s, duration);
        const auto first = s.due_scan(1000);
        CHECK(first.session_token != 0 && first.request_token != 0);
        CHECK(s.due_scan(1000).request_token == 0);
        observation item = ap("one", "aa:00:00:00:00:01", -40);
        CHECK(s.complete_scan(first, &item, 1, 1001));
        const std::uint64_t interval = static_cast<std::uint64_t>(s.effective_interval_seconds()) * 1000;
        CHECK(s.effective_interval_seconds() >= scan_interval_seconds);
        CHECK(s.due_scan(1000 + interval - 1).request_token == 0);
        auto request = s.due_scan(1000 + interval);
        CHECK(request.request_token != 0);
        s.fail_scan(request);
        const std::size_t expected_scans = std::min<std::size_t>(
            max_scans, (duration + s.effective_interval_seconds() - 1) /
                          s.effective_interval_seconds());
        for (std::size_t i = 3; i <= expected_scans; ++i) {
            request = s.due_scan(1000 + interval * (i - 1));
            CHECK(request.request_token != 0);
            s.fail_scan(request);
        }
        CHECK(s.scan_count() == expected_scans);
        CHECK(s.due_scan(1000 + interval * expected_scans).request_token == 0);
    }
}

void test_results_hidden_unknown_duplicates_and_ordering()
{
    session empty; start(empty, 10);
    auto empty_request = empty.due_scan(1000);
    CHECK(empty.complete_scan(empty_request, nullptr, 0, 1001));
    CHECK(empty.size() == 0);

    session s; start(s, 30);
    auto request = s.due_scan(1000);
    observation items[] = {
        ap("weak", "aa:00:00:00:00:03", -80),
        ap("", "aa:00:00:00:00:02", -50, 0, unknown_value, false),
        ap("same", "aa:00:00:00:00:01", -70),
        ap("same-strong", "aa:00:00:00:00:01", -40, 11, 6),
        ap("no-bssid", "", -60, 0, unknown_value),
    };
    CHECK(s.complete_scan(request, items, 5, 1001));
    CHECK(s.size() == 4);
    CHECK(s.at(0)->latest.rssi == -40);
    CHECK(std::strcmp(s.at(0)->latest.ssid, "same-strong") == 0);
    CHECK(s.at(0)->sample_count == 1);
    CHECK(s.at(1)->latest.ssid_known == false);
    CHECK(s.at(1)->latest.channel == 0);
    CHECK(s.at(1)->latest.security == unknown_value);
    CHECK(std::strcmp(s.at(2)->latest.bssid, "") == 0);
    CHECK(s.at(2)->latest.rssi == -60);
    CHECK(s.at(3)->latest.rssi == -80);
    CHECK(s.at(4) == nullptr);
    const std::string rendered = s.render();
    CHECK(rendered.find("<hidden> | aa:00:00:00:00:02 | ch=unknown | security=unknown") != std::string::npos);
    CHECK(rendered.find("no-bssid | <unknown> | ch=unknown | security=unknown") != std::string::npos);
    CHECK(rendered.find("samples=1 min=-40 avg=-40 max=-40") != std::string::npos);
}

void test_security_catalog_and_empty_bssid_identity()
{
    session s; start(s, 30);
    auto request = s.due_scan(1000);
    const char *security_names[] = {
        "open", "wep", "wpa-psk", "wpa2-psk", "wpa-wpa2-psk", "wpa2-enterprise",
        "wpa3-psk", "wpa2-wpa3-psk", "wapi-psk", "owe", "wpa3-enterprise-192",
        "wpa3-extended-psk", "wpa3-extended-psk-mixed", "dpp", "wpa3-enterprise",
        "wpa2-wpa3-enterprise", "wpa-enterprise",
    };
    observation first[16]{};
    for (std::size_t i = 0; i < 16; ++i) {
        char name[16], bssid[18];
        std::snprintf(name, sizeof(name), "mode-%02zu", i);
        std::snprintf(bssid, sizeof(bssid), "02:00:00:00:02:%02x", static_cast<unsigned>(i));
        first[i] = ap(name, bssid, -30 - static_cast<int>(i), 1,
                      static_cast<unsigned>(i));
    }
    CHECK(s.complete_scan(request, first, 16, 1001));
    request = s.due_scan(6000);
    auto last = ap("mode-16", "02:00:00:00:02:10", -60, 1, 16);
    CHECK(s.complete_scan(request, &last, 1, 6001));
    const std::string rendered = s.render();
    for (const char *name : security_names)
        CHECK(rendered.find(std::string("security=") + name) != std::string::npos);

    session empty_bssid; start(empty_bssid, 10);
    auto empty_request = empty_bssid.due_scan(1000);
    observation same_name[] = {
        ap("same", "", -70, 6), ap("same", "", -40, 6), ap("same", "", -50, 11),
    };
    CHECK(empty_bssid.complete_scan(empty_request, same_name, 3, 1001));
    CHECK(empty_bssid.size() == 2);
    bool found_strong_same_channel = false;
    for (std::size_t i = 0; i < empty_bssid.size(); ++i) {
        if (empty_bssid.at(i)->latest.channel == 6) {
            found_strong_same_channel = true;
            CHECK(empty_bssid.at(i)->sample_count == 1);
            CHECK(empty_bssid.at(i)->latest.rssi == -40);
        }
    }
    CHECK(found_strong_same_channel);

    session mixed_identity; start(mixed_identity, 10);
    auto mixed_request = mixed_identity.due_scan(1000);
    observation identified[] = {
        ap("same", "02:00:00:00:04:01", -50, 6),
        ap("same", "", -40, 6),
        ap("same", "", -45, 6, 3, false),
    };
    CHECK(mixed_identity.complete_scan(mixed_request, identified, 3, 1001));
    CHECK(mixed_identity.size() == 3);
}

void test_mask_identifier_unknown_and_input_sanitization()
{
    CHECK(mask_identifier(nullptr) == "<hidden>");
    CHECK(mask_identifier("") == "<hidden>");
    CHECK(mask_identifier("<hidden>") == "<hidden>");
    CHECK(mask_identifier("<unknown>") == "<unknown>");
    CHECK(mask_identifier("Cafe") == "****");
    CHECK(mask_identifier("aa:bb:cc") == "**:**:**");

    session s; start(s, 10);
    auto request = s.due_scan(1000);
    observation hidden_unknown = ap("", "", -45, 0, unknown_value, false);
    observation malformed = ap("Cafe|North\n", "AA:BB|CC:DD:EE:FF", -50);
    malformed.ssid[0] = static_cast<char>(0x01);
    malformed.ssid[1] = 'X';
    malformed.ssid[2] = static_cast<char>(0x80);
    malformed.ssid[3] = '\0';
    observation delimiters = ap("A|B\rC", "AA:BB|CC\rDD:EE:FF", -55);
    observation values[] = {hidden_unknown, malformed, delimiters};
    CHECK(s.complete_scan(request, values, 3, 1001));
    const std::string rendered = s.render();
    CHECK(rendered.find("<hidden> | <unknown>") != std::string::npos);
    CHECK(rendered.find("?X? | aa:bb?cc:dd:ee:ff") != std::string::npos);
    CHECK(rendered.find("A?B?C | aa:bb?cc?dd:ee:ff") != std::string::npos);
    const std::string export_masked = s.export_text(true);
    CHECK(export_masked.find("ap=<hidden>|<unknown>") != std::string::npos);
    CHECK(export_masked.find("ap=***|**:*****:**:**:**") != std::string::npos);
    const std::string export_clear = s.export_text(false);
    CHECK(export_clear.find("ap=A?B?C|aa:bb?cc?dd:ee:ff") != std::string::npos);
}

void test_empty_results_input_cap_and_session_render_states()
{
    session idle;
    CHECK(idle.render().find("wifi survey: idle") == 0);
    CHECK(!idle.cancel());
    CHECK(!idle.expire(1000));

    session s; start(s, 10);
    CHECK(s.render().find("wifi survey: sampling") == 0);
    auto request = s.due_scan(1000);
    CHECK(s.complete_scan(request, nullptr, 0, 1001));
    CHECK(s.size() == 0);
    request = s.due_scan(6000);
    observation over_driver_cap[17]{};
    for (std::size_t i = 0; i < 17; ++i) {
        char name[16], bssid[18];
        std::snprintf(name, sizeof(name), "cap-%02zu", i);
        std::snprintf(bssid, sizeof(bssid), "02:00:00:00:03:%02x", static_cast<unsigned>(i));
        over_driver_cap[i] = ap(name, bssid, -80 + static_cast<int>(i));
    }
    CHECK(s.complete_scan(request, over_driver_cap, 17, 6001));
    CHECK(s.size() == 16);
    bool extra_seen = false;
    for (std::size_t i = 0; i < s.size(); ++i)
        extra_seen = extra_seen || std::strcmp(s.at(i)->latest.ssid, "cap-16") == 0;
    CHECK(!extra_seen);
    CHECK(s.expire(11000));
    CHECK(s.render().find("wifi survey: complete") == 0);
    CHECK(!s.expire(12000));

    session cancelled; start(cancelled, 10);
    CHECK(cancelled.cancel());
    CHECK(!cancelled.cancel());
    CHECK(cancelled.render().find("wifi survey: cancelled") == 0);
}

void test_network_cap_replacement_and_duplicate_count()
{
    session s; start(s, 30);
    auto request = s.due_scan(1000);
    std::vector<observation> first;
    for (int i = 0; i < 16; ++i) {
        char ssid[16], bssid[18];
        std::snprintf(ssid, sizeof(ssid), "net-%02d", i);
        std::snprintf(bssid, sizeof(bssid), "02:00:00:00:00:%02x", i);
        first.push_back(ap(ssid, bssid, -90 + i));
    }
    CHECK(s.complete_scan(request, first.data(), first.size(), 1001));
    request = s.due_scan(6000);
    std::vector<observation> second;
    for (int i = 16; i < 32; ++i) {
        char ssid[16], bssid[18];
        std::snprintf(ssid, sizeof(ssid), "net-%02d", i);
        std::snprintf(bssid, sizeof(bssid), "02:00:00:00:00:%02x", i);
        second.push_back(ap(ssid, bssid, i == 31 ? -20 : -70));
    }
    CHECK(s.complete_scan(request, second.data(), second.size(), 6001));
    CHECK(s.size() == max_networks);
    request = s.due_scan(11000);
    observation overflow[] = {ap("strong", "02:00:00:00:01:00", -10)};
    CHECK(s.complete_scan(request, overflow, 1, 11001));
    CHECK(s.size() == max_networks);
    CHECK(std::strcmp(s.at(0)->latest.ssid, "strong") == 0);
    request = s.due_scan(16000);
    observation rejected[] = {
        ap("weaker", "ff:00:00:00:00:00", -120),
        ap("equal-worse-tie", "ff:ff:ff:ff:ff:ff", s.at(s.size() - 1)->latest.rssi),
    };
    CHECK(s.complete_scan(request, rejected, 2, 16001));
    CHECK(s.size() == max_networks);
    CHECK(std::strcmp(s.at(0)->latest.ssid, "strong") == 0);
    CHECK(std::strcmp(s.at(s.size() - 1)->latest.bssid, "02:00:00:00:00:01") == 0);
}

void test_trends_cancel_stale_expiry_and_session_ephemerality()
{
    session s; start(s, 30);
    auto req = s.due_scan(1000);
    auto observation1 = ap("x", "aa:00:00:00:00:01", -80);
    s.fail_scan({req.session_token + 1, req.request_token});
    CHECK(s.due_scan(1000).request_token == 0);
    s.fail_scan({req.session_token, req.request_token + 1});
    CHECK(s.due_scan(1000).request_token == 0);
    CHECK(s.scan_count() == 1);
    CHECK(s.complete_scan(req, &observation1, 1, 1001));
    req = s.due_scan(6000);
    auto observation2 = ap("x", "aa:00:00:00:00:01", -40);
    CHECK(s.complete_scan(req, &observation2, 1, 6001));
    CHECK(s.at(0)->sample_count == 2);
    CHECK(s.at(0)->minimum_rssi == -80 && s.at(0)->maximum_rssi == -40);
    CHECK(s.at(0)->rssi_sum / s.at(0)->sample_count == -60);
    req = s.due_scan(11000);
    CHECK(s.cancel());
    CHECK(!s.complete_scan(req, &observation1, 1, 11001));
    CHECK(!s.active() && s.size() == 1);
    CHECK(s.start(10, 12000));
    CHECK(s.size() == 0 && s.scan_count() == 0);
    auto fresh = s.due_scan(12000);
    CHECK(!s.complete_scan(req, &observation1, 1, 12001));
    CHECK(s.complete_scan(fresh, &observation1, 1, 12001));
    auto final = s.due_scan(17000);
    CHECK(!s.complete_scan(final, &observation2, 1, 22000));
    CHECK(!s.active() && s.size() == 1);
}

void test_export_version_bounds_masking_and_sanitization()
{
    session s; start(s, 30);
    auto req = s.due_scan(1000);
    auto item = ap("Cafe|North", "AA:BB:CC:DD:EE:FF", -55);
    CHECK(s.complete_scan(req, &item, 1, 1001));
    const std::string masked = s.export_text(true);
    const std::string clear = s.export_text(false);
    CHECK(masked.find("version=1\nformat=cyberdeck-wifi-survey\n") == 0);
    CHECK(masked.find("**********") != std::string::npos);
    CHECK(masked.find("aa:bb:cc:dd:ee:ff") == std::string::npos);
    CHECK(masked.find("Cafe?North") == std::string::npos);
    CHECK(clear.find("Cafe?North") != std::string::npos);
    CHECK(clear.find("aa:bb:cc:dd:ee:ff") != std::string::npos);
    CHECK(masked.size() <= 8192 && clear.size() <= 8192);
    CHECK(s.export_text(true).empty() == false);
    s.clear();
    CHECK(s.export_text(true).find("version=1\n") == 0);
    CHECK(s.size() == 0 && s.scan_count() == 0);
}

void test_equal_signal_ordering_and_null_batch()
{
    session s; start(s, 10);
    auto request = s.due_scan(1000);
    observation tied[] = {
        ap("z", "aa:00:00:00:00:02", -50, 6),
        ap("a", "aa:00:00:00:00:01", -50, 6),
        ap("same", "", -50, 11),
        ap("same", "", -50, 6),
    };
    CHECK(s.complete_scan(request, tied, 4, 1001));
    CHECK(s.size() == 4);
    CHECK(s.at(0)->latest.channel == 6);
    CHECK(s.at(1)->latest.channel == 11);
    CHECK(std::strcmp(s.at(2)->latest.bssid, "aa:00:00:00:00:01") == 0);
    CHECK(std::strcmp(s.at(3)->latest.bssid, "aa:00:00:00:00:02") == 0);

    request = s.due_scan(6000);
    CHECK(s.complete_scan(request, nullptr, 17, 6001));
    CHECK(s.size() == 4);
    CHECK(s.at(0)->sample_count == 1);
}
}

int main()
{
    test_duration_and_single_session_boundaries();
    test_scan_schedule_sequential_interval_and_max_count();
    test_results_hidden_unknown_duplicates_and_ordering();
    test_security_catalog_and_empty_bssid_identity();
    test_mask_identifier_unknown_and_input_sanitization();
    test_empty_results_input_cap_and_session_render_states();
    test_network_cap_replacement_and_duplicate_count();
    test_trends_cancel_stale_expiry_and_session_ephemerality();
    test_export_version_bounds_masking_and_sanitization();
    test_equal_signal_ordering_and_null_batch();
    std::printf("wifi survey: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
