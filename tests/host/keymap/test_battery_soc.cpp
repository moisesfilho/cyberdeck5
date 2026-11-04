/*
 * Host contract for the pure state-of-charge estimator.
 *
 * TEST-SOC-01..18 and the state-of-charge half of TEST-SOC-26 from the approved
 * test matrix are exercised here; the policy that feeds this module is covered
 * by test_battery_protection.cpp.  The estimator is linked against the real
 * production sources, without ESP-IDF, FreeRTOS, I2C, NVS, LVGL or hardware.
 */
#include "cyberdeck_battery.h"
#include "cyberdeck_battery_soc.h"

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

static_assert(cyberdeck_battery_soc::pack_capacity_mah == 2000,
              "TEST-SOC-08/12 vectors assume the approved 2000 mAh pack");

using cyberdeck_battery_soc::input;
using cyberdeck_battery_soc::reading;
using cyberdeck_battery_soc::state;

input sample(std::int32_t bus_voltage_mv,
             std::int32_t current_ma,
             std::uint32_t elapsed_ms,
             bool charging = false,
             bool valid = true,
             bool present = true)
{
    input value{};
    value.valid = valid;
    value.present = present;
    value.charging = charging;
    value.bus_voltage_mv = bus_voltage_mv;
    value.current_ma = current_ma;
    value.elapsed_ms = elapsed_ms;
    return value;
}

void feed(state &value, const input &item, int count)
{
    for (int i = 0; i < count; ++i) {
        (void)value.observe(item);
    }
}

/* TEST-SOC-01 / TEST-SOC-02: the first sample seeds from the open-circuit
 * voltage, so a charging sample at 8060 mV reports the same 94 % as the resting
 * 7978 mV sample after the cut.  The terminal voltage alone would report 98. */
void test_seed_uses_open_circuit_voltage()
{
    CHECK_EQ(cyberdeck_battery::ocv_from_terminal_mv(8060, -251), 7978);
    CHECK_EQ(cyberdeck_battery::ocv_from_terminal_mv(7978, 3), 7978);
    CHECK_EQ(cyberdeck_battery::percentage_from_bus_voltage_mv(7978), 94);
    CHECK_EQ(cyberdeck_battery::equivalent_series_mohm, 330);

    state value;
    const reading seeded = value.observe(sample(8060, -251, 0, true));
    CHECK(seeded.seeded);
    CHECK_EQ(seeded.percentage, 94);

    const reading after_cut = value.observe(sample(7978, 3, 1000));
    CHECK(after_cut.seeded);
    CHECK_EQ(after_cut.percentage, 94);
}

/* TEST-SOC-03 / TEST-SOC-04: an unseeded hold reports nothing, and the seeding
 * open-circuit voltage saturates in the approved window. */
void test_seed_requires_a_valid_sample_and_saturates()
{
    state value;
    const reading unseeded = value.observe(sample(0, 0, 0, false, false));
    CHECK(!unseeded.seeded);
    CHECK_EQ(unseeded.percentage, 0);

    const reading seeded = value.observe(sample(7050, 0, 0));
    CHECK(seeded.seeded);
    CHECK_EQ(seeded.percentage, 50);

    state low;
    CHECK_EQ(low.observe(sample(6000, 0, 0)).percentage, 0);
    state high;
    CHECK_EQ(high.observe(sample(9000, 0, 0)).percentage, 100);
    state below;
    CHECK_EQ(below.observe(sample(5000, 0, 0)).percentage, 0);
}

/* TEST-SOC-05: the series-resistance compensation only applies up to
 * |current| <= 500 mA. */
void test_compensation_window()
{
    CHECK_EQ(cyberdeck_battery::ocv_from_terminal_mv(8060, -500), 7895);
    CHECK_EQ(cyberdeck_battery::ocv_from_terminal_mv(8060, -501), 8060);
    CHECK_EQ(cyberdeck_battery::ocv_from_terminal_mv(8060, 500), 8100);

    state charging_window;
    CHECK_EQ(charging_window.observe(sample(8060, -500, 0, true)).percentage, 90);
    state outside_window;
    CHECK_EQ(outside_window.observe(sample(8060, -501, 0, true)).percentage, 98);
    state discharge_window;
    CHECK_EQ(discharge_window.observe(sample(8060, 500, 0)).percentage, 100);
}

