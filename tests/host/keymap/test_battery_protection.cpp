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

    /* REQ-BAT-UI-001 / AC-BAT-UI-001: only an INVALID INA read keeps the
     * snapshot unavailable.  A valid INA sample must publish availability even
     * when CHG_STAT could not be read, and the missing pin becomes `unknown`
     * (never `not_charging`).  The resolved state is still `unknown` here
     * because 8000 mV only starts the five-vote external confirmation; an
     * unresolved state and an unavailable measurement are different facts. */
    const auto invalid_chg = value.observe(sample(true, false, false, 8000, 0, 50));
    CHECK(invalid_chg.state == battery_state::unknown);
    CHECK(invalid_chg.available);
    CHECK_EQ(invalid_chg.percentage, 50);
    CHECK_EQ(invalid_chg.bus_voltage_mv, 8000);
    CHECK(invalid_chg.charge == charge_signal::unknown);
    CHECK(invalid_chg.charge != charge_signal::not_charging);

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
    /* An invalid INA read is the only case that preserves the last safe
     * snapshot, and it must not erase the last safe voltage/percentage. */
    CHECK_EQ(value.last_safe_snapshot().percentage, safe.percentage);
    CHECK_EQ(value.last_safe_snapshot().bus_voltage_mv, safe.bus_voltage_mv);

    const auto after_chg_failure =
        value.observe(sample(true, false, false, 0, 0, 0));
    /* REQ-BAT-UI-001: the CHG_STAT failure must not be published as a
     * negative charger answer, and it must not fabricate a charging transition
     * that would then drive CHG_EN off.  The snapshot is re-derived from the
     * still-valid INA sample instead of being frozen. */
    CHECK(after_chg_failure.charge == charge_signal::unknown);
    CHECK(after_chg_failure.charge != charge_signal::not_charging);
    CHECK(after_chg_failure.state == battery_state::battery);
    CHECK(after_chg_failure.available);
    CHECK_EQ(after_chg_failure.percentage, 0);
    CHECK_EQ(after_chg_failure.bus_voltage_mv, 0);
    CHECK(!value.protection_active());
    CHECK(value.charger_enabled());

    /* A further invalid INA read keeps the most recent published values. */
    const auto after_second_ina_failure =
        value.observe(sample(false, false, false, 0, 0, 0));
    CHECK(after_second_ina_failure.state == battery_state::battery);
    CHECK(after_second_ina_failure.available);
    CHECK_EQ(after_second_ina_failure.percentage, 0);
    CHECK_EQ(after_second_ina_failure.bus_voltage_mv, 0);
    CHECK_EQ(value.last_safe_snapshot().percentage, 0);
    CHECK_EQ(value.last_safe_snapshot().bus_voltage_mv, 0);
    CHECK(value.charger_enabled());
}

/* REQ-BAT-UI-001 / AC-BAT-UI-001: a valid INA226 read must keep updating the
 * snapshot when CHG_STAT cannot be read, so a transient expander error can no
 * longer freeze or hide the header indicator. */
void test_valid_ina_with_invalid_chg_stat_does_not_freeze_snapshot()
{
    state value;

    const auto baseline = value.observe(sample(true, true, true, 8000, -40, 55));
    CHECK(baseline.available);
    CHECK(baseline.state == battery_state::charging);
    CHECK(baseline.charge == charge_signal::charging);
    CHECK_EQ(baseline.percentage, 55);

    /* CHG_STAT unreadable, INA226 valid: voltage, current, percentage,
     * availability and the derived state all advance. */
    const auto after = value.observe(sample(true, false, false, 7400, 120, 62));
    CHECK(after.available);
    CHECK(after.state == battery_state::battery);
    CHECK_EQ(after.percentage, 62);
    CHECK_EQ(after.bus_voltage_mv, 7400);
    CHECK_EQ(after.current_ma, 120);
    CHECK(after.charge == charge_signal::unknown);
    CHECK(after.charge != charge_signal::not_charging);
    CHECK(after.percentage != baseline.percentage);
    CHECK(after.bus_voltage_mv != baseline.bus_voltage_mv);
    CHECK_EQ(value.last_safe_snapshot().percentage, 62);
    CHECK_EQ(value.last_safe_snapshot().bus_voltage_mv, 7400);
    CHECK_EQ(value.last_safe_snapshot().current_ma, 120);

    /* The same holds when the raw level happens to be stuck low: a failed read
     * is `unknown` regardless of the electrical level that was attempted. */
    const auto stuck_low_failed =
        value.observe(sample(true, false, true, 7600, 0, 70));
    CHECK(stuck_low_failed.charge == charge_signal::unknown);
    CHECK(stuck_low_failed.charge != charge_signal::charging);
    CHECK(stuck_low_failed.available);
    CHECK_EQ(stuck_low_failed.percentage, 70);
    CHECK(stuck_low_failed.state == battery_state::battery);

    /* An unreadable CHG_STAT must not disable protection: the policy still
     * classifies from the current and the full condition set is re-evaluated
     * from the valid measurement. */
    state protectable;
    const auto charged =
        protectable.observe(sample(true, false, false, 8300, -100, 95));
    CHECK(charged.state == battery_state::charging);
    CHECK(charged.charge == charge_signal::unknown);
    CHECK_EQ(charged.percentage, 95);
    CHECK_EQ(charged.bus_voltage_mv, 8300);
    CHECK(protectable.protection_active());
    CHECK(!protectable.charger_enabled());

    /* Recovery: once CHG_STAT is readable again the signal is published. */
    const auto recovered = value.observe(sample(true, true, true, 8000, -30, 66));
    CHECK(recovered.charge == charge_signal::charging);
    CHECK(recovered.state == battery_state::charging);
    CHECK_EQ(recovered.percentage, 66);
}

