#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "platform/display/cyberdeck_screen_protection.h"

namespace cyberdeck_screen_console {

/* Screen protection port.  NVS persistence stays in the adapter, so this layer
 * never performs storage I/O. */
struct port {
    virtual ~port() = default;
    virtual void turn_on() = 0;
    virtual void turn_off() = 0;
    /* Returns false when the timeout could not be persisted. */
    virtual bool set_timeout_minutes(std::uint16_t minutes) = 0;
};

/* Handles `screen on`, `screen off` and `screen timeout <0-1440>`. */
std::string execute(port &io, std::string_view args);

} // namespace cyberdeck_screen_console
