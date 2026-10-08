#!/usr/bin/env python3
"""Behavioral tests for the fail-closed production coverage scope guard."""
from __future__ import annotations

import contextlib
import io
import json
import subprocess
import tempfile
from pathlib import Path
import runpy
import importlib.util
import sys

ROOT = Path(__file__).resolve().parents[3]
GUARD = ROOT / "tools/coverage_scope_guard.py"
_GUARD_SOURCE = runpy.run_path(str(GUARD))
ALLOWLIST = _GUARD_SOURCE["ALLOWLIST"]
FORBIDDEN_ALLOWLIST = _GUARD_SOURCE["FORBIDDEN_ALLOWLIST"]
# Production source root used by the CI invocation of the guard
# (.github/workflows/quality-gate.yml: --source-root components/cyberdeck/src).
SOURCE_ROOT = ROOT / "components/cyberdeck/src"

# The hardware/ESP-IDF adapters allowlisted before this change.  They stay
# allowlisted; this list only pins what must NOT disappear silently.
PRE_EXISTING_ALLOWLIST = frozenset({
    "components/cyberdeck/src/apps/bluetooth/ble_mgr.cpp",
    "components/cyberdeck/src/apps/screenshot/screenshot_server.cpp",
    "components/cyberdeck/src/apps/ssh/ssh_client.cpp",
    "components/cyberdeck/src/apps/wifi/wifi_mgr.cpp",
    "components/cyberdeck/src/apps/wifi/wifi_storage.cpp",
    "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp",
    "components/cyberdeck/src/platform/display/screen_off.cpp",
    "components/cyberdeck/src/platform/input/tab5_keyboard.cpp",
    "components/cyberdeck/src/platform/logging/event_log.cpp",
    "components/cyberdeck/src/platform/sensors/battery_protection.cpp",
    "components/cyberdeck/src/platform/sensors/imu_reader.cpp",
    "components/cyberdeck/src/platform/sensors/ina226_reader.cpp",
})

# The four adapters added to the allowlist: no host build exists for them.
NEWLY_ALLOWLISTED = frozenset({
    "components/cyberdeck/src/apps/system/cyberdeck_service_ports.cpp",
    "components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp",
    "components/cyberdeck/src/apps/system/cyberdeck_recovery.cpp",
    "components/cyberdeck/src/platform/display/cyberdeck_display_port.cpp",
    "components/cyberdeck/src/apps/editor/cyberdeck_editor_app.cpp",
})

# The reviewed allowlist as a whole.  The counterfactual fixtures below build
# their tree from this set instead of the live ALLOWLIST so each scenario
# exercises the guard itself and cannot be masked by allowlist drift (drift is
# the exclusive job of TEST-COV-001..005).
REVIEWED_ALLOWLIST = {path: f"fixture stub for {path}"
                      for path in PRE_EXISTING_ALLOWLIST | NEWLY_ALLOWLISTED}

# TUs that must never be allowlisted, whatever else changes.
REQUIRED_FORBIDDEN = frozenset({
    "components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_background.cpp",
    "components/cyberdeck/src/apps/shell/cyberdeck_cat_worker.cpp",
    "components/cyberdeck/src/platform/input/cyberdeck_keyboard_dispatch.cpp",
    "components/cyberdeck/src/platform/display/cyberdeck_header_view.cpp",
    "components/cyberdeck/src/platform/display/cyberdeck_terminal_view.cpp",
})

# Host-linkable translation units covered by the host suite: they must always
# be reported by gcovr, never allowlisted.
DEMO_APP = "components/cyberdeck/src/apps/demo/cyberdeck_demo_app.cpp"
EDITOR_VIEW = "components/cyberdeck/src/platform/display/cyberdeck_editor_view.cpp"
ELIGIBLE = "components/cyberdeck/src/apps/pure.cpp"


def run(root: Path, source_root: Path, report: Path) -> int:
    return subprocess.run(
        ["python3", str(GUARD), "--root", str(root), "--source-root",
         str(source_root), "--gcovr-json", str(report)], check=False,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True).returncode


