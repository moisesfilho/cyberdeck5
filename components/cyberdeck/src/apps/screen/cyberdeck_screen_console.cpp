#include "apps/screen/cyberdeck_screen_console.h"

#include "apps/console/cyberdeck_console_args.h"

namespace cyberdeck_screen_console {
namespace {

constexpr std::string_view kUnknownCommand = "unknown command; type help\n";

} // namespace

std::string execute(port &io, std::string_view args)
{
    const std::string normalized = cyberdeck_console::collapse_spaces(cyberdeck_console::strip(args));
    const std::string_view verb(normalized);

    if (verb == "on") {
        io.turn_on();
        return "screen on\n";
    }
    if (verb == "off") {
        io.turn_off();
        return "screen off\n";
    }
    if (verb.compare(0, 7, "timeout") == 0 && (verb.size() == 7 || verb[7] == ' ')) {
        const std::string_view value = verb.size() == 7 ? std::string_view{} : verb.substr(8);
        std::uint16_t minutes = 0;
        if (cyberdeck_screen_protection::parse_timeout_minutes(std::string(value).c_str(), minutes) !=
            cyberdeck_screen_protection::timeout_parse_result::ok)
            return "screen timeout: expected an integer from 0 to 1440\n";
        if (!io.set_timeout_minutes(minutes)) return "screen timeout: unable to persist setting\n";
        if (minutes == 0) return "screen timeout disabled (0 minutes)\n";
        return "screen timeout set to " + std::to_string(minutes) + " minutes\n";
    }
    return std::string(kUnknownCommand);
}

} // namespace cyberdeck_screen_console
