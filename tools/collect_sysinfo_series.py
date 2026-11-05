#!/usr/bin/env python3
"""Sample sys.info once per second over a single Serial-JTAG session."""

import argparse
import csv
from datetime import datetime, timezone
from pathlib import Path
import time

import serial

from cyberdeck_cli import CyberdeckSession


FIELDS = (
    "timestamp_utc", "elapsed_ms", "latency_ms", "error", "fw_version",
    "uptime", "free_heap", "internal_free", "internal_largest", "dma_free",
    "dma_largest", "psram_free", "safe_mode", "interrupted_boots",
    "internal_min_free", "dma_min_free", "psram_min_free",
    "serial_stack_high_water_bytes",
)
TELEMETRY = FIELDS[4:]


def collect(session: CyberdeckSession, seconds: int, output: Path) -> int:
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
                frames = session.exchange({"type": "sys.info"})
                frame = frames[-1]
                if frame.get("ok") is not True or not isinstance(frame.get("result"), dict):
                    raise ValueError("sys.info returned an error")
                info = frame["result"]
                if not isinstance(info.get("free_heap"), int) or not isinstance(info.get("uptime"), str):
                    raise ValueError("sys.info missing required telemetry")
                row.update({key: info.get(key) for key in TELEMETRY})
            except (OSError, RuntimeError, ValueError, serial.SerialException) as exc:
                row["error"] = type(exc).__name__
                failures += 1
            row["latency_ms"] = round((time.monotonic() - begun) * 1000)
            writer.writerow(row)
            stream.flush()
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--seconds", type=int, default=60, help="integer number of 1 Hz samples (1..3600)")
    parser.add_argument("--out", type=Path, required=True, help="new CSV path (never overwritten)")
    args = parser.parse_args()
    if not 1 <= args.seconds <= 3600:
        parser.error("--seconds must be 1..3600")
    with serial.Serial(args.port, 115200, timeout=0.8) as transport:
        failures = collect(CyberdeckSession(transport), args.seconds, args.out)
    print(f"samples={args.seconds} failures={failures} csv={args.out}")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
