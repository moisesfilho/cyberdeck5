/*
 * Host contract for battery state, CHG_STAT polarity and charger protection.
 *
 * The production policy is linked directly: it stays pure (no ESP-IDF,
 * FreeRTOS, I2C, NVS, LVGL or BSP).  Coverage of the approved matrix:
 * classification/votes, the terminal-voltage cut, the latch release rules
 * (TEST-SOC-19..26), the absence reset and its unseeded-release guard
 * (TEST-SOC-17), the classification-before-estimate order (TEST-SOC-36) and the
 * byte-identical status line and view inputs (TEST-SOC-27/28).
 */
#include "cyberdeck_battery_protection.h"

#include <cstdio>
#include <cstring>

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
                   std::uint32_t elapsed_ms)
{
    observation value{};
    value.ina_valid = ina_valid;
    value.chg_valid = chg_valid;
    value.raw_chg_stat_low = raw_chg_stat_low;
    value.bus_voltage_mv = voltage_mv;
    value.current_ma = current_ma;
    value.elapsed_ms = elapsed_ms;
    return value;
}

void feed(state &value, const observation &input, int count)
{
    for (int i = 0; i < count; ++i) {
        (void)value.observe(input);
    }
}

/* Puts the policy into the latched state through the approved charging sample:
 * charging at exactly 8200 mV seeds 94 % from the 8118 mV open-circuit
 * voltage and cuts the charger. */
void engage_latch(state &value)
{
    const auto cut = value.observe(sample(true, true, true, 8200, -251, 0));
    CHECK_EQ(cut.state, battery_state::charging);
    CHECK_EQ(cut.percentage, 94);
    CHECK(value.protection_active());
    CHECK(!value.charger_enabled());
}

void test_constants_and_active_low_chg_stat()
{
    using namespace cyberdeck_battery_protection;
    CHECK_EQ(current_uncertainty_ma, 15);
    CHECK_EQ(external_voltage_mv, 7900);
    CHECK_EQ(absent_voltage_mv, 8330);
    CHECK_EQ(state_vote_count, 5);
    CHECK_EQ(no_source_vote_count, 5);
    CHECK_EQ(protection_enter_voltage_mv, 8200);
    CHECK_EQ(protection_exit_percentage, 85);
    CHECK(default_protection_enabled);

    CHECK(decode_chg_stat(true, true) == charge_signal::charging);
    CHECK(decode_chg_stat(true, false) == charge_signal::not_charging);
    CHECK(decode_chg_stat(false, false) == charge_signal::unknown);
    CHECK(decode_chg_stat(false, true) == charge_signal::unknown);
}

/* TEST-SOC-27: the status line stays byte-identical, now with the estimated
 * state of charge as the percentage field. */
