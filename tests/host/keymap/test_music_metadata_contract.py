#!/usr/bin/env python3
"""Structural contract for the NOW PLAYING track-identity region.

REQ-MUSIC-META-03/04. The region renders the title/artist/album of the track the
player is actually running, so three things must stay true:

- it is fed by `playback.current` from the playback snapshot, never by the
  status text, the queue listing or a browser snapshot (TEST-MUSIC-META-UI-WIRING);
- its single production literal (the unavailable placeholder) stays inside the
  real `cyberdeck_font` coverage, derived from the font source and reusing the
  scanner of `test_music_player_ui_contract.py`
  (TEST-MUSIC-META-GLYPH-ASCII);
- `k_max_album_bytes` plus that literal fits the serial `ui.dump` text clamp,
  so the device dump can show the whole album
  (TEST-MUSIC-META-BOUNDS-DUMP).

Both the album capacity and the dump clamp are read from their headers and never
pinned here, so raising either one intentionally keeps the invariant visible.
"""

from pathlib import Path
import re
import sys

from test_music_player_ui_contract import (
    FONT,
    block_span,
    font_coverage,
    function_body,
    strip_comments,
)

ROOT = Path(__file__).resolve().parents[3]
MODEL = ROOT / "components/cyberdeck/include/apps/music/cyberdeck_music_model.h"
VIEW = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_music_view.cpp"
VIEW_HEADER = (ROOT / "components/cyberdeck/include/platform/display"
               / "cyberdeck_music_view.h")
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
DUMP = (ROOT / "components/cyberdeck/include/apps/serial"
        / "cyberdeck_ui_dump_serializer.h")

PLACEHOLDER_LITERAL = re.compile(
    r'constexpr\s+const\s+char\s*\*\s*kUnavailableText\s*=\s*"((?:[^"\\\n]|\\.)*)"\s*;')


def flat(source: str) -> str:
    return re.sub(r"\s+", " ", source)


def definition_body(source: str, signature: str) -> str:
    """Body of the definition whose `signature + " {"` is unique in `source`.

    `function_body` anchors on the first textual match, which for
    `cyberdeck_ui.cpp` is a forward declaration; this helper anchors on the
    definition instead.
    """
    start = source.index(signature)
    low, high = block_span(source, start)
    return source[low:high]


def album_capacity() -> int:
    match = re.search(
        r"inline\s+constexpr\s+std::size_t\s+k_max_album_bytes\s*=\s*(\d+)\s*;",
        MODEL.read_text(encoding="utf-8"))
    if not match:
        raise AssertionError("k_max_album_bytes must be a named constant in the model header")
    return int(match.group(1))


def dump_text_limit() -> int:
    match = re.search(
        r"constexpr\s+std::size_t\s+k_max_text_bytes\s*=\s*(\d+)\s*;",
        DUMP.read_text(encoding="utf-8"))
    if not match:
        raise AssertionError("ui_dump::k_max_text_bytes not found")
    return int(match.group(1))


def placeholder_literal(view_source: str) -> str:
    literals = PLACEHOLDER_LITERAL.findall(strip_comments(view_source))
    if len(literals) != 1:
        raise AssertionError(
            "the region needs one `kUnavailableText` literal, "
            f"found {len(literals)}")
    return literals[0]


