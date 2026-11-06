#include "apps/event_log/cyberdeck_event_log_console.h"

#include "apps/console/cyberdeck_console_args.h"

namespace cyberdeck_event_log_console {
namespace {

/* Accepts `lines <1-64>`; the verb must be separated from the value and no
 * trailing token is allowed, matching the legacy session parser. */
bool parse_window(std::string_view value, std::size_t &lines)
{
    value = cyberdeck_console::strip_left(value);
    if (value.size() <= 5 || value.compare(0, 5, "lines") != 0) return false;
    if (value[5] != ' ' && value[5] != '\t') return false;

    value = cyberdeck_console::strip_left(value.substr(5));
    if (value.empty()) return false;

    std::size_t parsed = 0;
    std::size_t index = 0;
    for (; index < value.size() && value[index] >= '0' && value[index] <= '9'; ++index) {
        parsed = parsed * 10U + static_cast<std::size_t>(value[index] - '0');
        if (parsed > max_window_lines) return false;
    }
    if (index == 0 || index != value.size()) return false;
    if (parsed < min_window_lines) return false;
    lines = parsed;
    return true;
}

} // namespace

std::string execute(port &io, std::string_view args, state &current)
{
    const std::string_view rest = cyberdeck_console::strip(args);
    if (rest.empty()) {
        std::string output = "ultimos eventos:\n";
        const std::string logged = io.recent_events(current.window_lines);
        output += logged.empty() ? "(nenhum evento disponivel)\n" : logged;
        return output;
    }
    if (rest == "lines") return "log lines: " + std::to_string(current.window_lines) + "\n";

    std::size_t requested = 0;
    if (!parse_window(rest, requested)) return "usage: log [lines <1-64>]\n";
    current.window_lines = requested;
    return "log lines set to " + std::to_string(requested) + "\n";
}

} // namespace cyberdeck_event_log_console
