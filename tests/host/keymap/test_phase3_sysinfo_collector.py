#!/usr/bin/env python3
"""Validate bounded read-only serial sampling with a fake persistent session."""

from pathlib import Path
import csv
import sys
from tempfile import TemporaryDirectory
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
import collect_sysinfo_series as collector  # noqa: E402


class FakeSession:
    def __init__(self):
        self.requests = []

    def exchange(self, request):
        self.requests.append(request)
        if len(self.requests) == 2:
            raise RuntimeError("timeout")
        return [{"ok": True, "result": {"uptime": "00:00:12", "free_heap": 1000,
                                       "internal_free": 400, "ssid": "do-not-copy"}}]


def main() -> None:
    session = FakeSession()
    with TemporaryDirectory() as directory:
        output = Path(directory) / "sysinfo.csv"
        with patch.object(collector.time, "sleep"):
            failures = collector.collect(session, 2, output)
        rows = list(csv.DictReader(output.open(encoding="utf-8")))
        assert session.requests == [{"type": "sys.info"}] * 2
        assert failures == 1 and len(rows) == 2
        assert rows[0]["uptime"] == "00:00:12" and rows[0]["internal_free"] == "400"
        assert rows[1]["error"] == "RuntimeError" and not rows[1]["free_heap"]
        assert "ssid" not in output.read_text(encoding="utf-8")
        assert b"\r\n" not in output.read_bytes()
        try:
            collector.collect(session, 1, output)
        except FileExistsError:
            pass
        else:
            raise AssertionError("collector overwrote previous evidence")
    print("PASS: phase-3 sys.info collector preserves errors, ownership and privacy")


if __name__ == "__main__":
    main()