void test_status_names_and_exact_formatter_contract()
{
    using namespace cyberdeck_battery_protection;

    CHECK(std::strcmp(state_name(battery_state::unknown), "unknown") == 0);
    CHECK(std::strcmp(state_name(battery_state::battery), "battery") == 0);
    CHECK(std::strcmp(state_name(battery_state::external), "external") == 0);
    CHECK(std::strcmp(state_name(battery_state::charging), "charging") == 0);
    CHECK(std::strcmp(state_name(battery_state::absent), "absent") == 0);
    CHECK(std::strcmp(charge_signal_name(charge_signal::unknown), "unknown") == 0);
    CHECK(std::strcmp(charge_signal_name(charge_signal::not_charging), "not_charging") == 0);
    CHECK(std::strcmp(charge_signal_name(charge_signal::charging), "charging") == 0);

    const snapshot value{
        battery_state::external, charge_signal::not_charging, true,
        63, 8123, -47, false, true, false,
    };
    constexpr char expected[] =
        "battery: state=external charge=not_charging available=true "
        "voltage_mv=8123 current_ma=-47 percentage=63 protection=active "
        "charger=off\n";
    char buffer[256] = {};
    const int result = format_status_line(buffer, sizeof(buffer), value);
    CHECK_EQ(result, static_cast<int>(std::strlen(expected)));
    CHECK(std::strcmp(buffer, expected) == 0);

    const snapshot measured{
        battery_state::charging, charge_signal::not_charging, true,
        94, 8191, -251, true, true, false,
    };
    constexpr char measured_expected[] =
        "battery: state=charging charge=not_charging available=true "
        "voltage_mv=8191 current_ma=-251 percentage=94 protection=active "
        "charger=off\n";
    char measured_buffer[256] = {};
    CHECK_EQ(format_status_line(measured_buffer, sizeof(measured_buffer), measured),
             static_cast<int>(std::strlen(measured_expected)));
    CHECK(std::strcmp(measured_buffer, measured_expected) == 0);

    char truncated[16];
    std::memset(truncated, 'X', sizeof(truncated));
    const int required = format_status_line(truncated, sizeof(truncated), value);
    CHECK_EQ(required, static_cast<int>(std::strlen(expected)));
    CHECK(truncated[sizeof(truncated) - 1] == '\0');
    CHECK(std::strncmp(truncated, expected, sizeof(truncated) - 1) == 0);

    char sentinel[] = {'X', 'Y', 'Z'};
    CHECK_EQ(format_status_line(sentinel, 0, value), 0);
    CHECK(sentinel[0] == 'X' && sentinel[1] == 'Y' && sentinel[2] == 'Z');
    CHECK_EQ(format_status_line(nullptr, 0, value), 0);
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
    const auto invalid_ina = value.observe(sample(false, true, false, 8000, 0, 0));
    CHECK(invalid_ina.state == battery_state::unknown);
    CHECK(!invalid_ina.available);

    /* REQ-BAT-UI-001 / AC-BAT-UI-001: only an INVALID INA read keeps the
     * snapshot unavailable.  A valid INA sample must publish availability even
     * when CHG_STAT could not be read, and the missing pin becomes `unknown`
     * (never `not_charging`).  The resolved state is still `unknown` here
     * because 8000 mV only starts the five-vote external confirmation; an
     * unresolved state and an unavailable measurement are different facts. */
    const auto invalid_chg = value.observe(sample(true, false, false, 8000, 0, 0));
    CHECK(invalid_chg.state == battery_state::unknown);
    CHECK(invalid_chg.available);
    CHECK_EQ(invalid_chg.bus_voltage_mv, 8000);
    CHECK(invalid_chg.charge == charge_signal::unknown);
    CHECK(invalid_chg.charge != charge_signal::not_charging);

    const auto battery = value.observe(sample(true, true, false, 7500, 16, 0));
    CHECK(battery.state == battery_state::battery);
    CHECK(battery.available);
    CHECK_EQ(battery.bus_voltage_mv, 7500);

    /* Policy/protection contract: CHG_STAT low still classifies the policy
     * snapshot as charging even at zero current. The separate presentation
     * contract (test_battery_view.cpp) must still render a battery glyph. */
    const auto charging_from_pin = value.observe(sample(true, true, true, 8000, 0, 0));
    CHECK(charging_from_pin.state == battery_state::charging);
    CHECK(charging_from_pin.available);
    CHECK(charging_from_pin.charge == charge_signal::charging);
    CHECK_EQ(charging_from_pin.current_ma, 0);
    CHECK(charging_from_pin.charger_enabled);
    CHECK(!charging_from_pin.protection_active);

    /* Exactly +15 mA is inside the uncertainty band; +16 is discharge. */
    const auto boundary_current = value.observe(sample(true, true, false, 7500, 15, 0));
    CHECK(boundary_current.state == battery_state::battery);
    const auto above_current = value.observe(sample(true, true, false, 7500, 16, 0));
    CHECK(above_current.state == battery_state::battery);
    const auto charge_current = value.observe(sample(true, true, false, 8000, -16, 0));
    CHECK(charge_current.state == battery_state::charging);

    /* External and absent are voltage-derived states and require five
     * consecutive matching votes.  A threshold value is inclusive. */
    state external_value;
    const observation external_sample = sample(true, true, false, 7900, 0, 0);
    feed(external_value, external_sample, 4);
    CHECK(external_value.snapshot().state != battery_state::external);
    const auto external = external_value.observe(external_sample);
    CHECK(external.state == battery_state::external);
    CHECK(external.available);

    state below_external;
    feed(below_external, sample(true, true, false, 7899, 0, 0), 8);
    CHECK(below_external.snapshot().state != battery_state::external);
    CHECK(below_external.snapshot().state != battery_state::absent);

    state absent_value;
    const observation absent_sample = sample(true, true, false, 8330, 0, 0);
    feed(absent_value, absent_sample, 4);
    CHECK(absent_value.snapshot().state != battery_state::absent);
    const auto absent = absent_value.observe(absent_sample);
    CHECK(absent.state == battery_state::absent);
    CHECK(absent.available);

    /* A disconnected pack leaves the measured bus below the approved battery
     * window instead of raising it to the external-power range. */
    state low_voltage_absent;
    const observation disconnected_sample = sample(true, true, false, 1907, 0, 0);
    feed(low_voltage_absent, disconnected_sample, 4);
    CHECK(low_voltage_absent.snapshot().state != battery_state::absent);
    const auto disconnected = low_voltage_absent.observe(disconnected_sample);
    CHECK(disconnected.state == battery_state::absent);
    CHECK(disconnected.available);

    state below_absent;
    feed(below_absent, sample(true, true, false, 8329, 0, 0), 8);
    CHECK(below_absent.snapshot().state != battery_state::absent);

    /* Below the valid battery window, absence wins over a readable charging
     * signal after exactly the approved five votes. */
    state low_voltage;
    const observation low_charging = sample(true, true, true, 5999, -120, 0);
    feed(low_voltage, low_charging, 4);
    CHECK(low_voltage.snapshot().state != battery_state::absent);
    const auto low_absent = low_voltage.observe(low_charging);
    CHECK(low_absent.state == battery_state::absent);
    CHECK(low_absent.charge == charge_signal::charging);
    CHECK(!low_absent.protection_active);
    CHECK(low_absent.charger_enabled);

    /* A non-matching vote resets the consecutive counter. */
    state reset_votes;
    feed(reset_votes, absent_sample, 4);
    (void)reset_votes.observe(sample(true, true, false, 7800, 0, 0));
    feed(reset_votes, absent_sample, 4);
    CHECK(reset_votes.snapshot().state != battery_state::absent);
    const auto after_fifth = reset_votes.observe(absent_sample);
    CHECK(after_fifth.state == battery_state::absent);
}

