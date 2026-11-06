#pragma once

#include "apps/event_log/cyberdeck_event_log_console.h"
#include "apps/runtime/cyberdeck_app_runtime.h"

namespace cyberdeck_event_log_console_app {

/* `log` as a compiled application: the manifest owns the command, the port owns
 * the backend and this class owns only the session-scoped window. */
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

    void attach(cyberdeck_event_log_console::port &io) { port_ = &io; }

    cyberdeck_apps::result execute(std::string_view, std::string_view args) override
    {
        if (port_ == nullptr) return {cyberdeck_apps::result_status::rejected, "log: console unavailable\n"};
        return {cyberdeck_apps::result_status::handled,
                cyberdeck_event_log_console::execute(*port_, args, state_)};
    }

private:
    static constexpr cyberdeck_apps::manifest k_manifest{
        "cyberdeck.event_log.shell",
        "Event log console",
        "1.0.0",
        "show recent events",
        "",
        {},
        0,
        {"event_log"},
        1,
        1000,
        "1",
        cyberdeck_apps::app_type::service,
        {},
        0,
        0,
        0,
        {"log"},
        1,
        false,
        {{{"log [lines <1-64>]", "lines <1-64>: set the number of recent events\n"}}},
    };

    bool running_ = false;
    cyberdeck_event_log_console::port *port_ = nullptr;
    cyberdeck_event_log_console::state state_{};
};

inline application &global_application()
{
    static application instance;
    return instance;
}

} // namespace cyberdeck_event_log_console_app
