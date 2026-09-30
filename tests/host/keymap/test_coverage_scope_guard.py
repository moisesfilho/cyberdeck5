#!/usr/bin/env python3
"""Behavioral tests for the fail-closed production coverage scope guard."""
from __future__ import annotations

import json
import subprocess
import tempfile
from pathlib import Path
import runpy
import importlib.util
import sys

ROOT = Path(__file__).resolve().parents[3]
GUARD = ROOT / "tools/coverage_scope_guard.py"
ALLOWLIST = runpy.run_path(str(GUARD))["ALLOWLIST"]


def run(root: Path, source_root: Path, report: Path) -> int:
    return subprocess.run(
        ["python3", str(GUARD), "--root", str(root), "--source-root",
         str(source_root), "--gcovr-json", str(report)], check=False,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True).returncode


def run_with_forbidden_allowlist(root: Path, source_root: Path, report: Path,
                                forbidden: str) -> int:
    spec = importlib.util.spec_from_file_location("coverage_scope_guard_test", GUARD)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    module.ALLOWLIST = dict(module.ALLOWLIST)
    module.ALLOWLIST[forbidden] = "must never be allowlisted"
    old = sys.argv
    sys.argv = [str(GUARD), "--root", str(root), "--source-root", str(source_root),
                "--gcovr-json", str(report)]
    try:
        return module.main()
    finally:
        sys.argv = old


def main() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        source = root / "components/cyberdeck/src"
        source.mkdir(parents=True)
        for item in ALLOWLIST:
            path = root / item
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("int adapter() { return 0; }\n")
        required = "components/cyberdeck/src/features/pure.cpp"
        (source / "features").mkdir(exist_ok=True)
        (source / "features/pure.cpp").write_text("int f() { return 1; }\n")
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
        (source / "features/bluetooth").mkdir(exist_ok=True)
        allowlisted = "components/cyberdeck/src/features/bluetooth/ble_mgr.cpp"
        (source / "features/bluetooth/ble_mgr.cpp").write_text("int g() {}\n")
        check([required, allowlisted], 1)
        # Every eligible TU must be represented; forbidden allowlists fail closed.
        forbidden = "components/cyberdeck/src/features/shell/cyberdeck_cat_worker.cpp"
        (source / "features/shell").mkdir(exist_ok=True)
        (source / "features/shell/cyberdeck_cat_worker.cpp").write_text("int h() {}\n")
        check([required, allowlisted, forbidden], 1)
        assert run_with_forbidden_allowlist(root, source, report, forbidden) == 1
    print("coverage scope guard tests passed")


if __name__ == "__main__":
    main()
