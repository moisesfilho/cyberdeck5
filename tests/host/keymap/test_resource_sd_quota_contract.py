#!/usr/bin/env python3
"""Host-only acceptance contracts for the approved distribution boundary."""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
INCLUDE = ROOT / "components/cyberdeck/include/apps/runtime"
SOURCE = ROOT / "components/cyberdeck/src/apps/runtime"
PLAN = ROOT / "docs/OS-TRANSFORMATION-PLAN.pt-BR.md"
TRACE = ROOT / "tests/host/keymap/os_transformation_traceability.md"

def text(path: Path, failures: list[str]) -> str:
    if not path.exists():
        failures.append(f"implementation absent: {path.relative_to(ROOT)}")
        return ""
    return path.read_text(encoding="utf-8")

def require(value: bool, message: str, failures: list[str]) -> None:
    if not value:
        failures.append(message)

def resource_catalog_contract(failures: list[str]) -> None:
    header = text(INCLUDE / "cyberdeck_resource_catalog.h", failures)
    source = text(SOURCE / "cyberdeck_resource_catalog.cpp", failures)
    require("std::array" in header, "REQ-RES-01: compiled asset catalog must use fixed storage", failures)
    require("k_max" in header and "asset" in header.lower(), "AC-RES-01: catalog capacity must be named and bounded", failures)
    require("const" in header and "at(" in header, "AC-RES-01: catalog lookup must be readonly", failures)
    require(not re.search(r"\b(push_back|emplace_back|resize|vector|list)\b", header), "AC-RES-01: catalog must not expose unbounded growth", failures)
    require("missing" in source.lower() or "absent" in source.lower(), "REQ-RES-01: absent assets must not break boot", failures)

def sd_package_contract(failures: list[str]) -> None:
    header = text(INCLUDE / "cyberdeck_sd_package.h", failures)
    source = text(SOURCE / "cyberdeck_sd_package.cpp", failures)
    for token in ("metadata", "version", "checksum", "data", "assets"):
        require(token in header.lower(), f"REQ-SD-01: SD package must declare {token}", failures)
    require("absent" in source.lower() or "missing" in source.lower(), "AC-SD-01: absent package must fail closed", failures)
    require("corrupt" in source.lower() or "checksum" in source.lower(), "AC-SD-01: checksum failure must fail closed", failures)
    require(not re.search(r"\b(install|execute|loader|elf|dlopen|xTaskCreate)\b", header + source, re.I), "AC-SD-01: package must not install or execute code", failures)

def quota_contract(failures: list[str]) -> None:
    header = text(INCLUDE / "cyberdeck_app_quota.h", failures)
    source = text(SOURCE / "cyberdeck_app_quota.cpp", failures)
    for token in ("resources", "grants", "stack", "queue", "bounded_read", "logger", "output", "lifecycle"):
        require(token in header.lower(), f"REQ-QUOTA-01: quota must name {token}", failures)
    require("std::array" in header and "k_max" in header, "AC-QUOTA-01: quotas must have fixed named storage", failures)
    require("revoke" in header.lower() or "generation" in source.lower(), "AC-QUOTA-01: grants must be revocable", failures)
    require(not re.search(r"\b(vector|deque|list|push_back|emplace_back)\b", header), "AC-QUOTA-01: quota state must not grow without bound", failures)

def guardrail_contract(failures: list[str]) -> None:
    plan = text(PLAN, failures)
    trace = text(TRACE, failures)
    corpus = (plan + trace).lower()
    for token in ("req-guard-01", "event bus", "reply universal", "loader", "elf", "tactility", "multi-device"):
        require(token in corpus, f"REQ-GUARD-01: guardrail documentation missing {token}", failures)
    require("fora de escopo" in plan.lower() and "sandbox" in plan.lower(), "AC-GUARD-01: dynamic loading must remain behind sandbox guardrails", failures)

def traceability_contract(failures: list[str]) -> None:
    trace = text(TRACE, failures)
    for req in ("REQ-RES-01", "REQ-SD-01", "REQ-QUOTA-01", "REQ-GUARD-01", "REQ-TRACE-01"):
        require(req in trace, f"REQ-TRACE-01: traceability missing {req}", failures)
    require("CASE_ID" in trace or "<PII type=" in trace, "AC-TRACE-01: approved case identifier is missing", failures)
    require("multi-device" in trace.lower() and "fora de escopo" in (trace + text(PLAN, failures)).lower(),
            "AC-TRACE-01: matrix must document, not implement, excluded device scope", failures)

def main() -> int:
    failures: list[str] = []
    for contract in (resource_catalog_contract, sd_package_contract, quota_contract, guardrail_contract, traceability_contract):
        contract(failures)
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("PASS: resource, SD package, quota and scope contracts")
    return 0

if __name__ == "__main__":
    sys.exit(main())
