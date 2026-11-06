#pragma once

#include "apps/bluetooth/cyberdeck_bluetooth_console.h"
#include "apps/runtime/cyberdeck_app_runtime.h"

namespace cyberdeck_bluetooth_console_app {

/* `bluetooth` as a compiled application.  The scan list, the state machine and
 * the input ownership rules stay in the UI composition behind the port. */
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

    void attach(cyberdeck_bluetooth_console::port &io) { port_ = &io; }

    cyberdeck_apps::result execute(std::string_view, std::string_view args) override
    {
        if (port_ == nullptr) return {cyberdeck_apps::result_status::rejected, "bluetooth: console unavailable\n"};
        return {cyberdeck_apps::result_status::handled, cyberdeck_bluetooth_console::execute(*port_, args)};
    }

private:
    static constexpr cyberdeck_apps::manifest k_manifest{
        "cyberdeck.bluetooth.shell",
        "Bluetooth console",
        "1.0.0",
        "search for or list paired Bluetooth devices",
        "",
        {},
        0,
        {"ble"},
        1,
        1000,
        "1",
        cyberdeck_apps::app_type::service,
        {},
        0,
        0,
        0,
        {"bluetooth"},
        1,
        false,
        {{{"bluetooth [search|paired]", "search: find nearby devices\npaired: list paired devices\n"}}},
    };

    bool running_ = false;
    cyberdeck_bluetooth_console::port *port_ = nullptr;
};

inline application &global_application()
{
    static application instance;
    return instance;
}

} // namespace cyberdeck_bluetooth_console_app
