#!/usr/bin/env python3
"""Keep the ID3 decode off the scan task stack and expose its real peak.

The scan runs on a 6144-byte FreeRTOS task. `read_metadata` must therefore fill
the bounded `metadata` struct instead of copying a whole `track` onto that
stack, and `scan_entry` must log the stack high-water mark before it deletes
itself so the real peak is observable on the device.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
COMPONENT = ROOT / "components/cyberdeck"
MUSIC = COMPONENT / "src/apps/music"
metadata_source = (MUSIC / "cyberdeck_music_metadata.cpp").read_text(encoding="utf-8")
metadata_header = (COMPONENT / "include/apps/music/cyberdeck_music_metadata.h").read_text(encoding="utf-8")
app_source = (MUSIC / "cyberdeck_music_app.cpp").read_text(encoding="utf-8")
model_header = (COMPONENT / "include/apps/music/cyberdeck_music_model.h").read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    return source[start:source.index("\n}", start)]


read_metadata = function_body(metadata_source, "void read_metadata(")
read_metadata_signature = read_metadata[:read_metadata.index("{")]
scan_entry = function_body(app_source, "void application::scan_entry(")
scan_sync = function_body(app_source, "bool application::scan_directory_sync(")
walk_start = scan_sync.index("walk = [&](std::string_view directory")
walk_end = scan_sync.index("\n    };", walk_start)
walk_body = scan_sync[walk_start:walk_end]
metadata_struct = re.search(r"struct metadata \{\n(.*?)\n\};", model_header, re.S)

high_water_mark = scan_entry.find("uxTaskGetStackHighWaterMark(nullptr)")
telemetry = re.search(r"ESP_LOGI\([^;]*uxTaskGetStackHighWaterMark\(nullptr\)", scan_entry)

checks = {
    "read_metadata takes the bounded metadata struct, not a track":
        "metadata &out" in read_metadata_signature and "track" not in read_metadata_signature,
    "read_metadata declares no track on the scan task stack":
        re.search(r"\btrack\b", read_metadata) is None,
    "read_metadata decodes into the lightweight metadata struct":
        re.search(r"\bmetadata\s+decoded\b", read_metadata) is not None,
    "metadata struct holds only bounded title and artist buffers":
        metadata_struct is not None
        and "k_max_title_bytes" in metadata_struct.group(1)
        and "k_max_artist_bytes" in metadata_struct.group(1)
        and "path" not in metadata_struct.group(1)
        and "duration_ms" not in metadata_struct.group(1),
    "metadata header documents the light struct contract":
        "metadata &out" in metadata_header and "track &item" not in metadata_header,
    "scan fills a metadata buffer instead of the candidate track":
        "metadata tags{};" in app_source and "read_metadata(descriptor, verified_size, tags)" in app_source
        and "read_metadata(descriptor, verified_size, candidate)" not in app_source,
    "scan candidate is allocated once on the heap before walk and allocation failure aborts":
        re.search(r"std::unique_ptr\s*<\s*track\s*>\s*candidate\s*\(\s*new\s*\(std::nothrow\)\s*track\s*\{\}\s*\)", scan_sync)
        is not None
        and scan_sync.index("std::unique_ptr<track> candidate") < walk_start
        and "if (!candidate)\n        return false;" in scan_sync,
    "walk reinitializes and reuses the heap candidate, never a stack track candidate":
        "*candidate = track{};" in walk_body
        and re.search(r"\btrack\s+candidate\s*\{", walk_body) is None
        and "(*tracks)[count++] = *candidate;" in walk_body
        and "*largest = *candidate;" in walk_body,
    "scan task stack budget stays at the approved 6144 bytes":
        '"music_scan", 6144' in app_source,
    "scan_entry logs the stack high-water mark":
        telemetry is not None and "stack_high_water_mark_bytes=%u" in telemetry.group(0)
        and "stack_bytes=6144" in telemetry.group(0),
    "high-water-mark log follows completion and precedes self-deletion":
        high_water_mark > scan_entry.index("xSemaphoreGive(completed)")
        and high_water_mark < scan_entry.index("vTaskDelete(nullptr)"),
}

for description, passed in checks.items():
    print(f"{'PASS' if passed else 'FAIL'}: {description}")
if not all(checks.values()):
    raise SystemExit(1)
