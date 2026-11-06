// Behavioral tests for the `log` console application (pure layer + manifest).

#include <cstdio>
#include <string>

#include "apps/event_log/cyberdeck_event_log_console.h"
#include "apps/event_log/cyberdeck_event_log_console_app.h"

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

struct fake_port final : cyberdeck_event_log_console::port {
    std::string events;
    std::size_t requested = 0;
    int calls = 0;
    std::string recent_events(std::size_t count) override
    {
        ++calls;
        requested = count;
        return events;
    }
};

struct fixture {
    fake_port port;
    cyberdeck_event_log_console::state state;
    std::string run(const char *args) { return cyberdeck_event_log_console::execute(port, args, state); }
};

void test_window_default_and_listing()
{
    fixture f;
    check(f.state.window_lines == 20, "the compiled default window is 20 lines");

    f.port.events = "evt one\n";
    check_eq(f.run(""), "ultimos eventos:\nevt one\n", "log prints the header and the events");
    check(f.port.requested == 20, "log requests the configured 20 recent lines");
    check(f.port.calls == 1, "log reads the port once");

    fixture empty;
    check_eq(empty.run(""), "ultimos eventos:\n(nenhum evento disponivel)\n",
             "an empty log reports the absence of events");
    check(empty.port.requested == 20, "an empty log still requests the configured window");
}

void test_window_override()
{
    fixture f;
    check_eq(f.run("lines"), "log lines: 20\n", "log lines reports the default");
    check(f.port.calls == 0, "log lines never reads the log");

    check_eq(f.run("lines 1"), "log lines set to 1\n", "log lines 1 accepts the lower boundary");
    check(f.state.window_lines == 1, "the lower boundary is stored");

    f.port.events.clear();
    (void)f.run("");
    check(f.port.requested == 1, "the override controls the next read");

    check_eq(f.run("lines 64"), "log lines set to 64\n", "log lines 64 accepts the upper boundary");
    (void)f.run("");
    check(f.port.requested == 64, "the upper boundary controls the next read");
    check_eq(f.run("lines"), "log lines: 64\n", "log lines exposes the active override");
}

void test_invalid_window_keeps_value()
{
    fixture f;
    (void)f.run("lines 7");
    for (const char *invalid : {"lines 0", "lines 65", "lines nope", "lines 4 extra", "line 5"}) {
        check_eq(f.run(invalid), "usage: log [lines <1-64>]\n", "invalid log-lines syntax reports usage");
        check(f.state.window_lines == 7, "invalid log-lines syntax does not alter the session value");
    }
}

void test_session_reset()
{
    auto &app = cyberdeck_event_log_console_app::global_application();
    fake_port port;
    app.attach(port);
    (void)app.execute("log", " lines 5");
    check(app.execute("log", " lines").output == "log lines: 5\n", "the application keeps the session window");
    app.reset_session_state();
    check(app.execute("log", " lines").output == "log lines: 20\n",
          "a new session restores the compiled default");
}

void test_manifest_registration_shape()
{
    auto &app = cyberdeck_event_log_console_app::global_application();
    const cyberdeck_apps::manifest &manifest = app.get_manifest();
    check_eq(std::string(manifest.id), "cyberdeck.event_log.shell", "the log console owns its own id");
    check_eq(std::string(manifest.command), "", "the command is declared in the list, not the single field");
    check(manifest.command_count == 1 && manifest.commands[0] == "log", "the manifest declares the log command");
    check(!manifest.owns_console, "the log console never owns the console");
    check(manifest.dependency_count == 0, "the log console depends on no service");
    check(manifest.command_help_entries[0].usage == "log [lines <1-64>]", "help keeps the approved usage");
}

} // namespace

int main()
{
    test_window_default_and_listing();
    test_window_override();
    test_invalid_window_keeps_value();
    test_session_reset();
    test_manifest_registration_shape();

    std::printf("test_event_log_console: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
