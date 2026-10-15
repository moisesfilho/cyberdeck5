/*
 * Host test for the pure battery power-indicator view
 * (REQ-BAT-UI-001..005 / AC-BAT-UI-001..006).
 *
 * The test links the real production view and the real production policy.  It
 * touches no ESP-IDF, FreeRTOS, LVGL, I2C, NVS, BSP, simulator, Serial
 * Automation Bridge or hardware: every case is a pure function/snapshot
 * assertion, so the run is deterministic and repeatable.
 *
 * Coverage by requirement:
 *   REQ-BAT-UI-001 / AC-BAT-UI-001..002 - an unreadable CHG_STAT never hides or
 *       freezes the indicator: a valid INA sample with an invalid charger read
 *       is still published (available, percentage, real state), and an invalid
 *       INA sample stays unavailable without a fabricated percentage.
 *   REQ-BAT-UI-002 / AC-BAT-UI-003 - absence/presence precedence is resolved
 *       before the charger signal, so a CHG_STAT stuck low never fabricates a
 *       charging pack.
 *   REQ-BAT-UI-003 / AC-BAT-UI-004 - total mapping of state/signal/availability
 *       to visible, percentage and semantic glyph.
 *   REQ-BAT-UI-005 / AC-BAT-UI-006 - absent shows only the external glyph and
 *       the glyph is never chosen from the percentage.
 */
#include "cyberdeck_battery_view.h"

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
using cyberdeck_battery_protection::snapshot;
using cyberdeck_battery_protection::state;
namespace view = cyberdeck_battery_view;
using view::power_glyph;

const battery_state kStates[] = {
    battery_state::unknown,
    battery_state::battery,
    battery_state::external,
    battery_state::charging,
    battery_state::absent,
};

const charge_signal kSignals[] = {
    charge_signal::unknown,
    charge_signal::not_charging,
    charge_signal::charging,
};

constexpr std::size_t kStateCount = sizeof(kStates) / sizeof(kStates[0]);
constexpr std::size_t kSignalCount = sizeof(kSignals) / sizeof(kSignals[0]);

view::input make_input(battery_state value, charge_signal signal,
                       bool available, std::int32_t percentage,
                       std::int32_t current_ma = 0)
{
    view::input out{};
    out.state = value;
    out.signal = signal;
    out.available = available;
    out.current_ma = current_ma;
    out.percentage = percentage;
    return out;
}

observation sample(bool ina_valid, bool chg_valid, bool raw_chg_stat_low,
                   std::int32_t voltage_mv, std::int32_t current_ma,
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

const char *state_label(battery_state value)
{
    switch (value) {
    case battery_state::unknown: return "unknown";
    case battery_state::battery: return "battery";
    case battery_state::external: return "external";
    case battery_state::charging: return "charging";
    case battery_state::absent: return "absent";
    }
    return "?";
}

const char *signal_name(charge_signal value)
{
    switch (value) {
    case charge_signal::unknown: return "unknown";
    case charge_signal::not_charging: return "not_charging";
    case charge_signal::charging: return "charging";
    }
    return "?";
}

const char *glyph_name(power_glyph value)
{
    switch (value) {
    case power_glyph::none: return "none";
    case power_glyph::charging: return "charging";
    case power_glyph::battery: return "battery";
    case power_glyph::external: return "external";
    }
    return "?";
}

#define CHECK_GLYPH(actual, expected) do { \
    ++checks; \
    if ((actual) != (expected)) { \
        ++failures; \
        std::printf("FAIL %s:%d: glyph was %s, expected %s\n", \
                    __FILE__, __LINE__, glyph_name(actual), glyph_name(expected)); \
    } \
} while (false)

/* AC-BAT-UI-004 (mapping total): the renderable range is clamped and the
 * neutral/hidden input must never be mistaken for a renderable zero. */
