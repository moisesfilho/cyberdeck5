#!/usr/bin/env python3
"""Structural TDD contract for the bounded local-shell tokenizer fix."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
SHELL = ROOT / "components/cyberdeck/src/features/shell/cyberdeck_local_shell.cpp"
WORKER = ROOT / "components/cyberdeck/src/features/shell/cyberdeck_cat_worker.cpp"
HEADER = ROOT / "components/cyberdeck/include/features/shell/cyberdeck_cat_worker.h"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    shell = SHELL.read_text(encoding="utf-8")
    worker = WORKER.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")

    # The execute path must not pull locale/stringstream machinery onto the
    # bounded worker stack.  This is intentionally source-level and strict.
    for forbidden in ("#include <sstream>", "istringstream", "std::locale", "#include <locale>"):
        require(forbidden not in shell, f"local shell must not use {forbidden}")
    require(re.search(r"split_words|token", shell, re.I),
            "local shell must retain an explicit tokenizer seam")
    require(re.search(r"' '|'\\t'|isspace", shell),
            "tokenizer must explicitly recognize spaces/tabs as separators")
    require("std::vector<std::string>" in shell,
            "tokenizer must preserve command/option/argument token boundaries")

    # Keep the regression guard attached to the real worker and require the
    # approved auditable 6144-byte stack constant at the creation site.
    stack_match = re.search(
        r"(?:constexpr|const)\s+(?:size_t|uint32_t|unsigned)\s+"
        r"k_cat_worker_stack_size\s*=\s*6144\s*;",
        worker,
    )
    require(stack_match is not None,
            "cat worker stack constant must remain explicitly set to 6144")
    require(re.search(
        r"xTaskCreate\s*\([^;]*,\s*k_cat_worker_stack_size\s*,",
        worker,
        re.S,
    ), "cat worker must use the approved 6144-byte stack constant")
    require("cat_worker_task" in worker and "xQueueReceive" in worker,
            "cat worker and queue receive path must remain present")
    require("cyberdeck_cat_worker_teardown" in worker and "cyberdeck_cat_worker_teardown" in header,
            "cat worker lifecycle teardown must remain exposed")
    print("PASS: local-shell tokenizer structural contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
