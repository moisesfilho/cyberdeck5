#include "apps/bluetooth/cyberdeck_bluetooth_console.h"

#include "apps/console/cyberdeck_console_args.h"

namespace cyberdeck_bluetooth_console {

std::string execute(port &io, std::string_view args)
{
    const std::string_view verb = cyberdeck_console::strip(args);
    if (verb == "search") {
        io.start_search();
        return "Bluetooth search started.\n";
    }
    if (verb == "paired") {
        io.open_paired();
        return {};
    }
    return "unknown command; type help\n";
}

} // namespace cyberdeck_bluetooth_console
