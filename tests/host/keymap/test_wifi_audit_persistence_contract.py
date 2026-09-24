#!/usr/bin/env python3
"""Structural contract for the real injectable Wi-Fi audit persistence layer.

The C++ test owns the behavioral assertions.  This script verifies that those
assertions are wired to the production header/source, the host target, and the
project map rather than silently exercising a test-only implementation.
"""
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
HEADER = ROOT / "components/cyberdeck/include/features/wifi/cyberdeck_wifi_audit_persistence.h"
SOURCE = ROOT / "components/cyberdeck/src/features/wifi/cyberdeck_wifi_audit_persistence.cpp"
CONTRACT = ROOT / "tests/host/keymap/contracts/cyberdeck_wifi_audit_persistence.h"
CPP = ROOT / "tests/host/keymap/test_wifi_audit_persistence.cpp"
MAKEFILE = ROOT / "tests/host/keymap/Makefile"
COMPONENT = ROOT / "components/cyberdeck/CMakeLists.txt"
CODEMAP = ROOT / "code-map.md"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_re(text: str, pattern: str, message: str) -> None:
    require(re.search(pattern, text, re.IGNORECASE | re.DOTALL) is not None, message)


def main() -> int:
    require(HEADER.exists(), "production audit persistence header is missing (expected RED)")
    require(SOURCE.exists(), "production audit persistence source is missing (expected RED)")
    require(CONTRACT.exists(), "host contract header is missing")
    require(CPP.exists(), "behavioral audit persistence test is missing")

    contract = CONTRACT.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    cpp = CPP.read_text(encoding="utf-8")
    makefile = MAKEFILE.read_text(encoding="utf-8")
    component = COMPONENT.read_text(encoding="utf-8")
    codemap = CODEMAP.read_text(encoding="utf-8")

    # The declaration seam is intentionally narrow: a fake adapter must be
    # enough to exercise the transaction, bounded hand-off, and ACK ownership.
    for symbol in (
        "class file_ops",
        "class completion_sink",
        "class audit_persistence",
        "open_exclusive",
        "std::ptrdiff_t write",
        "virtual int fsync",
        "virtual int close",
        "virtual int rename",
        "virtual int unlink",
        "rejected_queue_full",
        "pump_one",
        "drain",
        "in_flight",
        "initialize",
        "teardown",
        "enqueue",
        "pump_one",
        "pending",
    ):
        require(symbol in contract, f"contract header must declare {symbol}")
        require(symbol in header, f"production header must expose {symbol}")

    for symbol in (
        "artifact_state",
        "persistence_error",
        "submit_status",
        "queue_capacity",
        "max_payload",
    ):
        require(symbol in contract, f"contract header must define {symbol}")
        require(symbol in header, f"production header must define {symbol}")

    # The production implementation must own the durable sequence rather than
    # treating a successful return from write as publication.
    for token in (
        "open_exclusive",
        "write",
        "fsync",
        "close",
        "rename",
        "recover",
        "rollback",
        "completion",
        "publish",
        "drain",
        "queue",
    ):
        require_re(source, re.escape(token), f"production persistence must use {token}")
    require(".wifi-audit.tmp" in source, "production persistence must use the stable .tmp slot")
    require(".wifi-audit.bak" in source, "production persistence must use the stable .bak slot")
    require(
        re.search(r"mutex|lock_guard|unique_lock|atomic|condition_variable", source, re.I)
        is not None,
        "production persistence must synchronize concurrent enqueue/pump/ACK paths",
    )
    require("std::atomic<bool> torn_down" in source and
            "std::atomic<bool> initialized" in source,
            "production persistence must publish terminal lifecycle state atomically")
    require("std::lock_guard<std::mutex> lock(impl_->mutex);" in source,
            "production persistence must serialize teardown with pump/enqueue/drain")
    require(
        re.search(r"rollback.{0,160}(preserve|preserv|\.bak|\.tmp)|((preserv|\.bak|\.tmp).{0,160}rollback)",
                 source, re.I | re.S)
        is not None,
        "rollback failure path must explicitly preserve recoverable sidecars",
    )

    # The host target must compile the real production source, include the
    # declared header, and link the threading runtime used by the concurrency
    # tests.
    require("test_wifi_audit_persistence.cpp" in makefile, "Makefile must register the behavioral test")
    require("cyberdeck_wifi_audit_persistence.cpp" in makefile, "Makefile must link production persistence source")
    require("cyberdeck_wifi_audit_persistence.h" in makefile, "Makefile must depend on production header")
    require("test_wifi_audit_persistence_contract" in makefile, "Makefile must register the structural contract")
    require("test_wifi_audit_persistence" in makefile, "Makefile must register the behavioral target")
    require("BINS" in makefile and "test_wifi_audit_persistence" in makefile.split("BINS", 1)[1].split("\n", 1)[0],
            "behavioral binary must be cleaned by the host BINS list")
    require("-pthread" in makefile, "Makefile must link -pthread for the concurrency host test")

    require("cyberdeck_wifi_audit_persistence.cpp" in component,
            "component build must register the production persistence source")
    require("cyberdeck_wifi_audit_persistence" in codemap,
            "code-map.md must document the injectable persistence seam")
    require("test_wifi_audit_persistence.cpp" in codemap,
            "code-map.md must map the new host test")

    # Guard against accidentally replacing the behavioral suite with a source
    # grep-only contract.
    for scenario in (
        "test_success_publishes_exact_content_before_ack",
        "test_existing_destination_uses_backup_then_publishes",
        "test_all_io_failures_are_reported_without_success_ack",
        "test_recovery_restores_backup",
        "test_recovery_preserves_orphan_tmp",
        "test_recovery_rename_failure",
        "test_rollback_restores_old_destination",
        "test_rollback_failure_preserves_both_recoverable_sidecars",
        "test_single_shot_queue_full",
        "test_ack_delivery_failure",
        "test_concurrent_enqueue",
        "test_concurrent_teardown_with_pump_enqueue_and_drain",
    ):
        require(scenario in cpp, f"behavioral test is missing scenario {scenario}")
    require("<thread>" in cpp and "std::thread" in cpp,
            "behavioral test must exercise real host concurrency")
    require("fail_write" in cpp and "fail_fsync" in cpp and "fail_close" in cpp and
            "fail_rename_once" in cpp,
            "behavioral test must inject every requested I/O failure")

    print("PASS: wifi audit persistence structural contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
