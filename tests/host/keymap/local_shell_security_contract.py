#!/usr/bin/env python3
"""Source-level contract for race-safe confined-file opening."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
SHELL = ROOT / "components/cyberdeck/src/features/shell/cyberdeck_local_shell.cpp"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    source = SHELL.read_text(encoding="utf-8")
    require("O_NOFOLLOW" in source, "file open must reject a symlink at open time")
    require(re.search(r'\bopen\s*\(', source), "cat must use a descriptor-based secure open")
    require(re.search(r'\bfstat\s*\(', source), "cat must verify the opened descriptor")
    require(re.search(r'(?:fdopen|read\s*\()', source),
            "cat must read from the descriptor that was validated")
    require("std::ifstream file(host" not in source,
            "cat must not reopen a path after a separate lstat preflight")
    print("PASS: local shell secure-open contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