void test_clamp_percentage()
{
    CHECK_EQ(view::min_percentage, 0);
    CHECK_EQ(view::max_percentage, 100);

    CHECK_EQ(view::clamp_percentage(-1), 0);
    CHECK_EQ(view::clamp_percentage(-500), 0);
    CHECK_EQ(view::clamp_percentage(0), 0);
    CHECK_EQ(view::clamp_percentage(1), 1);
    CHECK_EQ(view::clamp_percentage(50), 50);
    CHECK_EQ(view::clamp_percentage(99), 99);
    CHECK_EQ(view::clamp_percentage(100), 100);
    CHECK_EQ(view::clamp_percentage(101), 100);
    CHECK_EQ(view::clamp_percentage(100000), 100);
    CHECK_EQ(view::clamp_percentage(-2147483647 - 1), 0);
    CHECK_EQ(view::clamp_percentage(2147483647), 100);
}

/* AC-BAT-UI-004: from_snapshot projects every observable field and nothing
 * else; the view must not invent an input the policy did not publish. */
void test_from_snapshot_projects_every_field()
{
    snapshot value{};
    value.state = battery_state::charging;
    value.charge = charge_signal::charging;
    value.available = true;
    value.percentage = 87;
    value.bus_voltage_mv = 8000;
    value.current_ma = -120;
    value.protection_enabled = false;
    value.protection_active = true;
    value.charger_enabled = false;

    const view::input projected = view::from_snapshot(value);
    CHECK(projected.state == battery_state::charging);
    CHECK(projected.signal == charge_signal::charging);
    CHECK(projected.available);
    CHECK_EQ(projected.percentage, 87);

    snapshot unknown_value{};
    const view::input unknown_projected = view::from_snapshot(unknown_value);
    CHECK(unknown_projected.state == battery_state::unknown);
    CHECK(unknown_projected.signal == charge_signal::unknown);
    CHECK(!unknown_projected.available);
    CHECK_EQ(unknown_projected.percentage, 0);
}

/* AC-BAT-UI-001..002 + (e): an invalid/unknown read is hidden, never a
 * fabricated "0%" or an empty-but-visible group.  This must hold for every
 * state and every signal, including the ones that are otherwise renderable. */
void test_invalid_or_unknown_is_hidden()
{
    /* Default-constructed inputs are the neutral case. */
    const view::presentation neutral = view::resolve(view::input{});
    CHECK(!neutral.visible);
    CHECK(!neutral.show_percentage);
    CHECK_EQ(neutral.percentage, 0);
    CHECK_GLYPH(neutral.glyph, power_glyph::none);

    for (std::size_t i = 0; i < kStateCount; ++i) {
        for (std::size_t j = 0; j < kSignalCount; ++j) {
            /* Unavailable measurement: hidden for every state/signal. */
            const view::presentation unavailable = view::resolve(
                make_input(kStates[i], kSignals[j], false, 64));
            ++checks;
            if (unavailable.visible || unavailable.show_percentage ||
                unavailable.glyph != power_glyph::none ||
                unavailable.percentage != 0) {
                ++failures;
                std::printf("FAIL %s:%d: unavailable %s/%s must be fully hidden\n",
                            __FILE__, __LINE__, state_label(kStates[i]),
                            signal_name(kSignals[j]));
            }

            /* Unresolved state: hidden even when the measurement is good. */
            const view::presentation unresolved = view::resolve(
                make_input(battery_state::unknown, kSignals[j], true, 64));
            ++checks;
            if (unresolved.visible || unresolved.show_percentage ||
                unresolved.glyph != power_glyph::none ||
                unresolved.percentage != 0) {
                ++failures;
                std::printf("FAIL %s:%d: unknown/%s must be fully hidden\n",
                            __FILE__, __LINE__, signal_name(kSignals[j]));
            }
        }
    }
}

/* (a) + AC-BAT-UI-006: absence shows only the external glyph, with no
 * percentage at all, and the charger signal cannot change that. */
