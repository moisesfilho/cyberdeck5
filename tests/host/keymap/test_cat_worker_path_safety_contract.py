#!/usr/bin/env python3
"""Structural regression contract for the cat worker stack-overflow fix.

    The worker must not instantiate or execute the general local shell.  That
    path performs component normalization with ``std::filesystem::path`` and bounded vectors,
which is still enough to overflow the 6144-byte worker stack on the target.
Cat needs a dedicated, bounded API whose result owns its output on the heap.
"""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
WORKER = ROOT / "components/cyberdeck/src/features/shell/cyberdeck_cat_worker.cpp"
SHELL = ROOT / "components/cyberdeck/src/features/shell/cyberdeck_local_shell.cpp"
SHELL_HEADER = ROOT / "components/cyberdeck/include/features/shell/cyberdeck_local_shell.h"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def body(source: str, marker: str) -> str:
    start = source.find(marker)
    require(start >= 0, f"missing {marker}")
    opening = source.find("{", start)
    require(opening >= 0, f"missing body for {marker}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    raise AssertionError(f"unterminated body for {marker}")


def main() -> int:
    worker = WORKER.read_text(encoding="utf-8")
    shell = SHELL.read_text(encoding="utf-8")
    header = SHELL_HEADER.read_text(encoding="utf-8")

    task = body(worker, "void cat_worker_task")
    # Do not ban the shared ``cyberdeck_local_shell_*`` identifier prefix: the
    # dedicated cat API intentionally has that prefix and is the required
    # worker seam.  Reject only construction/use of the generic shell/path
    # machinery.
    for forbidden in (
        r"\bcyberdeck_local_shell\s+\w+\b",
        r"std::filesystem::path",
        r"fs::path",
        r"std::vector",
        r"resolve\(",
        r"\.execute\(",
    ):
        require(not re.search(forbidden, task),
                f"cat worker must not use general/path-heavy operation: {forbidden}")

    require("cyberdeck_local_shell.h" not in worker,
            "cat worker must not include the general local-shell API")
    require(re.search(r"cyberdeck_local_shell_result\s+cyberdeck_local_shell_cat\s*\(", header),
            "local shell must expose a cat-specific result API")
    require("cyberdeck_local_shell_cat(" in task,
            "cat worker must call the dedicated cat API")
    require("lv_async_call" in task,
            "cat worker must hand results back asynchronously")

    cat = body(shell, "cyberdeck_local_shell_cat")
    for forbidden in ("std::vector", "fs::path", "std::filesystem::path"):
        require(forbidden not in cat,
                f"cat-specific API must not construct path/vector objects: {forbidden}")
    require("std::string output" in cat and "output.reserve" in cat,
            "cat-specific API must own output in bounded heap-backed storage")
    require("12288" in cat and "1024" in cat,
            "cat-specific API must preserve 12288-byte output and 1024-byte read bounds")
    require("S_ISREG" in cat and "fstat" in cat,
            "cat-specific API must validate the opened object as a regular file")
    require("O_NOFOLLOW" in cat or "openat" in cat,
            "cat-specific API must retain descriptor/symlink safety")
    require("output.size() > k_cat_max_output_bytes" in cat,
            "cat-specific API must enforce the accumulated output bound")

    print("PASS: cat worker path-safety and dedicated-API contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
