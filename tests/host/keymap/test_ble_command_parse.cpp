// Contract for the `bluetooth` console application: only the two approved
// subcommands exist, and they are routed to the port exactly once.

#include <cstdio>
#include <cstring>
#include <string>

#include "apps/bluetooth/cyberdeck_bluetooth_console.h"

namespace {

int failures = 0;
int checks = 0;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    } \
} while (0)

#define CHECK_STR(actual, expected) do { \
    ++checks; \
    if (std::strcmp((actual), (expected)) != 0) { \
        ++failures; \
        std::printf("FAIL %s:%d: expected '%s', got '%s'\n", __FILE__, __LINE__, \
                    (expected), (actual)); \
    } \
} while (0)

struct fake_port final : cyberdeck_bluetooth_console::port {
    int searches = 0;
    int paired = 0;
    void start_search() override { ++searches; }
    void open_paired() override { ++paired; }
    bool untouched() const { return searches == 0 && paired == 0; }
};

void test_search_is_routed()
{
    fake_port port;
    const std::string output = cyberdeck_bluetooth_console::execute(port, " search");
    CHECK_STR(output.c_str(), "Bluetooth search started.\n");
    CHECK(port.searches == 1);
    CHECK(port.paired == 0);
}

void test_paired_is_routed()
{
    fake_port port;
    const std::string output = cyberdeck_bluetooth_console::execute(port, " paired");
    CHECK(output.empty());
    CHECK(port.paired == 1);
    CHECK(port.searches == 0);
}

void test_only_the_two_approved_subcommands_exist()
{
    const char *unknown[] = {
        "",
        " ",
        " status",
        " list",
        " scan",
        " pair",
        " unpair",
        " forget",
        " connect",
        " disconnect",
        " classic",
        " serial",
        " spp",
        " a2dp",
        " avrcp",
        " search extra",
        " paired extra",
        " search paired",
        " paired search",
        " Search",
        " PAIRED",
        "\tsearch extra",
    };
    for (const char *text : unknown) {
        fake_port port;
        CHECK_STR(cyberdeck_bluetooth_console::execute(port, text).c_str(),
                  "unknown command; type help\n");
        CHECK(port.untouched());
    }
}

void test_prefix_collisions_do_not_alias_the_bluetooth_commands()
{
    const char *others[] = {
        "wifi", "wifi search", "wifi saved", "screen on", "log", "clear",
        "battery protection status", "help", "ssh user@host",
    };
    for (const char *text : others) {
        fake_port port;
        CHECK_STR(cyberdeck_bluetooth_console::execute(port, text).c_str(),
                  "unknown command; type help\n");
        CHECK(port.untouched());
    }
}

} // namespace

int main()
{
    test_search_is_routed();
    test_paired_is_routed();
    test_only_the_two_approved_subcommands_exist();
    test_prefix_collisions_do_not_alias_the_bluetooth_commands();

    std::printf("ble shell command contract: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
