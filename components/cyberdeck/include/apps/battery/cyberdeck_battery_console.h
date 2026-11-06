#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "platform/sensors/cyberdeck_battery_protection.h"

namespace cyberdeck_battery_console {

/* Battery protection port.  The adapter owns the policy instance and the I2C
 * expander, so this layer only formats what the policy publishes. */
struct port {
    virtual ~port() = default;
    virtual bool protection_started() = 0;
    virtual bool set_protection(bool enabled) = 0;
    virtual bool snapshot(cyberdeck_battery_protection::snapshot &out) = 0;
    virtual bool hardware_diagnostics(cyberdeck_battery_protection::hardware_diagnostics &out) = 0;
};

/* Handles `battery protection on`, `battery protection off` and
 * `battery protection status`. */
std::string execute(port &io, std::string_view args);

} // namespace cyberdeck_battery_console
