#!/usr/bin/env python3
"""Collect read-only 1 Hz GET /battery snapshots without using Serial-JTAG."""

import argparse
import csv
from datetime import datetime, timezone
import json
from pathlib import Path
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import urlopen


FIELDS = (
    "timestamp_utc", "elapsed_ms", "latency_ms", "http_status", "error",
    "fw_version", "uptime_s", "state", "charge", "available", "bus_voltage_mv",
    "current_ma", "percentage", "protection_enabled", "protection_active",
    "charger_enabled", "sampled_age_ms",
)


def collect(url: str, seconds: int, output: Path) -> int:
    parsed = urlsplit(url)
    if parsed.scheme != "http" or not parsed.hostname:
        raise ValueError("battery collector requires an HTTP URL with a host")
    output.parent.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    failures = 0
    with output.open("x", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS, lineterminator="\n")
        writer.writeheader()
        for index in range(seconds):
            due = start + index
            if due > time.monotonic():
                time.sleep(due - time.monotonic())
            row = {"timestamp_utc": datetime.now(timezone.utc).isoformat(),
                   "elapsed_ms": round((time.monotonic() - start) * 1000)}
            begun = time.monotonic()
            try:
                # The URL scheme and host are validated before any file or request.
                with urlopen(url, timeout=0.8) as response:  # nosec B310
                    row["http_status"] = response.status
                    if response.status != 200:
                        raise ValueError("HTTP status is not 200")
                    body = response.read(4097)
                    if len(body) > 4096:
                        raise ValueError("response exceeds 4096 bytes")
                    data = json.loads(body)
                    if not isinstance(data, dict) or data.get("schema") != 1:
                        raise ValueError("unexpected battery schema")
                    row.update({key: data.get(key) for key in FIELDS if key in data})
            except (HTTPError, URLError, OSError, ValueError, UnicodeError) as exc:
                row["error"] = type(exc).__name__
                failures += 1
            row["latency_ms"] = round((time.monotonic() - begun) * 1000)
            writer.writerow(row)
            stream.flush()
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="Tab5 LAN IP or hostname; never stored in CSV")
    parser.add_argument("--seconds", type=int, default=60, help="integer number of 1 Hz samples (1..3600)")
    parser.add_argument("--out", type=Path, required=True, help="new CSV path (existing file is not overwritten)")
    args = parser.parse_args()
    if not 1 <= args.seconds <= 3600:
        parser.error("--seconds must be 1..3600")
    url = f"http://{args.host}/battery"
    failures = collect(url, args.seconds, args.out)
    print(f"samples={args.seconds} failures={failures} csv={args.out}")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