/* TEST-SOC-19 / TEST-SOC-20: the cut is decided by the terminal bus voltage
 * while charging; the percentage no longer gates it. */
void test_protection_entry_by_terminal_voltage()
{
    using namespace cyberdeck_battery_protection;

    state below;
    const auto at_8199 = below.observe(sample(true, true, true, 8199, -251, 0));
    CHECK_EQ(at_8199.state, battery_state::charging);
    CHECK(!below.protection_active());
    CHECK(below.charger_enabled());

    state disabled;
    CHECK(disabled.set_protection_enabled(false));
    (void)disabled.observe(sample(true, true, true, 8201, -251, 0));
    CHECK(!disabled.protection_active());
    CHECK(disabled.charger_enabled());

    state not_charging;
    (void)not_charging.observe(sample(true, true, false, 8300, 100, 0));
    CHECK(!not_charging.protection_active());
    CHECK(not_charging.charger_enabled());

    state exact;
    engage_latch(exact);
}

/* TEST-SOC-21: the latch holds through 300 samples of a resting pack (external
 * after five votes) with no new charger transition. */
void test_latch_holds_without_new_transitions()
{
    state value;
    engage_latch(value);

    bool charger = value.charger_enabled();
    int transitions = 0;
    bool stayed_active = true;
    for (int i = 0; i < 300; ++i) {
        (void)value.observe(sample(true, true, false, 8112, 3, 1000));
        if (value.charger_enabled() != charger) {
            ++transitions;
            charger = value.charger_enabled();
        }
        if (!value.protection_active()) stayed_active = false;
    }
    CHECK(stayed_active);
    CHECK_EQ(transitions, 0);
    CHECK(!value.charger_enabled());
    CHECK_EQ(value.snapshot().percentage, 94);
    CHECK(value.snapshot().state == battery_state::external);
}

/* TEST-SOC-22: leaving the charging state, an unreadable INA or an unresolved
 * state does not release the latch. */