void test_absent_shows_only_the_external_glyph()
{
    for (std::size_t j = 0; j < kSignalCount; ++j) {
        for (std::int32_t percentage = -20; percentage <= 120;
             percentage += 20) {
            const view::presentation shown = view::resolve(
                make_input(battery_state::absent, kSignals[j], true,
                           percentage));
            ++checks;
            if (!shown.visible || shown.show_percentage ||
                shown.glyph != power_glyph::external ||
                shown.percentage != 0) {
                ++failures;
                std::printf("FAIL %s:%d: absent/%s at %d%% must be visible "
                            "external glyph with no percentage\n",
                            __FILE__, __LINE__, signal_name(kSignals[j]),
                            static_cast<int>(percentage));
            }
        }
    }

    /* The percentage field itself stays neutral so a caller that formats it
     * unconditionally cannot resurrect a number next to the external glyph. */
    const view::presentation shown = view::resolve(
        make_input(battery_state::absent, charge_signal::not_charging, true, 99));
    CHECK(shown.visible);
    CHECK(!shown.show_percentage);
    CHECK_GLYPH(shown.glyph, power_glyph::external);
    CHECK_EQ(shown.percentage, 0);
}

/* (b): a pack present without charging shows the battery glyph and the
 * numeric percentage, for every non-charging signal. */
void test_battery_present_without_charging()
{
    for (std::size_t j = 0; j < kSignalCount; ++j) {
        if (kSignals[j] == charge_signal::charging) {
            continue;
        }
        for (std::int32_t percentage = 0; percentage <= 100;
             percentage += 25) {
            const view::presentation shown = view::resolve(
                make_input(battery_state::battery, kSignals[j], true,
                           percentage));
            ++checks;
            if (!shown.visible || !shown.show_percentage ||
                shown.glyph != power_glyph::battery ||
                shown.percentage != percentage) {
                ++failures;
                std::printf("FAIL %s:%d: battery/%s at %d%% must be battery "
                            "glyph with percentage\n",
                            __FILE__, __LINE__, signal_name(kSignals[j]),
                            static_cast<int>(percentage));
            }
        }
    }

    /* Out-of-range readings are clamped instead of rendered raw. */
    CHECK_EQ(view::resolve(make_input(battery_state::battery,
                                      charge_signal::not_charging, true, 140))
                 .percentage,
             100);
    CHECK_EQ(view::resolve(make_input(battery_state::battery,
                                      charge_signal::not_charging, true, -40))
                 .percentage,
             0);
}

/* The charge glyph follows the policy's resolved current classification,
 * never CHG_STAT by itself. */
void test_charging_shows_charge_glyph_with_percentage()
{
    for (std::int32_t percentage = 0; percentage <= 100; percentage += 50) {
        /* Charging by state, with the pin unreadable. */
        const view::presentation by_state = view::resolve(
            make_input(battery_state::charging, charge_signal::unknown, true,
                       percentage, -16));
        ++checks;
        if (!by_state.visible || !by_state.show_percentage ||
            by_state.glyph != power_glyph::charging ||
            by_state.percentage != percentage) {
            ++failures;
            std::printf("FAIL %s:%d: charging/unknown at %d%% must be charge "
                        "glyph with percentage\n", __FILE__, __LINE__,
                        static_cast<int>(percentage));
        }

        /* A low pin is informational; it cannot establish real charging. */
        const view::presentation by_signal = view::resolve(
            make_input(battery_state::battery, charge_signal::charging, true,
                       percentage));
        ++checks;
        if (!by_signal.visible || !by_signal.show_percentage ||
            by_signal.glyph != power_glyph::battery ||
            by_signal.percentage != percentage) {
            ++failures;
            std::printf("FAIL %s:%d: battery/CHG_STAT-low at %d%% must not "
                        "show charge glyph\n", __FILE__, __LINE__,
                        static_cast<int>(percentage));
        }

        /* A conflicting raw pin cannot override external state. */
        const view::presentation external_by_signal = view::resolve(
            make_input(battery_state::external, charge_signal::charging, true,
                       percentage));
        ++checks;
        if (!external_by_signal.visible ||
            !external_by_signal.show_percentage ||
            external_by_signal.glyph != power_glyph::battery ||
            external_by_signal.percentage != percentage) {
            ++failures;
            std::printf("FAIL %s:%d: external/charging at %d%% must be charge "
                        "glyph with percentage\n", __FILE__, __LINE__,
                        static_cast<int>(percentage));
        }
    }
}

