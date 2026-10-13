#!/usr/bin/env python3
"""Synthetic positive/negative linker-map fixtures, no compiler/device required."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("poweroff_link_guard", ROOT / "tools/poweroff_link_guard.py")
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)

records = ""
for symbol in ("__wrap_ff_disk_write", "__wrap_ff_disk_ioctl"):
    records += f"{symbol} esp-idf/cyberdeck/libcyberdeck.a(cyberdeck_poweroff.cpp.obj)\n"
    records += "                        esp-idf/fatfs/libfatfs.a(ff.c.obj)\n"
text = "Cross Reference Table\n\nSymbol File\n" + records
assert guard.check_map(text) == []
assert guard.check_map(text.replace("Cross Reference Table", "missing"))
assert guard.check_map(text.replace("__wrap_ff_disk_write", "unrelated"))
assert guard.check_map(text.replace("__wrap_ff_disk_ioctl", "unrelated"))
assert guard.check_map(text.replace("cyberdeck_poweroff.cpp.obj", "unrelated.obj"))
assert guard.check_map(text.replace("esp-idf/fatfs/libfatfs.a(ff.c.obj)", "unrelated.a(other.obj)"))
assert guard.check_map(text.replace("                        esp-idf/fatfs/libfatfs.a(ff.c.obj)\n", ""))
print("PASS: poweroff linker guard rejects missing hooks, wrong owner and unreferenced wrappers")
