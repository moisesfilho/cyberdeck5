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
    "clear [terminal]\n"
    "  clear - clear the terminal\n"
    "app [terminal]\n"
    "  app [list|info|start|stop] [id] - manage compiled-in applications\n"
    "  list: list registered applications\n"
    "  info <id>: show application details\n"
    "  start <id>: start an application\n"
    "  stop <id>: stop an application\n"
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
