#pragma once

#include <cstdint>

namespace cyberdeck_battery_soc {

/* Approved pack capacity used to turn integrated current into a state of
 * charge.  It is part of the contract: changing it changes every vector, so it
 * is pinned by a static_assert in the host test. */
inline constexpr std::int32_t pack_capacity_mah = 2000;

/* An interval longer than this is a reading gap: it is neither integrated nor
 * re-anchored, and it clears the accumulated quiet time so a gap cannot stitch
 * the 60000 ms at-rest window together.  A zero interval is a neutral
 * re-observation of the same instant: nothing is integrated, the quiet time is
 * neither extended nor cleared, and no re-anchor happens. */
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
    /* Time since the last valid sample.  Zero is a neutral re-observation;
     * an interval above max_elapsed_ms is a reading gap that restarts the quiet
     * window. */
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
