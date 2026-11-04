/*
 * Host-only contract for the pure state-of-charge estimator.
 *
 * This is a test contract, not a production implementation.  The firmware must
 * expose the same declarations from
 * components/cyberdeck/include/platform/sensors/cyberdeck_battery_soc.h and
 * implement them in cyberdeck_battery_soc.cpp without ESP-IDF, FreeRTOS, I2C,
 * NVS, LVGL or BSP dependencies.
 *
 * The estimator keeps a coulomb counter in 1e-4 % units (remainder transported
 * between observations), seeded and re-anchored only from the open-circuit
 * voltage, and publishes at most one percentage point of movement per sample.
 * A zero elapsed interval is a neutral re-observation (nothing integrated, no
 * quiet time change, no re-anchor); an interval above max_elapsed_ms is a
 * real reading gap that integrates nothing and clears the quiet time.
 */
#pragma once

#include <cstdint>

#if defined(__has_include)
#  if __has_include("platform/sensors/cyberdeck_battery_soc.h")
#    include "platform/sensors/cyberdeck_battery_soc.h"
#    define CYBERDECK_BATTERY_SOC_HAS_PRODUCTION_HEADER 1
#  endif
#endif

#ifndef CYBERDECK_BATTERY_SOC_HAS_PRODUCTION_HEADER

namespace cyberdeck_battery_soc {

/* Approved pack capacity used to turn integrated current into a state of
 * charge.  It is part of the contract: changing it changes every vector, so it
 * is pinned by a static_assert in the host test. */
inline constexpr std::int32_t pack_capacity_mah = 2000;

/* An interval longer than this is a reading gap: it is neither integrated nor
 * re-anchored, and it clears the accumulated quiet time so a gap cannot stitch
 * the 60000 ms at-rest window together.  A zero interval is a neutral
 * re-observation of the same instant. */
inline constexpr std::uint32_t max_elapsed_ms = 60000;

/* Continuous at-rest time required before the estimate is re-anchored to the
 * open-circuit voltage. */
inline constexpr std::uint32_t reanchor_quiet_ms = 60000;
inline constexpr std::int32_t reanchor_current_tolerance_ma = 15;

/* The published state of charge moves at most this many points per
 * observation, so a re-anchor cannot jump the header value. */
inline constexpr std::int32_t max_slew_percentage_points = 1;

struct input {
    /* A real INA226 sample.  false holds the estimate and integrates nothing. */
    bool valid{false};
    /* A pack is in the bay.  false resets the estimate. */
    bool present{false};
    /* Classified charge state: never re-anchor while true. */
    bool charging{false};
    std::int32_t bus_voltage_mv{0};
    std::int32_t current_ma{0};
    /* Time since the last valid sample. */
    std::uint32_t elapsed_ms{0};
};

struct reading {
    bool seeded{false};
    std::int32_t percentage{0};
};

class state {
public:
    reading observe(const input &value);
    reading current() const;

private:
    void reset();
    void seed_from_ocv(std::int32_t ocv_mv);
    void integrate(std::int32_t current_ma, std::uint32_t elapsed_ms);
    void update_quiet(const input &value);
    void publish();

    bool seeded_{false};
    /* Charge counter in 1e-4 % units, clamped to 0..100 %. */
    std::int64_t accumulator_{0};
    /* Remainder transported between observations so sub-unit integration is
     * not lost. */
    std::int64_t remainder_{0};
    std::int32_t percentage_{0};
    std::uint32_t quiet_ms_{0};
};

} // namespace cyberdeck_battery_soc

#endif // CYBERDECK_BATTERY_SOC_HAS_PRODUCTION_HEADER
