#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace cyberdeck_shell_help {

inline constexpr std::string_view kEntrySeparator = " - ";
inline constexpr std::string_view kTerminalTag = " [terminal]";
inline constexpr std::string_view kAppTag = " [app]";
inline constexpr std::string_view kAppDescription = "start compiled application";
inline constexpr std::string_view kParameterIndent = "  ";

// The catalog is the only ordered source for help rows.  Keeping the table
// header-only makes it usable by host-only shell consumers without bringing
// LVGL, ESP-IDF, or UI state into the formatter.
struct entry {
    std::string_view command;
    std::string_view usage;
    std::string_view description;
    std::string_view parameters;
};

inline constexpr std::size_t formatted_entry_size(const entry &item)
{
    return item.command.size() + kTerminalTag.size() + 1 + kParameterIndent.size() +
           item.usage.size() +
           kEntrySeparator.size() + item.description.size() + 1 + item.parameters.size();
}

inline constexpr std::array<entry, 18> kCatalog = {{
    {"help", "help", "show this help", ""},
    {"wifi", "wifi [search|saved|audit|survey]", "manage Wi-Fi, passively survey networks, and explicitly export results", "  search: scan for networks\n  saved: manage saved networks\n  audit: inspect or export Wi-Fi audit results\n  survey: run a passive survey\n"},
    {"log", "log [lines <1-64>]", "show recent events", "  lines <1-64>: set the number of recent events\n"},
    {"clear", "clear", "clear the terminal", ""},
    {"screen", "screen [on|off|timeout <0-1440>]", "control screen protection", "  on|off: enable or disable screen protection\n  timeout <0-1440>: set inactivity timeout in minutes; 0 disables it\n"},
    {"battery", "battery [protection on|off|status]", "show or control battery protection", "  protection on: enable battery protection\n  protection off: disable battery protection\n  protection status: show current protection status\n"},
    {"bluetooth", "bluetooth [search|paired]", "search for or list paired Bluetooth devices", "  search: find nearby devices\n  paired: list paired devices\n"},
    {"app", "app [list|info|start|stop] [id]", "manage compiled-in applications", "  list: list registered applications\n  info <id>: show application details\n  start <id>: start an application\n  stop <id>: stop an application\n"},
    {"ssh", "ssh [user@]host[:port]", "start an SSH session", "  [user@]: optional login name\n  host: remote host name or address\n  [:port]: optional port (default 22)\n"},
    {"pwd", "pwd", "print working directory", ""},
    {"cd", "cd [path]", "change working directory", "  [path]: destination (default home directory)\n"},
    {"ls", "ls [-a] [path]", "list directory contents", "  [-a]: include hidden entries\n  [path]: directory to list (default current directory)\n"},
    {"cat", "cat <file>", "print a regular file", "  <file>: file to display\n"},
    {"touch", "touch <file>", "create an empty file", "  <file>: file to create\n"},
    {"mkdir", "mkdir <directory>", "create a directory", "  <directory>: directory to create\n"},
    {"rm", "rm [-r] <path>", "remove a file or directory", "  [-r]: recursively remove a directory\n  <path>: target to remove\n"},
    {"rmdir", "rmdir <directory>", "remove an empty directory", "  <directory>: empty directory to remove\n"},
    {"poweroff", "poweroff", "power off this device after exact y confirmation", "  Confirmation: type exactly y when prompted; any other input cancels\n"},
}};

inline std::string format_entry(const entry &item)
{
    std::string result;
    result.reserve(formatted_entry_size(item));
    result.append(item.command.data(), item.command.size());
    result.append(kTerminalTag.data(), kTerminalTag.size());
    result.push_back('\n');
    result.append(kParameterIndent.data(), kParameterIndent.size());
    result.append(item.usage.data(), item.usage.size());
    result.append(kEntrySeparator.data(), kEntrySeparator.size());
    result.append(item.description.data(), item.description.size());
    result.push_back('\n');
    result.append(item.parameters.data(), item.parameters.size());
    return result;
}

inline std::string format_app_entry(std::string_view command)
{
    std::string result;
    result.reserve(command.size() + kAppTag.size() + kParameterIndent.size() +
                   kAppDescription.size() + 2);
    result.append(command.data(), command.size());
    result.append(kAppTag.data(), kAppTag.size());
    result.push_back('\n');
    result.append(kParameterIndent.data(), kParameterIndent.size());
    result.append(kAppDescription.data(), kAppDescription.size());
    result.push_back('\n');
    return result;
}

inline std::string text()
{
    std::size_t capacity = 0;
    for (const entry &item : kCatalog) {
        capacity += formatted_entry_size(item);
    }

    std::string result;
    result.reserve(capacity);
    for (const entry &item : kCatalog) {
        result += format_entry(item);
    }
    return result;
}

inline std::string command_text(const char *command)
{
    if (command == nullptr) return {};

    const std::string_view requested(command);
    for (const entry &item : kCatalog) {
        if (item.command == requested) return format_entry(item);
    }
    return {};
}

// These names mirror the public formatter API while keeping the actual
// catalog and formatting implementation in this pure, host-linkable module.
inline std::string cyberdeck_help_text()
{
    return text();
}

inline std::string cyberdeck_command_help_text(const char *command)
{
    return command_text(command);
}

} // namespace cyberdeck_shell_help
