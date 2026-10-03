"""TEST-CAP-01..04 traceability and wiring contract for Phase 9."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
TEST = Path(__file__).with_name("test_phase9_capabilities.cpp")
MAKEFILE = Path(__file__).with_name("Makefile")

TRACE = {
    "TEST-CAP-01": ("display", "input", "storage", "network"),
    "TEST-CAP-02": ("ble", "serial", "screenshot", "unknown", "duplicate", "over-limit"),
    "TEST-CAP-03": ("event_log", "clock", "battery", "invalid display context", "stale"),
    "TEST-CAP-04": ("init-fail", "start-fail", "teardown-fail", "timeout", "independent", "restart"),
}


def main() -> int:
    source = TEST.read_text(encoding="utf-8")
    makefile = MAKEFILE.read_text(encoding="utf-8")
    failures = []
    for test_id, markers in TRACE.items():
        if f"{test_id}:" not in source:
            failures.append(f"{test_id} marker is missing")
        for marker in markers:
            if marker not in source:
                failures.append(f"{test_id} lacks scenario marker {marker!r}")
    for required in ("test_phase9_capabilities", "APP_FACADES_SRC", "APP_RUNTIME_SRC"):
        if required not in makefile:
            failures.append(f"Makefile lacks {required}")
    if not failures:
        print("PASS: phase 9 traceability (REQ/AC -> TEST -> evidence)")
        return 0
    for failure in failures:
        print(f"FAIL: {failure}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