def main() -> int:
    failures: list[str] = []

    def require(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    view_source = VIEW.read_text(encoding="utf-8")
    header_source = VIEW_HEADER.read_text(encoding="utf-8")
    ui_source = UI.read_text(encoding="utf-8")

    # --- TEST-MUSIC-META-UI-WIRING -----------------------------------------
    try:
        process = flat(definition_body(strip_comments(ui_source),
                                       "void process_music_status(lv_timer_t *) {"))
    except (AssertionError, ValueError):
        process = ""
        failures.append("cyberdeck_ui.cpp: process_music_status() definition not found")
    calls = re.findall(r"render_now_playing\(([^;]*?)\);", process)
    require(calls != [], "the UI must feed the identity region from the playback snapshot")
    require(all("playback.current" in call for call in calls),
            "every render_now_playing() call must pass playback.current, not a status "
            "string or a browser snapshot: found " + repr(calls))
    require("playback.current" in process and "s_music_view.render_now_playing(" in process,
            "the region must be fed by s_music_view.render_now_playing(playback.current)")
    guarded = re.findall(
        r"if \(has_playback_snapshot\) s_music_view\.render_now_playing\(playback\.current\);",
        process)
    require(len(guarded) == len(calls),
            "each render_now_playing(playback.current) must stay behind "
            "`if (has_playback_snapshot)`, so a session without a snapshot keeps the "
            "unavailable placeholders")
    try:
        render_browser = flat(function_body(strip_comments(view_source), "void view::render_browser("))
    except (AssertionError, ValueError):
        render_browser = ""
        failures.append("cyberdeck_music_view.cpp: render_browser() not found")
    require("meta_title_" not in render_browser and "meta_artist_" not in render_browser and
            "meta_album_" not in render_browser and "meta_" not in render_browser,
            "render_browser() must never write or clear the identity region")
    try:
        render = flat(function_body(strip_comments(view_source), "void view::render("))
    except (AssertionError, ValueError):
        render = ""
        failures.append("cyberdeck_music_view.cpp: render() not found")
    require("meta_" not in render,
            "render() (status text) must never write or clear the identity region")
    try:
        region = flat(function_body(strip_comments(view_source), "void view::render_now_playing("))
    except (AssertionError, ValueError):
        region = ""
        failures.append("cyberdeck_music_view.cpp: render_now_playing() not found")
    for field in ("item.title", "item.artist", "item.album"):
        require(field in region,
                f"render_now_playing() must render {field} verbatim")
    for label in ("meta_title_", "meta_artist_", "meta_album_"):
        require(f"lv_label_set_text({label}" in region,
                f"render_now_playing() must paint {label}")
    require(region.count("kUnavailableText") == 3,
            "each empty field must render the single kUnavailableText placeholder")
    require("meta_title_from_filename_ = item.title_from_filename" in region,
            "the fallback marker must come from the track, never be inferred from text")

    # The region is created after `controls_`, so the historical child indices of
    # the card used by the view/device assertions do not move.
    try:
        create = flat(function_body(strip_comments(view_source), "bool view::create("))
    except (AssertionError, ValueError):
        create = ""
        failures.append("cyberdeck_music_view.cpp: create() not found")
    controls_create = create.find("controls_ = lv_obj_create(now_playing_)")
    meta_create = create.find("meta_title_ = lv_label_create(now_playing_)")
    require(controls_create >= 0 and meta_create > controls_create,
            "the identity labels must be created after controls_ so the historic card "
            "child indices stay stable")
    require("meta_artist_ = lv_label_create(now_playing_)" in create and
            "meta_album_ = lv_label_create(now_playing_)" in create,
            "all three identity labels must be real labels of the card")
    try:
        destroy = flat(function_body(strip_comments(view_source), "void view::destroy()"))
    except (AssertionError, ValueError):
        destroy = ""
        failures.append("cyberdeck_music_view.cpp: destroy() not found")
    require("meta_title_ = meta_artist_ = meta_album_ = nullptr;" in destroy,
            "destroy() must drop the identity labels")
    for member in ("meta_title_text_", "meta_artist_text_", "meta_album_text_"):
        require(f"{member}.clear();" in destroy,
                f"destroy() must clear {member} so a recreated card has no residual text")

    # Header seam used by the tests and the UI.
    require("struct now_playing_text" in header_source and
            "std::string_view title;" in header_source and
            "std::string_view artist;" in header_source and
            "std::string_view album;" in header_source and
            "bool title_from_filename = false;" in header_source,
            "the view header must expose now_playing_text{title,artist,album,title_from_filename}")
    require("void render_now_playing(const cyberdeck_music::track &item);" in header_source,
            "the view header must expose render_now_playing(const track &)")
    require("now_playing_text now_playing() const;" in header_source,
            "the view header must expose the read-only now_playing() seam")

    # --- TEST-MUSIC-META-GLYPH-ASCII ---------------------------------------
    covered = font_coverage(FONT.read_text(encoding="utf-8"))
    require(0x2013 not in covered and 0x4E00 not in covered,
            "the glyph coverage must stay the real cyberdeck_font cmap (non-vacuity)")
    try:
        placeholder = placeholder_literal(view_source)
    except AssertionError as error:
        placeholder = ""
        failures.append(str(error))
    require(placeholder != "", "the unavailable placeholder must not be empty")
    if placeholder:
        require(all(0x20 <= ord(char) <= 0x7E for char in placeholder),
                f"the placeholder {placeholder!r} must stay printable ASCII")
        missing = sorted({ord(char) for char in placeholder if ord(char) not in covered})
        require(not missing,
                "the placeholder has code points outside cyberdeck_font: " +
                ", ".join(f"U+{code:04X}" for code in missing))

    # --- TEST-MUSIC-META-BOUNDS-DUMP ---------------------------------------
    try:
        capacity = album_capacity()
        limit = dump_text_limit()
    except AssertionError as error:
        capacity = 0
        limit = 0
        failures.append(str(error))
    require(capacity > 0 and limit > 0,
            "k_max_album_bytes and k_max_text_bytes must stay positive")
    longest_literal = len(placeholder)
    require(capacity + longest_literal <= limit,
            f"k_max_album_bytes ({capacity}) + the longest region literal "
            f"({longest_literal}) must fit ui_dump::k_max_text_bytes ({limit}); "
            "otherwise the device dump truncates the album")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("PASS: the NOW PLAYING identity region is fed only by "
          "playback.current behind has_playback_snapshot, render()/render_browser() "
          "never own it, the identity labels are created after controls_ and cleared "
          "by destroy(), the single unavailable placeholder is printable ASCII inside "
          f"the real cyberdeck_font cmap, and k_max_album_bytes ({capacity}) + the "
          f"placeholder ({longest_literal}) fits ui_dump::k_max_text_bytes ({limit})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
