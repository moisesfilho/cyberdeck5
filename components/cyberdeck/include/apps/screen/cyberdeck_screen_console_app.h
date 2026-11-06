#pragma once

#include "apps/screen/cyberdeck_screen_console.h"
#include "apps/runtime/cyberdeck_app_runtime.h"

namespace cyberdeck_screen_console_app {

/* `screen` as a compiled application.  The adapter owns NVS and the display
 * backlight; the application only parses the verb and formats the reply. */
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

    void attach(cyberdeck_screen_console::port &io) { port_ = &io; }

    cyberdeck_apps::result execute(std::string_view, std::string_view args) override
    {
        if (port_ == nullptr) return {cyberdeck_apps::result_status::rejected, "screen: console unavailable\n"};
        return {cyberdeck_apps::result_status::handled, cyberdeck_screen_console::execute(*port_, args)};
    }

private:
    static constexpr cyberdeck_apps::manifest k_manifest{
        "cyberdeck.screen.shell",
        "Screen console",
        "1.0.0",
        "control screen protection",
        "",
        {},
        0,
        {"display"},
        1,
        1000,
        "1",
        cyberdeck_apps::app_type::service,
        {},
        0,
        0,
        0,
        {"screen"},
        1,
        false,
        {{{"screen [on|off|timeout <0-1440>]", "on|off: enable or disable screen protection\ntimeout <0-1440>: set inactivity timeout in minutes; 0 disables it\n"}}},
    };

    bool running_ = false;
    cyberdeck_screen_console::port *port_ = nullptr;
};

inline application &global_application()
{
    static application instance;
    return instance;
}

} // namespace cyberdeck_screen_console_app