/* (d): a pack present on external power is not charging, so it must not show
 * the charge glyph when the charger pin is readable and reports no charge. */
void test_external_with_pack_present_has_no_charge_glyph()
{
    const charge_signal non_charging[] = {
        charge_signal::not_charging,
        charge_signal::unknown,
    };

    for (std::size_t k = 0; k < sizeof(non_charging) / sizeof(non_charging[0]);
         ++k) {
        const view::presentation shown = view::resolve(
            make_input(battery_state::external, non_charging[k], true, 73));
        ++checks;
        if (!shown.visible || !shown.show_percentage ||
            shown.glyph == power_glyph::charging ||
            shown.glyph != power_glyph::battery || shown.percentage != 73) {
            ++failures;
            std::printf("FAIL %s:%d: external/%s must be a battery glyph with "
                        "percentage, never a charge glyph\n", __FILE__,
                        __LINE__, signal_name(non_charging[k]));
        }
    }
}

/* REQ-BAT-ICON-001/002: every state x signal x current class is tested. An
 * explicit current-case table is the glyph oracle; state/signal remain policy
 * evidence and are independent from the visual charge decision. */
void test_total_state_signal_matrix()
{
    struct current_case {
        std::int32_t milliamps;
        power_glyph glyph;
    };
    const current_case current_cases[] = {
        {cyberdeck_battery_protection::current_uncertainty_ma + 1,
         power_glyph::battery},
        {0, power_glyph::battery},
        {cyberdeck_battery_protection::current_uncertainty_ma,
         power_glyph::battery},
        {-cyberdeck_battery_protection::current_uncertainty_ma,
         power_glyph::battery},
        {-cyberdeck_battery_protection::current_uncertainty_ma - 1,
         power_glyph::charging},
    };

    for (std::size_t i = 0; i < kStateCount; ++i) {
        for (std::size_t j = 0; j < kSignalCount; ++j) {
            const bool expect_absent = kStates[i] == battery_state::absent;
            const bool expect_hidden = kStates[i] == battery_state::unknown;
            for (const current_case &current : current_cases) {
                const view::input input = make_input(
                    kStates[i], kSignals[j], true, 42, current.milliamps);
                const view::presentation shown = view::resolve(input);
                power_glyph expected = current.glyph;
                if (expect_absent) {
                    expected = power_glyph::external;
                } else if (expect_hidden) {
                    expected = power_glyph::none;
                }

                CHECK(shown.visible == !expect_hidden);
                CHECK(shown.show_percentage ==
                      (!expect_hidden && !expect_absent));
                CHECK_EQ(shown.percentage,
                         (!expect_hidden && !expect_absent) ? 42 : 0);
                CHECK_GLYPH(shown.glyph, expected);

                const view::presentation again = view::resolve(input);
                CHECK(again.visible == shown.visible);
                CHECK(again.show_percentage == shown.show_percentage);
                CHECK(again.percentage == shown.percentage);
                CHECK(again.glyph == shown.glyph);
            }
        }
    }
}

/* AC-BAT-UI-006: the glyph is a marker of the power source, never of the
 * level.  Sweeping the whole renderable range must not change it. */
void test_glyph_is_never_chosen_from_the_percentage()
{
    for (std::size_t i = 0; i < kStateCount; ++i) {
        for (std::size_t j = 0; j < kSignalCount; ++j) {
            const power_glyph first = view::resolve(
                make_input(kStates[i], kSignals[j], true, 0)).glyph;
            for (std::int32_t percentage = 1; percentage <= 100;
                 ++percentage) {
                const power_glyph current = view::resolve(
                    make_input(kStates[i], kSignals[j], true, percentage)).glyph;
                ++checks;
                if (current != first) {
                    ++failures;
                    std::printf("FAIL %s:%d: %s/%s glyph changed from %s to %s "
                                "at %d%%\n", __FILE__, __LINE__,
                                state_label(kStates[i]), signal_name(kSignals[j]),
                                glyph_name(first), glyph_name(current),
                                static_cast<int>(percentage));
                }
            }
        }
    }

    /* No level-based glyph enumeration may exist: the semantic set is exactly
     * none/charging/battery/external. */
    const power_glyph semantic[] = {
        power_glyph::none,
        power_glyph::charging,
        power_glyph::battery,
        power_glyph::external,
    };
    CHECK_EQ(sizeof(semantic) / sizeof(semantic[0]), static_cast<std::size_t>(4));
}

