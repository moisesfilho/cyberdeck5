#!/usr/bin/env python3
"""Structural contract for the fixed, metadata-only virtual namespaces."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
HEADER = ROOT / "components/cyberdeck/include/apps/shell/cyberdeck_vfs_namespace.h"
SOURCE = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_vfs_namespace.cpp"
SHELL = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    shell = SHELL.read_text(encoding="utf-8")
    for name in ("apps", "data", "dev", "tmp", "system"):
        require(f'{{"{name}"' in source, f"missing fixed namespace {name}")
    require("k_namespace_count = 5" in header, "namespace count must be fixed")
    require("char path[k_max_path_bytes + 1]" in header, "resolver output must be bounded")
    require("std::vector" not in source and "new " not in source,
            "namespace resolver must not use unbounded allocation")
    for forbidden in ("esp_", "freertos", "lvgl", "open(", "opendir("):
        require(forbidden not in source.lower(), f"pure namespace module imports {forbidden}")
    require("cyberdeck_vfs_namespace::resolve" in shell,
            "local shell must consult the namespace resolver")
    require("virtual namespace is read-only metadata" in shell,
            "virtual namespace operations must fail explicitly")
    require("src/apps/shell/cyberdeck_vfs_namespace.cpp" in
            (ROOT / "components/cyberdeck/CMakeLists.txt").read_text(encoding="utf-8"),
            "ESP-IDF component must compile the namespace module")
    print("PASS: local-shell namespace structural contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