/* TEST-SOC-06 / TEST-SOC-07: coulomb counting follows the sign of the current,
 * positive discharging and negative charging. */
void test_integration_follows_current_sign()
{
    state discharging;
    (void)discharging.observe(sample(7050, 0, 0));
    feed(discharging, sample(7900, 250, 1000), 600);
    CHECK_EQ(discharging.current().percentage, 47);

    state charging;
    (void)charging.observe(sample(7050, 0, 0));
    feed(charging, sample(7900, -250, 1000, true), 600);
    CHECK_EQ(charging.current().percentage, 52);
}

/* TEST-SOC-08 / TEST-SOC-09: the division remainder is transported between
 * observations; truncating every sample would report 16 and 50. */
void test_remainder_is_transported()
{
    state value;
    (void)value.observe(sample(7050, 0, 0));
    feed(value, sample(7900, 250, 1000), 10000);
    CHECK_EQ(value.current().percentage, 15);

    state fine;
    (void)fine.observe(sample(7050, 0, 0));
    feed(fine, sample(7900, 500, 1), 14400);
    CHECK_EQ(fine.current().percentage, 49);
}

/* TEST-SOC-10 / TEST-SOC-11: no interval and an interval above the approved
 * window integrate nothing; the window itself is inclusive. */
void test_elapsed_window()
{
    state zero;
    (void)zero.observe(sample(7050, 0, 0));
    feed(zero, sample(7900, 250, 0), 100);
    CHECK_EQ(zero.current().percentage, 50);
    (void)zero.observe(sample(7900, 250, 15000));
    CHECK_EQ(zero.current().percentage, 49);

    state boundary;
    (void)boundary.observe(sample(7050, 0, 0));
    (void)boundary.observe(sample(7900, 250, 60000));
    CHECK_EQ(boundary.current().percentage, 49);

    state beyond;
    (void)beyond.observe(sample(7050, 0, 0));
    (void)beyond.observe(sample(7900, 250, 60001));
    CHECK_EQ(beyond.current().percentage, 50);

    /* A sample above the window must not count towards the quiet time either:
     * the estimate only re-anchors after 60000 ms of short at-rest samples. */
    state quiet_gap;
    (void)quiet_gap.observe(sample(7050, 0, 0));
    feed(quiet_gap, sample(7900, 250, 1000), 2880);
    CHECK_EQ(quiet_gap.current().percentage, 40);
    (void)quiet_gap.observe(sample(7050, 0, 60001));
    feed(quiet_gap, sample(7050, 0, 1000), 59);
    CHECK_EQ(quiet_gap.current().percentage, 40);
    /* Quiet window plus three confirmations before the anchor fires. */
    for (int i = 0; i < 3; ++i) {
        const reading held = quiet_gap.observe(sample(7050, 0, 1000));
        if (i < 2) {
            CHECK_EQ(held.percentage, 40);
        } else {
            CHECK_EQ(held.percentage, 41);
        }
    }
}

/* TEST-SOC-12: a re-anchor moves at most one point per observation. */
void test_reanchor_slew()
{
    state value;
    (void)value.observe(sample(7050, 0, 0));
    feed(value, sample(7900, 250, 1000), 2880);
    CHECK_EQ(value.current().percentage, 40);

    int previous = value.current().percentage;
    int largest_step = 0;
    for (int i = 0; i < 59; ++i) {
        const reading held = value.observe(sample(7050, 0, 1000));
        const int step = held.percentage - previous;
        if (step > largest_step) largest_step = step;
        previous = held.percentage;
    }
    CHECK_EQ(value.current().percentage, 40);
    CHECK_EQ(largest_step, 0);

    /* The anchor needs the quiet window plus three consecutive confirmations of
     * the same target, so the first two samples past 60000 ms still hold 40. */
    for (int i = 0; i < 2; ++i) {
        const reading held = value.observe(sample(7050, 0, 1000));
        CHECK_EQ(held.percentage, 40);
        previous = held.percentage;
    }

    for (int expected = 41; expected <= 50; ++expected) {
        const reading move = value.observe(sample(7050, 0, 1000));
        const int step = move.percentage - previous;
        if (step > largest_step) largest_step = step;
        previous = move.percentage;
        CHECK_EQ(move.percentage, expected);
    }
    CHECK_EQ(largest_step, 1);

    /* One anchor per at-rest episode: the value converges once and then stays
     * put instead of being rewritten from the open-circuit voltage. */
    for (int i = 0; i < 10; ++i) {
        const reading steady = value.observe(sample(7050, 0, 1000));
        CHECK_EQ(steady.percentage, 50);
    }
}

