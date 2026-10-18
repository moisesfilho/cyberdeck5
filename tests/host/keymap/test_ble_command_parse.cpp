/*
 * TDD RED host contract for the `bluetooth search` / `bluetooth paired` shell
 * surface (REQ-BLE-002).
 *
 * The shell parser is host-linkable, so this test exercises the real production
 * implementation instead of inspecting it.  It is RED until the coder adds
 * CYBERDECK_CMD_BLUETOOTH_SEARCH / CYBERDECK_CMD_BLUETOOTH_PAIRED to
 * components/cyberdeck/include/apps/shell/cyberdeck_shell_utils.h, routes
 * both spellings in cyberdeck_parse_command() and adds the single `bluetooth`
 * row to the shared help catalog.
 *
 * Bluetooth Classic is explicitly out of scope (REQ-BLE-011): there is no
 * subcommand for it, and none may appear here.
 */
#include "apps/shell/cyberdeck_shell_utils.h"

#include <cstdio>
#include <cstdlib>
#include <string>

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

#define CHECK_EQ(actual, expected) do { \
    ++checks; \
    if (static_cast<long>(actual) != static_cast<long>(expected)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s (actual=%ld, expected=%ld)\n", \
                    __FILE__, __LINE__, #actual, \
                    static_cast<long>(actual), static_cast<long>(expected)); \
    } \
} while (0)

#define CHECK_STR(actual, expected) do { \
    ++checks; \
    const std::string actual_value = (actual); \
    const std::string expected_value = (expected); \
    if (actual_value != expected_value) { \
        ++failures; \
        std::printf("FAIL %s:%d: expected '%s', actual '%s'\n", \
                    __FILE__, __LINE__, expected_value.c_str(), \
                    actual_value.c_str()); \
    } \
} while (0)

/* Fail-fast position guard.  CHECK() alone is non-fatal, so a npos result
 * would still reach the arithmetic/substr below it and turn a missing catalog
 * row into a bogus comparison or an out-of-range index.  This guard records
 * the failure, prints it and stops the run before any npos arithmetic runs. */
std::size_t require_found(std::size_t position, const char *what, int line)
{
    ++checks;
    if (position == std::string::npos) {
        ++failures;
        std::printf("FAIL %s:%d: required text not found: %s (position == npos)\n",
                    __FILE__, line, what);
        std::fflush(stdout);
        std::exit(1);
    }
    return position;
}

#define REQUIRE_FOUND(position, what) require_found((position), (what), __LINE__)

void test_bluetooth_search_is_routed()
{
    const cyberdeck_cmd_t command = cyberdeck_parse_command("bluetooth search");
    CHECK_EQ(command.type, CYBERDECK_CMD_BLUETOOTH_SEARCH);
    CHECK_STR(command.args, "");
    CHECK(!command.confirmed);
}

void test_bluetooth_paired_is_routed()
{
    const cyberdeck_cmd_t command = cyberdeck_parse_command("bluetooth paired");
    CHECK_EQ(command.type, CYBERDECK_CMD_BLUETOOTH_PAIRED);
    CHECK_STR(command.args, "");
    CHECK(!command.confirmed);
}

void test_only_the_two_approved_subcommands_exist()
{
    const char *unknown[] = {
        "bluetooth",
        "bluetooth ",
        "bluetooth status",
        "bluetooth list",
        "bluetooth scan",
        "bluetooth pair",
        "bluetooth unpair",
        "bluetooth forget",
        "bluetooth connect",
        "bluetooth disconnect",
        "bluetooth classic",
        "bluetooth serial",
        "bluetooth spp",
        "bluetooth a2dp",
        "bluetooth avrcp",
        "bluetooth search extra",
        "bluetooth paired extra",
        "bluetooth search paired",
        "bluetooth paired search",
        "Bluetooth search",
        "BLUETOOTH PAIRED",
        "bluetooth Search",
        "bluetooth \tsearch",
    };
    for (const char *text : unknown) {
        const cyberdeck_cmd_t command = cyberdeck_parse_command(text);
        CHECK(command.type == CYBERDECK_CMD_UNKNOWN);
        if (command.type != CYBERDECK_CMD_UNKNOWN) {
            std::printf("      rejected input was '%s'\n", text);
        }
    }

    /* The bare verb is not a command: it must not become a no-op success. */
    const cyberdeck_cmd_t bare = cyberdeck_parse_command("bluetooth");
    CHECK(bare.type == CYBERDECK_CMD_UNKNOWN);
    CHECK_STR(bare.args, "bluetooth");
}

void test_prefix_collisions_do_not_alias_the_bluetooth_commands()
{
    const char *others[] = {
        "wifi", "wifi search", "wifi saved", "screen on", "log", "clear",
        "battery protection status", "help",
    };
    for (const char *text : others) {
        const cyberdeck_cmd_t command = cyberdeck_parse_command(text);
        CHECK(command.type != CYBERDECK_CMD_BLUETOOTH_SEARCH);
        CHECK(command.type != CYBERDECK_CMD_BLUETOOTH_PAIRED);
    }
    CHECK(cyberdeck_parse_command("wifi search").type == CYBERDECK_CMD_WIFI_SEARCH);
    CHECK(cyberdeck_parse_command("bluetooth search").type != CYBERDECK_CMD_WIFI_SEARCH);
}

