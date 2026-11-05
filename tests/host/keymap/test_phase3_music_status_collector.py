#!/usr/bin/env python3
"""Check 1 Hz music telemetry collection without Serial-JTAG or private metadata."""

import csv
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
import collect_music_status_series as collector  # noqa: E402


class FakeSession:
    def __init__(self):
        self.requests = []

    def exchange(self, request):
        self.requests.append(request)
        if len(self.requests) == 4:
            return [{"ok": False, "error_code": "player_not_running"}]
        if request["type"] == "music.status":
            return [{"ok": True, "result": {
                "state": "stopped", "queue_scanning": False,
                "track_count": 2, "position_ms": 0, "duration_ms": 120000,
                "title": "PRIVATE TITLE", "path": "/sdcard/private.mp3"}}]
        if request["type"] == "music.scan":
            return [{"ok": True, "result": {"queue": {
                "completed": True, "succeeded": True, "duration_ms": 35,
                "stack_high_water_bytes": 3000}, "browser": {
                "completed": False, "succeeded": False, "duration_ms": 0,
                "stack_high_water_bytes": 0}}}]
        return [{"ok": True, "result": {
            "fw_version": "1.6.0", "uptime": "00:00:12",
            **{key: 400 for key in collector.HEAP_FIELDS},
            "ssid": "PRIVATE NETWORK"}}]


def main() -> None:
    session = FakeSession()
    with TemporaryDirectory() as directory:
        output = Path(directory) / "music.csv"
        with patch.object(collector.time, "sleep"):
            failures = collector.collect(session, 2, output)
        rows = list(csv.DictReader(output.open(encoding="utf-8")))
        assert session.requests == [{"type": "music.status"}, {"type": "music.scan"},
                                    {"type": "sys.info"},
                                    {"type": "music.status"}]
        assert failures == 1 and len(rows) == 2
        assert rows[0]["state"] == "stopped" and rows[0]["track_count"] == "2"
        assert rows[0]["queue_duration_ms"] == "35" and rows[0]["queue_stack_high_water_bytes"] == "3000"
        assert rows[1]["error"] == "ValueError" and not rows[1]["track_count"]
        assert "PRIVATE" not in output.read_text(encoding="utf-8")
        assert b"\r\n" not in output.read_bytes()
        try:
            collector.collect(session, 1, output)
        except FileExistsError:
            pass
        else:
            raise AssertionError("collector overwrote evidence")
    print("PASS: phase-3 music collector is read-only, single-session and private")


if __name__ == "__main__":
    main()
