#include "platform/sensors/cyberdeck_battery_soc.h"

#include "platform/sensors/battery_status.h"

#include <cstdint>

namespace cyberdeck_battery_soc {

namespace {

/* 1e-4 % units per whole percentage point. */
constexpr std::int64_t percentage_unit_scale = 10000;

/* Coulomb-counting divisor: one unit (1e-4 %) is current_ma * elapsed_ms /
 * (3.6 * capacity), kept as integer math derived from the approved capacity. */
constexpr std::int64_t integration_divisor = 36LL * pack_capacity_mah / 10;

constexpr std::int64_t max_accumulator = 100 * percentage_unit_scale;

std::int32_t clamp_percentage(const std::int64_t accumulator)
{
    if (accumulator <= 0) {
        return 0;
    }
    if (accumulator >= max_accumulator) {
        return 100;
    }
    return static_cast<std::int32_t>(accumulator / percentage_unit_scale);
}

} // namespace

reading state::current() const
{
    return {seeded_, percentage_};
}

void state::reset()
{
    seeded_ = false;
    accumulator_ = 0;
    remainder_ = 0;
    percentage_ = 0;
    quiet_ms_ = 0;
    anchored_ = false;
    anchor_settled_ = false;
    anchor_votes_ = 0;
    anchor_percentage_ = 0;
    anchor_accumulator_ = 0;
}

void state::seed_from_ocv(const std::int32_t ocv_mv)
{
    const std::int32_t seeded = cyberdeck_battery::percentage_from_bus_voltage_mv(ocv_mv);
    seeded_ = true;
    accumulator_ = static_cast<std::int64_t>(seeded) * percentage_unit_scale;
    remainder_ = 0;
    percentage_ = seeded;
    quiet_ms_ = 0;
    anchored_ = false;
    anchor_settled_ = false;
    anchor_votes_ = 0;
    anchor_percentage_ = 0;
    anchor_accumulator_ = 0;
}

void state::integrate(const std::int32_t current_ma, const std::uint32_t elapsed_ms)
{
    const std::int64_t numerator =
        static_cast<std::int64_t>(current_ma) * static_cast<std::int64_t>(elapsed_ms);
    const std::int64_t combined = remainder_ + numerator;
    const std::int64_t units = combined / integration_divisor;
    remainder_ = combined % integration_divisor;
    /* Positive current is discharge, so it removes charge from the counter. */
    accumulator_ -= units;
}

void state::update_quiet(const input &value)
{
    const std::int32_t abs_current =
        value.current_ma >= 0 ? value.current_ma : -value.current_ma;
    if (value.charging || abs_current > reanchor_current_tolerance_ma) {
        /* The at-rest episode is over: a new anchor requires a fresh quiet
         * window and a fresh confirmation count. */
        quiet_ms_ = 0;
        anchored_ = false;
        anchor_settled_ = false;
        anchor_votes_ = 0;
        return;
    }
    const std::uint32_t remaining =
        reanchor_quiet_ms > quiet_ms_ ? reanchor_quiet_ms - quiet_ms_ : 0;
    if (remaining == 0) {
        return;
    }
    quiet_ms_ += value.elapsed_ms < remaining ? value.elapsed_ms : remaining;
}

/* Confirms the open-circuit target only after the approved number of
 * consecutive at-rest samples report the same point, and arms a single anchor
 * per at-rest episode.  An oscillating pack therefore never re-anchors. */
void state::update_anchor(const std::int32_t ocv_mv)
{
    if (quiet_ms_ < reanchor_quiet_ms || anchored_) {
        return;
    }
    const std::int32_t target = cyberdeck_battery::percentage_from_bus_voltage_mv(ocv_mv);
    if (anchor_votes_ > 0 && target == anchor_percentage_) {
        if (anchor_votes_ < anchor_confirmations) {
            ++anchor_votes_;
        }
    } else {
        anchor_percentage_ = target;
        anchor_votes_ = 1;
    }
    if (anchor_votes_ < anchor_confirmations) {
        return;
    }
    /* The centre of the bin keeps integration half a point away from the
     * display edge, so a single unit of charge cannot flip the header. */
    anchor_accumulator_ =
        static_cast<std::int64_t>(target) * percentage_unit_scale +
        percentage_unit_scale / 2;
    accumulator_ = anchor_accumulator_;
    anchored_ = true;
    anchor_settled_ = false;
}

/* Moves the counter one point per observation towards the confirmed anchor
 * until the published value reaches the target, then stops for the rest of the
 * episode (integration from the centre is preserved). */
void state::settle_anchor()
{
    if (!anchored_ || anchor_settled_) {
        return;
    }
    if (percentage_ == anchor_percentage_) {
        anchor_settled_ = true;
        return;
    }
    accumulator_ = anchor_accumulator_;
}

void state::publish()
{
    if (accumulator_ < 0) {
        accumulator_ = 0;
    }
    if (accumulator_ > max_accumulator) {
        accumulator_ = max_accumulator;
    }

    /* Limit the published movement to the approved slew while keeping the
     * transported remainder: only a jump larger than one point is collapsed to
     * the boundary accumulator that still maps to that one-point step. */
    const std::int32_t candidate = clamp_percentage(accumulator_);
    if (candidate > percentage_ + max_slew_percentage_points) {
        percentage_ += max_slew_percentage_points;
        accumulator_ =
            (static_cast<std::int64_t>(percentage_ + 1) * percentage_unit_scale) - 1;
        return;
    }
    if (candidate < percentage_ - max_slew_percentage_points) {
        percentage_ -= max_slew_percentage_points;
        accumulator_ = static_cast<std::int64_t>(percentage_) * percentage_unit_scale;
        return;
    }
    percentage_ = candidate;
}

reading state::observe(const input &value)
{
    if (!value.present) {
        reset();
        return current();
    }
    if (!value.valid) {
        return current();
    }

    const std::int32_t ocv = cyberdeck_battery::ocv_from_terminal_mv(
        value.bus_voltage_mv, value.current_ma);

    if (!seeded_) {
        seed_from_ocv(ocv);
        return current();
    }

    if (value.elapsed_ms == 0) {
        /* The same instant observed again (a second 1 Hz timer or an on-demand
         * status query): integrate nothing, keep the quiet window, do not
         * re-anchor. */
        publish();
        return current();
    }
    if (value.elapsed_ms > max_elapsed_ms) {
        /* A gap longer than the approved window is a real reading gap: it is
         * neither integrated nor re-anchored, and it must not be stitched into
         * the 60000 ms quiet window.  The at-rest episode restarts as well. */
        quiet_ms_ = 0;
        anchored_ = false;
        anchor_settled_ = false;
        anchor_votes_ = 0;
        publish();
        return current();
    }

    integrate(value.current_ma, value.elapsed_ms);
    update_quiet(value);
    update_anchor(ocv);
    settle_anchor();

    publish();
    return current();
}

} // namespace cyberdeck_battery_soc
