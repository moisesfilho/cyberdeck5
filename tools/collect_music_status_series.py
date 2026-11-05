#!/usr/bin/env python3
"""Collect read-only player status and heap at 1 Hz on one Serial-JTAG session."""

import argparse
import csv
from datetime import datetime, timezone
from pathlib import Path
import time

import serial

from cyberdeck_cli import CyberdeckSession


STATUS_FIELDS = ("state", "queue_scanning", "track_count", "position_ms", "duration_ms")
HEAP_FIELDS = ("internal_free", "internal_min_free", "dma_free", "dma_min_free",
               "psram_free", "psram_min_free", "serial_stack_high_water_bytes")
FIELDS = ("timestamp_utc", "elapsed_ms", "latency_ms", "error", "fw_version", "uptime",
          *STATUS_FIELDS, *HEAP_FIELDS)


def query(session: CyberdeckSession, kind: str) -> dict:
    frames = session.exchange({"type": kind})
    if not frames or frames[-1].get("ok") is not True or not isinstance(frames[-1].get("result"), dict):
        raise ValueError(f"{kind} unavailable")
    return frames[-1]["result"]


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
                status = query(session, "music.status")
                info = query(session, "sys.info")
                if (status.get("state") not in ("stopped", "playing", "paused", "error")
                        or type(status.get("queue_scanning")) is not bool
                        or any(type(status.get(key)) is not int for key in STATUS_FIELDS[2:])
                        or not isinstance(info.get("fw_version"), str)
                        or not isinstance(info.get("uptime"), str)
                        or any(type(info.get(key)) is not int for key in HEAP_FIELDS)):
                    raise ValueError("missing or invalid telemetry")
                row.update({key: status[key] for key in STATUS_FIELDS})
                row.update({key: info[key] for key in ("fw_version", "uptime", *HEAP_FIELDS)})
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
    parser.add_argument("--seconds", type=int, default=60, help="1..3600 samples at 1 Hz")
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