void test_latch_ignores_non_charging_states()
{
    state value;
    engage_latch(value);

    feed(value, sample(true, true, false, 8112, 3, 1000), 300);
    CHECK(value.protection_active());
    CHECK(!value.charger_enabled());

    feed(value, sample(false, true, false, 0, 0, 0), 300);
    CHECK(value.protection_active());
    CHECK(!value.charger_enabled());

    const auto unknown_signal = value.observe(sample(true, false, false, 8000, 0, 0));
    CHECK(unknown_signal.charge == charge_signal::unknown);
    CHECK(value.protection_active());
    CHECK(!value.charger_enabled());
}

/* TEST-SOC-23: the estimated state of charge releases the latch on the first
 * sample at <= 85 %, without votes, as soon as the pack is no longer charging. */
void test_latch_releases_by_state_of_charge()
{
    state value;
    engage_latch(value);

    /* Four discharging samples followed by one charging sample keep the
     * no-source counter below its five consecutive votes. */
    for (int i = 0; i < 9; ++i) {
        const bool charging = (i % 5) == 4;
        const auto held = value.observe(sample(true, true, charging, 7800, 1000, 60000));
        CHECK(value.protection_active());
        CHECK(!value.charger_enabled());
        if (i == 8) CHECK_EQ(held.percentage, 86);
    }
    CHECK(value.snapshot().percentage >
          cyberdeck_battery_protection::protection_exit_percentage);
    CHECK_EQ(value.snapshot().state, battery_state::battery);

    /* The release sample discharges to 85 %; because it also completes the
     * no-source count, the state-of-charge path itself is isolated by
     * test_state_of_charge_release_requires_non_charging below. */
    const auto released = value.observe(sample(true, true, false, 7900, 1000, 60000));
    CHECK_EQ(released.percentage, 85);
    CHECK_EQ(released.state, battery_state::battery);
    CHECK(!value.protection_active());
    CHECK(value.charger_enabled());
}

/* R-reentry: a state-of-charge release must never fire while the pack still
 * classifies as charging, otherwise the next 8200 mV sample would cut again and
 * recreate the cycle this policy removes. */
void test_state_of_charge_release_requires_non_charging()
{
    state value;
    engage_latch(value);

    for (int i = 0; i < 10; ++i) {
        const bool charging = (i % 5) == 4;
        const auto held = value.observe(sample(true, true, charging, 7800, 1000, 60000));
        if (i == 9) {
            CHECK_EQ(held.percentage, 85);
            CHECK_EQ(held.state, battery_state::charging);
        }
        CHECK(value.protection_active());
        CHECK(!value.charger_enabled());
    }

    /* Once the charging evidence is gone the same estimate leaves the latch on
     * the first sample, without waiting for five votes. */
    const auto released = value.observe(sample(true, true, false, 7900, 1000, 60000));
    CHECK(released.percentage <= cyberdeck_battery_protection::protection_exit_percentage);
    CHECK(!value.protection_active());
    CHECK(value.charger_enabled());
}

/* TEST-SOC-24: five consecutive samples without an external source release the
 * latch; one external sample in between restarts the count. */
void test_latch_releases_after_five_samples_without_source()
{
    state value;
    engage_latch(value);

    for (int i = 1; i <= 4; ++i) {
        (void)value.observe(sample(true, true, false, 7800, 100, 1000));
        CHECK(value.protection_active());
        CHECK(!value.charger_enabled());
    }
    const auto fifth = value.observe(sample(true, true, false, 7800, 100, 1000));
    CHECK(!value.protection_active());
    CHECK(value.charger_enabled());
    CHECK(fifth.percentage > cyberdeck_battery_protection::protection_exit_percentage);

    /* A single high-voltage, low-current sample is not a confirmed `external`
     * state (that needs five votes), so it still counts as a sample without an
     * external source and completes the five-sample release. */
    state mixed;
    engage_latch(mixed);
    const observation sequence[] = {
        sample(true, true, false, 7800, 100, 1000),
        sample(true, true, false, 7800, 100, 1000),
        sample(true, true, false, 7800, 100, 1000),
        sample(true, true, false, 7800, 100, 1000),
        sample(true, true, false, 7900, 0, 1000),
        sample(true, true, false, 7800, 100, 1000),
        sample(true, true, false, 7800, 100, 1000),
        sample(true, true, false, 7800, 100, 1000),
        sample(true, true, false, 7800, 100, 1000),
    };
    for (int i = 0; i < 4; ++i) {
        (void)mixed.observe(sequence[i]);
        CHECK(mixed.protection_active());
        CHECK(!mixed.charger_enabled());
    }
    (void)mixed.observe(sequence[4]);
    CHECK(!mixed.protection_active());
    CHECK(mixed.charger_enabled());
    for (int i = 5; i < 9; ++i) {
        (void)mixed.observe(sequence[i]);
        CHECK(!mixed.protection_active());
        CHECK(mixed.charger_enabled());
    }
}

