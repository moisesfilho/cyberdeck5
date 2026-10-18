#pragma once

#include <string_view>

namespace cyberdeck_help_test {

// Approved legacy catalog payload before production appends runtime manifests.
// New formatter contract: every entry renders as
//   "<command> [terminal]\n" "  <usage> - <description>\n" <parameters lines>
// where parameters lines (when present) are two-space indented and the
// catalog's trailing newline always closes the last rendered line.
// Production must render this through cyberdeck_help_text(); this file is
// test-only and is never a second production help implementation.
inline constexpr std::string_view kUnifiedHelpText =
    "help [terminal]\n"
    "  help - show this help\n"
    "wifi [terminal]\n"
    "  wifi [search|saved|audit|survey] - manage Wi-Fi, passively survey networks, and explicitly export results\n"
    "  search: scan for networks\n"
    "  saved: manage saved networks\n"
    "  audit: inspect or export Wi-Fi audit results\n"
    "  survey: run a passive survey\n"
    "log [terminal]\n"
    "  log [lines <1-64>] - show recent events\n"
    "  lines <1-64>: set the number of recent events\n"
    "clear [terminal]\n"
    "  clear - clear the terminal\n"
    "screen [terminal]\n"
    "  screen [on|off|timeout <0-1440>] - control screen protection\n"
    "  on|off: enable or disable screen protection\n"
    "  timeout <0-1440>: set inactivity timeout in minutes; 0 disables it\n"
    "battery [terminal]\n"
    "  battery [protection on|off|status] - show or control battery protection\n"
    "  protection on: enable battery protection\n"
    "  protection off: disable battery protection\n"
    "  protection status: show current protection status\n"
    "bluetooth [terminal]\n"
    "  bluetooth [search|paired] - search for or list paired Bluetooth devices\n"
    "  search: find nearby devices\n"
    "  paired: list paired devices\n"
    "app [terminal]\n"
    "  app [list|info|start|stop] [id] - manage compiled-in applications\n"
    "  list: list registered applications\n"
    "  info <id>: show application details\n"
    "  start <id>: start an application\n"
    "  stop <id>: stop an application\n"
    "ssh [terminal]\n"
    "  ssh [user@]host[:port] - start an SSH session\n"
    "  [user@]: optional login name\n"
    "  host: remote host name or address\n"
    "  [:port]: optional port (default 22)\n"
    "pwd [terminal]\n"
    "  pwd - print working directory\n"
    "cd [terminal]\n"
    "  cd [path] - change working directory\n"
    "  [path]: destination (default home directory)\n"
    "ls [terminal]\n"
    "  ls [-a] [path] - list directory contents\n"
    "  [-a]: include hidden entries\n"
    "  [path]: directory to list (default current directory)\n"
    "cat [terminal]\n"
    "  cat <file> - print a regular file\n"
    "  <file>: file to display\n"
    "touch [terminal]\n"
    "  touch <file> - create an empty file\n"
    "  <file>: file to create\n"
    "mkdir [terminal]\n"
    "  mkdir <directory> - create a directory\n"
    "  <directory>: directory to create\n"
    "rm [terminal]\n"
    "  rm [-r] <path> - remove a file or directory\n"
    "  [-r]: recursively remove a directory\n"
    "  <path>: target to remove\n"
    "rmdir [terminal]\n"
    "  rmdir <directory> - remove an empty directory\n"
    "  <directory>: empty directory to remove\n"
    "poweroff [terminal]\n"
    "  poweroff - power off this device after exact y confirmation\n"
    "  Confirmation: type exactly y when prompted; any other input cancels\n";

} // namespace cyberdeck_help_test