/* R-gap: a real reading gap (> 60000 ms) clears the accumulated quiet time, so
 * 40 s + gap + 30 s must not re-anchor while 60 s continuous does.  A zero
 * interval is a neutral re-observation: 40 s + dt=0 + 20 s still re-anchors.
 * Either way the anchor fires on the third consecutive confirmation after the
 * quiet window, never on the window boundary itself. */
void test_reading_gap_breaks_quiet_window()
{
    state gapped;
    (void)gapped.observe(sample(7050, 0, 0));
    feed(gapped, sample(7900, 250, 1000), 2880);
    CHECK_EQ(gapped.current().percentage, 40);

    feed(gapped, sample(7050, 0, 1000), 40);
    CHECK_EQ(gapped.current().percentage, 40);
    (void)gapped.observe(sample(7050, 0, 60001));
    CHECK_EQ(gapped.current().percentage, 40);
    feed(gapped, sample(7050, 0, 1000), 30);
    CHECK_EQ(gapped.current().percentage, 40);

    /* Only 60000 ms of uninterrupted rest after the gap may start the
     * confirmation; the anchor fires on the third one. */
    feed(gapped, sample(7050, 0, 1000), 30);
    CHECK_EQ(gapped.current().percentage, 40);
    /* That sample is the first confirmation; the next two complete the count. */
    (void)gapped.observe(sample(7050, 0, 1000));
    CHECK_EQ(gapped.current().percentage, 40);
    (void)gapped.observe(sample(7050, 0, 1000));
    CHECK_EQ(gapped.current().percentage, 41);

    state continuous;
    (void)continuous.observe(sample(7050, 0, 0));
    feed(continuous, sample(7900, 250, 1000), 2880);
    CHECK_EQ(continuous.current().percentage, 40);
    feed(continuous, sample(7050, 0, 1000), 59);
    CHECK_EQ(continuous.current().percentage, 40);
    for (int i = 0; i < 2; ++i) {
        (void)continuous.observe(sample(7050, 0, 1000));
        CHECK_EQ(continuous.current().percentage, 40);
    }
    (void)continuous.observe(sample(7050, 0, 1000));
    CHECK_EQ(continuous.current().percentage, 41);

    /* A zero interval is the neutral re-observation declared by the contract
     * (two 1 Hz timer samples in the same tick or an on-demand status query):
     * it keeps the accumulated quiet window, so the re-anchor still happens. */
    state zero_gap;
    (void)zero_gap.observe(sample(7050, 0, 0));
    feed(zero_gap, sample(7900, 250, 1000), 2880);
    CHECK_EQ(zero_gap.current().percentage, 40);
    feed(zero_gap, sample(7050, 0, 1000), 40);
    CHECK_EQ(zero_gap.current().percentage, 40);
    (void)zero_gap.observe(sample(7050, 0, 0));
    CHECK_EQ(zero_gap.current().percentage, 40);
    feed(zero_gap, sample(7050, 0, 1000), 19);
    CHECK_EQ(zero_gap.current().percentage, 40);
    for (int i = 0; i < 3; ++i) {
        const reading held = zero_gap.observe(sample(7050, 0, 1000));
        if (i < 2) {
            CHECK_EQ(held.percentage, 40);
        } else {
            CHECK_EQ(held.percentage, 41);
        }
    }
}