/* Device regression: a resting 2S pack at ~92 % sits above 7900 mV, so a
 * voltage-only external test never saw the pack running on the battery and the
 * latch never released.  Only the confirmed state counts: `battery` releases
 * after five samples, a confirmed `external` never releases. */
void test_no_source_uses_confirmed_state_not_voltage()
{
    using namespace cyberdeck_battery_protection;

    state on_battery;
    engage_latch(on_battery);
    for (int i = 1; i <= 6; ++i) {
        const auto value = on_battery.observe(sample(true, true, false, 8000, 150, 1000));
        CHECK_EQ(value.state, battery_state::battery);
        if (i < 5) {
            CHECK(on_battery.protection_active());
            CHECK(!on_battery.charger_enabled());
        } else {
            CHECK(!on_battery.protection_active());
            CHECK(on_battery.charger_enabled());
        }
    }

    state external_power;
    engage_latch(external_power);
    for (int i = 0; i < 6; ++i) {
        const auto value = external_power.observe(sample(true, true, false, 8040, 2, 1000));
        CHECK(external_power.protection_active());
        CHECK(!external_power.charger_enabled());
        if (i == 5) CHECK_EQ(value.state, battery_state::external);
    }
}

/* TEST-SOC-25: disabling releases immediately and re-enabling without a source
 * does not cut again. */
void test_disable_releases_immediately()
{
    state value;
    engage_latch(value);
    CHECK(value.set_protection_enabled(false));
    CHECK(!value.protection_active());
    CHECK(value.charger_enabled());

    (void)value.observe(sample(true, true, false, 7800, 100, 1000));
    CHECK(value.set_protection_enabled(true));
    CHECK(!value.protection_active());
    CHECK(value.charger_enabled());

    (void)value.observe(sample(true, true, true, 8200, -251, 1000));
    CHECK(value.protection_active());
    CHECK(!value.charger_enabled());
}

/* TEST-SOC-26 (protection half): the measured replay cuts zero times at
 * 8191 mV and exactly once at 8201 mV, keeping the latch stable afterwards. */
void test_measured_replay_cuts_once()
{
    state resting;
    int transitions = 0;
    bool charger = resting.charger_enabled();
    for (int i = 0; i < 40; ++i) {
        (void)resting.observe(sample(true, true, true, 8191, -251, 1000));
        if (resting.charger_enabled() != charger) {
            ++transitions;
            charger = resting.charger_enabled();
        }
    }
    for (int i = 0; i < 40; ++i) {
        (void)resting.observe(sample(true, true, false, 8108, 3, 1000));
        if (resting.charger_enabled() != charger) {
            ++transitions;
            charger = resting.charger_enabled();
        }
    }
    CHECK_EQ(transitions, 0);
    CHECK_EQ(resting.snapshot().percentage, 94);

    state cutting;
    transitions = 0;
    charger = cutting.charger_enabled();
    for (int i = 0; i < 40; ++i) {
        (void)cutting.observe(sample(true, true, true, 8201, -251, 1000));
        if (cutting.charger_enabled() != charger) {
            ++transitions;
            charger = cutting.charger_enabled();
        }
    }
    for (int i = 0; i < 40; ++i) {
        (void)cutting.observe(sample(true, true, false, 8108, 3, 1000));
        if (cutting.charger_enabled() != charger) {
            ++transitions;
            charger = cutting.charger_enabled();
        }
    }
    CHECK_EQ(transitions, 1);
    CHECK(cutting.protection_active());
    CHECK(!cutting.charger_enabled());
    CHECK_EQ(cutting.snapshot().percentage, 95);
}

