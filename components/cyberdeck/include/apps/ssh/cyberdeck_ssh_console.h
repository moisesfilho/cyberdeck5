#pragma once

#include <string>
#include <string_view>

namespace cyberdeck_ssh_console {

/* SSH console port.  Session modes, the line composer, the output filter and
 * the host-key UI stay in the UI composition; the port only starts a session
 * with the already parsed target. */
struct port {
    virtual ~port() = default;
    /* Resets the filter and composer, shows the SSH chrome and requests the
     * connection.  Returns false when the client rejected the request. */
    virtual bool begin_session(const char *user, const char *host, int port) = 0;
    virtual void log_error(const char *message) = 0;
};

/* Handles `ssh [user@]host[:port]`. */
std::string execute(port &io, std::string_view args);

} // namespace cyberdeck_ssh_console
