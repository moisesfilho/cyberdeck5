#!/usr/bin/env python3
"""Regression contract for the cat worker stack budget.

The host cannot reproduce an ESP-IDF stack-protection fault, but it can reject
the known dangerous configuration before a firmware build: the worker invokes
the path-heavy local-shell ``cat`` flow while running with a 4096-byte stack.
This contract deliberately does not prescribe heap allocation or alter the
confinement/size contracts; it only requires explicit headroom for that call
chain and keeps the worker stack declaration easy to audit.
"""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
WORKER = ROOT / "components/cyberdeck/src/features/shell/cyberdeck_cat_worker.cpp"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    source = WORKER.read_text(encoding="utf-8")

    # Keep the stack size named and visible at the task creation site.  A
    # literal 4096 is the configuration that reproduced the Guru Meditation.
    match = re.search(
        r'xTaskCreate\s*\(\s*[^,]+,\s*"[^"]+"\s*,\s*([^,\s]+)',
        source,
        re.DOTALL,
    )
    require(match is not None, "cat worker stack size must be explicit")
    stack_token = match.group(1)
    require(stack_token != "4096", "cat worker must not regress to the 4096-byte stack")

    if stack_token.isdigit():
        require(int(stack_token) >= 6144,
                "cat worker stack must provide headroom for local-shell cat")
    else:
        declaration = re.search(
            rf"(?:constexpr|const)\s+(?:size_t|uint32_t|unsigned)\s+{re.escape(stack_token)}\s*=\s*(\d+)",
            source,
        )
        require(declaration is not None,
                "cat worker stack size token must have an auditable constant declaration")
        require(int(declaration.group(1)) >= 6144,
                "cat worker stack constant must provide local-shell headroom")

    require("cat_worker_task" in source and "cyberdeck_local_shell" in source,
            "regression must remain attached to the real cat worker call path")
    print("PASS: cat worker stack-footprint contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