/* TEST-SOC-17 (extension): the reset of an absent pack publishes no percentage
 * and leaves the estimate unseeded, so the fabricated 0 % cannot release the
 * latch; the approved five samples without an external source still do. */
void test_absence_reset_cannot_release_through_an_unseeded_estimate()
{
    state value;
    engage_latch(value);
    CHECK(value.protection_active());
    CHECK(!value.charger_enabled());
    CHECK_EQ(value.snapshot().percentage, 94);

    /* A removed pack floats near 1.9 V and needs five votes before the state
     * becomes absent; meanwhile the previous `charging` state is retained. */
    for (int i = 0; i < 4; ++i) {
        (void)value.observe(sample(true, true, false, 1900, 0, 1000));
        CHECK(value.snapshot().state == battery_state::charging);
        CHECK(value.protection_active());
        CHECK(!value.charger_enabled());
    }

    const auto absent = value.observe(sample(true, true, false, 1900, 0, 1000));
    CHECK_EQ(absent.state, battery_state::absent);
    CHECK_EQ(absent.percentage, 0);
    /* Releasing here would need the unseeded 0 % to stand in for a real
     * estimate: only the first no-source vote has been counted. */
    CHECK(value.protection_active());
    CHECK(!value.charger_enabled());

    for (int i = 0; i < 3; ++i) {
        (void)value.observe(sample(true, true, false, 1900, 0, 1000));
        CHECK(value.protection_active());
        CHECK(!value.charger_enabled());
    }
    (void)value.observe(sample(true, true, false, 1900, 0, 1000));
    CHECK(!value.protection_active());
    CHECK(value.charger_enabled());
}

/* TEST-SOC-36: the sample is classified before the estimator consumes it, so
 * the charging evidence of the current observation blocks the re-anchor on that
 * same sample.  A stale (previous-state) classification re-anchors one sample
 * early and reports 41 instead of 40. */
void test_classification_precedes_the_state_of_charge()
{
    state value;
    CHECK_EQ(value.observe(sample(true, true, false, 7115, 0, 0)).percentage, 50);
    feed(value, sample(true, true, false, 7900, 250, 1000), 2880);
    CHECK_EQ(value.snapshot().percentage, 40);

    /* 59000 ms of at-rest samples: one sample short of the re-anchor window. */
    feed(value, sample(true, true, false, 7115, 0, 1000), 59);
    CHECK_EQ(value.snapshot().percentage, 40);

    /* This sample both completes the window and reports charging. */
    const auto charging = value.observe(sample(true, true, true, 7115, 0, 1000));
    CHECK_EQ(charging.state, battery_state::charging);
    CHECK_EQ(charging.percentage, 40);

    /* The quiet window restarted here: 59 further at-rest samples still cannot
     * re-anchor, and the 60th moves exactly one point. */
    feed(value, sample(true, true, false, 7115, 0, 1000), 59);
    CHECK_EQ(value.snapshot().percentage, 40);
    const auto reanchored = value.observe(sample(true, true, false, 7115, 0, 1000));
    CHECK_EQ(reanchored.percentage, 41);
}

/* TEST-SOC-17: the absent pack resets the estimator, so no percentage is
 * published for the external glyph. */
void test_absence_resets_the_estimate()
{
    using namespace cyberdeck_battery_protection;
    state value;
    engage_latch(value);

    feed(value, sample(true, true, false, 8400, 0, 0), 4);
    CHECK_EQ(value.snapshot().state, battery_state::charging);
    const auto absent = value.observe(sample(true, true, false, 8400, 0, 0));
    CHECK_EQ(absent.state, battery_state::absent);
    CHECK(absent.available);
    CHECK_EQ(absent.percentage, 0);
}

