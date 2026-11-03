#!/usr/bin/env python3
"""Apply or verify the experimental CPU-staging patch for ESP-Hosted 3.0.10."""

from __future__ import annotations

import argparse
import hashlib
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "managed_components/espressif__esp_hosted"
PATCH = ROOT / "tools/esp-hosted-cpu-staging.patch"
FILES = (
    Path("host/mcu/eh_host_mcu_transport/src/eh_host_bus_sdio.c"),
    Path("host/mcu/eh_host_mcu_transport/src/eh_host_mcu_transport_channels.c"),
)
BASE = (
    "c0cd81b566113db352254f533f64cc1a5928cd25d5d5975cd15f99a8dec8f8c4",
    "8b4c199ca71c6f1c3ffeae4d461dea0a1fac6903c72869d4f9c368bcbfbafd6d",
)
STAGED = (
    "ecc52309d88b011eaa2567e23289145c3a9135051665c09aea64b288b4bb0a78",
    "35c8680718d01b67761595f38193fbff7ee04feab8fa027c9bab8170af9c9e61",
)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def current_hashes() -> tuple[str, ...]:
    return tuple(digest(COMPONENT / path) for path in FILES)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("apply", "verify", "restore"))
    args = parser.parse_args()

    manifest = (COMPONENT / "idf_component.yml").read_text()
    if "version: 3.0.10" not in manifest or "2e3f6c8bd717d67698319a6a24f9f4218d8db068" not in manifest:
        raise SystemExit("Unexpected ESP-Hosted component version/revision; refusing patch")

    hashes = current_hashes()
    if args.action == "verify":
        if hashes != STAGED:
            raise SystemExit(f"CPU-staging patch is not active: {hashes}")
        print("ESP-Hosted CPU-staging patch verified")
        return 0

    expected = BASE if args.action == "apply" else STAGED
    if hashes != expected:
        raise SystemExit(f"Refusing {args.action}; source hashes do not match expected state: {hashes}")

    command = ["patch", "--batch", "--forward" if args.action == "apply" else "--reverse",
               "-p1", "-i", str(PATCH)]
    preflight = subprocess.run([*command[:2], "--dry-run", *command[2:]],
                              cwd=COMPONENT, capture_output=True, text=True, check=False)
    if preflight.returncode:
        raise SystemExit(f"Patch preflight failed without changing sources: {preflight.stdout}{preflight.stderr}")

    result = subprocess.run(command, cwd=COMPONENT, check=False)
    if result.returncode:
        raise SystemExit(f"patch command failed ({result.returncode})")

    target = STAGED if args.action == "apply" else BASE
    hashes = current_hashes()
    if hashes != target:
        raise SystemExit(f"Patch result hash mismatch: {hashes}")
    print(f"ESP-Hosted CPU-staging {args.action} complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
