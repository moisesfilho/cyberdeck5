#pragma once

#include <string_view>

namespace cyberdeck_help_test {

// The host tests' approved unified catalog.  The existing common-command
// order is retained and local commands follow it.  Production must render
// this through cyberdeck_help_text(); this file is test-only and is never a
// second production help implementation.
inline constexpr std::string_view kUnifiedHelpText =
    "help - show this help\n"
    "wifi [search|saved|audit] - show status, manage Wi-Fi, or audit\n"
    "log - show recent events\n"
    "clear - clear the terminal\n"
    "screen [on|off|timeout <0-1440>] - control screen protection\n"
    "battery [protection on|off|status] - show or control battery protection\n"
    "ssh [user@]host[:port] - start an SSH session\n"
    "pwd - print working directory\n"
    "cd [path] - change working directory\n"
    "ls [-a] [path] - list directory contents\n"
    "cat <file> - print a regular file\n"
    "touch <file> - create an empty file\n"
    "mkdir <directory> - create a directory\n"
    "rm [-r] <path> - remove a file or directory\n"
    "rmdir <directory> - remove an empty directory\n";

} // namespace cyberdeck_help_test
