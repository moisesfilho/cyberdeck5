/*
 * RED host contract for battery state, CHG_STAT polarity, and charger
 * protection.  The production source is intentionally absent at this stage;
 * the test fixes the pure behavior that the coder must implement without
 * touching a simulator, Serial Automation Bridge, I2C, or power rails.
 */
#include "cyberdeck_battery_protection.h"

#include <cstdio>

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
        std::printf("FAIL %s:%d: %s (actual=%ld, expected=%ld)\n", \
                    __FILE__, __LINE__, #actual, \
                    static_cast<long>(actual), static_cast<long>(expected)); \
    } \
} while (false)

using cyberdeck_battery_protection::battery_state;
using cyberdeck_battery_protection::charge_signal;
using cyberdeck_battery_protection::observation;
using cyberdeck_battery_protection::state;

observation sample(bool ina_valid,
                   bool chg_valid,
                   bool raw_chg_stat_low,
                   std::int32_t voltage_mv,
                   std::int32_t current_ma,
                   std::int32_t percentage)
{
    return {ina_valid, chg_valid, raw_chg_stat_low, voltage_mv,
            current_ma, percentage};
}

void feed(state &value, const observation &input, int count)
{
    for (int i = 0; i < count; ++i) {
        (void)value.observe(input);
    }
}

void test_constants_and_active_low_chg_stat()
{
    using namespace cyberdeck_battery_protection;
    CHECK_EQ(current_uncertainty_ma, 15);
    CHECK_EQ(external_voltage_mv, 7900);
    CHECK_EQ(absent_voltage_mv, 8330);
    CHECK_EQ(state_vote_count, 5);
    CHECK_EQ(protection_enter_percentage, 90);
    CHECK_EQ(protection_enter_voltage_mv, 8200);
    CHECK_EQ(protection_exit_percentage, 85);
    CHECK(default_protection_enabled);

    CHECK(decode_chg_stat(true, true) == charge_signal::charging);
    CHECK(decode_chg_stat(true, false) == charge_signal::not_charging);
    CHECK(decode_chg_stat(false, false) == charge_signal::unknown);
    CHECK(decode_chg_stat(false, true) == charge_signal::unknown);
}

void test_default_and_persisted_option()
{
    using namespace cyberdeck_battery_protection;
    state value;
    const auto initial = value.snapshot();
    CHECK(initial.state == battery_state::unknown);
    CHECK(!initial.available);
    CHECK(initial.protection_enabled);
    CHECK(!initial.protection_active);
    CHECK(initial.charger_enabled);
    CHECK(value.protection_enabled());
    CHECK(!value.protection_active());
    CHECK(value.charger_enabled());
    CHECK(value.persistence_view().protection_enabled);

    CHECK(value.set_protection_enabled(false));
    CHECK(!value.protection_enabled());
    CHECK(value.charger_enabled());
    CHECK(!value.persistence_view().protection_enabled);

    CHECK(value.set_protection_enabled(true));
    CHECK(value.protection_enabled());
    CHECK(value.persistence_view().protection_enabled);

    CHECK(value.restore_protection_enabled(false));
    CHECK(!value.protection_enabled());
    CHECK(value.charger_enabled());
    CHECK(value.restore_protection_enabled(true));
    CHECK(value.protection_enabled());
}

void test_state_classification_and_vote_boundaries()
{
    using namespace cyberdeck_battery_protection;

    state value;
    const auto invalid_ina = value.observe(sample(false, true, false, 8000, 0, 50));
    CHECK(invalid_ina.state == battery_state::unknown);
    CHECK(!invalid_ina.available);

    const auto invalid_chg = value.observe(sample(true, false, false, 8000, 0, 50));
    CHECK(invalid_chg.state == battery_state::unknown);
    CHECK(!invalid_chg.available);

    const auto battery = value.observe(sample(true, true, false, 7500, 16, 70));
    CHECK(battery.state == battery_state::battery);
    CHECK(battery.available);
    CHECK_EQ(battery.percentage, 70);
    CHECK_EQ(battery.bus_voltage_mv, 7500);

    const auto charging_from_pin = value.observe(sample(true, true, true, 8000, 0, 50));
    CHECK(charging_from_pin.state == battery_state::charging);
    CHECK(charging_from_pin.available);

    /* Exactly +15 mA is inside the uncertainty band; +16 is discharge. */
    const auto boundary_current = value.observe(sample(true, true, false, 7500, 15, 50));
    CHECK(boundary_current.state == battery_state::battery);
    const auto above_current = value.observe(sample(true, true, false, 7500, 16, 50));
    CHECK(above_current.state == battery_state::battery);
    const auto charge_current = value.observe(sample(true, true, false, 8000, -16, 50));
    CHECK(charge_current.state == battery_state::charging);

    /* External and absent are voltage-derived states and require five
     * consecutive matching votes.  A threshold value is inclusive. */
    state external_value;
    const observation external_sample = sample(true, true, false, 7900, 0, 50);
    feed(external_value, external_sample, 4);
    CHECK(external_value.snapshot().state != battery_state::external);
    const auto external = external_value.observe(external_sample);
    CHECK(external.state == battery_state::external);
    CHECK(external.available);

    state below_external;
    feed(below_external,
         sample(true, true, false, 7899, 0, 50), 8);
    CHECK(below_external.snapshot().state != battery_state::external);
    CHECK(below_external.snapshot().state != battery_state::absent);

    state absent_value;
    const observation absent_sample = sample(true, true, false, 8330, 0, 50);
    feed(absent_value, absent_sample, 4);
    CHECK(absent_value.snapshot().state != battery_state::absent);
    const auto absent = absent_value.observe(absent_sample);
    CHECK(absent.state == battery_state::absent);
    CHECK(absent.available);

    state below_absent;
    feed(below_absent,
         sample(true, true, false, 8329, 0, 50), 8);
    CHECK(below_absent.snapshot().state != battery_state::absent);

    /* A non-matching vote resets the consecutive counter. */
    state reset_votes;
    feed(reset_votes, absent_sample, 4);
    (void)reset_votes.observe(sample(true, true, false, 7800, 0, 50));
    feed(reset_votes, absent_sample, 4);
    CHECK(reset_votes.snapshot().state != battery_state::absent);
    const auto after_fifth = reset_votes.observe(absent_sample);
    CHECK(after_fifth.state == battery_state::absent);
}

