#pragma once

#include <string_view>

namespace cyberdeck_help_test {

// Approved legacy catalog payload before production appends runtime manifests.
// Production must render this through cyberdeck_help_text(); this file is
// test-only and is never a second production help implementation.
inline constexpr std::string_view kUnifiedHelpText =
    "help - show this help [terminal]\n"
    "wifi [search|saved|audit] - show status, manage Wi-Fi, or audit [terminal]\n"
    "log [lines <1-64>] - show recent events [terminal]\n"
    "clear - clear the terminal [terminal]\n"
    "screen [on|off|timeout <0-1440>] - control screen protection [terminal]\n"
    "battery [protection on|off|status] - show or control battery protection [terminal]\n"
    "bluetooth [search|paired] - search for or list paired Bluetooth devices [terminal]\n"
    "app [list|info|start|stop] [id] - manage compiled-in applications [terminal]\n"
    "ssh [user@]host[:port] - start an SSH session [terminal]\n"
    "pwd - print working directory [terminal]\n"
    "cd [path] - change working directory [terminal]\n"
    "ls [-a] [path] - list directory contents [terminal]\n"
    "cat <file> - print a regular file [terminal]\n"
    "touch <file> - create an empty file [terminal]\n"
    "mkdir <directory> - create a directory [terminal]\n"
    "rm [-r] <path> - remove a file or directory [terminal]\n"
    "rmdir <directory> - remove an empty directory [terminal]\n"
    "poweroff - power off this device after exact y confirmation [terminal]\n";

} // namespace cyberdeck_help_test