/* REQ-BAT-UI-002 / AC-BAT-UI-003 (regression): the absence/presence
 * precedence is resolved before the charger signal, so a CHG_STAT stuck low
 * cannot fabricate a charging battery that is not in the bay.  The approved
 * states, thresholds and five consecutive votes are preserved. */
void test_absent_precedence_over_stuck_low_chg_stat()
{
    state value;
    const observation stuck_low_charging = sample(true, true, true, 8400, 0, 50);

    /* Boundary of the guarantee: below absent_voltage_mv there is no absence
     * evidence, so a stuck-low pin is still honored as charging.  REQ-BAT-UI-002
     * only requires the absence precedence, not a rejection of a readable
     * charger answer. */
    feed(value, sample(true, true, true, 7800, 0, 50), 8);
    CHECK(value.snapshot().state == battery_state::charging);
    CHECK(value.snapshot().charge == charge_signal::charging);
    CHECK(value.snapshot().state != battery_state::absent);

    /* Fewer than the approved votes must not publish the absence. */
    state absent_value;
    feed(absent_value, stuck_low_charging, 4);
    CHECK(absent_value.snapshot().state != battery_state::absent);

    /* The fifth consecutive vote confirms the absence and it outranks the
     * stuck-low charger signal and the measured current. */
    const auto absent = absent_value.observe(stuck_low_charging);
    CHECK(absent.state == battery_state::absent);
    CHECK(absent.available);
    CHECK(absent.charge == charge_signal::charging);
    CHECK(!absent.protection_active);
    CHECK(absent.charger_enabled);

    /* A stuck-low pin with a pack actually present still charges and still
     * trips protection, so the absence rule must not suppress a real charge.
     * 8300 mV is above the 8200 mV protection threshold and below the
     * 8330 mV absence threshold. */
    state present;
    feed(present, sample(true, true, true, 8300, -200, 95), 8);
    CHECK(present.snapshot().state == battery_state::charging);
    CHECK(present.snapshot().state != battery_state::absent);
    CHECK_EQ(present.snapshot().percentage, 95);
    CHECK(present.protection_active());
    CHECK(!present.charger_enabled());

    /* The absence also wins over a real charge current at the absent
     * threshold, and protection must not engage without a pack. */
    state absorbed;
    feed(absorbed, sample(true, true, true, 8400, -200, 95), 5);
    CHECK(absorbed.snapshot().state == battery_state::absent);
    CHECK(!absorbed.protection_active());
    CHECK(absorbed.charger_enabled());

    /* The precedence survives a current that would otherwise report a
     * discharge at the absent voltage. */
    state drained;
    feed(drained, sample(true, true, true, 8400, 900, 50), 5);
    CHECK(drained.snapshot().state == battery_state::absent);
    CHECK(drained.snapshot().current_ma == 900);

    /* The approved threshold is inclusive and one millivolt below it is not
     * an absence, even with the charger pin stuck low. */
    state below;
    feed(below, sample(true, true, true, 8329, 0, 50), 10);
    CHECK(below.snapshot().state != battery_state::absent);
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
    test_valid_ina_with_invalid_chg_stat_does_not_freeze_snapshot();
    test_absent_precedence_over_stuck_low_chg_stat();

    std::printf("battery protection pure contract: %d checks, %d failures\n",
                checks, failures);
    return failures == 0 ? 0 : 1;
}
