#!/usr/bin/env python3
"""Deterministic host contracts for the remaining OS transformation backlog."""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
RUNTIME = ROOT / "components/cyberdeck/include/apps/runtime"
RUNTIME_SRC = ROOT / "components/cyberdeck/src/apps/runtime"
SHELL = ROOT / "components/cyberdeck/src/apps/shell"
SHELL_H = ROOT / "components/cyberdeck/include/apps/shell"
MAIN = ROOT / "main/app_main.cpp"
SYSTEM = ROOT / "components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp"
SSH = ROOT / "components/cyberdeck/src/apps/ssh/ssh_client.cpp"
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
PLAN = ROOT / "docs/OS-TRANSFORMATION-PLAN.pt-BR.md"
TRACE = ROOT / "tests/host/keymap/os_transformation_traceability.md"


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def read(path: Path, failures: list[str]) -> str:
    if not path.exists():
        failures.append(f"implementation gap: missing {path.relative_to(ROOT)}")
        return ""
    return path.read_text(encoding="utf-8")


def storage_contract(failures: list[str]) -> None:
    header = read(RUNTIME / "cyberdeck_app_storage.h", failures)
    source = read(RUNTIME_SRC / "cyberdeck_app_storage.cpp", failures)
    require("bounded_read" in header, "REQ-SDK-01: storage must expose bounded_read", failures)
    require("read_status" in header and "eof" in header and "error" in header,
            "REQ-SDK-01: read result must distinguish success, EOF, and error", failures)
    require("ownership" in header.lower(), "REQ-SDK-01: output ownership must be documented", failures)
    require("k_max_read_bytes" in header, "AC-SDK-01: read budget must be named and bounded", failures)
    require(not re.search(r"\b(FILE|DIR|fd|handle|vfs)\b", header, re.I),
            "REQ-SDK-01: public storage API must not expose VFS or handles", failures)
    require("capacity" in header and "bytes" in header,
            "AC-SDK-01: bounded_read must expose capacity and byte count", failures)
    require("bounded_read" in source, "REQ-SDK-01: storage implementation is missing", failures)


def logger_contract(failures: list[str]) -> None:
    header = read(RUNTIME / "cyberdeck_app_logger.h", failures)
    source = read(RUNTIME_SRC / "cyberdeck_app_logger.cpp", failures)
    require("enum class level" in header, "REQ-LOG-01: logger levels must be typed", failures)
    require("event" in header and "payload" in header,
            "REQ-LOG-01: logger must define bounded events and payloads", failures)
    require("k_max_" in header, "AC-LOG-01: logger limits must be named bounds", failures)
    require("string_view" in header and "std::string" not in header,
            "REQ-LOG-01: logger input must not be unbounded owned strings", failures)
    require("secret" in header.lower() or "redact" in header.lower(),
            "AC-LOG-01: logger must explicitly exclude or redact secrets", failures)
    require("write" in source or "write_event" in source,
            "REQ-LOG-01: bounded logger implementation is missing", failures)


def command_catalog_contract(failures: list[str]) -> None:
    header = read(RUNTIME / "cyberdeck_command_catalog.h", failures)
    source = read(RUNTIME_SRC / "cyberdeck_command_catalog.cpp", failures)
    manifest = read(RUNTIME / "cyberdeck_app_runtime.h", failures)
    console = read(SHELL_H / "cyberdeck_shell_console.h", failures)
    require("catalog" in header.lower() and "manifest" in header.lower(),
            "REQ-CMD-01: catalog must be an explicit manifest-backed contract", failures)
    require("k_max" in header and "command" in header,
            "AC-CMD-01: command catalog must be bounded", failures)
    require("dispatch" in header and "legacy" in header.lower(),
            "REQ-CMD-01: catalog must define dispatch and legacy preservation", failures)
    require("commands" in manifest and "command_count" in manifest,
            "REQ-CMD-01: manifests must retain bounded declared commands", failures)
    require("is_legacy_command" in console or "legacy" in console.lower(),
            "AC-CMD-01: legacy command ownership must remain explicit", failures)
    require("catalog" in source.lower(), "REQ-CMD-01: catalog implementation is missing", failures)


def reply_contract(failures: list[str]) -> None:
    ui = read(UI, failures)
    ssh = read(SSH, failures)
    console = read(SHELL / "cyberdeck_shell_console.cpp", failures)
    require("s_scrollback.append(data, len);" in ui,
            "REQ-REPLY-01: existing output must be retained verbatim", failures)
    require("ssh_channel_write" in ssh, "REQ-REPLY-01: SSH transport path must remain", failures)
    require("framing" not in console.lower() and "universal_reply" not in ui,
            "AC-REPLY-01: universal reply framing must not be introduced", failures)
    require("cyberdeck_ui_term_dump" in ui,
            "AC-REPLY-01: existing output inspection path must remain", failures)


def boot_contract(failures: list[str]) -> None:
    main = read(MAIN, failures)
    system = read(SYSTEM, failures)
    require("cyberdeck_system_apps_register" in main and "cyberdeck_system_apps_start" in main,
            "REQ-BOOT-01: boot must delegate composition to the supervisor", failures)
    require("cyberdeck_recovery::safe_mode" in main and "cyberdeck_system_apps_start_safe_mode" in main,
            "AC-BOOT-01: safe mode recovery path must remain explicit", failures)
    require("start_all" in system, "REQ-BOOT-01: startup must use declarative supervisor order", failures)
    require(main.find("cyberdeck_recovery::init") < main.find("cyberdeck_system_apps_register"),
            "AC-BOOT-01: recovery attempt must precede supervisor startup", failures)
    require(main.find("cyberdeck_system_apps_commit_ready") > main.find("cyberdeck_system_apps_start"),
            "AC-BOOT-01: checkpoint must follow startup", failures)


def documentation_contract(failures: list[str]) -> None:
    plan = read(PLAN, failures)
    trace = read(TRACE, failures)
    for identifier in ("REQ-SDK-01", "REQ-LOG-01", "REQ-CMD-01", "REQ-REPLY-01", "REQ-BOOT-01", "REQ-DOC-01",
                       "AC-SDK-01", "AC-LOG-01", "AC-CMD-01", "AC-REPLY-01", "AC-BOOT-01", "AC-DOC-01"):
        require(identifier in trace, f"REQ-DOC-01: traceability is missing {identifier}", failures)
    require("Backlog do SDK" in plan and "bounded-read" in plan,
            "REQ-DOC-01: approved backlog must remain documented", failures)


def main() -> int:
    failures: list[str] = []
    for contract in (storage_contract, logger_contract, command_catalog_contract,
                     reply_contract, boot_contract, documentation_contract):
        contract(failures)
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("PASS: OS transformation backlog contracts")
    return 0


if __name__ == "__main__":
    sys.exit(main())
