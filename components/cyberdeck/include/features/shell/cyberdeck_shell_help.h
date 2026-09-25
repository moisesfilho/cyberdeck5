#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace cyberdeck_shell_help {

// The catalog is the only ordered source for help rows.  Keeping the table
// header-only makes it usable by host-only shell consumers without bringing
// LVGL, ESP-IDF, or UI state into the formatter.
struct entry {
    std::string_view command;
    std::string_view usage;
    std::string_view description;
};

inline constexpr std::array<entry, 15> kCatalog = {{
    {"help", "help", "show this help"},
    {"wifi", "wifi [search|saved|audit]", "show status, manage Wi-Fi, or audit"},
    {"log", "log", "show recent events"},
    {"clear", "clear", "clear the terminal"},
    {"screen", "screen [on|off|timeout <0-1440>]", "control screen protection"},
    {"battery", "battery [protection on|off|status]", "show or control battery protection"},
    {"ssh", "ssh [user@]host[:port]", "start an SSH session"},
    {"pwd", "pwd", "print working directory"},
    {"cd", "cd [path]", "change working directory"},
    {"ls", "ls [-a] [path]", "list directory contents"},
    {"cat", "cat <file>", "print a regular file"},
    {"touch", "touch <file>", "create an empty file"},
    {"mkdir", "mkdir <directory>", "create a directory"},
    {"rm", "rm [-r] <path>", "remove a file or directory"},
    {"rmdir", "rmdir <directory>", "remove an empty directory"},
}};

inline std::string format_entry(const entry &item)
{
    std::string result;
    result.reserve(item.usage.size() + item.description.size() + 4);
    result.append(item.usage.data(), item.usage.size());
    result += " - ";
    result.append(item.description.data(), item.description.size());
    result.push_back('\n');
    return result;
}

inline std::string text()
{
    std::size_t capacity = 0;
    for (const entry &item : kCatalog) {
        capacity += item.usage.size() + item.description.size() + 4;
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
