#!/usr/bin/env python3
"""Structural guard for the ESP FATFS directory-enumeration branch."""

from pathlib import Path


SOURCE = Path(__file__).resolve().parents[3] / "components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp"
source = SOURCE.read_text(encoding="utf-8")
start = source.index("DIR *cyberdeck_local_shell_opendir(")
end = source.index("\nnamespace {", start)
function = source[start:end]
esp_start = function.index("#if defined(ESP_PLATFORM)")
host_start = function.index("#else", esp_start)
esp_branch = function[esp_start:host_start]

checks = {
    "ESP branch validates virtual paths before touching the filesystem":
        esp_branch.index("valid_virtual_path") < esp_branch.index("::stat(") < esp_branch.index("::opendir("),
    "ESP branch rejects non-directories before enumeration":
        esp_branch.index("!S_ISDIR(info.st_mode)") < esp_branch.index("::opendir("),
    "ESP branch uses direct opendir, not descriptor-only O_DIRECTORY":
        "return ::opendir(physical_path.c_str());" in esp_branch and "::open(" not in esp_branch,
    "virtual /sdcard alias and unsafe relative paths remain rejected":
        'relative == "sdcard"' in esp_branch and "cat_relative_safe(relative)" in esp_branch,
}

for description, passed in checks.items():
    print(f"{'PASS' if passed else 'FAIL'}: {description}")
if not all(checks.values()):
    raise SystemExit(1)
