/*
 * RED host contract for the pure battery voltage/state seam.
 *
 * REQ-BAT-001 -> percentage_from_bus_voltage_mv, explicit 6000..8230 mV
 *                  window, and saturation.
 * REQ-BAT-002 -> absent/unavailable/discharging/charging/neutral states;
 *                  zero or indeterminate current is neutral, never absent.
 * REQ-BAT-003 -> the UI consumes this snapshot without a level glyph.
 * REQ-BAT-004 -> reader failures are non-fatal and have no charger-control
 *                  seam (covered by the structural companion).
 * REQ-BAT-005 -> the INA226 task/mutex/1s seam is checked by the reader
 *                  companion; this test remains host-only and hardware-free.
 */
#include "cyberdeck_battery.h"

#include <cstdio>
#include <initializer_list>
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

void test_approved_bus_voltage_window()
{
    CHECK_EQ(cyberdeck_battery::empty_bus_voltage_mv, 6000);
    CHECK_EQ(cyberdeck_battery::full_bus_voltage_mv, 8230);

    CHECK_EQ(cyberdeck_battery::percentage_from_bus_voltage_mv(6000), 0);
    CHECK_EQ(cyberdeck_battery::percentage_from_bus_voltage_mv(7115), 50);
    CHECK_EQ(cyberdeck_battery::percentage_from_bus_voltage_mv(8230), 100);
}

void test_percentage_is_saturated_at_both_limits()
{
    CHECK_EQ(cyberdeck_battery::percentage_from_bus_voltage_mv(5999), 0);
    CHECK_EQ(cyberdeck_battery::percentage_from_bus_voltage_mv(0), 0);
    CHECK_EQ(cyberdeck_battery::percentage_from_bus_voltage_mv(-1), 0);
    CHECK_EQ(cyberdeck_battery::percentage_from_bus_voltage_mv(8231), 100);
    CHECK_EQ(cyberdeck_battery::percentage_from_bus_voltage_mv(8400), 100);
    CHECK_EQ(cyberdeck_battery::percentage_from_bus_voltage_mv(
                 std::numeric_limits<std::int32_t>::max()), 100);
}

void test_current_sign_classification()
{
    CHECK(cyberdeck_battery::classify_current_ma(1) ==
          cyberdeck_battery::charge_class::discharging);
    CHECK(cyberdeck_battery::classify_current_ma(250) ==
          cyberdeck_battery::charge_class::discharging);
    CHECK(cyberdeck_battery::classify_current_ma(-1) ==
          cyberdeck_battery::charge_class::charging);
    CHECK(cyberdeck_battery::classify_current_ma(-250) ==
          cyberdeck_battery::charge_class::charging);

    /* REQ-BAT-002: a zero/default current is the host seam's encoding for a
     * zero or indeterminate direction.  It is an active neutral sample, not an
     * absent battery and not a charging sample. */
    const auto zero_state = cyberdeck_battery::classify_current_ma(0);
    CHECK(zero_state == cyberdeck_battery::charge_class::neutral);

    const cyberdeck_battery::sample default_current{};
    const auto default_state = cyberdeck_battery::classify_current_ma(
        default_current.current_ma);
    CHECK(default_state == cyberdeck_battery::charge_class::neutral);
}

void test_absence_and_read_failure_are_distinct_unavailable_states()
{
    const auto absent = cyberdeck_battery::classify_sample(
        {false, true, 7115, 1});
    CHECK(!absent.available);
    CHECK(absent.charge == cyberdeck_battery::charge_class::absent);

    const auto read_failure = cyberdeck_battery::classify_sample(
        {true, false, 7115, 1});
    CHECK(!read_failure.available);
    CHECK(read_failure.charge == cyberdeck_battery::charge_class::unavailable);

    const auto both_unavailable = cyberdeck_battery::classify_sample(
        {false, false, 0, -1});
    CHECK(!both_unavailable.available);
    CHECK(both_unavailable.charge == cyberdeck_battery::charge_class::absent);
}

void test_available_snapshot_combines_voltage_percentage_and_direction()
{
    const auto discharging = cyberdeck_battery::classify_sample(
        {true, true, 7115, 1});
    CHECK(discharging.available);
    CHECK_EQ(discharging.percentage, 50);
    CHECK(discharging.charge == cyberdeck_battery::charge_class::discharging);

    const auto charging = cyberdeck_battery::classify_sample(
        {true, true, 8230, -1});
    CHECK(charging.available);
    CHECK_EQ(charging.percentage, 100);
    CHECK(charging.charge == cyberdeck_battery::charge_class::charging);

    const auto neutral = cyberdeck_battery::classify_sample(
        {true, true, 6000, 0});
    CHECK(neutral.available);
    CHECK_EQ(neutral.percentage, 0);
    CHECK(neutral.charge == cyberdeck_battery::charge_class::neutral);
    CHECK(neutral.charge != cyberdeck_battery::charge_class::absent);
    CHECK(neutral.charge != cyberdeck_battery::charge_class::unavailable);
    CHECK(neutral.charge != cyberdeck_battery::charge_class::charging);
}

} // namespace

int main()
{
    test_approved_bus_voltage_window();
    test_percentage_is_saturated_at_both_limits();
    test_current_sign_classification();
    test_absence_and_read_failure_are_distinct_unavailable_states();
    test_available_snapshot_combines_voltage_percentage_and_direction();

    std::printf("battery voltage contract (REQ-BAT-001/002): %d checks, %d failures\n",
                checks, failures);
    return failures == 0 ? 0 : 1;
}
