#pragma once

#include <cstdint>
#include <memory>

#ifdef __cplusplus
extern "C" {
#endif

namespace cyberdeck_battery_protection {

inline constexpr std::int32_t current_uncertainty_ma = 15;
inline constexpr std::int32_t external_voltage_mv = 7900;
inline constexpr std::int32_t absent_voltage_mv = 8330;
inline constexpr std::uint8_t state_vote_count = 5;
inline constexpr std::int32_t protection_enter_percentage = 90;
inline constexpr std::int32_t protection_enter_voltage_mv = 8200;
inline constexpr std::int32_t protection_exit_percentage = 85;
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
    std::int32_t percentage{0};
};

struct snapshot {
    battery_state state{battery_state::unknown};
    bool available{false};
    std::int32_t percentage{0};
    std::int32_t bus_voltage_mv{0};
    std::int32_t current_ma{0};
    bool protection_enabled{true};
    bool protection_active{false};
    bool charger_enabled{true};
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

} // namespace cyberdeck_battery_protection

#ifdef __cplusplus
}
#endif