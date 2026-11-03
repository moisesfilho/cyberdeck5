#pragma once

#include "esp_err.h"

#include "platform/sensors/battery_status.h"

namespace cyberdeck_battery {
struct raw_diagnostics {
    bool valid{false};
    std::uint16_t bus_voltage_register{0};
    std::uint16_t current_register{0};
    sample converted{};
    std::int64_t sampled_at_us{0};
};
}

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ina226_reader_init(void);
esp_err_t ina226_reader_start(void);
bool ina226_reader_started(void);
bool ina226_reader_get_snapshot(cyberdeck_battery::snapshot *out_snapshot);
bool ina226_reader_get_raw_sample(cyberdeck_battery::sample *out_sample);
bool ina226_reader_get_raw_diagnostics(cyberdeck_battery::raw_diagnostics *out_diagnostics);

#ifdef __cplusplus
}
#endif