def load_guard():
    """Load a private copy of the guard module so ALLOWLIST can be replaced."""
    spec = importlib.util.spec_from_file_location("coverage_scope_guard_test", GUARD)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def run_with_forbidden_allowlist(root: Path, source_root: Path, report: Path,
                                forbidden: str) -> int:
    module = load_guard()
    module.ALLOWLIST = dict(module.ALLOWLIST)
    module.ALLOWLIST[forbidden] = "must never be allowlisted"
    old = sys.argv
    sys.argv = [str(GUARD), "--root", str(root), "--source-root", str(source_root),
                "--gcovr-json", str(report)]
    try:
        return module.main()
    finally:
        sys.argv = old


def run_guard(root: Path, source_root: Path, report: Path,
              allowlist: dict[str, str]) -> tuple[int, str]:
    """Run the guard against `allowlist` and capture its diagnostics."""
    module = load_guard()
    module.ALLOWLIST = dict(allowlist)
    old = sys.argv
    sys.argv = [str(GUARD), "--root", str(root), "--source-root", str(source_root),
                "--gcovr-json", str(report)]
    output = io.StringIO()
    try:
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            code = module.main()
    finally:
        sys.argv = old
    return code, output.getvalue()


def production_sources(root: Path, source_root: Path) -> set[str]:
    return {path.relative_to(root).as_posix() for path in source_root.rglob("*.cpp")}


def make_fixture(tmp: Path, allowlist) -> tuple[Path, Path, Path]:
    """Build a temporary repository whose production sources are exactly the
    allowlist entries plus one eligible TU, with a report covering that TU."""
    root = tmp
    source = root / "components/cyberdeck/src"
    source.mkdir(parents=True)
    for item in allowlist:
        path = root / item
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("int adapter() { return 0; }\n")
    (source / "apps").mkdir(exist_ok=True)
    (source / "apps/pure.cpp").write_text("int f() { return 1; }\n")
    report = root / "coverage.json"
    write_report(report, [ELIGIBLE])
    return root, source, report


def write_report(report: Path, files) -> None:
    report.write_text(json.dumps({"files": [{"file": item} for item in files]}))


# --- TEST-COV-001..005: structural contract of the reviewed allowlist ---------

def test_allowlist_is_exactly_the_reviewed_set() -> None:
    """TEST-COV-001: 12 pre-existing adapters plus exactly 5 new ones."""
    expected = PRE_EXISTING_ALLOWLIST | NEWLY_ALLOWLISTED
    assert len(ALLOWLIST) == 17, \
        f"allowlist must hold exactly 17 entries, found {len(ALLOWLIST)}"
    assert set(ALLOWLIST) == expected, (
        "allowlist drifted from the reviewed set: "
        f"missing={sorted(expected - set(ALLOWLIST))} "
        f"unexpected={sorted(set(ALLOWLIST) - expected)}")
    for path, reason in ALLOWLIST.items():
        assert isinstance(path, str) and path, f"allowlist key must be a non-empty string: {path!r}"
        assert isinstance(reason, str) and reason.strip(), \
            f"allowlist entry {path} must carry a non-empty justification"


def test_new_entries_are_real_production_sources() -> None:
    """TEST-COV-002: the 4 new entries exist under the real production root."""
    assert SOURCE_ROOT.is_dir(), f"production source root is missing: {SOURCE_ROOT}"
    production = production_sources(ROOT, SOURCE_ROOT)
    for path in sorted(NEWLY_ALLOWLISTED):
        assert path.startswith("components/cyberdeck/src/"), \
            f"{path} is outside the production source root"
        assert (ROOT / path).is_file(), f"{path} does not exist in the repository"
        assert path in production, f"{path} is not discovered by the production source scan"


def test_allowlist_never_intersects_forbidden() -> None:
    """TEST-COV-003: allowlist and forbidden allowlist stay disjoint."""
    overlap = set(ALLOWLIST) & set(FORBIDDEN_ALLOWLIST)
    assert not overlap, f"forbidden TUs must never be allowlisted: {sorted(overlap)}"
    for path in sorted(NEWLY_ALLOWLISTED):
        assert path not in FORBIDDEN_ALLOWLIST, \
            f"{path} is allowlisted and must not also be forbidden"