/* Flicker regression (device, fw 1.4.0): while at rest the open-circuit
 * voltage oscillates one millivolt around the 94 % bin edge (7994 <-> 7995 on
 * the approved 2100 mV window).  Rewriting the anchor from every sample made the
 * header blink 93 <-> 94 once per second; an alternating target never confirms,
 * so the estimate stays untouched. */
void test_rest_anchor_ignores_oscillating_open_circuit()
{
    /* TEST-SOC-38: the stimulus straddles a bin edge by exactly one point.  The
     * two voltages are single constants used by both the assertions and the
     * loop, so a future edit that moves them inside one bin (which would stop
     * exercising the alternating anchor target) fails here instead of passing
     * silently. */
    const std::int32_t below_edge_mv = 7994;
    const std::int32_t above_edge_mv = 7995;
    CHECK_EQ(cyberdeck_battery::percentage_from_bus_voltage_mv(below_edge_mv), 94);
    CHECK_EQ(cyberdeck_battery::percentage_from_bus_voltage_mv(above_edge_mv), 95);

    state value;
    (void)value.observe(sample(below_edge_mv, 0, 0));
    CHECK_EQ(value.current().percentage, 94);

    bool constant = true;
    int changes = 0;
    int previous = value.current().percentage;
    for (int i = 1; i <= 80; ++i) {
        const std::int32_t voltage =
            (i % 2) == 0 ? above_edge_mv : below_edge_mv;
        const reading held = value.observe(sample(voltage, 0, 1000));
        if (held.percentage != 94) constant = false;
        if (held.percentage != previous) ++changes;
        previous = held.percentage;
    }
    CHECK(constant);
    CHECK_EQ(changes, 0);
    CHECK_EQ(value.current().percentage, 94);
}

/* The confirmed anchor is written at the centre of the target bin: one unit of
 * charge no longer flips the header, and resting integration is preserved
 * instead of being reset by a repeated anchor. */
void test_rest_anchor_centers_the_bin_and_keeps_integration()
{
    state value;
    (void)value.observe(sample(7050, 0, 0));
    /* Quiet window plus three confirmations: the target is the current 50 %. */
    feed(value, sample(7050, 0, 1000), 62);
    CHECK_EQ(value.current().percentage, 50);

    /* 10 s at 14 mA stay inside the rest band and remove 19 units of charge.
     * A value written at the bin edge (50.0000 %) would already show 49. */
    feed(value, sample(7050, 14, 1000), 10);
    CHECK_EQ(value.current().percentage, 50);

    /* The same integration survives 2600 more seconds at 14 mA; a repeated
     * anchor would have pulled the counter back to the centre every second. */
    feed(value, sample(7050, 14, 1000), 2600);
    CHECK_EQ(value.current().percentage, 49);
}

/* The confirmation count restarts when the at-rest episode is interrupted, and
 * the anchor remains single-shot for the rest of the episode. */
void test_rest_anchor_requires_confirmation_and_restarts()
{
    state value;
    (void)value.observe(sample(7994, 0, 0));
    /* Stable rest: one anchor, centred at 94.5 %, display unchanged. */
    feed(value, sample(7994, 0, 1000), 62);
    CHECK_EQ(value.current().percentage, 94);

    /* 100 mA ends the episode without moving the centred value. */
    (void)value.observe(sample(7994, 100, 1000));
    CHECK_EQ(value.current().percentage, 94);

    /* A new episode needs the quiet window and three confirmations again; the
     * target is now 50 %, so only the third confirmation starts the descent. */
    feed(value, sample(7050, 0, 1000), 59);
    CHECK_EQ(value.current().percentage, 94);
    for (int i = 0; i < 2; ++i) {
        (void)value.observe(sample(7050, 0, 1000));
        CHECK_EQ(value.current().percentage, 94);
    }
    (void)value.observe(sample(7050, 0, 1000));
    CHECK_EQ(value.current().percentage, 93);
}

