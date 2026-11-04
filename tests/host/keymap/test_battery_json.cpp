/*
 * Host test for the pure GET /battery JSON formatter.
 *
 * The formatter is the only part of the endpoint that can be linked without
 * ESP-IDF: `screenshot_server.cpp` needs the HTTP server, the display port and
 * the battery adapter.  This target therefore exercises the real production
 * formatter (and the real policy naming helpers it reuses) and leaves the
 * route, guards and mutex to test_battery_http_contract.py.
 *
 * Coverage:
 *   TEST-BAT-JSON-01 - fixed field order and exact document for a known sample.
 *   TEST-BAT-JSON-02 - unknown values are null, never zero, and a failed
 *       snapshot renders state/charge "unavailable".
 *   TEST-BAT-JSON-03 - a valid snapshot without an INA reading keeps the
 *       protection/charger facts but nulls the telemetry.
 *   TEST-BAT-JSON-04 - no truncation: capacity and kMaxJsonBytes boundaries
 *       fail closed with an empty buffer.
 *   TEST-BAT-JSON-05 - schema is 1 and the state/charge tokens match the
 *       canonical policy helpers for every enum value.
 *   TEST-BAT-JSON-06 - negative values and int64 timestamps are not truncated.
 *   TEST-BAT-JSON-07 - the firmware version is escaped (fail closed when the
 *       escaped field does not fit).
 *   TEST-BAT-JSON-08 - sampled_age_ms is derived only from a valid interval.
 */
#include "platform/sensors/cyberdeck_battery_json.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int failures = 0;
int checks = 0;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    } \
} while (false)

#define CHECK_EQ(actual, expected) do { \
    ++checks; \
    if ((actual) != (expected)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s (actual=%lld, expected=%lld)\n", \
                    __FILE__, __LINE__, #actual, \
                    static_cast<long long>(actual), \
                    static_cast<long long>(expected)); \
    } \
} while (false)

using cyberdeck_battery_protection::battery_state;
using cyberdeck_battery_protection::charge_signal;
using cyberdeck_battery_protection::snapshot;
namespace json = cyberdeck_battery_json;

constexpr std::size_t kCapacity = json::kMaxJsonBytes + 1;

struct render {
    char buffer[kCapacity] = {};
    int length = 0;
};

render format(const json::view &value)
{
    render out;
    out.length = json::format_json(out.buffer, sizeof(out.buffer), value);
    return out;
}

json::view sample_view()
{
    json::view value{};
    value.snapshot_valid = true;
    value.policy.state = battery_state::charging;
    value.policy.charge = charge_signal::not_charging;
    value.policy.available = true;
    value.policy.percentage = 93;
    value.policy.bus_voltage_mv = 8191;
    value.policy.current_ma = -251;
    value.policy.protection_enabled = true;
    value.policy.protection_active = false;
    value.policy.charger_enabled = true;
    value.sampled_at_us = 1000000;
    value.now_us = 1250000;
    value.fw_version = "1.4.0";
    value.uptime_s = 42;
    return value;
}

bool contains_in_order(const char *document, const char *const *keys, std::size_t count)
{
    std::size_t cursor = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const char *found = std::strstr(document + cursor, keys[index]);
        if (found == nullptr) return false;
        cursor = static_cast<std::size_t>(found - document) + std::strlen(keys[index]);
    }
    return true;
}

/* TEST-BAT-JSON-01 */
void test_fixed_document_and_field_order()
{
    const render out = format(sample_view());
    const char *expected =
        "{\"schema\":1,\"state\":\"charging\",\"charge\":\"not_charging\",\"available\":true,"
        "\"bus_voltage_mv\":8191,\"current_ma\":-251,\"percentage\":93,"
        "\"protection_enabled\":true,\"protection_active\":false,\"charger_enabled\":true,"
        "\"sampled_at_us\":1000000,\"sampled_age_ms\":250,\"fw_version\":\"1.4.0\","
        "\"uptime_s\":42}";
    CHECK(out.length > 0);
    CHECK_EQ(out.length, static_cast<int>(std::strlen(expected)));
    CHECK(std::strcmp(out.buffer, expected) == 0);

    const char *const keys[] = {
        "\"schema\"", "\"state\"", "\"charge\"", "\"available\"",
        "\"bus_voltage_mv\"", "\"current_ma\"", "\"percentage\"",
        "\"protection_enabled\"", "\"protection_active\"", "\"charger_enabled\"",
        "\"sampled_at_us\"", "\"sampled_age_ms\"", "\"fw_version\"", "\"uptime_s\"",
    };
    CHECK(contains_in_order(out.buffer, keys, sizeof(keys) / sizeof(keys[0])));
    /* No field may be emitted twice. */
    CHECK(std::strstr(out.buffer, "\"state\"") != nullptr);
    CHECK(std::strstr(std::strstr(out.buffer, "\"state\"") + 1, "\"state\"") == nullptr);
}