def test_forbidden_allowlist_keeps_its_members() -> None:
    """TEST-COV-004: the 5 forbidden TUs remain forbidden."""
    assert len(FORBIDDEN_ALLOWLIST) == 5, \
        f"forbidden allowlist must keep 5 entries, found {len(FORBIDDEN_ALLOWLIST)}"
    dropped = REQUIRED_FORBIDDEN - set(FORBIDDEN_ALLOWLIST)
    assert not dropped, f"forbidden TUs dropped from the guard: {sorted(dropped)}"


def test_demo_app_is_not_allowlisted() -> None:
    """TEST-COV-005: the covered demo application is neither listed nor hidden."""
    assert (ROOT / DEMO_APP).is_file(), f"{DEMO_APP} must exist to be covered"
    assert DEMO_APP not in ALLOWLIST, \
        f"{DEMO_APP} is covered by the host binary test_demo_app and must not be allowlisted"
    assert DEMO_APP not in FORBIDDEN_ALLOWLIST, \
        f"{DEMO_APP} is host-linkable and must not be forbidden"
    assert DEMO_APP in production_sources(ROOT, SOURCE_ROOT), \
        f"{DEMO_APP} must be discovered by the production source scan"


def test_editor_view_is_host_covered_and_not_allowlisted() -> None:
    """TEST-COV-011: the editor view uses the real host display harness."""
    assert (ROOT / EDITOR_VIEW).is_file(), f"{EDITOR_VIEW} must exist"
    assert EDITOR_VIEW not in ALLOWLIST
    assert EDITOR_VIEW not in FORBIDDEN_ALLOWLIST
    assert EDITOR_VIEW in production_sources(ROOT, SOURCE_ROOT)
    makefile = (ROOT / "tests/host/keymap/Makefile").read_text()
    assert "$(EDITOR_VIEW_SRC)" in makefile


# --- TEST-COV-006..010: fail-closed counterfactuals over temporary fixtures ----

def test_allowlisted_and_covered_is_rejected() -> None:
    """TEST-COV-006: covering one of the 4 new adapters fails closed."""
    with tempfile.TemporaryDirectory() as tmp:
        root, source, report = make_fixture(Path(tmp), REVIEWED_ALLOWLIST)
        write_report(report, [ELIGIBLE, *sorted(NEWLY_ALLOWLISTED)])
        code, output = run_guard(root, source, report, REVIEWED_ALLOWLIST)
        assert code == 1, f"a covered allowlisted TU must fail closed, got {code}"
        assert "allowlisted TUs are covered and must be removed from allowlist" in output, output
        for path in sorted(NEWLY_ALLOWLISTED):
            assert path in output, f"the diagnostic must name {path}: {output}"


def test_allowlist_typo_outside_source_root_is_rejected() -> None:
    """TEST-COV-007: a misspelled allowlist entry is not a real production TU."""
    typo = "components/cyberdeck/src/apps/system/cyberdeck_system_app.cpp"
    with tempfile.TemporaryDirectory() as tmp:
        root, source, report = make_fixture(Path(tmp), REVIEWED_ALLOWLIST)
        broken = dict(REVIEWED_ALLOWLIST)
        broken[typo] = "misspelled adapter that does not exist"
        code, output = run_guard(root, source, report, broken)
        assert code == 1, f"an allowlist entry outside the production root must fail, got {code}"
        assert "outside production source root" in output, output
        assert typo in output, f"the diagnostic must name the offending entry: {output}"


def test_removing_an_allowlist_entry_is_rejected() -> None:
    """TEST-COV-008: dropping one of the 4 new entries makes the guard demand
    coverage for a TU that no host binary builds."""
    for removed in sorted(NEWLY_ALLOWLISTED):
        with tempfile.TemporaryDirectory() as tmp:
            root, source, report = make_fixture(Path(tmp), REVIEWED_ALLOWLIST)
            reduced = {path: reason for path, reason in REVIEWED_ALLOWLIST.items()
                        if path != removed}
            code, output = run_guard(root, source, report, reduced)
            assert code == 1, \
                f"dropping {removed} from the allowlist must fail closed, got {code}"
            assert "production TUs missing from gcovr JSON" in output, output
            assert removed in output, f"the diagnostic must name {removed}: {output}"


