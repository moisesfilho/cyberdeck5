#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>

#ifdef __cplusplus
extern "C" {
#endif

namespace cyberdeck_battery_protection {

inline constexpr std::int32_t current_uncertainty_ma = 15;
inline constexpr std::int32_t external_voltage_mv = 7900;
inline constexpr std::int32_t absent_voltage_mv = 8330;
inline constexpr std::uint8_t state_vote_count = 5;
/* Consecutive samples without an external source (battery/absent) that release
 * the charger latch. */
inline constexpr std::uint8_t no_source_vote_count = 5;
inline constexpr std::int32_t protection_enter_voltage_mv = 8200;
/* Latch release threshold on the estimated state of charge.  It follows the
 * approved percentage scale: 90 % of the 2100 mV window is ~7890 mV, which
 * preserves the ~210 mV release band of the previous policy (85 % of
 * 6000..8230 mV = 7895 mV).  The physical band is what matters; this value
 * tracks the scale whenever the scale changes. */
inline constexpr std::int32_t protection_exit_percentage = 90;
inline constexpr bool default_protection_enabled = true;

enum class battery_state {
    unknown,
    battery,
    external,
    charging,
    absent,
};

enum class charge_signal {
    unknown,
    not_charging,
    charging,
};

struct observation {
    bool ina_valid{false};
    bool chg_valid{false};
    bool raw_chg_stat_low{false};
    std::int32_t bus_voltage_mv{0};
    std::int32_t current_ma{0};
    /* Time since the previous valid INA226 sample; 0 means no measurable
     * interval and the state-of-charge estimate is not integrated. */
    std::uint32_t elapsed_ms{0};
};

struct snapshot {
    battery_state state{battery_state::unknown};
    /* Last decoded CHG_STAT evidence.  A failed read is `unknown`, never
     * `not_charging`, so consumers cannot read a missing reading as a
     * negative charger answer. */
    charge_signal charge{charge_signal::unknown};
    bool available{false};
    /* Estimated state of charge: coulomb counting anchored to the
     * open-circuit voltage, never a direct reading of the terminal voltage. */
    std::int32_t percentage{0};
    std::int32_t bus_voltage_mv{0};
    std::int32_t current_ma{0};
    bool protection_enabled{true};
    bool protection_active{false};
    bool charger_enabled{true};
};

struct hardware_diagnostics {
    std::int64_t captured_at_us{0};
    std::int64_t policy_sampled_at_us{0};
    bool chg_stat_available{false};
    bool chg_stat_raw_low{false};
    std::int64_t chg_stat_sampled_at_us{0};
    bool expander_available{false};
    bool expander_registers_valid{false};
    std::uint8_t expander_register_origin{0};
    std::uint8_t expander_direction{0};
    std::uint8_t expander_output_latch{0};
    std::uint8_t expander_hiz{0};
    bool chg_qc_read_ok{false};
    bool chg_qc_enabled{false};
    bool chg_en_read_ok{false};
    bool chg_en_enabled{false};
    bool ina_available{false};
    bool ina_error{false};
    std::uint16_t ina_bus_raw{0};
    std::uint16_t ina_current_raw{0};
    std::int32_t ina_bus_voltage_mv{0};
    std::int32_t ina_current_ma{0};
    std::int64_t ina_sampled_at_us{0};
};

struct persisted_option {
    bool protection_enabled{true};
};

class state {
public:
    using snapshot_t = snapshot;

    state();
    ~state();

    snapshot_t observe(const observation &value);
    snapshot_t snapshot() const;
    snapshot_t last_safe_snapshot() const;

    bool set_protection_enabled(bool enabled);
    bool restore_protection_enabled(bool enabled);
    bool protection_enabled() const;
    bool protection_active() const;
    bool charger_enabled() const;
    persisted_option persistence_view() const;

private:
    struct impl;
    std::unique_ptr<impl> pimpl_{nullptr};
};

charge_signal decode_chg_stat(bool valid, bool raw_low);
const char *state_name(battery_state value);
const char *charge_signal_name(charge_signal value);
int format_status_line(char *buffer, std::size_t capacity, const snapshot &value);

} // namespace cyberdeck_battery_protection

#ifdef __cplusplus
}
#endif