void test_invalid_inputs_preserve_last_safe_protection_state()
{
    using namespace cyberdeck_battery_protection;
    state value;
    const auto safe = value.observe(sample(true, true, true, 8300, -100, 0));
    CHECK_EQ(safe.state, battery_state::charging);
    CHECK_EQ(safe.percentage, 100);
    CHECK(value.protection_active());
    CHECK(!value.charger_enabled());

    const auto after_ina_failure = value.observe(sample(false, true, true, 0, 0, 0));
    CHECK(after_ina_failure.state == safe.state);
    CHECK(!after_ina_failure.available);
    CHECK(value.protection_active());
    CHECK(!value.charger_enabled());
    CHECK(value.last_safe_snapshot().state == safe.state);
    /* An invalid INA read is the only case that preserves the last safe
     * snapshot, and it must not erase the last safe voltage/percentage. */
    CHECK_EQ(value.last_safe_snapshot().percentage, safe.percentage);
    CHECK_EQ(value.last_safe_snapshot().bus_voltage_mv, safe.bus_voltage_mv);

    const auto after_chg_failure = value.observe(sample(true, false, false, 7400, 0, 0));
    /* REQ-BAT-UI-001: the CHG_STAT failure must not be published as a
     * negative charger answer, and it must not fabricate a charging transition.
     * The snapshot is re-derived from the still-valid INA sample.  The
     * percentage is the coulomb-counted estimate, so a lower terminal voltage
     * no longer recomputes it and the latch stays engaged. */
    CHECK(after_chg_failure.charge == charge_signal::unknown);
    CHECK(after_chg_failure.charge != charge_signal::not_charging);
    CHECK(after_chg_failure.state == battery_state::battery);
    CHECK(after_chg_failure.available);
    CHECK_EQ(after_chg_failure.percentage, safe.percentage);
    CHECK_EQ(after_chg_failure.bus_voltage_mv, 7400);
    CHECK(value.protection_active());
    CHECK(!value.charger_enabled());

    /* A further invalid INA read keeps the most recent published values. */
    const auto after_second_ina_failure = value.observe(sample(false, false, false, 0, 0, 0));
    CHECK(after_second_ina_failure.state == battery_state::battery);
    CHECK(!after_second_ina_failure.available);
    CHECK_EQ(after_second_ina_failure.percentage, 100);
    CHECK_EQ(after_second_ina_failure.bus_voltage_mv, 7400);
    CHECK_EQ(value.last_safe_snapshot().percentage, 100);
    CHECK_EQ(value.last_safe_snapshot().bus_voltage_mv, 7400);
    CHECK(value.protection_active());
    CHECK(!value.charger_enabled());
}

void test_valid_ina_with_invalid_chg_stat_does_not_freeze_snapshot()
{
    state value;

    const auto baseline = value.observe(sample(true, true, true, 8000, -40, 0));
    CHECK(baseline.available);
    CHECK(baseline.state == battery_state::charging);
    CHECK(baseline.charge == charge_signal::charging);
    CHECK_EQ(baseline.percentage, 89);

    /* CHG_STAT unreadable, INA226 valid: voltage, current, percentage,
     * availability and the derived state all advance. */
    const auto after = value.observe(sample(true, false, false, 7400, 120, 0));
    CHECK(after.available);
    CHECK(after.state == battery_state::battery);
    CHECK_EQ(after.percentage, baseline.percentage);
    CHECK_EQ(after.bus_voltage_mv, 7400);
    CHECK_EQ(after.current_ma, 120);
    CHECK(after.charge == charge_signal::unknown);
    CHECK(after.charge != charge_signal::not_charging);
    CHECK(after.bus_voltage_mv != baseline.bus_voltage_mv);
    CHECK_EQ(value.last_safe_snapshot().percentage, baseline.percentage);
    CHECK_EQ(value.last_safe_snapshot().bus_voltage_mv, 7400);
    CHECK_EQ(value.last_safe_snapshot().current_ma, 120);

    /* The same holds when the raw level happens to be stuck low: a failed read
     * is `unknown` regardless of the electrical level that was attempted. */
    const auto stuck_low_failed = value.observe(sample(true, false, true, 7600, 0, 0));
    CHECK(stuck_low_failed.charge == charge_signal::unknown);
    CHECK(stuck_low_failed.charge != charge_signal::charging);
    CHECK(stuck_low_failed.available);
    CHECK_EQ(stuck_low_failed.percentage, baseline.percentage);
    CHECK(stuck_low_failed.state == battery_state::battery);

    /* An unreadable CHG_STAT must not disable protection: the policy still
     * classifies from the current and the terminal voltage cuts the charger. */
    state protectable;
    const auto charged = protectable.observe(sample(true, false, false, 8300, -100, 0));
    CHECK(charged.state == battery_state::charging);
    CHECK(charged.charge == charge_signal::unknown);
    CHECK_EQ(charged.percentage, 100);
    CHECK_EQ(charged.bus_voltage_mv, 8300);
    CHECK(protectable.protection_active());
    CHECK(!protectable.charger_enabled());

    /* Recovery: once CHG_STAT is readable again the signal is published. */
    const auto recovered = value.observe(sample(true, true, true, 8000, -30, 0));
    CHECK(recovered.charge == charge_signal::charging);
    CHECK(recovered.state == battery_state::charging);
    CHECK_EQ(recovered.percentage, 89);
}

