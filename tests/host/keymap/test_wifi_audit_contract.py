#!/usr/bin/env python3
"""Structural contracts for the passive/local Wi-Fi audit integration."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
AUDIT = ROOT / "components/cyberdeck/src/features/wifi/cyberdeck_wifi_audit.cpp"
COMPONENT = ROOT / "components/cyberdeck/CMakeLists.txt"
MAKEFILE = ROOT / "tests/host/keymap/Makefile"

def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)

def function_body(source: str, name: str) -> str:
    """Return one sink body without inspecting test helpers or declarations."""
    marker = source.find(name)
    require(marker >= 0, f"audit must define {name}")
    opening = source.find("{", marker)
    require(opening >= 0, f"{name} must have a body")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    raise AssertionError(f"unterminated body for {name}")

def require_before(body: str, first: str, second: str, message: str) -> None:
    require(first in body, f"missing {first!r} while checking {message}")
    require(second in body, f"missing {second!r} while checking {message}")
    require(body.index(first) < body.index(second), message)

def main() -> int:
    ui = UI.read_text(encoding="utf-8")
    require("wifi audit" in ui, "TUI must recognize wifi audit")
    require("wifi audit export" in ui, "TUI must recognize audit export")
    require("confirmed" in ui or "confirm" in ui, "export needs explicit confirmation")
    require(AUDIT.exists(), "audit implementation must provide a dedicated module")
    audit = AUDIT.read_text(encoding="utf-8")
    lowered = audit.lower()
    for word in ("esp_wifi_scan_start", "esp_wifi_scan_get_ap_records", "probe",
                 "pcap", "promiscuous", "capture"):
        require(word not in lowered, f"audit must not use forbidden API/data: {word}")
    # Secret markers are checked only in actual UI/log/export sinks.  Do not
    # scan a detector/helper definition, or it will report its own literals.
    sinks = "\n".join(function_body(audit, name) for name in
                       ("std::string render_ui", "std::string render_log", "std::string safe_export"))
    for word in ("password", "passphrase", "psk"):
        require(word not in sinks.lower(), f"audit sink must not expose secret data: {word}")
    require("worker" in audit or "task" in audit, "audit must run outside callbacks/UI")
    require("snapshot" in audit and "token" in audit, "audit needs snapshot/token handoff")
    require("/sdcard" in audit, "export must be confined to /sdcard")
    require("cyberdeck_wifi_audit.cpp" in COMPONENT.read_text(encoding="utf-8"),
            "component build must register audit source")
    require("features/wifi/cyberdeck_wifi_audit.h" in MAKEFILE.read_text(encoding="utf-8"),
            "host test must include the production audit header")
    require("test_wifi_audit" in MAKEFILE.read_text(encoding="utf-8"),
            "host Makefile must register audit tests")
    # The host build has no ESP-IDF hardware, so this safety seam is checked
    # structurally instead of by inventing a fake worker API.
    run = function_body(audit, "void run()")
    require("struct work_item" in audit and "bool export_job" in audit,
            "worker items must distinguish audit from export work")
    require("operation::audit" in run and "operation::export_job" in run,
            "worker must have separate audit and export operations")
    require_before(run, "if (live && item.token != 0 && item.token == token)",
                   "esp_wifi_sta_get_ap_info", "lifecycle/token gate must precede hardware")
    require_before(run, "if (item.export_job && status == state::ready)",
                   "esp_wifi_sta_get_ap_info", "export classification must precede hardware")
    require_before(run, "if (!item.export_job && status == state::collecting)",
                   "esp_wifi_sta_get_ap_info", "audit classification must precede hardware")
    require("operation::stale" in run and "must not collect" in run,
            "stale/cancelled/teardown work must have an explicit discard path")
    stale = run[run.index("} else {", run.index("operation::export_job")):]
    require("esp_wifi_sta_get_ap_info" not in stale and
            "esp_netif_get_handle_from_ifkey" not in stale,
            "discard path must not collect from hardware")
    publish = function_body(audit, "void publish(")
    require_before(publish, "if (!live || t != token || status != state::collecting) return;",
                   "if (failed || !a.associated)", "publish must revalidate lifecycle/token first")

    teardown = function_body(audit, "void audit_controller::teardown()")
    require_before(teardown, "s.live = false;", "xQueueSend(s.work, &stop, portMAX_DELAY)",
                   "teardown must invalidate lifecycle before wakeup")
    require_before(teardown, "s.token = 0;", "xQueueSend(s.work, &stop, portMAX_DELAY)",
                   "teardown must invalidate token before wakeup")
    require_before(teardown, "xSemaphoreTake(s.stopped, portMAX_DELAY)", "vQueueDelete(s.exports)",
                   "teardown must join worker before deleting queues")
    print("PASS: wifi audit structural and safety contracts")
    return 0

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
