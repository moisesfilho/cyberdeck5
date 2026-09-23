#!/usr/bin/env python3
"""Structural TDD contract for asynchronous, confined local ``cat``."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
SHELL = ROOT / "components/cyberdeck/src/features/shell/cyberdeck_local_shell.cpp"
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
WORKER = ROOT / "components/cyberdeck/src/features/shell/cyberdeck_cat_worker.cpp"
WORKER_HEADER = ROOT / "components/cyberdeck/include/features/shell/cyberdeck_cat_worker.h"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    shell = SHELL.read_text(encoding="utf-8")
    ui = UI.read_text(encoding="utf-8")
    worker = WORKER.read_text(encoding="utf-8")
    header = WORKER_HEADER.read_text(encoding="utf-8")
    source = shell + "\n" + ui + "\n" + worker
    require(re.search(r'command\s*==\s*"cat"', shell), "local shell must recognize cat")
    require("cat" in shell and "help" in shell, "help must document cat")
    require("12288" in source, "cat output budget must be exactly 12288 bytes")
    require(re.search(r'(?:file_size|stat|fstat)[\s\S]{0,500}(?:limit|LIMIT|12288)', shell),
            "cat must preflight file size against the output limit")
    require(re.search(r'(?:regular|S_ISREG|is_regular_file)', shell),
            "cat must accept regular files only")
    require(".." in shell and ("symlink" in shell or "S_ISLNK" in shell),
            "cat path resolution must retain traversal and symlink protection")
    require(re.search(r'(?:chunk|CHUNK|buffer|BUFFER)[^\n]{0,100}(?:256|512|1024|2048|4096)', shell),
            "cat must use a bounded read chunk")
    require(re.search(r'(?:Queue|queue|fifo|FIFO)', source), "cat work must use a queue/fifo handoff")
    require(re.search(r'(?:worker|WORKER|task|Task)', source), "cat I/O must run in a worker/task")
    require(re.search(r'(?:lv_async_call|async|Async)', source), "cat result must be handed back asynchronously")
    # The real declaration is `k_cat_work_queue_capacity = 8`; do not infer
    # capacity from an unrelated number elsewhere in the source.
    require(re.search(r'k_cat_work_queue_capacity\s*=\s*8\b', worker),
            "cat handoff must declare queue capacity 8")
    require(re.search(r'static_assert\s*\(\s*CAT_WORK_QUEUE_CAPACITY\s*==\s*8', ui),
            "UI cat handoff must preserve queue capacity 8")
    require("xQueueSend(s_queue, &request, 0)" in worker,
            "enqueue must be non-blocking and expose the full-queue result")
    require(re.search(r'(?:stop|shutdown|teardown|destroy|deinit|join)', worker, re.I),
            "cat worker must provide an explicit lifecycle teardown")
    require(re.search(r'(?:stop|shutdown|teardown|destroy|deinit|join)', header, re.I),
            "cat worker header must expose lifecycle teardown")
    require(re.search(r'(?:generation|token|active|stale|closing|destroyed)', worker + header, re.I),
            "late async callbacks must have an invalidation/lifecycle guard")
    require(re.search(r'lv_async_call[\s\S]{0,250}(?:delete|free|reject|failure|LV_RESULT)', worker),
            "lv_async_call failure must release or reject its result")
    for forbidden in ("system(", "popen("):
        require(forbidden not in shell, f"cat implementation must not use {forbidden}")
    print("PASS: cat structural contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
