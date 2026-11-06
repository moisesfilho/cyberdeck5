// End-to-end host contract for the six console command applications:
// runtime registration -> execute_line dispatch -> narrow port call.
//
// The console tests exercise each application against a fake port directly.
// This test adds the missing seam between them: a real runtime, the real
// manifests and the real command dispatch that the shell uses.

#include <cstdio>
#include <string>

#include "apps/battery/cyberdeck_battery_console_app.h"
#include "apps/bluetooth/cyberdeck_bluetooth_console_app.h"
#include "apps/event_log/cyberdeck_event_log_console_app.h"
#include "apps/runtime/cyberdeck_app_runtime.h"
#include "apps/screen/cyberdeck_screen_console_app.h"
#include "apps/ssh/cyberdeck_ssh_console_app.h"
#include "apps/wifi/cyberdeck_wifi_console_app.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char *what)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

void check_eq(const std::string &actual, const std::string &expected, const char *what)
{
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::printf("FAIL: %s\n  expected: %s\n  actual:   %s\n", what, expected.c_str(), actual.c_str());
    }
}

struct wifi_port final : cyberdeck_wifi_console::port {
    int searches = 0;
    status current_status() override { return {}; }
    search_outcome begin_search() override
    {
        ++searches;
        return search_outcome::started;
    }
    saved_outcome open_saved() override { return saved_outcome::opened; }
    void audit_begin() override {}
    save_outcome audit_save() override { return save_outcome::requested; }
    survey_start_outcome survey_start(std::uint32_t) override { return survey_start_outcome::started; }
    survey_action_outcome survey_cancel() override { return survey_action_outcome::applied; }
    survey_action_outcome survey_export(bool, bool) override { return survey_action_outcome::applied; }
};

struct ssh_port final : cyberdeck_ssh_console::port {
    int sessions = 0;
    int errors = 0;
    bool begin_session(const char *, const char *, int) override
    {
        ++sessions;
        return true;
    }
    void log_error(const char *) override { ++errors; }
};

struct bluetooth_port final : cyberdeck_bluetooth_console::port {
    int searches = 0;
    int paired = 0;
    void start_search() override { ++searches; }
    void open_paired() override { ++paired; }
};

struct battery_port final : cyberdeck_battery_console::port {
    int reads = 0;
    bool protection_started() override { return true; }
    bool set_protection(bool) override { return true; }
    bool snapshot(cyberdeck_battery_protection::snapshot &out) override
    {
        out = {};
        return true;
    }
    bool hardware_diagnostics(cyberdeck_battery_protection::hardware_diagnostics &out) override
    {
        ++reads;
        out = {};
        return false;
    }
};

struct screen_port final : cyberdeck_screen_console::port {
    int toggles = 0;
    void turn_on() override { ++toggles; }
    void turn_off() override { ++toggles; }
    bool set_timeout_minutes(std::uint16_t) override { return true; }
};

struct log_port final : cyberdeck_event_log_console::port {
    std::size_t last_count = 0;
    std::string recent_events(std::size_t count) override
    {
        last_count = count;
        return "evt\n";
    }
};

void test_dispatch_reaches_each_console()
{
    wifi_port wifi_io;
    ssh_port ssh_io;
    bluetooth_port bluetooth_io;
    battery_port battery_io;
    screen_port screen_io;
    log_port log_io;

    auto &wifi_app = cyberdeck_wifi_console_app::global_application();
    auto &ssh_app = cyberdeck_ssh_console_app::global_application();
    auto &bluetooth_app = cyberdeck_bluetooth_console_app::global_application();
    auto &battery_app = cyberdeck_battery_console_app::global_application();
    auto &screen_app = cyberdeck_screen_console_app::global_application();
    auto &log_app = cyberdeck_event_log_console_app::global_application();

    wifi_app.attach(wifi_io);
    ssh_app.attach(ssh_io);
    bluetooth_app.attach(bluetooth_io);
    battery_app.attach(battery_io);
    screen_app.attach(screen_io);
    log_app.attach(log_io);

    cyberdeck_apps::runtime runtime;
    check(runtime.register_application(wifi_app), "wifi console registers");
    check(runtime.register_application(ssh_app), "ssh console registers");
    check(runtime.register_application(bluetooth_app), "bluetooth console registers");
    check(runtime.register_application(battery_app), "battery console registers");
    check(runtime.register_application(screen_app), "screen console registers");
    check(runtime.register_application(log_app), "log console registers");

    // Dispatch works without any start(): a console application never depends on
    // its own lifecycle nor on the homonymous service being up.
    check_eq(runtime.execute_line("wifi search").output, "Scanning Wi-Fi networks...\n",
             "the runtime routes wifi to the console application");
    check(wifi_io.searches == 1, "the wifi port receives the search request");

    check_eq(runtime.execute_line("ssh example.com").output, "",
             "the runtime routes ssh to the console application");
    check(ssh_io.sessions == 1 && ssh_io.errors == 0, "the ssh port starts one session");

    check_eq(runtime.execute_line("bluetooth search").output, "Bluetooth search started.\n",
             "the runtime routes bluetooth to the console application");
    check(bluetooth_io.searches == 1, "the bluetooth port receives the search request");
    check_eq(runtime.execute_line("bluetooth paired").output, "",
             "the runtime routes the paired list to the console application");
    check(bluetooth_io.paired == 1, "the bluetooth port opens the paired list");

    check_eq(runtime.execute_line("battery protection on").output, "battery protection enabled\n",
             "the runtime routes battery to the console application");
    check(runtime.execute_line("battery protection status").output.find("battery hardware: unavailable") !=
              std::string::npos,
          "the battery status reports unavailable diagnostics instead of inventing evidence");
    check(battery_io.reads == 1, "the battery port is read once per status");

    check_eq(runtime.execute_line("screen off").output, "screen off\n",
             "the runtime routes screen to the console application");
    check(screen_io.toggles == 1, "the screen port receives the toggle");

    check_eq(runtime.execute_line("log lines 3").output, "log lines set to 3\n",
             "the runtime routes log to the console application");
    check_eq(runtime.execute_line("log").output, "ultimos eventos:\nevt\n",
             "the runtime renders the log window through the port");
    check(log_io.last_count == 3, "the session window reaches the log port");

    // An unknown subcommand stays with the console application, never with the
    // legacy parser.
    check_eq(runtime.execute_line("wifi nope").output, "unknown command; type help\n",
             "an unknown wifi subcommand keeps the legacy reply");
    // A non-declared command is not captured by the supervisor.
    check(runtime.execute_line("pwd").status == cyberdeck_apps::result_status::not_handled,
          "a legacy command is never handled by the runtime");
}

void test_stop_does_not_disable_the_command()
{
    // `app stop` must not silently remove the command from the console: the
    // application stays dispatchable by design (documented in ARCHITECTURE).
    log_port io;
    auto &app = cyberdeck_event_log_console_app::global_application();
    app.reset_session_state();
    app.attach(io);
    cyberdeck_apps::runtime runtime;
    check(runtime.register_application(app), "log console registers for the stop scenario");
    check(runtime.start_application(app.get_manifest().id), "the console application starts");
    check(runtime.stop_application(app.get_manifest().id), "the console application stops");
    check_eq(runtime.execute_line("log lines").output, "log lines: 20\n",
             "a stopped console still answers its command");
}

} // namespace

int main()
{
    test_dispatch_reaches_each_console();
    test_stop_does_not_disable_the_command();

    std::printf("test_console_dispatch: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
