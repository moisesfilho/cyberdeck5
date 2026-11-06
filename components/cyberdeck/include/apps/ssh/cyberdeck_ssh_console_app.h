#pragma once

#include "apps/runtime/cyberdeck_app_runtime.h"
#include "apps/ssh/cyberdeck_ssh_console.h"

namespace cyberdeck_ssh_console_app {

/* `ssh` as a compiled application.  Modes, the composer, the output filter and
 * the host-key UI remain in the UI composition behind the port. */
class application final : public cyberdeck_apps::application {
public:
    const cyberdeck_apps::manifest &get_manifest() const override { return k_manifest; }
    bool start() override
    {
        running_ = true;
        return true;
    }
    bool stop() override
    {
        running_ = false;
        return true;
    }
    bool running() const override { return running_; }

    void attach(cyberdeck_ssh_console::port &io) { port_ = &io; }

    cyberdeck_apps::result execute(std::string_view, std::string_view args) override
    {
        if (port_ == nullptr) return {cyberdeck_apps::result_status::rejected, "ssh: console unavailable\n"};
        return {cyberdeck_apps::result_status::handled, cyberdeck_ssh_console::execute(*port_, args)};
    }

private:
    static constexpr cyberdeck_apps::manifest k_manifest{
        "cyberdeck.ssh.shell",
        "SSH console",
        "1.0.0",
        "start an SSH session",
        "",
        {},
        0,
        {"network", "storage"},
        2,
        1000,
        "1",
        cyberdeck_apps::app_type::service,
        {},
        0,
        0,
        0,
        {"ssh"},
        1,
        false,
        {{{"ssh [user@]host[:port]", "[user@]: optional login name\nhost: remote host name or address\n[:port]: optional port (default 22)\n"}}},
    };

    bool running_ = false;
    cyberdeck_ssh_console::port *port_ = nullptr;
};

inline application &global_application()
{
    static application instance;
    return instance;
}

} // namespace cyberdeck_ssh_console_app