/* The single-anchor episode is restarted by a real reading gap and by a pack
 * reset.  Without clearing the flag the pack could never re-anchor again for
 * the rest of the boot, so a new at-rest episode must anchor a new target. */
void test_anchor_episode_restarts_after_gap_and_reset()
{
    state gapped;
    (void)gapped.observe(sample(7994, 0, 0));
    CHECK_EQ(gapped.current().percentage, 94);
    feed(gapped, sample(7994, 0, 1000), 62);
    CHECK_EQ(gapped.current().percentage, 94);

    /* 60001 ms is a reading gap: the episode ends and the anchor flag clears. */
    (void)gapped.observe(sample(7994, 0, 60001));
    CHECK_EQ(gapped.current().percentage, 94);
    /* 60 s of rest only starts the confirmation (first vote) ... */
    feed(gapped, sample(7050, 0, 1000), 60);
    CHECK_EQ(gapped.current().percentage, 94);
    (void)gapped.observe(sample(7050, 0, 1000));
    CHECK_EQ(gapped.current().percentage, 94);
    /* ... and the third confirmation anchors the new 50 % target: the value
     * starts its one-point convergence instead of staying at 94 forever. */
    (void)gapped.observe(sample(7050, 0, 1000));
    CHECK_EQ(gapped.current().percentage, 93);

    state removed;
    (void)removed.observe(sample(7994, 0, 0));
    feed(removed, sample(7994, 0, 1000), 62);
    CHECK_EQ(removed.current().percentage, 94);
    const reading reset = removed.observe(sample(0, 0, 0, false, true, false));
    CHECK(!reset.seeded);
    (void)removed.observe(sample(7994, 0, 0));
    CHECK_EQ(removed.current().percentage, 94);
    feed(removed, sample(7050, 0, 1000), 60);
    CHECK_EQ(removed.current().percentage, 94);
    (void)removed.observe(sample(7050, 0, 1000));
    CHECK_EQ(removed.current().percentage, 94);
    (void)removed.observe(sample(7050, 0, 1000));
    CHECK_EQ(removed.current().percentage, 93);
}

/* TEST-SOC-13 / TEST-SOC-14: charging and a current above the tolerance block
 * the re-anchor, so the estimate keeps only the integrated drift. */
void test_reanchor_is_blocked()
{
    state charging;
    (void)charging.observe(sample(7050, 0, 0));
    feed(charging, sample(7050, 3, 1000, true), 120);
    CHECK_EQ(charging.current().percentage, 49);

    state current;
    (void)current.observe(sample(7050, 0, 0));
    feed(current, sample(7050, 16, 1000), 120);
    CHECK_EQ(current.current().percentage, 49);
}

/* TEST-SOC-15 / TEST-SOC-16: an invalid sample holds the estimate without
 * re-seeding, and an absent pack resets it. */
void test_hold_and_reset()
{
    state value;
    (void)value.observe(sample(7050, 0, 0));
    feed(value, sample(7900, 2000, 60000), 3);
    CHECK_EQ(value.current().percentage, 47);

    feed(value, sample(0, 0, 1000, false, false), 10);
    const reading held = value.current();
    CHECK(held.seeded);
    CHECK_EQ(held.percentage, 47);

    const reading resumed = value.observe(sample(7050, 0, 1000));
    CHECK(resumed.seeded);
    CHECK_EQ(resumed.percentage, 47);

    state removed;
    (void)removed.observe(sample(8191, -251, 0, true));
    CHECK_EQ(removed.current().percentage, 100);
    const reading reset = removed.observe(sample(0, 0, 0, false, true, false));
    CHECK(!reset.seeded);
    CHECK_EQ(reset.percentage, 0);
    const reading reseeded = removed.observe(sample(7050, 0, 0));
    CHECK(reseeded.seeded);
    CHECK_EQ(reseeded.percentage, 50);
}

