#pragma once

#include "apps/runtime/cyberdeck_app_runtime.h"
#include "apps/wifi/cyberdeck_wifi_console.h"

namespace cyberdeck_wifi_console_app {

/* `wifi` as a compiled application.  Menus, pickers, audit and the survey
 * session stay in the UI composition behind the port. */
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
    void reset_session_state() override { state_ = {}; }

    void attach(cyberdeck_wifi_console::port &io) { port_ = &io; }

    cyberdeck_apps::result execute(std::string_view, std::string_view args) override
    {
        if (port_ == nullptr) return {cyberdeck_apps::result_status::rejected, "wifi: console unavailable\n"};
        return {cyberdeck_apps::result_status::handled, cyberdeck_wifi_console::execute(*port_, args, state_)};
    }

private:
    static constexpr cyberdeck_apps::manifest k_manifest{
        "cyberdeck.wifi.shell",
        "Wi-Fi console",
        "1.0.0",
        "manage Wi-Fi, passively survey networks, and explicitly export results",
        "",
        {},
        0,
        {"network"},
        1,
        1000,
        "1",
        cyberdeck_apps::app_type::service,
        {},
        0,
        0,
        0,
        {"wifi"},
        1,
        false,
        {{{"wifi [search|saved|audit|survey]", "search: scan for networks\nsaved: manage saved networks\naudit: inspect or export Wi-Fi audit results\nsurvey: run a passive survey\n"}}},
    };

    bool running_ = false;
    cyberdeck_wifi_console::port *port_ = nullptr;
    cyberdeck_wifi_console::state state_{};
};

inline application &global_application()
{
    static application instance;
    return instance;
}

} // namespace cyberdeck_wifi_console_app