/* TEST-BAT-JSON-02 */
void test_unknown_values_are_null()
{
    json::view value{};
    const render out = format(value);
    const char *expected =
        "{\"schema\":1,\"state\":\"unavailable\",\"charge\":\"unavailable\",\"available\":false,"
        "\"bus_voltage_mv\":null,\"current_ma\":null,\"percentage\":null,"
        "\"protection_enabled\":null,\"protection_active\":null,\"charger_enabled\":null,"
        "\"sampled_at_us\":null,\"sampled_age_ms\":null,\"fw_version\":null,"
        "\"uptime_s\":0}";
    CHECK(out.length > 0);
    CHECK(std::strcmp(out.buffer, expected) == 0);
    CHECK(std::strstr(out.buffer, ":0,") == nullptr);
}

/* TEST-BAT-JSON-03 */
void test_valid_snapshot_without_telemetry()
{
    json::view value{};
    value.snapshot_valid = true;
    value.policy.state = battery_state::battery;
    value.policy.charge = charge_signal::unknown;
    value.policy.available = false;
    value.policy.percentage = 77;
    value.policy.bus_voltage_mv = 7000;
    value.policy.current_ma = 120;
    value.policy.protection_enabled = false;
    value.policy.protection_active = false;
    value.policy.charger_enabled = false;
    value.sampled_at_us = 500;
    value.now_us = 500;
    value.fw_version = "1.4.0";
    value.uptime_s = 3;

    const render out = format(value);
    CHECK(out.length > 0);
    CHECK(std::strstr(out.buffer, "\"state\":\"battery\"") != nullptr);
    CHECK(std::strstr(out.buffer, "\"charge\":\"unknown\"") != nullptr);
    CHECK(std::strstr(out.buffer, "\"available\":false") != nullptr);
    CHECK(std::strstr(out.buffer, "\"bus_voltage_mv\":null") != nullptr);
    CHECK(std::strstr(out.buffer, "\"current_ma\":null") != nullptr);
    CHECK(std::strstr(out.buffer, "\"percentage\":null") != nullptr);
    CHECK(std::strstr(out.buffer, "\"protection_enabled\":false") != nullptr);
    CHECK(std::strstr(out.buffer, "\"charger_enabled\":false") != nullptr);
    CHECK(std::strstr(out.buffer, "\"sampled_age_ms\":0") != nullptr);
}

/* TEST-BAT-JSON-04 */
void test_buffer_and_size_limits_fail_closed()
{
    json::view value = sample_view();
    char buffer[kCapacity] = {};
    const int length = json::format_json(buffer, sizeof(buffer), value);
    CHECK(length > 0);

    /* Exact fit, including the NUL terminator, still works. */
    char exact[kCapacity] = {};
    CHECK_EQ(json::format_json(exact, static_cast<std::size_t>(length) + 1, value), length);

    /* One byte short must not truncate the document. */
    char short_buffer[kCapacity] = {};
    std::memset(short_buffer, 'x', sizeof(short_buffer));
    short_buffer[sizeof(short_buffer) - 1] = '\0';
    CHECK_EQ(json::format_json(short_buffer, static_cast<std::size_t>(length), value), -1);
    CHECK(short_buffer[0] == '\0');

    /* Invalid arguments never write a document. */
    char tiny[2] = {'x', '\0'};
    CHECK_EQ(json::format_json(tiny, sizeof(tiny), value), -1);
    CHECK_EQ(json::format_json(nullptr, kCapacity, value), -1);
    CHECK_EQ(json::format_json(tiny, 0, value), -1);

    /* A field that would push the body past the documented limit fails closed
     * instead of emitting a truncated (invalid) document. */
    json::view base = sample_view();
    base.fw_version = "";
    const render base_render = format(base);
    CHECK(base_render.length > 0);
    const std::size_t expected_accept =
        json::kMaxJsonBytes - static_cast<std::size_t>(base_render.length);

    std::size_t accepted = 0;
    json::view padded = sample_view();
    char version[400] = {};
    for (std::size_t index = 0; index < sizeof(version) - 1; ++index) {
        std::memset(version, 'a', index);
        padded.fw_version = version;
        const render out = format(padded);
        if (out.length <= 0) break;
        CHECK(out.length <= static_cast<int>(json::kMaxJsonBytes));
        accepted = index;
    }
    CHECK_EQ(accepted, expected_accept);
    json::view over = sample_view();
    char too_long[256] = {};
    std::memset(too_long, 'a', sizeof(too_long) - 1);
    over.fw_version = too_long;
    const render rejected = format(over);
    CHECK_EQ(rejected.length, -1);
    CHECK(rejected.buffer[0] == '\0');
}