def test_demo_app_is_a_required_covered_tu() -> None:
    """TEST-COV-009: a covered demo app passes as an ordinary eligible TU."""
    with tempfile.TemporaryDirectory() as tmp:
        root, source, report = make_fixture(Path(tmp), REVIEWED_ALLOWLIST)
        demo = root / DEMO_APP
        demo.parent.mkdir(parents=True, exist_ok=True)
        demo.write_text("int demo_app() { return 0; }\n")
        write_report(report, [ELIGIBLE, DEMO_APP])
        code, output = run_guard(root, source, report, REVIEWED_ALLOWLIST)
        assert code == 0, f"a covered demo app must pass the guard, got {code}: {output}"


def test_allowlisting_the_covered_demo_app_is_rejected() -> None:
    """TEST-COV-010: allowlisting the now-covered demo app fails closed."""
    with tempfile.TemporaryDirectory() as tmp:
        root, source, report = make_fixture(Path(tmp), REVIEWED_ALLOWLIST)
        demo = root / DEMO_APP
        demo.parent.mkdir(parents=True, exist_ok=True)
        demo.write_text("int demo_app() { return 0; }\n")
        broken = dict(REVIEWED_ALLOWLIST)
        broken[DEMO_APP] = "host-linkable demo application"
        write_report(report, [ELIGIBLE, DEMO_APP])
        code, output = run_guard(root, source, report, broken)
        assert code == 1, f"allowlisting a covered TU must fail closed, got {code}"
        assert "allowlisted TUs are covered and must be removed from allowlist" in output, output
        assert DEMO_APP in output, f"the diagnostic must name {DEMO_APP}: {output}"


def allowlist_membership_scenarios() -> None:
    test_allowlist_is_exactly_the_reviewed_set()
    test_new_entries_are_real_production_sources()
    test_allowlist_never_intersects_forbidden()
    test_forbidden_allowlist_keeps_its_members()
    test_demo_app_is_not_allowlisted()
    test_editor_view_is_host_covered_and_not_allowlisted()


def allowlist_counterfactual_scenarios() -> None:
    test_allowlisted_and_covered_is_rejected()
    test_allowlist_typo_outside_source_root_is_rejected()
    test_removing_an_allowlist_entry_is_rejected()
    test_demo_app_is_a_required_covered_tu()
    test_allowlisting_the_covered_demo_app_is_rejected()


def main() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        source = root / "components/cyberdeck/src"
        source.mkdir(parents=True)
        for item in ALLOWLIST:
            path = root / item
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("int adapter() { return 0; }\n")
        required = "components/cyberdeck/src/apps/pure.cpp"
        (source / "features").mkdir(exist_ok=True)
        (source / "apps/pure.cpp").write_text("int f() { return 1; }\n")
        report = root / "coverage.json"

        def check(files, expected):
            report.write_text(json.dumps({"files": [{"file": f} for f in files]}))
            assert run(root, source, report) == expected

        check([required], 0)
        check([], 1)                         # missing TU
        check(["/outside/not-production.cpp"], 1)  # outside source
        report.write_text("not-json")
        assert run(root, source, report) == 1
        # Existing hardware allowlist entries are rejected when covered.
        (source / "apps/bluetooth").mkdir(exist_ok=True)
        allowlisted = "components/cyberdeck/src/apps/bluetooth/ble_mgr.cpp"
        (source / "apps/bluetooth/ble_mgr.cpp").write_text("int g() {}\n")
        check([required, allowlisted], 1)
        # Every eligible TU must be represented; forbidden allowlists fail closed.
        forbidden = "components/cyberdeck/src/apps/shell/cyberdeck_cat_worker.cpp"
        (source / "apps/shell").mkdir(exist_ok=True)
        (source / "apps/shell/cyberdeck_cat_worker.cpp").write_text("int h() {}\n")
        check([required, allowlisted, forbidden], 1)
        assert run_with_forbidden_allowlist(root, source, report, forbidden) == 1
    allowlist_membership_scenarios()
    allowlist_counterfactual_scenarios()
    print("coverage scope guard tests passed")


if __name__ == "__main__":
    main()
