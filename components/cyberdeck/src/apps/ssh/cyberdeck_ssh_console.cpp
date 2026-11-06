#include "apps/ssh/cyberdeck_ssh_console.h"

#include "apps/console/cyberdeck_console_args.h"
#include "apps/shell/cyberdeck_shell_utils.h"

namespace cyberdeck_ssh_console {

std::string execute(port &io, std::string_view args)
{
    const std::string_view target = cyberdeck_console::strip(args);
    if (target.empty()) return "usage: ssh [user@]host[:port]\n";

    std::string user;
    std::string host;
    int port_number = 22;
    if (!cyberdeck_parse_ssh_target(std::string(target).c_str(), user, host, port_number))
        return "usage: ssh [user@]host[:port]\n";

    if (!io.begin_session(user.c_str(), host.c_str(), port_number)) {
        constexpr const char *kError = "[ERROR] unable to start SSH session";
        io.log_error(kError);
        return std::string(kError) + "\n";
    }
    return {};
}

} // namespace cyberdeck_ssh_console
