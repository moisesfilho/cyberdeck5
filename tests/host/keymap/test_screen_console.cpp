// Behavioral tests for the `screen` console application (pure layer + manifest).

#include <cstdint>
#include <cstdio>
#include <string>

#include "apps/screen/cyberdeck_screen_console.h"
#include "apps/screen/cyberdeck_screen_console_app.h"

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

struct fake_port final : cyberdeck_screen_console::port {
    int on = 0;
    int off = 0;
    int timeout = -1;
    bool persist = true;
    void turn_on() override { ++on; }
    void turn_off() override { ++off; }
    bool set_timeout_minutes(std::uint16_t minutes) override
    {
        timeout = minutes;
        return persist;
    }
};

void test_power_verbs()
{
    fake_port port;
    check_eq(cyberdeck_screen_console::execute(port, " on"), "screen on\n", "screen on is acknowledged");
    check(port.on == 1, "screen on reaches the port");
    check_eq(cyberdeck_screen_console::execute(port, " off"), "screen off\n", "screen off is acknowledged");
    check(port.off == 1, "screen off reaches the port");
}

void test_timeout_parsing()
{
    fake_port port;
    check_eq(cyberdeck_screen_console::execute(port, " timeout 300"), "screen timeout set to 300 minutes\n",
             "a valid timeout is confirmed");
    check(port.timeout == 300, "the parsed minutes reach the port");

    check_eq(cyberdeck_screen_console::execute(port, " timeout 0"), "screen timeout disabled (0 minutes)\n",
             "zero disables the timeout");
    check(port.timeout == 0, "zero is forwarded so persistence can store it");

    check_eq(cyberdeck_screen_console::execute(port, " timeout 1440"),
             "screen timeout set to 1440 minutes\n", "the upper boundary is accepted");
    check(port.timeout == 1440, "the upper boundary reaches the port");
}

void test_timeout_rejections()
{
    for (const char *invalid : {" timeout 1441", " timeout 99999", " timeout -1", " timeout abc",
                                " timeout", " timeout ", " timeout 5 extra"}) {
        fake_port port;
        check_eq(cyberdeck_screen_console::execute(port, invalid),
                 "screen timeout: expected an integer from 0 to 1440\n", "an invalid timeout is rejected");
        check(port.timeout == -1, "an invalid timeout never reaches the port");
    }
}

void test_persist_failure()
{
    fake_port port;
    port.persist = false;
    check_eq(cyberdeck_screen_console::execute(port, " timeout 5"),
             "screen timeout: unable to persist setting\n", "a failed persist is reported");
    check(port.timeout == 5, "the value still reaches the port before the failure");
}

void test_whitespace_and_unknown()
{
    fake_port port;
    check_eq(cyberdeck_screen_console::execute(port, "  timeout\t 12 "), "screen timeout set to 12 minutes\n",
             "separators between tokens are normalized like the legacy parser");
    check(port.timeout == 12, "the normalized value is forwarded");

    for (const char *unknown : {"", " nope", " brightness 3", " on extra", " off now"}) {
        check_eq(cyberdeck_screen_console::execute(port, unknown), "unknown command; type help\n",
                 "an unsupported shape keeps the legacy unknown message");
    }
}

void test_manifest_registration_shape()
{
    const cyberdeck_apps::manifest &manifest = cyberdeck_screen_console_app::global_application().get_manifest();
    check_eq(std::string(manifest.id), "cyberdeck.screen.shell", "the screen console owns its own id");
    check(manifest.command_count == 1 && manifest.commands[0] == "screen", "the manifest declares the screen command");
    check(!manifest.owns_console, "the screen console never owns the console");
    check(manifest.dependency_count == 0, "the screen console depends on no service");
    check(manifest.command_help_entries[0].usage == "screen [on|off|timeout <0-1440>]", "help keeps the approved usage");
}

} // namespace

int main()
{
    test_power_verbs();
    test_timeout_parsing();
    test_timeout_rejections();
    test_persist_failure();
    test_whitespace_and_unknown();
    test_manifest_registration_shape();

    std::printf("test_screen_console: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