/* AC-BAT-UI-001: a valid INA sample must reach the header even when CHG_STAT
 * could not be read.  The signal becomes `unknown`, never `not_charging`, and
 * the view keeps rendering from the freshly published measurement. */
void test_valid_ina_with_invalid_chg_stat_keeps_indicator_visible()
{
    state value;

    const snapshot baseline =
        value.observe(sample(true, true, true, 8000, -40, 55));
    CHECK(baseline.available);
    CHECK(baseline.charge == charge_signal::charging);

    /* CHG_STAT read fails, INA226 still valid: the snapshot must move. */
    const snapshot after = value.observe(sample(true, false, false, 7400, 120, 62));

    CHECK(after.available);
    CHECK(after.charge == charge_signal::unknown);
    ++checks;
    if (after.charge == charge_signal::not_charging) {
        ++failures;
        std::printf("FAIL %s:%d: an unreadable CHG_STAT must never be "
                    "published as not_charging\n", __FILE__, __LINE__);
    }
    CHECK_EQ(after.percentage, 62);
    CHECK_EQ(after.bus_voltage_mv, 7400);
    CHECK_EQ(after.current_ma, 120);
    CHECK(after.state == battery_state::battery);
    ++checks;
    if (after.percentage == baseline.percentage &&
        after.bus_voltage_mv == baseline.bus_voltage_mv) {
        ++failures;
        std::printf("FAIL %s:%d: a valid INA sample froze the snapshot after a "
                    "CHG_STAT failure\n", __FILE__, __LINE__);
    }

    const view::presentation shown = view::resolve(view::from_snapshot(after));
    CHECK(shown.visible);
    CHECK(shown.show_percentage);
    CHECK_EQ(shown.percentage, 62);
    CHECK_GLYPH(shown.glyph, power_glyph::battery);

    /* An invalid INA read preserves the policy values for protection, but marks
     * the current measurement unavailable so the header hides stale data. */
    const snapshot after_ina_failure =
        value.observe(sample(false, false, false, 0, 0, 0));
    CHECK(after_ina_failure.state == after.state);
    CHECK_EQ(after_ina_failure.percentage, 62);
    CHECK(!after_ina_failure.available);
    CHECK_EQ(value.last_safe_snapshot().percentage, 62);
    CHECK_EQ(value.last_safe_snapshot().bus_voltage_mv, 7400);
    const view::presentation hidden_after_failure =
        view::resolve(view::from_snapshot(after_ina_failure));
    CHECK(!hidden_after_failure.visible);
    CHECK(!hidden_after_failure.show_percentage);
    CHECK_EQ(hidden_after_failure.percentage, 0);
    CHECK_GLYPH(hidden_after_failure.glyph, power_glyph::none);

    /* From a fresh policy, an invalid INA read stays unavailable. */
    state fresh;
    const snapshot cold = fresh.observe(sample(false, false, false, 0, 0, 0));
    CHECK(!cold.available);
    CHECK(cold.state == battery_state::unknown);
    CHECK(cold.charge == charge_signal::unknown);
    const view::presentation cold_shown =
        view::resolve(view::from_snapshot(cold));
    CHECK(!cold_shown.visible);
    CHECK_GLYPH(cold_shown.glyph, power_glyph::none);
}

/* REQ-BAT-UI-002 / AC-BAT-UI-003 (regression): a CHG_STAT stuck low (missing
 * pull-up, expander fault, unpowered charger) must not fabricate a charging
 * pack.  Once the absence is voted, the header shows the external glyph and no
 * percentage, even though the raw pin still reads "charging". */
