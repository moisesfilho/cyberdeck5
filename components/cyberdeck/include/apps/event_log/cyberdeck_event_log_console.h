#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#ifndef CONFIG_CYBERDECK_LOG_LINES
#define CONFIG_CYBERDECK_LOG_LINES 20
#endif

namespace cyberdeck_event_log_console {

inline constexpr std::size_t min_window_lines = 1;
inline constexpr std::size_t max_window_lines = 64;
inline constexpr std::size_t default_window_lines = CONFIG_CYBERDECK_LOG_LINES;

/* Event log console port.  The host adapter owns the persistent backend, so
 * this layer never links FreeRTOS, NVS or the event log implementation. */
struct port {
    virtual ~port() = default;
    /* Bounded window, oldest first.  The caller owns the returned text. */
    virtual std::string recent_events(std::size_t count) = 0;
};

/* Session-scoped window size.  The supervisor clears it when a new console is
 * created, so the compiled default is restored for every session. */
struct state {
    std::size_t window_lines = default_window_lines;
};

/* Handles `log`, `log lines` and `log lines <1-64>` and returns the exact
 * terminal text the legacy switch used to append. */
std::string execute(port &io, std::string_view args, state &current);

} // namespace cyberdeck_event_log_console