void test_protection_requires_all_three_conditions()
{
    using namespace cyberdeck_battery_protection;
    state value;

    const auto below_percentage =
        value.observe(sample(true, true, true, 8300, -100, 89));
    CHECK(!value.protection_active());
    CHECK(value.charger_enabled());
    CHECK(below_percentage.state == battery_state::charging);

    const auto below_voltage =
        value.observe(sample(true, true, true, 8199, -100, 90));
    CHECK(!value.protection_active());
    CHECK(value.charger_enabled());
    CHECK(below_voltage.state == battery_state::charging);

    const auto not_charging =
        value.observe(sample(true, true, false, 8300, 100, 100));
    CHECK(!value.protection_active());
    CHECK(value.charger_enabled());
    CHECK(not_charging.state == battery_state::battery);

    const auto at_thresholds =
        value.observe(sample(true, true, true, 8200, -100, 90));
    CHECK(at_thresholds.state == battery_state::charging);
    CHECK(value.protection_active());
    CHECK(!value.charger_enabled());
}

void test_hysteresis_and_disabled_option_reenable()
{
    using namespace cyberdeck_battery_protection;
    state value;
    (void)value.observe(sample(true, true, true, 8300, -100, 95));
    CHECK(value.protection_active());
    CHECK(!value.charger_enabled());

    const auto hysteresis_band =
        value.observe(sample(true, true, true, 8300, -100, 86));
    CHECK(value.protection_active());
    CHECK(!value.charger_enabled());
    CHECK(hysteresis_band.state == battery_state::charging);

    const auto released =
        value.observe(sample(true, true, true, 8300, -100, 85));
    CHECK(!value.protection_active());
    CHECK(value.charger_enabled());
    CHECK(released.state == battery_state::charging);

    (void)value.observe(sample(true, true, true, 8300, -100, 95));
    CHECK(value.protection_active());
    CHECK(!value.charger_enabled());

    CHECK(value.set_protection_enabled(false));
    CHECK(!value.protection_active());
    CHECK(value.charger_enabled());

    /* Disabled means enabled charger, even while all protection predicates
     * are true; the option must be explicitly turned on again. */
    const auto while_disabled =
        value.observe(sample(true, true, true, 8300, -100, 95));
    CHECK(while_disabled.state == battery_state::charging);
    CHECK(!value.protection_active());
    CHECK(value.charger_enabled());

    CHECK(value.set_protection_enabled(true));
    (void)value.observe(sample(true, true, true, 8300, -100, 95));
    CHECK(value.protection_active());
    CHECK(!value.charger_enabled());
}

void test_invalid_inputs_preserve_last_safe_protection_state()
{
    using namespace cyberdeck_battery_protection;
    state value;
    const auto safe = value.observe(sample(true, true, true, 8300, -100, 95));
    CHECK(value.protection_active());
    CHECK(!value.charger_enabled());

    const auto after_ina_failure =
        value.observe(sample(false, true, true, 0, 0, 0));
    CHECK(after_ina_failure.state == safe.state);
    CHECK(after_ina_failure.available);
    CHECK(value.protection_active());
    CHECK(!value.charger_enabled());
    CHECK(value.last_safe_snapshot().state == safe.state);

    const auto after_chg_failure =
        value.observe(sample(true, false, false, 0, 0, 0));
    CHECK(after_chg_failure.state == safe.state);
    CHECK(after_chg_failure.available);
    CHECK(value.protection_active());
    CHECK(!value.charger_enabled());

    /* A failed read must not manufacture a state transition that could turn
     * CHG_EN off, and it must not erase the last safe voltage/percentage. */
    CHECK_EQ(value.last_safe_snapshot().percentage, safe.percentage);
    CHECK_EQ(value.last_safe_snapshot().bus_voltage_mv, safe.bus_voltage_mv);
}

} // namespace

int main()
{
    test_constants_and_active_low_chg_stat();
    test_default_and_persisted_option();
    test_state_classification_and_vote_boundaries();
    test_protection_requires_all_three_conditions();
    test_hysteresis_and_disabled_option_reenable();
    test_invalid_inputs_preserve_last_safe_protection_state();

    std::printf("battery protection pure contract: %d checks, %d failures\n",
                checks, failures);
    return failures == 0 ? 0 : 1;
}