/* TEST-BAT-JSON-05 */
void test_schema_and_canonical_tokens()
{
    const render out = format(sample_view());
    CHECK(std::strstr(out.buffer, "{\"schema\":1,") == out.buffer);

    const battery_state states[] = {
        battery_state::unknown, battery_state::battery, battery_state::external,
        battery_state::charging, battery_state::absent,
    };
    for (battery_state state : states) {
        json::view value{};
        value.snapshot_valid = true;
        value.policy.state = state;
        value.policy.charge = charge_signal::unknown;
        const render rendered = format(value);
        const std::string token =
            std::string("\"state\":\"") + cyberdeck_battery_protection::state_name(state) + "\"";
        CHECK(rendered.length > 0);
        CHECK(std::strstr(rendered.buffer, token.c_str()) != nullptr);
    }

    const charge_signal signals[] = {
        charge_signal::unknown, charge_signal::not_charging, charge_signal::charging,
    };
    for (charge_signal signal : signals) {
        json::view value{};
        value.snapshot_valid = true;
        value.policy.state = battery_state::battery;
        value.policy.charge = signal;
        const render rendered = format(value);
        const std::string token =
            std::string("\"charge\":\"") + cyberdeck_battery_protection::charge_signal_name(signal) + "\"";
        CHECK(rendered.length > 0);
        CHECK(std::strstr(rendered.buffer, token.c_str()) != nullptr);
    }
}

/* TEST-BAT-JSON-06 */
void test_negative_and_int64_values()
{
    json::view value = sample_view();
    value.policy.current_ma = -3200;
    value.policy.percentage = -1;
    value.sampled_at_us = 9007199254740993LL; /* 2^53 + 1: not representable in a double */
    value.now_us = 9007199255740993LL;
    value.uptime_s = 9223372036854775LL;

    const render out = format(value);
    CHECK(out.length > 0);
    CHECK(std::strstr(out.buffer, "\"current_ma\":-3200") != nullptr);
    CHECK(std::strstr(out.buffer, "\"percentage\":-1") != nullptr);
    CHECK(std::strstr(out.buffer, "\"sampled_at_us\":9007199254740993") != nullptr);
    CHECK(std::strstr(out.buffer, "\"sampled_age_ms\":1000") != nullptr);
    CHECK(std::strstr(out.buffer, "\"uptime_s\":9223372036854775") != nullptr);
}

/* TEST-BAT-JSON-07 */
void test_firmware_version_is_escaped()
{
    json::view value = sample_view();
    value.fw_version = "1.4.0-\"quoted\"\\path";
    const render out = format(value);
    CHECK(out.length > 0);
    CHECK(std::strstr(out.buffer, "\"fw_version\":\"1.4.0-\\\"quoted\\\"\\\\path\"") != nullptr);

    value.fw_version = "1.4.0-\x01";
    const render control = format(value);
    CHECK(control.length > 0);
    CHECK(std::strstr(control.buffer, "\"fw_version\":\"1.4.0-\\u0001\"") != nullptr);

    value.fw_version = "";
    const render empty = format(value);
    CHECK(empty.length > 0);
    CHECK(std::strstr(empty.buffer, "\"fw_version\":\"\"") != nullptr);
}

/* TEST-BAT-JSON-08 */
void test_sampled_age_requires_a_valid_interval()
{
    json::view value = sample_view();

    value.sampled_at_us = 0;
    value.now_us = 1000000;
    CHECK(std::strstr(format(value).buffer, "\"sampled_age_ms\":null") != nullptr);
    CHECK(std::strstr(format(value).buffer, "\"sampled_at_us\":null") != nullptr);

    value.sampled_at_us = 2000000;
    value.now_us = 1000000; /* clock went backwards: unknown, never negative */
    const render backwards = format(value);
    CHECK(std::strstr(backwards.buffer, "\"sampled_age_ms\":null") != nullptr);
    CHECK(std::strstr(backwards.buffer, "\"sampled_at_us\":2000000") != nullptr);

    value.sampled_at_us = 999999;
    value.now_us = 1000000; /* sub-millisecond interval truncates to zero */
    CHECK(std::strstr(format(value).buffer, "\"sampled_age_ms\":0") != nullptr);
}

void test_declared_limit()
{
    CHECK_EQ(json::kMaxJsonBytes, 384);
    /* sizeof(view) stays small enough for a handler stack copy. */
    CHECK(sizeof(json::view) < 128);
}

} // namespace

int main()
{
    test_fixed_document_and_field_order();
    test_unknown_values_are_null();
    test_valid_snapshot_without_telemetry();
    test_buffer_and_size_limits_fail_closed();
    test_schema_and_canonical_tokens();
    test_negative_and_int64_values();
    test_firmware_version_is_escaped();
    test_sampled_age_requires_a_valid_interval();
    test_declared_limit();

    std::printf("battery json pure contract: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