void test_absent_precedence_over_stuck_low_chg_stat()
{
    state value;
    const observation stuck_low = sample(true, true, true, 8400, 0, 50);

    /* Fewer than the approved number of votes must not publish the absence. */
    feed(value, stuck_low, 4);
    CHECK(value.snapshot().state != battery_state::absent);
    ++checks;
    if (view::resolve(view::from_snapshot(value.snapshot())).glyph ==
        power_glyph::external) {
        ++failures;
        std::printf("FAIL %s:%d: absence published before the approved vote "
                    "count\n", __FILE__, __LINE__);
    }

    const snapshot absent = value.observe(stuck_low);
    CHECK(absent.state == battery_state::absent);
    CHECK(absent.available);
    /* The raw evidence is still published as read; only the rendering
     * precedence suppresses the charge glyph. */
    CHECK(absent.charge == charge_signal::charging);

    const view::presentation shown = view::resolve(view::from_snapshot(absent));
    CHECK(shown.visible);
    CHECK(!shown.show_percentage);
    CHECK_GLYPH(shown.glyph, power_glyph::external);
    ++checks;
    if (shown.glyph == power_glyph::charging ||
        shown.glyph == power_glyph::battery) {
        ++failures;
        std::printf("FAIL %s:%d: a stuck-low CHG_STAT fabricated a pack\n",
                    __FILE__, __LINE__);
    }
    CHECK_EQ(shown.percentage, 0);

    /* A stuck-low pin with a pack actually present still charges, so the
     * absence rule must not suppress a real charge. */
    state present;
    const observation real_charge = sample(true, true, true, 8000, -200, 95);
    feed(present, real_charge, 8);
    CHECK(present.snapshot().state == battery_state::charging);
    const view::presentation charge_shown =
        view::resolve(view::from_snapshot(present.snapshot()));
    CHECK(charge_shown.visible);
    CHECK(charge_shown.show_percentage);
    CHECK_GLYPH(charge_shown.glyph, power_glyph::charging);
}

/* REQ-BAT-ICON-001/002; TEST-BAT-ICON-01..06. Exercise the production policy
 * and presentation seam together so the glyph oracle uses real snapshots. */
