#!/usr/bin/env python3
"""Fail closed if the firmware's FatFS error hooks are not actually referenced."""
import argparse
from pathlib import Path
import re
import sys


def check_map(text: str) -> list[str]:
    _, marker, references = text.partition("Cross Reference Table")
    if not marker:
        return ["missing linker cross-reference table"]
    errors = []
    for symbol in ("__wrap_ff_disk_write", "__wrap_ff_disk_ioctl"):
        match = re.search(rf"(?m)^{symbol}\s+([^\n]+)\n((?:[ \t]+[^\n]*\n)*)", references)
        if not match:
            errors.append(f"missing {symbol}")
            continue
        if "cyberdeck_poweroff.cpp.obj" not in match.group(1):
            errors.append(f"unexpected owner for {symbol}")
        if not re.search(r"fatfs[/\\]libfatfs\.a\(ff\.c\.obj\)", match.group(2)):
            errors.append(f"FatFS does not call {symbol}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", required=True, type=Path)
    args = parser.parse_args()
    try:
        errors = check_map(args.map.read_text(encoding="utf-8"))
    except (OSError, UnicodeError) as error:
        errors = [str(error)]
    if errors:
        print("poweroff link guard FAILED: " + "; ".join(errors), file=sys.stderr)
        return 1
    print("poweroff link guard PASS: FatFS calls both persistent-error hooks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