/* TEST-SOC-18: the counter saturates at both ends. */
void test_saturation()
{
    state empty;
    (void)empty.observe(sample(6000, 0, 0));
    feed(empty, sample(7900, 250, 1000), 10000);
    CHECK_EQ(empty.current().percentage, 0);

    state full;
    (void)full.observe(sample(9000, 0, 0));
    feed(full, sample(7900, -250, 1000, true), 10000);
    CHECK_EQ(full.current().percentage, 100);
}

/* TEST-SOC-37: the recorded post-cut resting anchors (8103, 8106, 8112 and
 * 8116 mV, taken after the 8200 mV charger cut) must publish the policy
 * plateau, and the single rest anchor must hold it there.  With the previous
 * 8230 mV ceiling the plateau could never read 100 %; the ceiling is now the
 * anchor itself, so 100 % means "maximum charge of this policy" and is
 * reachable. */
void test_plateau_saturates_at_the_recorded_resting_anchors()
{
    const std::int32_t anchors[] = {8103, 8106, 8112, 8116};
    for (const std::int32_t anchor : anchors) {
        state seeded;
        const reading first = seeded.observe(sample(anchor, 3, 0));
        CHECK(first.seeded);
        CHECK_EQ(first.percentage, 100);

        /* Before the single rest anchor the seed sits exactly on 100 % and the
         * first integration units truncate to 99; the display must stay inside
         * {99, 100} and never fall to a distant bin. */
        bool bounded_before_anchor = true;
        bool anchored_to_100 = false;
        for (int i = 0; i < 63; ++i) {
            const reading held = seeded.observe(sample(anchor, 3, 1000));
            if (held.percentage < 99 || held.percentage > 100) {
                bounded_before_anchor = false;
            }
            if (held.percentage == 100) anchored_to_100 = true;
        }
        CHECK(bounded_before_anchor);
        CHECK(anchored_to_100);

        /* After the anchor the top bin keeps its half-point headroom (the same
         * bin-centre convention as every other bin): ten minutes of 1 Hz
         * samples at +3 mA must never drop the display to 99. */
        bool held_on_plateau = true;
        for (int i = 0; i < 600; ++i) {
            if (seeded.observe(sample(anchor, 3, 1000)).percentage != 100) {
                held_on_plateau = false;
            }
        }
        CHECK(held_on_plateau);
    }

    /* The ceiling is also pinned from behaviour alone, not only by the constant:
     * the last point below it truncates to 99 and the first point at it shows
     * 100. */
    CHECK_EQ(cyberdeck_battery::percentage_from_bus_voltage_mv(8099), 99);
    CHECK_EQ(cyberdeck_battery::percentage_from_bus_voltage_mv(8100), 100);
}

/* TEST-SOC-39: ten minutes of 1 Hz at-rest samples on the plateau may move the
 * header by at most one point (the pre-anchor truncation) and must end at 100,
 * never at the old 99.  The value must never fall back to the 94 <-> 98
 * oscillation. */
void test_plateau_stays_within_one_point_for_ten_minutes()
{
    state value;
    (void)value.observe(sample(8106, 3, 0));
    CHECK_EQ(value.current().percentage, 100);

    std::int32_t lowest = value.current().percentage;
    std::int32_t highest = value.current().percentage;
    int changes = 0;
    std::int32_t previous = value.current().percentage;
    for (int i = 0; i < 600; ++i) {
        const reading held = value.observe(sample(8106, 3, 1000));
        if (held.percentage < lowest) lowest = held.percentage;
        if (held.percentage > highest) highest = held.percentage;
        if (held.percentage != previous) ++changes;
        previous = held.percentage;
    }
    CHECK(highest - lowest <= 1);
    /* 99 only before the anchor fires; the single anchor then holds 100. */
    CHECK_EQ(lowest, 99);
    CHECK_EQ(highest, 100);
    CHECK_EQ(changes, 2);
    CHECK_EQ(value.current().percentage, 100);
}

/* TEST-SOC-40: the top bin is not frozen.  The half-point headroom is exactly
 * one bin centre (100.5 %), so the accumulator saturates there and the display
 * leaves 100 only after 0.5 % of charge is consumed - about 3.3 h at 3 mA
 * (5000 units * 7200 / 3 mA = 12,000,000 ms), which is why the decay below uses
 * a 60 s step at 16 mA: above the 15 mA rest tolerance the single rest anchor
 * cannot re-arm and mask the integration. */
