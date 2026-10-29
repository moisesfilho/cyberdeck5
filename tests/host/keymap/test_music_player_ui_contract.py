#!/usr/bin/env python3
"""Structural contract for music player UI text coverage and track-row padding.

The player renders with `cyberdeck_font`, generated with
`-r 0x20-0x7e,0xa0-0xff` plus the sparse Font Awesome block 0xF067..0xF293.
Any other code point in a UI literal renders as a missing-glyph box, so the
first contract scans the player UI literals only: track/ID3 content is dynamic
data and never comes from a literal.

`styles.btn` from the LVGL default theme applies `pad_ver = PAD_SMALL` and
`pad_hor = PAD_DEF`, which shrinks the content area below the glyph height and
clips track names. `lv_obj_set_style_pad_all()` does not touch
`pad_row`/`pad_column`, so both must be zeroed explicitly.
"""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
VIEW = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_music_view.cpp"
COMMAND_INPUT = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_music_command_input.cpp"
APP = ROOT / "components/cyberdeck/src/apps/music/cyberdeck_music_app.cpp"
FONT = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_font.c"

# Ranges generated into `cyberdeck_font`; the icon block is deliberately not
# accepted here because no player label uses a symbol glyph.
ALLOWED_RANGES = ((0x20, 0x7E), (0xA0, 0xFF))
UNCOVERED_SAMPLES = {
    0x2013: "EN DASH",
    0x2014: "EM DASH",
    0x2190: "LEFTWARDS ARROW",
    0x2191: "UPWARDS ARROW",
    0x2192: "RIGHTWARDS ARROW",
    0x2193: "DOWNWARDS ARROW",
    0x2212: "MINUS SIGN",
}


def strip_comments(source: str) -> str:
    """Drop // and /* */ comments while keeping string/char literal bytes."""
    out: list[str] = []
    index = 0
    length = len(source)
    while index < length:
        char = source[index]
        if char == "/" and source.startswith("//", index):
            while index < length and source[index] != "\n":
                index += 1
        elif char == "/" and source.startswith("/*", index):
            index += 2
            while index + 1 < length and source[index:index + 2] != "*/":
                index += 1
            index += 2
        elif char in "\"'":
            quote = char
            out.append(char)
            index += 1
            while index < length:
                if source[index] == "\\":
                    out.append(source[index:index + 2])
                    index += 2
                    continue
                out.append(source[index])
                if source[index] == quote:
                    index += 1
                    break
                index += 1
        else:
            out.append(char)
            index += 1
    return "".join(out)


def ui_literals(source: str) -> list[str]:
    return re.findall(r'"((?:[^"\\\n]|\\.)*)"', strip_comments(source))


def uncovered_literals(source: str) -> list[tuple[int, str]]:
    """Return `(codepoint, literal)` for every code point outside the font."""
    offenders: list[tuple[int, str]] = []
    for literal in ui_literals(source):
        for char in literal:
            code = ord(char)
            if any(low <= code <= high for low, high in ALLOWED_RANGES):
                continue
            offenders.append((code, literal))
    return offenders


def font_ranges() -> list[tuple[int, int]]:
    generated = FONT.read_text(encoding="utf-8")
    return [
        (int(start, 0), int(start, 0) + int(length) - 1)
        for start, length in re.findall(
            r"range_start = (\d+), \.range_length = (\d+)", generated)
    ]


def block_after(source: str, marker: str) -> str:
    start = source.index(marker) + len(marker)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:index]
    raise AssertionError(f"unclosed block after: {marker}")


def function_body(source: str, name: str) -> str:
    return block_after(source, name)


def track_create_issues(source: str) -> list[str]:
    issues: list[str] = []
    try:
        create = function_body(source, "bool view::create(")
        loop = block_after(create, "for (auto &track : tracks_)")
    except (AssertionError, ValueError):
        return ["track creation loop not found"]
    for setter in ("pad_all", "pad_row", "pad_column"):
        if not re.search(rf"lv_obj_set_style_{setter}\(\s*track\s*,\s*0\s*,\s*0\s*\)", loop):
            issues.append(f"track row must zero lv_obj_set_style_{setter}(track, 0, 0)")
    if not re.search(r"lv_obj_set_style_pad_all\(\s*track\s*,\s*0\s*,\s*0\s*\)", loop):
        issues.append("track row keeps the theme padding that clips the glyph")
    if "lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS)" not in loop:
        issues.append("track label must keep LV_LABEL_LONG_MODE_DOTS")
    return issues


