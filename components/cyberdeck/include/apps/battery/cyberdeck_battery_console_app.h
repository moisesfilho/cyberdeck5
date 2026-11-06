#pragma once

#include "apps/battery/cyberdeck_battery_console.h"
#include "apps/runtime/cyberdeck_app_runtime.h"

namespace cyberdeck_battery_console_app {

/* `battery` as a compiled application.  The protection policy and the IO
 * expander stay behind the port. */
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

    void attach(cyberdeck_battery_console::port &io) { port_ = &io; }

    cyberdeck_apps::result execute(std::string_view, std::string_view args) override
    {
        if (port_ == nullptr) return {cyberdeck_apps::result_status::rejected, "battery: console unavailable\n"};
        return {cyberdeck_apps::result_status::handled, cyberdeck_battery_console::execute(*port_, args)};
    }

private:
    static constexpr cyberdeck_apps::manifest k_manifest{
        "cyberdeck.battery.shell",
        "Battery console",
        "1.0.0",
        "show or control battery protection",
        "",
        {},
        0,
        {"battery"},
        1,
        1000,
        "1",
        cyberdeck_apps::app_type::service,
        {},
        0,
        0,
        0,
        {"battery"},
        1,
        false,
        {{{"battery [protection on|off|status]", "protection on: enable battery protection\nprotection off: disable battery protection\nprotection status: show current protection status\n"}}},
    };

    bool running_ = false;
    cyberdeck_battery_console::port *port_ = nullptr;
};

inline application &global_application()
{
    static application instance;
    return instance;
}

} // namespace cyberdeck_battery_console_app