void test_absent_precedence_over_stuck_low_chg_stat()
{
    state value;
    const observation stuck_low_charging = sample(true, true, true, 8400, 0, 0);

    /* Policy contract: below the absence threshold, CHG_STAT low continues to
     * classify the protection snapshot as charging. The icon's independent
     * INA-current decision is covered by test_battery_view.cpp. */
    feed(value, sample(true, true, true, 7800, 0, 0), 8);
    CHECK(value.snapshot().state == battery_state::charging);
    CHECK(value.snapshot().charge == charge_signal::charging);
    CHECK(value.snapshot().state != battery_state::absent);
    CHECK(value.snapshot().charger_enabled);
    CHECK(!value.snapshot().protection_active);

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
    feed(present, sample(true, true, true, 8300, -200, 0), 8);
    CHECK(present.snapshot().state == battery_state::charging);
    CHECK(present.snapshot().state != battery_state::absent);
    CHECK_EQ(present.snapshot().percentage, 100);
    CHECK(present.protection_active());
    CHECK(!present.charger_enabled());

    /* The absence also wins over a real charge current at the absent
     * threshold, and protection must not engage without a pack. */
    state absorbed;
    feed(absorbed, sample(true, true, true, 8400, -200, 0), 5);
    CHECK(absorbed.snapshot().state == battery_state::absent);
    CHECK(!absorbed.protection_active());
    CHECK(absorbed.charger_enabled());

    /* The precedence survives a current that would otherwise report a
     * discharge at the absent voltage. */
    state drained;
    feed(drained, sample(true, true, true, 8400, 900, 0), 5);
    CHECK(drained.snapshot().state == battery_state::absent);
    CHECK(drained.snapshot().current_ma == 900);

    /* The approved threshold is inclusive and one millivolt below it is not
     * an absence, even with the charger pin stuck low. */
    state below;
    feed(below, sample(true, true, true, 8329, 0, 0), 10);
    CHECK(below.snapshot().state != battery_state::absent);
}

} // namespace

int main()
{
    test_constants_and_active_low_chg_stat();
    test_status_names_and_exact_formatter_contract();
    test_default_and_persisted_option();
    test_state_classification_and_vote_boundaries();
    test_protection_entry_by_terminal_voltage();
    test_latch_holds_without_new_transitions();
    test_latch_ignores_non_charging_states();
    test_latch_releases_by_state_of_charge();
    test_state_of_charge_release_requires_non_charging();
    test_latch_releases_after_five_samples_without_source();
    test_no_source_uses_confirmed_state_not_voltage();
    test_disable_releases_immediately();
    test_measured_replay_cuts_once();
    test_absence_resets_the_estimate();
    test_absence_reset_cannot_release_through_an_unseeded_estimate();
    test_classification_precedes_the_state_of_charge();
    test_invalid_inputs_preserve_last_safe_protection_state();
    test_valid_ina_with_invalid_chg_stat_does_not_freeze_snapshot();
    test_absent_precedence_over_stuck_low_chg_stat();

    std::printf("battery protection pure contract: %d checks, %d failures\n",
                checks, failures);
    return failures == 0 ? 0 : 1;
}
