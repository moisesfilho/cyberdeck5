#!/usr/bin/env python3
"""Keep large music scan objects off task stacks and expose its real peak.

The scan runs on a 6144-byte FreeRTOS task. `read_metadata` must therefore fill
the bounded `metadata` struct instead of copying a whole `track` onto that
stack. Browser staging is a bounded heap allocation; `scan_entry` logs the
high-water mark before it signals completion so a read-only snapshot can
preserve the final value after the worker deletes itself.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
COMPONENT = ROOT / "components/cyberdeck"
MUSIC = COMPONENT / "src/apps/music"
metadata_source = (MUSIC / "cyberdeck_music_metadata.cpp").read_text(encoding="utf-8")
metadata_header = (COMPONENT / "include/apps/music/cyberdeck_music_metadata.h").read_text(encoding="utf-8")
app_source = (MUSIC / "cyberdeck_music_app.cpp").read_text(encoding="utf-8")
browser_source = (MUSIC / "cyberdeck_music_browser.cpp").read_text(encoding="utf-8")
model_header = (COMPONENT / "include/apps/music/cyberdeck_music_model.h").read_text(encoding="utf-8")
ui_source = (COMPONENT / "src/platform/display/cyberdeck_ui.cpp").read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    return source[start:source.index("\n}", start)]


read_metadata = function_body(metadata_source, "void read_metadata(")
read_metadata_signature = read_metadata[:read_metadata.index("{")]
scan_entry = function_body(app_source, "void application::scan_entry(")
scan_sync = function_body(app_source, "bool application::scan_directory_sync(")
scan_browser_sync = function_body(app_source, "bool application::scan_browser_sync(")
browser_scan_start = function_body(app_source, "bool application::browser_scan(")
browser_scan_into = function_body(browser_source, "bool scan_browser_directory_into(")
walk_start = scan_sync.index("walk = [&](std::string_view directory")
walk_end = scan_sync.index("\n    };", walk_start)
walk_body = scan_sync[walk_start:walk_end]
metadata_struct = re.search(r"struct metadata \{\n(.*?)\n\};", model_header, re.S)

high_water_mark = scan_entry.find("uxTaskGetStackHighWaterMark(nullptr)")
telemetry = re.search(r'ESP_LOGI\(k_tag, "scan stack_high_water_mark_bytes=%u stack_bytes=6144",\s*'
                      r'static_cast<unsigned>\(measurement\.stack_high_water_bytes\)\)', scan_entry)
ui_browser_callers = {
    "status timer": function_body(ui_source, "void process_music_status(lv_timer_t *)"),
    "browser action": function_body(ui_source, "bool music_view_browser_action(bool play_selected) {"),
    "view selection callback": function_body(ui_source, "void music_view_action(std::string_view name, void *) {"),
}

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
    "browser staging uses bounded heap allocation with RAII":
        "browser_snapshot_allocation_size(k_browser_entry_limit, allocation_size)" in scan_browser_sync
        and re.search(r"std::unique_ptr\s*<\s*browser_snapshot", scan_browser_sync) is not None
        and "scan_browser_directory_into(path, *staging, browser_ops_)" in scan_browser_sync,
    "browser worker avoids stack-local snapshot/model candidates":
        re.search(r"\bbrowser_snapshot\s+\w+\s*[{;]", scan_browser_sync) is None
        and re.search(r"\bbrowser_model\s+\w+\s*[{;]", scan_browser_sync) is None,
    "scan admission validates path without copying full browser model":
        "valid_browser_directory(directory)" in browser_scan_start
        and re.search(r"\bbrowser_model\s+candidate\b", browser_scan_start) is None,
    "private staging scanner and public transactional wrapper stay distinct":
        "staging" in browser_scan_into and "out = *" not in browser_scan_into
        and "out = *staging" in browser_source,
    "scan_entry logs the stack high-water mark":
        telemetry is not None and "stack_high_water_mark_bytes=%u" in telemetry.group(0)
        and "stack_bytes=6144" in telemetry.group(0),
    "high-water mark is published before completion, then logged before self-deletion":
        0 <= high_water_mark < scan_entry.index("scan_metrics_mutex_")
        < scan_entry.index("legacy_scan_running_.store(false")
        < scan_entry.index("xSemaphoreGive(completed)")
        < scan_entry.index("ESP_LOGI(k_tag, \"scan stack_high_water_mark_bytes")
        < scan_entry.index("vTaskDelete(nullptr)")
        and "self->scan_metrics_.queue = measurement" in scan_entry
        and "self->scan_metrics_.browser = measurement" in scan_entry,
    "LVGL owns one static BSS browser snapshot and copies it atomically through one helper":
        re.search(r"cyberdeck_music::browser_snapshot\s+s_music_browser_snapshot\s*;", ui_source) is not None
        and "return music.get_browser_snapshot(s_music_browser_snapshot);" in ui_source
        and ui_source.count("get_browser_snapshot(") == 1,
    "no LVGL callback or timer allocates a browser snapshot on its stack":
        re.search(r"\bcyberdeck_music::browser_snapshot\s+(?!s_music_browser_snapshot\b)\w+\s*[{;]", ui_source)
        is None,
    "timer uses the shared snapshot for initial scan and both render states":
        "refresh_music_browser_snapshot(music)" in ui_browser_callers["status timer"]
        and "s_music_browser_snapshot.count == 0" in ui_browser_callers["status timer"]
        and ui_browser_callers["status timer"].count("render_browser(s_music_browser_snapshot,") == 2,
    "browser action uses the same atomic snapshot for stale guard, navigation and playback selection":
        "refresh_music_browser_snapshot(app)" in ui_browser_callers["browser action"]
        and "browser_directory() != s_music_browser_snapshot.directory.data()" in ui_browser_callers["browser action"]
        and "s_music_browser_snapshot.entries[index]" in ui_browser_callers["browser action"]
        and "app.browser_play_selected(index) : app.browser_toggle_selected(index)" in ui_browser_callers["browser action"],
    "view selection callback refreshes the shared snapshot before selecting":
        "refresh_music_browser_snapshot(app)" in ui_browser_callers["view selection callback"]
        and "browser_directory() == s_music_browser_snapshot.directory.data()" in ui_browser_callers["view selection callback"]
        and "app.browser_select(row - (has_parent ? 1U : 0U))" in ui_browser_callers["view selection callback"],
}

for description, passed in checks.items():
    print(f"{'PASS' if passed else 'FAIL'}: {description}")
if not all(checks.values()):
    raise SystemExit(1)