void test_help_catalog_documents_bluetooth_exactly_once()
{
    const std::string help = cyberdeck_help_text();
    const std::string row = "bluetooth [search|paired]";

    const std::size_t first =
        REQUIRE_FOUND(help.find(row), "catalog row 'bluetooth [search|paired]'");
    CHECK(help.find(row, first + 1) == std::string::npos);
    /* The usage row keeps the shared "  <usage> - <description>" format. */
    CHECK_STR(help.substr(first + row.size(), 3), " - ");
    /* The usage row opens its own two-space indented line, immediately
     * below the bluetooth tag header of the new formatter. */
    const std::string tag = "bluetooth [terminal]\n";
    const std::size_t tag_position =
        REQUIRE_FOUND(help.find(tag), "tag header 'bluetooth [terminal]'");
    /* "<command> [terminal]\n" + "  " immediately precede the row. */
    CHECK(first == tag_position + tag.size() + 2);
    CHECK(first >= 3 && help[first - 1] == ' ' && help[first - 2] == ' ' &&
          help[first - 3] == '\n');
    /* The character after " - " is only indexed once its bounds are part of
     * the same assertion, so a truncated catalog fails instead of reading
     * past the end. */
    const std::size_t description_start = first + row.size() + 3;
    CHECK(description_start < help.size() && help[description_start] != ' ');

    /* The command-specific help comes from the same catalog: it is byte-equal
     * to its contiguous block in the full text and every line ends with LF. */
    const std::string command_help = cyberdeck_command_help_text("bluetooth");
    CHECK(command_help.find(row) != std::string::npos);
    CHECK(!command_help.empty() && command_help.back() == '\n');
    {
        const std::size_t block_position =
            REQUIRE_FOUND(help.find(tag), "tag header for the pinned bluetooth block");
        CHECK(command_help.compare(0, command_help.size(), help,
                                   block_position, command_help.size()) == 0);
    }
    CHECK_STR(cyberdeck_command_help_text("bluetoothx"), "");
    CHECK_STR(cyberdeck_command_help_text("Bluetooth"), "");

    /* The existing catalog rows are preserved: bluetooth is additive. */
    for (const char *existing : {"help - show this help", "wifi [search|saved|audit|survey]",
                                 "log [lines <1-64>] - show recent events", "clear - clear the terminal",
                                 "screen [on|off|timeout <0-1440>]",
                                 "battery [protection on|off|status]",
                                 "ssh [user@]host[:port]", "pwd - print working directory",
                                 "cd [path] - change working directory",
                                 "ls [-a] [path] - list directory contents",
                                 "cat <file> - print a regular file",
                                 "touch <file> - create an empty file",
                                 "mkdir <directory> - create a directory",
                                 "rm [-r] <path> - remove a file or directory",
                                  "rmdir <directory> - remove an empty directory",
                                  "poweroff - power off this device after exact y confirmation"}) {
        CHECK(help.find(existing) != std::string::npos);
    }

    /* Every rendered line ends with exactly one LF, so the terminal gains
     * clean lines only: 18 command tags plus 47 indented usage/parameter
     * lines make 65 LF-terminated lines in the catalog. */
    std::size_t lines = 0;
    for (char value : help) {
        if (value == '\n') ++lines;
    }
    CHECK_EQ(lines, std::size_t(65));
    {
        /* No empty internal line and every line is either a `<command>
         * [terminal]` tag header or a two-space indented line. */
        std::size_t line_start = 0;
        while (line_start < help.size()) {
            const std::size_t stop = help.find('\n', line_start);
            if (stop == std::string::npos) {
                /* O catalogo deve terminar em newline; falha limpa se nao. */
                CHECK(help.back() == '\n');
                break;
            }
            const std::size_t length = stop - line_start;
            CHECK(length != 0);
            const std::string line = help.substr(line_start, length);
            const bool tag_line = line.size() > 11 &&
                                  line.compare(line.size() - 11, 11, " [terminal]") == 0;
            const bool indented_line = line.compare(0, 2, "  ") == 0;
            CHECK(tag_line || indented_line);
            line_start = stop + 1;
        }
    }
}

} // namespace

int main()
{
    test_bluetooth_search_is_routed();
    test_bluetooth_paired_is_routed();
    test_only_the_two_approved_subcommands_exist();
    test_prefix_collisions_do_not_alias_the_bluetooth_commands();
    test_help_catalog_documents_bluetooth_exactly_once();

    std::printf("ble shell command contract: %d checks, %d failures\n",
                checks, failures);
    return failures == 0 ? 0 : 1;
}