def track_layout_issues(source: str) -> list[str]:
    issues: list[str] = []
    try:
        layout = function_body(source, "void view::layout()")
        loop = block_after(layout, "for (std::size_t i = 0; i < tracks_.size(); ++i)")
    except (AssertionError, ValueError):
        return ["track layout loop not found"]
    if "const int row_height = line;" not in layout:
        issues.append("track row height must stay derived from the font line")
    inset = re.search(r"constexpr int kTrackInset = (\d+);", layout)
    if not inset or not 1 <= int(inset.group(1)) <= 12:
        issues.append("track label needs an explicit 1..12 px horizontal inset")
        inset_value = None
    else:
        inset_value = int(inset.group(1))
    label_height = re.search(
        r"const int track_label_height =\s*std::min\(row_height, "
        r"static_cast<int>\(lv_font_get_line_height\(&cyberdeck_font\)\)\);", layout)
    if not label_height:
        issues.append("track label height must be derived from the row/font line")
    if not re.search(r"const int track_label_y = \(row_height - track_label_height\) / 2;", layout):
        issues.append("track label must be vertically centred in the row")
    if not re.search(r"lv_obj_set_pos\(\s*label\s*,\s*kTrackInset\s*,\s*track_label_y\s*\)", loop):
        issues.append("track label position must use the inset and the centred offset")
    if not re.search(
            r"lv_obj_set_size\(\s*label\s*,\s*std::max\(1, "
            r"browser_inner_width - kTrackInset \* 2\)\s*,\s*track_label_height\s*\)", loop):
        issues.append("track label must keep the horizontal inset in its width and use the row height")
    if inset_value is not None and re.search(r"lv_obj_set_pos\(\s*label\s*,\s*0\s*,\s*0\s*\)", loop):
        issues.append("track label must not fall back to the legacy (0,0) position")
    if re.search(r"lv_obj_set_size\(\s*label\s*,\s*browser_inner_width\s*,\s*row_height\s*\)", loop):
        issues.append("track label must not fall back to the legacy full-width row sizing")
    controls = block_after(layout, "for (std::size_t i = 0; i < buttons_.size(); ++i)")
    if not re.search(r"lv_obj_set_pos\(\s*label\s*,\s*0\s*,\s*0\s*\)", controls):
        issues.append("control button labels must stay unchanged")
    if "kMinimumButtonSize = 44" not in source:
        issues.append("control buttons must keep the 44 px minimum")
    return issues


def main() -> int:
    failures: list[str] = []

    def require(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    ranges = font_ranges()
    require(all(any(low <= sample <= high for low, high in ranges)
                for sample in (0x20, 0x7E, 0xA0, 0xFF)),
            "font coverage must contain the ASCII and Latin-1 ranges used by the player")
    require(not any(low <= code <= high for low, high in ranges
                    for code in UNCOVERED_SAMPLES),
            "the uncovered sample code points must stay outside the font coverage")

    for path in (VIEW, COMMAND_INPUT, APP):
        for code, literal in uncovered_literals(path.read_text(encoding="utf-8")):
            name = UNCOVERED_SAMPLES.get(code, "uncovered code point")
            failures.append(
                f"{path.name}: U+{code:04X} ({name}) in UI literal {literal!r} renders as a box")

    # Non-vacuity: the scanner must reject the two defects that were observed
    # in the screenshot evidence and must ignore comments.
    historic = 'const char *a = "-10s";'
    require(uncovered_literals(historic) == [],
            "scanner must accept ASCII replacements")
    minus = 'const char *a = "\u221210s"; const char *b = "Vol \u2212";'
    require(sorted({code for code, _ in uncovered_literals(minus)}) == [0x2212],
            "scanner must reject U+2212 MINUS SIGN")
    dash = 'text += " \u2014 " + std::string(artist);'
    require([code for code, _ in uncovered_literals(dash)] == [0x2014],
            "scanner must reject U+2014 EM DASH")
    commented = '// historical label: "\u221210s" and " \u2014 "\nconst char *a = "-";'
    require(uncovered_literals(commented) == [],
            "scanner must ignore code points that only appear in comments")

    view_source = VIEW.read_text(encoding="utf-8")
    require(track_create_issues(view_source) == [],
            "track rows must zero every theme padding channel")
    require(track_layout_issues(view_source) == [],
            "track labels must be inset and vertically centred inside the row")

    # Mutation proof: the legacy implementation must be rejected by the same
    # checks, so the contract cannot pass vacuously.
    legacy_padding = view_source.replace("        lv_obj_set_style_pad_row(track, 0, 0);\n", "")
    require(track_create_issues(legacy_padding) != [],
            "mutation without pad_row must be rejected")
    legacy_position = view_source.replace(
        "        lv_obj_set_pos(label, kTrackInset, track_label_y);\n"
        "        lv_obj_set_size(label, std::max(1, browser_inner_width - kTrackInset * 2), track_label_height);",
        "        lv_obj_set_pos(label, 0, 0);\n"
        "        lv_obj_set_size(label, browser_inner_width, row_height);",
    )
    require(track_layout_issues(legacy_position) != [],
            "mutation back to the legacy (0,0) full-width label must be rejected")
    legacy_centring = view_source.replace(
        "        lv_obj_set_pos(label, kTrackInset, track_label_y);",
        "        lv_obj_set_pos(label, kTrackInset, 0);")
    require(track_layout_issues(legacy_centring) != [],
            "mutation dropping the vertical centring must be rejected")

    for failure in failures:
        print(f"FAIL: {failure}")
    if failures:
        return 1
    print("PASS: player UI literals stay inside the font coverage and track rows "
          "zero the theme padding with an inset, centred label")
    return 0


if __name__ == "__main__":
    sys.exit(main())