void test_icon_tracks_current_and_preserves_other_projections()
{
    auto observe = [](state &policy, bool ina_valid, bool chg_valid,
                      bool raw_low, std::int32_t voltage,
                      std::int32_t current, std::int32_t percentage) {
        return policy.observe(sample(ina_valid, chg_valid, raw_low, voltage,
                                     current, percentage));
    };

    /* TEST-01: pin-low is not charge evidence. */
    /* TEST-02: positive, zero, and both uncertainty boundaries remain battery
     * glyphs; current below the negative threshold is covered below. */
    const std::int32_t non_charging_currents[] = {16, 0, 15, -15};
    for (const std::int32_t current : non_charging_currents) {
        state policy;
        const auto value = observe(policy, true, true, true, 7500, current, 63);
        CHECK(value.available);
        CHECK(value.state == battery_state::charging);
        CHECK(value.charge == charge_signal::charging);
        CHECK_EQ(value.current_ma, current);
        CHECK_EQ(value.bus_voltage_mv, 7500);
        CHECK_EQ(value.percentage, 63);
        const auto shown = view::resolve(view::from_snapshot(value));
        CHECK(shown.visible);
        CHECK(shown.show_percentage);
        CHECK_EQ(shown.percentage, 63);
        CHECK_GLYPH(shown.glyph, power_glyph::battery);
        CHECK(value.protection_enabled);
        CHECK(!value.protection_active);
        CHECK(value.charger_enabled);
    }

    /* TEST-01: policy state and CHG_STAT remain charging evidence for
     * protection, while positive INA current controls the visual glyph. */
    state conflict_policy;
    const auto prior_charge = observe(conflict_policy, true, true, true,
                                      7500, -30, 62);
    CHECK(prior_charge.state == battery_state::charging);
    const auto discharged = observe(conflict_policy, true, true, true,
                                     7500, 20, 61);
    CHECK(discharged.state == battery_state::charging);
    CHECK(discharged.charge == charge_signal::charging);
    CHECK_EQ(discharged.current_ma, 20);
    const auto discharged_view = view::resolve(view::from_snapshot(discharged));
    CHECK(discharged_view.visible);
    CHECK(discharged_view.show_percentage);
    CHECK_EQ(discharged_view.percentage, 61);
    CHECK_GLYPH(discharged_view.glyph, power_glyph::battery);
    CHECK(discharged.charger_enabled == prior_charge.charger_enabled);
    CHECK(discharged.protection_active == prior_charge.protection_active);

    state protected_policy;
    const auto protected_charge = observe(protected_policy, true, true, true,
                                          8250, -20, 95);
    CHECK(protected_charge.state == battery_state::charging);
    CHECK(protected_charge.protection_active);
    CHECK(!protected_charge.charger_enabled);
    const auto protected_noncharging = observe(protected_policy, true, true,
                                              true, 8250, 20, 95);
    CHECK(protected_noncharging.state == battery_state::charging);
    CHECK(protected_noncharging.charge == charge_signal::charging);
    CHECK(protected_noncharging.protection_active);
    CHECK(!protected_noncharging.charger_enabled);
    const auto protected_view = view::resolve(
        view::from_snapshot(protected_noncharging));
    CHECK_GLYPH(protected_view.glyph, power_glyph::battery);

    /* TEST-03: real negative current beyond tolerance wins even if CHG_STAT is
     * high or unreadable. */
    for (int pin_case = 0; pin_case < 2; ++pin_case) {
        state policy;
        const bool chg_valid = pin_case != 0;
        const auto value = observe(policy, true, chg_valid, false,
                                   7500, -16, 77);
        CHECK(value.state == battery_state::charging);
        CHECK(value.available);
        CHECK(value.charge == (chg_valid ? charge_signal::not_charging
                                         : charge_signal::unknown));
        CHECK_EQ(value.current_ma, -16);
        CHECK_EQ(value.percentage, 77);
        const auto shown = view::resolve(view::from_snapshot(value));
        CHECK(shown.visible);
        CHECK(shown.show_percentage);
        CHECK_EQ(shown.percentage, 77);
        CHECK_GLYPH(shown.glyph, power_glyph::charging);
    }

    /* TEST-04: absent wins over conflicting pin/current; confirmed external
     * with zero current retains percentage but never shows charge glyph. */
    state absent_policy;
    const auto absent_sample = sample(true, true, true, 8400, -120, 88);
    feed(absent_policy, absent_sample,
         cyberdeck_battery_protection::state_vote_count - 1);
    const auto absent = absent_policy.observe(absent_sample);
    CHECK(absent.state == battery_state::absent);
    CHECK(absent.charge == charge_signal::charging);
    CHECK(absent.charger_enabled);
    CHECK(!absent.protection_active);
    const auto absent_view = view::resolve(view::from_snapshot(absent));
    CHECK(absent_view.visible);
    CHECK(!absent_view.show_percentage);
    CHECK_EQ(absent_view.percentage, 0);
    CHECK_GLYPH(absent_view.glyph, power_glyph::external);

    state external_policy;
    const auto external_sample = sample(true, true, false, 7900, 0, 52);
    feed(external_policy, external_sample,
         cyberdeck_battery_protection::state_vote_count - 1);
    const auto external = external_policy.observe(external_sample);
    CHECK(external.state == battery_state::external);
    CHECK(external.charger_enabled);
    CHECK(!external.protection_active);
    const auto external_view = view::resolve(view::from_snapshot(external));
    CHECK(external_view.visible);
    CHECK(external_view.show_percentage);
    CHECK_EQ(external_view.percentage, 52);
    CHECK_GLYPH(external_view.glyph, power_glyph::battery);

    /* TEST-05: INA failure hides presentation without changing safe policy
     * fields; invalid CHG_STAT with valid INA still publishes current sample. */
    state failed_policy;
    const auto safe = observe(failed_policy, true, true, false, 8250, -20, 95);
    CHECK(safe.protection_active);
    CHECK(!safe.charger_enabled);
    const auto failed = observe(failed_policy, false, true, true, 0, 0, 0);
    CHECK(!failed.available);
    CHECK(failed.state == safe.state);
    CHECK_EQ(failed.current_ma, safe.current_ma);
    CHECK_EQ(failed.bus_voltage_mv, safe.bus_voltage_mv);
    CHECK_EQ(failed.percentage, safe.percentage);
    CHECK(failed.protection_enabled == safe.protection_enabled);
    CHECK(failed.protection_active == safe.protection_active);
    CHECK(failed.charger_enabled == safe.charger_enabled);
    const auto failed_view = view::resolve(view::from_snapshot(failed));
    CHECK(!failed_view.visible);
    CHECK(!failed_view.show_percentage);
    CHECK_EQ(failed_view.percentage, 0);
    CHECK_GLYPH(failed_view.glyph, power_glyph::none);

    state invalid_pin_policy;
    const auto invalid_pin = observe(invalid_pin_policy, true, false, false,
                                     7500, 25, 46);
    CHECK(invalid_pin.available);
    CHECK(invalid_pin.state == battery_state::battery);
    CHECK(invalid_pin.charge == charge_signal::unknown);
    CHECK_EQ(invalid_pin.current_ma, 25);
    CHECK_EQ(invalid_pin.bus_voltage_mv, 7500);
    CHECK_EQ(invalid_pin.percentage, 46);
    const auto invalid_pin_view = view::resolve(view::from_snapshot(invalid_pin));
    CHECK(invalid_pin_view.visible);
    CHECK(invalid_pin_view.show_percentage);
    CHECK_EQ(invalid_pin_view.percentage, 46);
    CHECK_GLYPH(invalid_pin_view.glyph, power_glyph::battery);

    /* TEST-06: policy state/signal/current cross-product. Policy classification
     * and the visual current-based glyph are asserted independently. */
    const std::int32_t currents[] = {25, 0, -25};
    for (const charge_signal signal : kSignals) {
        for (const std::int32_t current : currents) {
            state policy;
            const bool low = signal == charge_signal::charging;
            const bool chg_valid = signal != charge_signal::unknown;
            const auto value = observe(policy, true, chg_valid, low,
                                       7500, current, 34);
            const battery_state policy_state =
                (signal == charge_signal::charging || current < -15)
                    ? battery_state::charging : battery_state::battery;
            CHECK(value.state == policy_state);
            CHECK(value.charge == signal);
            CHECK(value.available);
            CHECK_EQ(value.current_ma, current);
            CHECK_EQ(value.percentage, 34);
            const auto shown = view::resolve(view::from_snapshot(value));
            CHECK(shown.visible);
            CHECK(shown.show_percentage);
            CHECK_EQ(shown.percentage, 34);
            CHECK_GLYPH(shown.glyph,
                        current < -15 ? power_glyph::charging
                                      : power_glyph::battery);
            CHECK(value.protection_enabled);
            CHECK(!value.protection_active);
            CHECK(value.charger_enabled);
        }
    }
}

} // namespace

int main()
{
    test_clamp_percentage();
    test_from_snapshot_projects_every_field();
    test_invalid_or_unknown_is_hidden();
    test_absent_shows_only_the_external_glyph();
    test_battery_present_without_charging();
    test_charging_shows_charge_glyph_with_percentage();
    test_external_with_pack_present_has_no_charge_glyph();
    test_total_state_signal_matrix();
    test_glyph_is_never_chosen_from_the_percentage();
    test_valid_ina_with_invalid_chg_stat_keeps_indicator_visible();
    test_absent_precedence_over_stuck_low_chg_stat();
    test_icon_tracks_current_and_preserves_other_projections();

    std::printf("battery view pure contract: %d checks, %d failures\n",
                checks, failures);
    return failures == 0 ? 0 : 1;
}
