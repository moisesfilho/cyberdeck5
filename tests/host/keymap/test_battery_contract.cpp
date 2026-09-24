/*
 * RED host contract for the pure battery percentage/classification seam.
 *
 * The production module is intentionally absent in this handoff.  The test
 * includes the test-only contract so the exact C++ ABI and semantics are
 * fixed before the coder creates the production header/source.
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

void test_approved_capacity_window()
{
    CHECK_EQ(cyberdeck_battery::percentage_from_capacity_mah(6000), 0);
    CHECK_EQ(cyberdeck_battery::percentage_from_capacity_mah(8400), 100);
    CHECK_EQ(cyberdeck_battery::percentage_from_capacity_mah(7200), 50);
}

void test_percentage_is_saturated_at_both_limits()
{
    CHECK_EQ(cyberdeck_battery::percentage_from_capacity_mah(5999), 0);
    CHECK_EQ(cyberdeck_battery::percentage_from_capacity_mah(0), 0);
    CHECK_EQ(cyberdeck_battery::percentage_from_capacity_mah(-1), 0);
    CHECK_EQ(cyberdeck_battery::percentage_from_capacity_mah(8401), 100);
    CHECK_EQ(cyberdeck_battery::percentage_from_capacity_mah(
                 std::numeric_limits<std::int32_t>::max()), 100);
}

void test_current_sign_classification()
{
    CHECK(cyberdeck_battery::classify_current_ma(1) ==
          cyberdeck_battery::charge_class::consuming);
    CHECK(cyberdeck_battery::classify_current_ma(250) ==
          cyberdeck_battery::charge_class::consuming);
    CHECK(cyberdeck_battery::classify_current_ma(-1) ==
          cyberdeck_battery::charge_class::charging);
    CHECK(cyberdeck_battery::classify_current_ma(-250) ==
          cyberdeck_battery::charge_class::charging);
    CHECK(cyberdeck_battery::classify_current_ma(0) ==
          cyberdeck_battery::charge_class::consuming);
}

void test_absence_and_read_failure_are_unavailable()
{
    const cyberdeck_battery::sample absent{false, true, 7200, 1};
    const cyberdeck_battery::sample read_failure{true, false, 7200, 1};
    const cyberdeck_battery::sample both_unavailable{false, false, 7200, -1};

    for (const auto &input : {absent, read_failure, both_unavailable}) {
        const auto result = cyberdeck_battery::classify_sample(input);
        CHECK(!result.available);
        CHECK(result.charge == cyberdeck_battery::charge_class::unavailable);
    }
}

void test_available_snapshot_combines_percentage_and_direction()
{
    const auto consuming = cyberdeck_battery::classify_sample({true, true, 7200, 1});
    CHECK(consuming.available);
    CHECK_EQ(consuming.percentage, 50);
    CHECK(consuming.charge == cyberdeck_battery::charge_class::consuming);

    const auto charging = cyberdeck_battery::classify_sample({true, true, 8400, -1});
    CHECK(charging.available);
    CHECK_EQ(charging.percentage, 100);
    CHECK(charging.charge == cyberdeck_battery::charge_class::charging);
}

} // namespace

int main()
{
    test_approved_capacity_window();
    test_percentage_is_saturated_at_both_limits();
    test_current_sign_classification();
    test_absence_and_read_failure_are_unavailable();
    test_available_snapshot_combines_percentage_and_direction();

    std::printf("battery pure contract: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