void test_plateau_headroom_is_half_a_point_and_not_frozen()
{
    state value;
    (void)value.observe(sample(8103, 3, 0));
    CHECK_EQ(value.current().percentage, 100);

    /* Fire the single rest anchor (quiet window plus three confirmations). */
    for (int i = 0; i < 63; ++i) {
        (void)value.observe(sample(8103, 3, 1000));
    }
    CHECK_EQ(value.current().percentage, 100);

    /* Charging current pushes the accumulator up; the clamp holds it at exactly
     * 100.5 % and the published display never exceeds 100. */
    for (int i = 0; i < 100; ++i) {
        const reading held = value.observe(sample(8103, -250, 1000, true));
        CHECK_EQ(held.percentage, 100);
    }

    /* 16 mA * 60000 ms / 7200 = 400/3 units per sample.  5000 units of headroom
     * mean 37 samples still read 100 and the 38th crosses the bin edge
     * (1,005,000 - 5066 = 999,934 -> 99). */
    for (int i = 0; i < 37; ++i) {
        CHECK_EQ(value.observe(sample(8103, 16, 60000)).percentage, 100);
    }
    CHECK_EQ(value.observe(sample(8103, 16, 60000)).percentage, 99);
    CHECK_EQ(value.current().percentage, 99);
}

/* TEST-SOC-26 (state-of-charge half): the measured replay stays on the policy
 * plateau.  Both seeds saturate, because the 8191 mV terminal sample (8118 mV
 * open circuit) and the 8201 mV one (8119 mV) are above the approved ceiling:
 * 100 % is the maximum charge of this policy, not a full pack.  The charging
 * phase leaves the accumulator at the top-bin centre (100.5 %), so the 40
 * resting samples at 8108 mV / +3 mA drain only 16 units and the display holds
 * 100 - never back to the old 94 <-> 98 oscillation. */
void test_measured_replay_is_stable()
{
    state value;
    (void)value.observe(sample(8191, -251, 0, true));

    bool charging_plateau = true;
    for (int i = 0; i < 40; ++i) {
        if (value.observe(sample(8191, -251, 1000, true)).percentage != 100) {
            charging_plateau = false;
        }
    }
    CHECK(charging_plateau);

    bool rest_plateau = true;
    int changes = 0;
    int previous = value.current().percentage;
    for (int i = 0; i < 40; ++i) {
        const reading held = value.observe(sample(8108, 3, 1000));
        if (held.percentage < 99 || held.percentage > 100) rest_plateau = false;
        if (held.percentage != previous) ++changes;
        previous = held.percentage;
    }
    CHECK(rest_plateau);
    CHECK_EQ(changes, 0);
    CHECK_EQ(value.current().percentage, 100);

    state raised;
    (void)raised.observe(sample(8201, -251, 0, true));
    CHECK_EQ(raised.current().percentage, 100);
}

} // namespace

int main()
{
    test_seed_uses_open_circuit_voltage();
    test_seed_requires_a_valid_sample_and_saturates();
    test_compensation_window();
    test_integration_follows_current_sign();
    test_remainder_is_transported();
    test_elapsed_window();
    test_reanchor_slew();
    test_reanchor_is_blocked();
    test_reading_gap_breaks_quiet_window();
    test_rest_anchor_ignores_oscillating_open_circuit();
    test_rest_anchor_centers_the_bin_and_keeps_integration();
    test_rest_anchor_requires_confirmation_and_restarts();
    test_anchor_episode_restarts_after_gap_and_reset();
    test_hold_and_reset();
    test_saturation();
    test_plateau_saturates_at_the_recorded_resting_anchors();
    test_plateau_stays_within_one_point_for_ten_minutes();
    test_plateau_headroom_is_half_a_point_and_not_frozen();
    test_measured_replay_is_stable();

    std::printf("battery state-of-charge contract (TEST-SOC-01..18/26/37..40): "
                "%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
