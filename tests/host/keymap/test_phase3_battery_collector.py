#!/usr/bin/env python3
"""Exercise the read-only battery series collector without network or hardware."""

import csv
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
import collect_battery_series as collector  # noqa: E402


class Response:
    def __init__(self, body: bytes, status: int = 200):
        self.body = body
        self.status = status

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        return False

    def read(self, length: int):
        return self.body[:length]


def main() -> None:
    with TemporaryDirectory() as directory:
        output = Path(directory) / "series.csv"
        replies = [Response(b'{"schema":1,"percentage":80,"uptime_s":20}'),
                   Response(b'{"schema":9,"percentage":10}')]
        with patch.object(collector, "urlopen", side_effect=replies) as get, \
             patch.object(collector.time, "sleep"):
            failures = collector.collect("http://127.0.0.1/battery", 2, output)
        rows = list(csv.DictReader(output.open(encoding="utf-8")))
        assert b"\r\n" not in output.read_bytes()
        assert get.call_count == 2
        assert failures == 1 and len(rows) == 2
        assert rows[0]["percentage"] == "80" and rows[0]["uptime_s"] == "20"
        assert rows[1]["error"] == "ValueError" and not rows[1]["percentage"]
        try:
            collector.collect("http://127.0.0.1/battery", 1, output)
        except FileExistsError:
            pass
        else:
            raise AssertionError("collector overwrote previous evidence")
        for url in ("file:///etc/passwd", "ftp://127.0.0.1/battery", "http:///battery"):
            invalid_output = Path(directory) / "invalid.csv"
            with patch.object(collector, "urlopen") as get:
                try:
                    collector.collect(url, 1, invalid_output)
                except ValueError:
                    pass
                else:
                    raise AssertionError(f"collector accepted invalid URL: {url}")
                get.assert_not_called()
            assert not invalid_output.exists()
    print("PASS: phase-3 battery collector preserves samples, errors and evidence")


if __name__ == "__main__":
    main()
