/*
 * RED host contract for the pure screen-protection policy/state seam.
 *
 * The production source is intentionally absent in this tester handoff.  This
 * binary fixes deterministic command parsing, default/range behavior, NVS
 * snapshot semantics, and the on/off inactivity state machine without ESP-IDF,
 * LVGL, NVS, BSP, simulator, or hardware.
 */
#include "cyberdeck_screen_protection.h"

#include <cstdio>
#include <limits>

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
    if ((actual) != (expected)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s (actual=%ld, expected=%ld)\n", \
                    __FILE__, __LINE__, #actual, \
                    static_cast<long>(actual), static_cast<long>(expected)); \
    } \
} while (0)

using cyberdeck_screen_protection::persisted_timeout;
using cyberdeck_screen_protection::state;
using cyberdeck_screen_protection::timeout_parse_result;

void expect_parse(const char *text,
                  timeout_parse_result expected_result,
                  std::uint16_t expected_value)
{
    constexpr std::uint16_t k_unchanged = 777;
    std::uint16_t value = k_unchanged;
    const timeout_parse_result result =
        cyberdeck_screen_protection::parse_timeout_minutes(text, value);
    CHECK(result == expected_result);
    if (expected_result == timeout_parse_result::ok) {
        CHECK_EQ(value, expected_value);
    } else {
        CHECK_EQ(value, k_unchanged);
    }
}

void test_approved_constants_and_default_state()
{
    CHECK_EQ(cyberdeck_screen_protection::default_timeout_minutes, 2);
    CHECK_EQ(cyberdeck_screen_protection::max_timeout_minutes, 1440);
    CHECK_EQ(cyberdeck_screen_protection::milliseconds_per_minute, 60000);

    state value;
    CHECK(value.screen_on());
    CHECK(value.timeout_enabled());
    CHECK_EQ(value.effective_timeout_minutes(), 2);
    CHECK_EQ(value.last_positive_timeout_minutes(), 2);
    CHECK_EQ(value.timeout_ms(), 120000);

    const persisted_timeout snapshot = value.persistence_view();
    CHECK_EQ(snapshot.effective_minutes, 2);
    CHECK_EQ(snapshot.last_positive_minutes, 2);
}

void test_timeout_parser_accepts_only_decimal_zero_through_1440()
{
    expect_parse("0", timeout_parse_result::ok, 0);
    expect_parse("1", timeout_parse_result::ok, 1);
    expect_parse("2", timeout_parse_result::ok, 2);
    expect_parse("120", timeout_parse_result::ok, 120);
    expect_parse("1440", timeout_parse_result::ok, 1440);
    expect_parse("  0\t", timeout_parse_result::ok, 0);
    expect_parse("\t1440\r\n", timeout_parse_result::ok, 1440);

    expect_parse(nullptr, timeout_parse_result::missing, 0);
    expect_parse("", timeout_parse_result::missing, 0);
    expect_parse(" \t\r\n", timeout_parse_result::missing, 0);

    expect_parse("-0", timeout_parse_result::invalid, 0);
    expect_parse("-1", timeout_parse_result::invalid, 0);
    expect_parse("+1", timeout_parse_result::invalid, 0);
    expect_parse("1.0", timeout_parse_result::invalid, 0);
    expect_parse("1x", timeout_parse_result::invalid, 0);
    expect_parse("0x10", timeout_parse_result::invalid, 0);
    expect_parse("1 2", timeout_parse_result::invalid, 0);
    expect_parse("7 8", timeout_parse_result::invalid, 0);

    expect_parse("1441", timeout_parse_result::out_of_range, 0);
    expect_parse("99999", timeout_parse_result::out_of_range, 0);
    expect_parse("999999999999999999999999999999999999",
                 timeout_parse_result::out_of_range, 0);

    std::uint16_t value = 321;
    CHECK(cyberdeck_screen_protection::parse_timeout_minutes(
              nullptr, value) == timeout_parse_result::missing);
    CHECK_EQ(value, 321);
    CHECK(cyberdeck_screen_protection::parse_timeout_minutes(
              "1441", value) == timeout_parse_result::out_of_range);
    CHECK_EQ(value, 321);
}

void test_zero_disables_without_losing_last_positive_timeout()
{
    state value;
    CHECK(value.set_timeout_minutes(17));
    CHECK(value.timeout_enabled());
    CHECK_EQ(value.effective_timeout_minutes(), 17);
    CHECK_EQ(value.last_positive_timeout_minutes(), 17);
    CHECK_EQ(value.timeout_ms(), 17 * 60000U);

    CHECK(value.set_timeout_minutes(0));
    CHECK(!value.timeout_enabled());
    CHECK_EQ(value.effective_timeout_minutes(), 0);
    CHECK_EQ(value.last_positive_timeout_minutes(), 17);
    CHECK_EQ(value.timeout_ms(), 0);
    CHECK(value.screen_on());

    const persisted_timeout disabled = value.persistence_view();
    CHECK_EQ(disabled.effective_minutes, 0);
    CHECK_EQ(disabled.last_positive_minutes, 17);

    CHECK(value.set_timeout_minutes(3));
    CHECK(value.timeout_enabled());
    CHECK_EQ(value.effective_timeout_minutes(), 3);
    CHECK_EQ(value.last_positive_timeout_minutes(), 3);
    CHECK_EQ(value.timeout_ms(), 180000);
}

void test_positive_boundaries_and_invalid_values_are_atomic()
{
    state minimum;
    CHECK(minimum.set_timeout_minutes(1));
    CHECK_EQ(minimum.effective_timeout_minutes(), 1);
    CHECK_EQ(minimum.last_positive_timeout_minutes(), 1);
    CHECK_EQ(minimum.timeout_ms(), 60000);

    state maximum;
    CHECK(maximum.set_timeout_minutes(1440));
    CHECK_EQ(maximum.effective_timeout_minutes(), 1440);
    CHECK_EQ(maximum.last_positive_timeout_minutes(), 1440);
    CHECK_EQ(maximum.timeout_ms(), 86400000U);

    const persisted_timeout before = maximum.persistence_view();
    CHECK(!maximum.set_timeout_minutes(1441));
    CHECK_EQ(maximum.effective_timeout_minutes(), before.effective_minutes);
    CHECK_EQ(maximum.last_positive_timeout_minutes(), before.last_positive_minutes);
    CHECK_EQ(maximum.timeout_ms(), 86400000U);

    maximum.set_timeout_minutes(0);
    const persisted_timeout after_disable = maximum.persistence_view();
    CHECK(!maximum.set_timeout_minutes(1441));
    CHECK_EQ(maximum.effective_timeout_minutes(), after_disable.effective_minutes);
    CHECK_EQ(maximum.last_positive_timeout_minutes(), after_disable.last_positive_minutes);
}

void test_restore_validates_persisted_effective_and_last_positive_values()
{
    state value;
    CHECK(value.restore({0, 7}));
    CHECK(!value.timeout_enabled());
    CHECK_EQ(value.effective_timeout_minutes(), 0);
    CHECK_EQ(value.last_positive_timeout_minutes(), 7);
    CHECK_EQ(value.timeout_ms(), 0);

    CHECK(value.restore({1440, 1440}));
    CHECK(value.timeout_enabled());
    CHECK_EQ(value.effective_timeout_minutes(), 1440);
    CHECK_EQ(value.last_positive_timeout_minutes(), 1440);
    CHECK_EQ(value.timeout_ms(), 86400000U);

    const persisted_timeout before = value.persistence_view();
    CHECK(!value.restore({0, 0}));
    CHECK(!value.restore({0, 1441}));
    CHECK(!value.restore({1441, 1441}));
    CHECK(!value.restore({5, 6}));
    CHECK(!value.restore({2, 0}));
    CHECK_EQ(value.effective_timeout_minutes(), before.effective_minutes);
    CHECK_EQ(value.last_positive_timeout_minutes(), before.last_positive_minutes);

    state restored;
    CHECK(restored.restore(value.persistence_view()));
    CHECK_EQ(restored.effective_timeout_minutes(), before.effective_minutes);
    CHECK_EQ(restored.last_positive_timeout_minutes(), before.last_positive_minutes);
    CHECK_EQ(restored.timeout_ms(), before.effective_minutes * 60000U);
}

void test_manual_power_commands_do_not_mutate_timeout_policy()
{
    state value;
    CHECK(value.set_timeout_minutes(9));
    value.turn_off();
    CHECK(!value.screen_on());
    CHECK(value.timeout_enabled());
    CHECK_EQ(value.effective_timeout_minutes(), 9);
    CHECK_EQ(value.last_positive_timeout_minutes(), 9);

    value.turn_on();
    CHECK(value.screen_on());
    CHECK(value.timeout_enabled());
    CHECK_EQ(value.effective_timeout_minutes(), 9);

    CHECK(value.set_timeout_minutes(0));
    value.turn_off();
    value.turn_on();
    CHECK(value.screen_on());
    CHECK(!value.timeout_enabled());
    CHECK_EQ(value.last_positive_timeout_minutes(), 9);

    value.turn_off();
    CHECK(value.set_timeout_minutes(11));
    CHECK(!value.screen_on());
    CHECK_EQ(value.last_positive_timeout_minutes(), 11);
}

void test_timer_uses_exact_threshold_and_only_reports_real_transitions()
{
    state value;
    CHECK(!value.evaluate_inactivity(119999));
    CHECK(value.screen_on());
    CHECK(value.evaluate_inactivity(120000));
    CHECK(!value.screen_on());
    CHECK(!value.evaluate_inactivity(120000));
    CHECK(!value.evaluate_inactivity(std::numeric_limits<std::uint32_t>::max()));

    value.turn_on();
    CHECK(value.screen_on());
    CHECK(!value.evaluate_inactivity(0));
    CHECK(value.screen_on());

    CHECK(value.set_timeout_minutes(1));
    CHECK(!value.evaluate_inactivity(59999));
    CHECK(value.evaluate_inactivity(60000));
    CHECK(!value.screen_on());

    value.turn_on();
    CHECK(value.set_timeout_minutes(1440));
    CHECK(!value.evaluate_inactivity(86399999));
    CHECK(value.screen_on());
    CHECK(value.evaluate_inactivity(86400000));
    CHECK(!value.screen_on());
}

void test_disabled_timer_never_turns_screen_off()
{
    state value;
    value.turn_off();
    value.turn_on();
    CHECK(value.set_timeout_minutes(0));
    CHECK(!value.evaluate_inactivity(120000));
    CHECK(!value.evaluate_inactivity(std::numeric_limits<std::uint32_t>::max()));
    CHECK(value.screen_on());

    const persisted_timeout snapshot = value.persistence_view();
    CHECK_EQ(snapshot.effective_minutes, 0);
    CHECK_EQ(snapshot.last_positive_minutes, 2);
}

} // namespace

int main()
{
    test_approved_constants_and_default_state();
    test_timeout_parser_accepts_only_decimal_zero_through_1440();
    test_zero_disables_without_losing_last_positive_timeout();
    test_positive_boundaries_and_invalid_values_are_atomic();
    test_restore_validates_persisted_effective_and_last_positive_values();
    test_manual_power_commands_do_not_mutate_timeout_policy();
    test_timer_uses_exact_threshold_and_only_reports_real_transitions();
    test_disabled_timer_never_turns_screen_off();

    std::printf("screen protection pure contract: %d checks, %d failures\n",
                checks, failures);
    return failures == 0 ? 0 : 1;
}
