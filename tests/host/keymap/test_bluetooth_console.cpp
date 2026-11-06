// Behavioral tests for the `bluetooth` console application (pure layer + manifest).

#include <cstdio>
#include <string>

#include "apps/bluetooth/cyberdeck_bluetooth_console.h"
#include "apps/bluetooth/cyberdeck_bluetooth_console_app.h"

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

struct fake_port final : cyberdeck_bluetooth_console::port {
    int searches = 0;
    int paired_lists = 0;
    void start_search() override { ++searches; }
    void open_paired() override { ++paired_lists; }
};

void test_search()
{
    fake_port port;
    check_eq(cyberdeck_bluetooth_console::execute(port, " search"), "Bluetooth search started.\n",
             "a search is acknowledged with the legacy text");
    check(port.searches == 1, "a search reaches the port exactly once");
}

void test_paired()
{
    fake_port port;
    check_eq(cyberdeck_bluetooth_console::execute(port, " paired"), "",
             "listing paired devices prints nothing itself");
    check(port.paired_lists == 1, "the paired list is opened through the port");
}

void test_unknown()
{
    fake_port port;
    for (const char *unknown : {"", " devices", " search extra", " paired now", "Search"}) {
        check_eq(cyberdeck_bluetooth_console::execute(port, unknown), "unknown command; type help\n",
                 "an unsupported shape keeps the legacy unknown message");
    }
    check(port.searches == 0 && port.paired_lists == 0, "an unsupported shape reaches no port");
}

void test_whitespace()
{
    fake_port port;
    check_eq(cyberdeck_bluetooth_console::execute(port, "   search  "), "Bluetooth search started.\n",
             "surrounding blanks are ignored");
    check_eq(cyberdeck_bluetooth_console::execute(port, "\tpaired\t"), "",
             "tabs around the verb are ignored");
    check(port.searches == 1 && port.paired_lists == 1, "both verbs reach the port once");
}

void test_manifest_registration_shape()
{
    const cyberdeck_apps::manifest &manifest =
        cyberdeck_bluetooth_console_app::global_application().get_manifest();
    check_eq(std::string(manifest.id), "cyberdeck.bluetooth.shell", "the bluetooth console owns its own id");
    check(manifest.command_count == 1 && manifest.commands[0] == "bluetooth",
          "the manifest declares the bluetooth command");
    check(manifest.resource_count == 1 && manifest.resources[0] == "ble", "only the BLE grant is declared");
    check(!manifest.owns_console, "the bluetooth console never owns the console");
    check(manifest.dependency_count == 0, "the bluetooth console depends on no service");
    check(manifest.command_help_entries[0].usage == "bluetooth [search|paired]", "help keeps the approved usage");
}

} // namespace

int main()
{
    test_search();
    test_paired();
    test_unknown();
    test_whitespace();
    test_manifest_registration_shape();

    std::printf("test_bluetooth_console: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
