#!/usr/bin/env python3
"""Structural contract for music player UI text coverage and track-row padding.

The player renders with `cyberdeck_font`, generated with
`-r 0x20-0x7e,0xa0-0xff` plus the sparse Font Awesome block 0xF067..0xF293.
Any other code point renders as a missing-glyph box, so this contract derives
the real coverage from the font source itself and rejects both flavours of the
defect: uncovered code points inside UI literals and `LV_SYMBOL_*` macros whose
glyph is not part of the font. Track/ID3 content is dynamic data and never comes
from a literal, so only literals are scanned.

`styles.btn` from the LVGL default theme applies `pad_ver = PAD_SMALL` and
`pad_hor = PAD_DEF`, which shrinks the content area below the glyph height and
clips track names. `lv_obj_set_style_pad_all()` does not touch
`pad_row`/`pad_column`, so both must be zeroed explicitly.

`LV_SYMBOL_*` comparisons are exempt from the font coverage only when the token
is an argument of a `strcmp` family call that also compares a variable assigned
from `lv_keyboard_get_button_text()`; that text is painted by LVGL's keyboard
font, not by `cyberdeck_font`. Everything else — including a render sharing a
statement with a comparison — is checked against the real font cmaps.

Code points come only from the versioned `lv_symbol_codepoints.json`. The LVGL
header lives in the gitignored `managed_components/`, which does not exist when
the aggregate runs in CI before the ESP-IDF warm-up; reading it here turned every
push red with `FileNotFoundError`. Keeping the map in the tree also keeps the
contract meaningful against the pinned LVGL 9.6 symbols it was written for, and
`verify_lv_symbol_codepoints` (outside the aggregate) re-parses the header and
fails on drift whenever `managed_components` is materialized.
"""

from pathlib import Path
import re
import sys

from lv_symbol_map import load_symbols, map_issues, parse_map, render_json

ROOT = Path(__file__).resolve().parents[3]
DISPLAY = ROOT / "components/cyberdeck/src/platform/display"
VIEW = DISPLAY / "cyberdeck_music_view.cpp"
VIEW_HEADER = (ROOT / "components/cyberdeck/include/platform/display"
               / "cyberdeck_music_view.h")
HEADER = DISPLAY / "cyberdeck_header_view.cpp"
COMMAND_INPUT = DISPLAY / "cyberdeck_music_command_input.cpp"
APP = ROOT / "components/cyberdeck/src/apps/music/cyberdeck_music_app.cpp"
FONT = DISPLAY / "cyberdeck_font.c"
SYMBOL_MAP = Path(__file__).resolve().with_name("lv_symbol_codepoints.json")

# Player controls hint. ASCII only: U+2190..U+2193 have no glyph in the font.
# The landscape variant is one row; portrait splits the same shortcuts after
# "seek" because the 71-character row needs 767.7 px at the real font advance
# (adv_w 173/16 = 10.8125 px/char) and the portrait surface only offers 680 px.
MUSIC_HINT = ("Enter play/pause  left/right seek  up/down volume  N/P track  "
              "Esc close")
MUSIC_HINT_PORTRAIT = MUSIC_HINT.replace("seek  up/down", "seek\nup/down")

# Macros used (or reserved for the header) by the UI that must stay covered.
REQUIRED_COVERED_SYMBOLS = (
    "LV_SYMBOL_PLUS",
    "LV_SYMBOL_MINUS",
    "LV_SYMBOL_CHARGE",
    "LV_SYMBOL_WIFI",
    "LV_SYMBOL_BLUETOOTH",
    "LV_SYMBOL_BATTERY_FULL",
    "LV_SYMBOL_BATTERY_3",
    "LV_SYMBOL_BATTERY_2",
    "LV_SYMBOL_BATTERY_1",
    "LV_SYMBOL_BATTERY_EMPTY",
)

# Ranges generated into `cyberdeck_font`; kept as a sanity check of the parser.
DOCUMENTED_RANGES = ((0x20, 0x7E), (0xA0, 0xFF))
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


def uncovered_literals(source: str, covered: set[int]) -> list[tuple[int, str]]:
    """Return `(codepoint, literal)` for every code point without a glyph."""
    offenders: list[tuple[int, str]] = []
    for literal in ui_literals(source):
        for char in literal:
            code = ord(char)
            if code in covered:
                continue
            offenders.append((code, literal))
    return offenders


def font_coverage(generated: str) -> set[int]:
    """Resolve the real glyph coverage from the generated `lv_font_t` tables.

    `unicode_list_*` holds offsets relative to `range_start` for sparse cmaps,
    which is exactly how LVGL resolves `LV_FONT_FMT_TXT_CMAP_SPARSE_TINY`.
    """
    lists: dict[str, list[int]] = {
        name: [int(value, 0) for value in re.findall(r"0x[0-9A-Fa-f]+|\d+", body)]
        for name, body in re.findall(
            r"static const uint16_t (unicode_list_\w+)\[\] = \{([^}]*)\}", generated)
    }
    cmaps = re.search(r"lv_font_fmt_txt_cmap_t cmaps\[\] =\s*\{(.*?)\n\};",
                      generated, re.S)
    if not cmaps:
        raise AssertionError("font cmap table not found")
    covered: set[int] = set()
    entries = re.findall(r"\{([^{}]*)\}", cmaps.group(1))
    if not entries:
        raise AssertionError("font cmap table is empty")
    for entry in entries:
        fields = dict(re.findall(r"\.(\w+) = ([^,]+)", entry))
        start = int(fields["range_start"], 0)
        kind = fields["type"].strip()
        if kind.endswith("FORMAT0_TINY") or kind.endswith("FORMAT0_FULL"):
            length = int(fields["range_length"], 0)
            covered |= set(range(start, start + length))
        elif kind.endswith("SPARSE_TINY") or kind.endswith("SPARSE_FULL"):
            name = fields["unicode_list"].strip()
            if name not in lists:
                raise AssertionError(f"missing unicode list: {name}")
            length = int(fields["list_length"], 0)
            covered |= {start + offset for offset in lists[name][:length]}
        else:
            raise AssertionError(f"unsupported font cmap type: {kind}")
    return covered


COMPARE_CALLS = ("strcmp", "strncmp", "strcasecmp", "strncasecmp")


def code_only(source: str) -> str:
    """Blank literal bytes so braces/parens inside strings cannot nest wrongly."""
    out = list(source)
    index = 0
    length = len(source)
    while index < length:
        char = source[index]
        if char not in "\"'":
            index += 1
            continue
        quote = char
        index += 1
        while index < length:
            if source[index] == "\\":
                width = min(2, length - index)
                out[index:index + width] = " " * width
                index += 2
                continue
            if source[index] == quote:
                index += 1
                break
            out[index] = " "
            index += 1
    return "".join(out)


def block_span(source: str, start: int) -> tuple[int, int]:
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return brace + 1, index
    raise AssertionError("unclosed block")


def enclosing_function(source: str, index: int) -> str:
    """Body of the innermost function definition that contains `index`.

    Fail-closed: an unresolved definition returns "" instead of the whole file,
    otherwise one `lv_keyboard_get_button_text()` anywhere would exempt every
    comparison in the translation unit. Control-flow blocks are excluded from
    the candidate set and a candidate only wins when its braces really span the
    token, so the exemption cannot leak to a sibling function.
    """
    code = code_only(source)
    definition = re.compile(
        r"(?m)^[ \t]*(?!if\b|for\b|while\b|switch\b|catch\b|else\b)"
        r"[A-Za-z_][\w:<>,\*&\s]*\b[A-Za-z_]\w*\s*\([^;{}]*\)\s*"
        r"(?:const\s*)?\{")
    for match in reversed([match for match in definition.finditer(code)
                           if match.start() <= index]):
        try:
            low, high = block_span(code, match.end() - 1)
        except AssertionError:
            continue
        if low <= index < high:
            return code[low:high]
    return ""


def call_around(source: str, index: int) -> tuple[str, str]:
    """`(callee, argument_text)` of the innermost call containing `index`."""
    depth = 0
    opener = -1
    for cursor in range(index - 1, -1, -1):
        char = source[cursor]
        if char == ")":
            depth += 1
        elif char == "(":
            if depth == 0:
                opener = cursor
                break
            depth -= 1
    if opener < 0:
        return "", ""
    depth = 0
    closer = -1
    for cursor in range(opener, len(source)):
        char = source[cursor]
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                closer = cursor
                break
    arguments = source[opener + 1:closer] if closer > 0 else ""
    cursor = opener - 1
    while cursor >= 0 and source[cursor].isspace():
        cursor -= 1
    end = cursor + 1
    while cursor >= 0 and (source[cursor].isalnum() or source[cursor] == "_"):
        cursor -= 1
    return source[cursor + 1:end], arguments


def keyboard_text_variables(body: str) -> set[str]:
    """Names assigned from `lv_keyboard_get_button_text(...)` in `body`."""
    return set(re.findall(
        r"\b(\w+)\s*=\s*lv_keyboard_get_button_text\s*\(", body))


def scan_symbols(label: str, source: str, symbols: dict[str, int],
                 covered: set[int]) -> tuple[list[str], set[str]]:
    """Check rendered `LV_SYMBOL_*` uses; return `(issues, rendered names)`.

    A symbol compared against virtual-keyboard button text is painted by LVGL's
    own keyboard font, so it is out of scope for `cyberdeck_font`. The
    exemption is granted only when the token is an argument of a `strcmp`
    family call that also compares a variable assigned from
    `lv_keyboard_get_button_text()` inside the same function body. A hardcoded
    symbol allowlist, a `strcmp` on any other operand, and a render that merely
    shares a statement with a comparison all stay flagged.
    """
    code = code_only(strip_comments(source))
    issues: list[str] = []
    rendered: set[str] = set()
    for match in re.finditer(r"\bLV_SYMBOL_[A-Z0-9_]+\b", code):
        name = match.group(0)
        if name not in symbols:
            issues.append(f"{label}: {name} is not defined by "
                      "lv_symbol_codepoints.json")
            continue
        callee, arguments = call_around(code, match.start())
        if callee in COMPARE_CALLS:
            body = enclosing_function(code, match.start())
            variables = keyboard_text_variables(body)
            if any(re.search(rf"\b{re.escape(variable)}\b", arguments)
                   for variable in variables):
                continue
            issues.append(
                f"{label}: {name} is compared without the virtual keyboard "
                "button text; validate the glyph against cyberdeck_font")
            continue
        rendered.add(name)
        if symbols[name] not in covered:
            issues.append(
                f"{label}: {name} (U+{symbols[name]:04X}) has no glyph in "
                "cyberdeck_font")
    return issues, rendered


def block_after(source: str, marker: str) -> str:
    low, high = block_span(source, source.index(marker) + len(marker))
    return source[low:high]


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
    # The default (light) theme rounds `btn` and paints a grey drop shadow, which
    # leaks as a bright arc at the row corners; both must be zeroed explicitly.
    for setter, value in (("radius", "0"), ("outline_width", "0"),
                          ("shadow_width", "0"), ("shadow_opa", "LV_OPA_TRANSP")):
        if not re.search(rf"lv_obj_set_style_{setter}\(\s*track\s*,\s*{value}\s*,\s*0\s*\)", loop):
            issues.append(f"track row must zero lv_obj_set_style_{setter}(track, {value}, 0)")
    return issues


def header_music_issues(source: str) -> list[str]:
    issues: list[str] = []
    try:
        create = function_body(source, "bool view::create(")
    except (AssertionError, ValueError):
        return ["header view create function not found"]
    begin = create.find("s_music_button = lv_btn_create(right)")
    end = create.find("lv_obj_add_event_cb(s_music_button", begin if begin >= 0 else 0)
    if begin < 0 or end < 0:
        return ["header music button creation block not found"]
    block = create[begin:end]
    for setter, value in (("radius", "0"), ("outline_width", "0"),
                          ("shadow_width", "0"), ("shadow_opa", "LV_OPA_TRANSP")):
        if not re.search(rf"lv_obj_set_style_{setter}\(\s*s_music_button\s*,\s*{value}\s*,\s*0\s*\)", block):
            issues.append(
                f"header music button must zero lv_obj_set_style_{setter}(s_music_button, {value}, 0)")
    if not re.search(r"lv_obj_set_size\(\s*s_music_button\s*,\s*26\s*,\s*36\s*\)", create):
        issues.append("header music button must keep the 26x36 target")
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


def progress_style_issues(source: str) -> list[str]:
    """AC-MUSIC-13: the player bar and controls stay inside the mono palette.

    The default LVGL theme is light: `styles.slider` paints the INDICATOR with
    `bg_color_primary` (blue) and rounds MAIN/INDICATOR, while `styles.knob`
    draws a circle larger than the track. `draw_knob` always initializes a rect
    draw descriptor for LV_PART_KNOB, so transparent background plus zero
    padding/border/shadow hides the ball without giving up the `lv_slider`
    tap/drag seek. The unfilled MAIN track uses the card border grey with an
    explicit opaque `bg_opa`, not the card surface, so the rail is visible even
    when the indicator is empty (the theme paints MAIN at `LV_OPA_20`).

    Comments are stripped before matching, so a commented-out setter cannot
    satisfy a requirement. Every pinned slot is also checked as last-write-wins
    (LVGL keeps one style value per part/property): an extra write after the
    override silently reverts it while a presence-only scan stays green, and the
    host shim folds all parts into one slot, so `test_display_views` reads only
    the final KNOB writes and cannot see the MAIN slots either.

    The control cells are `kSelected` (#303030) — the elevated grey of the
    selected library row — because `kBlack` is darker than the card surface they
    sit on and would read as a hole.
    """
    issues: list[str] = []
    try:
        create = strip_comments(function_body(source, "bool view::create("))
    except (AssertionError, ValueError):
        return ["player view create function not found"]
    for part in ("MAIN", "INDICATOR"):
        if not re.search(
                rf"lv_obj_set_style_radius\(\s*progress_\s*,\s*0\s*,\s*LV_PART_{part}\s*\)",
                create):
            issues.append(f"progress bar must flatten LV_PART_{part} with radius 0")
    # The unfilled track is the part that still shows the theme palette: the
    # default light theme paints slider MAIN with `bg_color_primary_muted`, a
    # blue-grey. Dropping this override keeps the bar blue behind the white
    # indicator, so the MAIN background is as load-bearing as the INDICATOR one.
    # The track must be the card border grey: `kSurface` matches the card and
    # would make the empty bar invisible (a loose white block at playback start).
    if not re.search(
            r"lv_obj_set_style_bg_color\(\s*progress_\s*,\s*kBorder\s*,\s*LV_PART_MAIN\s*\)",
            create):
        issues.append(
            "progress track must use kBorder so the rail stays visible instead "
            "of the theme primary_muted")
    # The same theme style also sets `bg_opa` to `LV_OPA_20` on MAIN, so the
    # border grey would be blended at 20% over the card and the rail would be
    # invisible on device (measured #080A08 on the bar band). MAIN therefore
    # needs an explicit opaque `bg_opa`; INDICATOR already gets `LV_OPA_COVER`
    # from the theme `bg_color_primary`.
    if not re.search(
            r"lv_obj_set_style_bg_opa\(\s*progress_\s*,\s*LV_OPA_COVER\s*,\s*LV_PART_MAIN\s*\)",
            create):
        issues.append(
            "progress track must set lv_obj_set_style_bg_opa(progress_, "
            "LV_OPA_COVER, LV_PART_MAIN) because the theme paints MAIN at "
            "LV_OPA_20 and the rail disappears")
    # LVGL keeps a single style value per part/property, so a later write wins
    # and silently reverts the override while a presence-only scan stays green
    # (measured class: the theme MAIN opacity). Every slot pinned above must
    # therefore also hold its expected value as the LAST write.
    for setter, part, value in (
            ("bg_color", "MAIN", "kBorder"),
            ("bg_opa", "MAIN", "LV_OPA_COVER"),
            ("border_width", "MAIN", "0"),
            ("radius", "MAIN", "0"),
            ("bg_color", "INDICATOR", "kWhite"),
            ("radius", "INDICATOR", "0"),
            ("bg_opa", "KNOB", "LV_OPA_TRANSP"),
            ("pad_all", "KNOB", "0"),
            ("radius", "KNOB", "0"),
            ("border_width", "KNOB", "0"),
            ("shadow_width", "KNOB", "0")):
        writes = re.findall(
            rf"lv_obj_set_style_{setter}\(\s*progress_\s*,\s*(\w+)\s*,\s*"
            rf"LV_PART_{part}\s*\)", create)
        if writes and writes[-1] != value:
            issues.append(
                f"the last LV_PART_{part} {setter} write must stay {value}, but it "
                f"is {writes[-1]}: a later write wins in LVGL and the theme style "
                "leaks back")
    if not re.search(
            r"lv_obj_set_style_bg_color\(\s*progress_\s*,\s*kWhite\s*,\s*LV_PART_INDICATOR\s*\)",
            create):
        issues.append("progress indicator must use kWhite instead of the theme primary")
    for setter, value in (("bg_opa", "LV_OPA_TRANSP"), ("pad_all", "0"),
                          ("radius", "0"), ("border_width", "0"),
                          ("shadow_width", "0")):
        if not re.search(
                rf"lv_obj_set_style_{setter}\(\s*progress_\s*,\s*{value}\s*,\s*"
                r"LV_PART_KNOB\s*\)", create):
            issues.append(
                f"progress knob must zero lv_obj_set_style_{setter}(progress_, {value}, "
                "LV_PART_KNOB)")
    if "LV_RADIUS_CIRCLE" in strip_comments(source):
        issues.append("the player view must not paint circular geometry")
    try:
        loop = block_after(create, "for (std::size_t index = 0; index < count; ++index)")
    except (AssertionError, ValueError):
        return issues + ["control button creation loop not found"]
    for match in re.finditer(
            r"lv_obj_set_style_border_width\(\s*buttons_\[index\]\s*,\s*(\d+)\s*,\s*0\s*\)",
            loop):
        if int(match.group(1)) > 0:
            issues.append("control buttons must not paint a border")
    if not re.search(
            r"lv_obj_set_style_border_width\(\s*buttons_\[index\]\s*,\s*0\s*,\s*0\s*\)",
            loop):
        issues.append("control buttons must zero lv_obj_set_style_border_width")
    # The control cells sit on the card surface, so a `kBlack` fill is darker than
    # the card itself and reads as a hole. They reuse the elevated grey of the
    # selected library row (`kSelected` #303030) with `kWhite` text; `kBlack`
    # stays the view surface background (`surface_`), which is not a control.
    if not re.search(
            r"lv_obj_set_style_bg_color\(\s*buttons_\[index\]\s*,\s*kSelected\s*,\s*0\s*\)",
            loop):
        issues.append(
            "control buttons must use kSelected (#303030) as bg_color, the same "
            "elevated grey as the selected library row, instead of a colour darker "
            "than the card")
    for match in re.finditer(
            r"lv_obj_set_style_bg_color\(\s*buttons_\[index\]\s*,\s*(\w+)\s*,\s*0\s*\)",
            loop):
        if match.group(1) == "kBlack":
            issues.append(
                "control buttons must not use kBlack as bg_color: kBlack is the "
                "darker-than-card view surface background, not the control grey "
                "(use kSelected #303030)")
    return issues


def card_inset_issues(source: str) -> list[str]:
    """AC-MUSIC-13: card children keep the same inset on both sides.

    `card()` applies `pad_all = 8` plus a 1 px border, and a child at `x = 8` is
    positioned relative to the card *content* area, which already starts at
    `border + pad` from the card border. The usable width is therefore
    `lv_obj_get_content_width(card) - 2 * kCardInset`; deriving it from the outer
    card width (`now_width`/`browser_width`) counted the border/pad once and
    pushed the progress bar, the right control column and every library row 1 px
    past the card border (device: bar right edge 574 vs card 573).
    """
    issues: list[str] = []
    inset = re.search(r"constexpr int kCardInset = (\d+);", strip_comments(source))
    if not inset:
        issues.append(
            "the card padding must be a named constexpr int kCardInset (it documents "
            "the pad_all applied by card())")
    elif int(inset.group(1)) != 8:
        issues.append(
            f"kCardInset must stay 8 because card() applies pad_all = 8, but it is "
            f"{inset.group(1)}")
    try:
        layout = strip_comments(function_body(source, "void view::layout()"))
    except (AssertionError, ValueError):
        return issues + ["player layout function not found"]
    twice_inset = r"(?:2\s*\*\s*kCardInset|kCardInset\s*\*\s*2)"
    for name, panel in (("inner_width", "now_playing_"),
                        ("browser_inner_width", "list_")):
        pattern = (rf"const int {name} =\s*std::max\(\s*1\s*,\s*"
                   rf"static_cast<int>\(\s*lv_obj_get_content_width\(\s*{panel}\s*\)\s*\)"
                   rf"\s*-\s*{twice_inset}\s*\)")
        if not re.search(pattern, layout):
            issues.append(
                f"{name} must be derived from lv_obj_get_content_width({panel}) minus "
                "2 * kCardInset so the card child keeps the same inset on both sides")
    flat = re.sub(r"\s+", " ", layout)
    for legacy in ("now_width - 16", "browser_width - 16"):
        if legacy in flat:
            issues.append(
                f"{legacy} measures the outer card width: it ignores the card border "
                "and padding, so the child ends 1 px past the card on the right")
    return issues


def controls_grid_issues(source: str) -> list[str]:
    """AC-MUSIC-13: the control grid keeps equal gaps and a content anchor.

    Device defects measured with `ui.dump` plus pixels (fw 01dd21a9): the three
    rows touched each other (`controls_height = button_height * control_rows`, so
    the block read as one continuous cell) and the block was placed with
    `card_height - controls_area - 8`, mixing the outer card height with a
    content-relative `y`. The last row therefore ended on the card border (694)
    instead of keeping the 17 px inset measured on the left/right.
    """
    issues: list[str] = []
    try:
        layout = strip_comments(function_body(source, "void view::layout()"))
    except (AssertionError, ValueError):
        return ["player layout function not found"]
    flat = re.sub(r"\s+", " ", layout)
    if not re.search(r"const int row_gap = pad;", flat):
        issues.append(
            "the gap between control rows must be the named `const int row_gap = pad;` "
            "so it stays equal to the 4 px used between columns")
    height = re.search(r"const int controls_height = ([^;]+);", flat)
    if not height:
        issues.append("controls_height must be declared in view::layout()")
    else:
        expression = re.sub(r"\s+", "", height.group(1))
        if "row_gap" not in expression or "(control_rows-1)" not in expression:
            issues.append(
                "controls_height must reserve one row_gap between rows "
                "(`button_height * control_rows + row_gap * (control_rows - 1)`); "
                f"found `{height.group(1).strip()}`")
        elif expression == "button_height*control_rows":
            issues.append(
                "controls_height = button_height * control_rows glues the rows "
                "together; the rows need `row_gap` between them")
    area = re.search(r"const int controls_area = ([^;]+);", flat)
    if not area:
        issues.append("controls_area must be declared in view::layout()")
    elif "content_height" not in re.sub(r"\s+", "", area.group(1)):
        if re.search(r"card_height", area.group(1)):
            issues.append(
                "`controls_area` must be derived from the card *content* height "
                "(`content_height`, i.e. `lv_obj_get_content_height(now_playing_)`), "
                "not from the outer `card_height`")
        else:
            issues.append(
                "`controls_area` must be clamped by the card content height "
                "(`std::min(controls_height, std::max(line, content_height - content_y - pad))`)")
    if not re.search(r"const int content_height = static_cast<int>\(\s*"
                     r"lv_obj_get_content_height\(\s*now_playing_\s*\)\s*\)", flat):
        issues.append(
            "the control block must be anchored by "
            "`lv_obj_get_content_height(now_playing_)` in `content_height`: `x = 8` is "
            "relative to the card content area, so the bottom inset must be measured "
            "there too")
    if not re.search(r"lv_obj_set_pos\(\s*controls_\s*,\s*8\s*,\s*content_height\s*-\s*"
                     r"controls_area\s*-\s*8\s*\)", flat):
        if re.search(r"card_height\s*-\s*controls_area", flat):
            issues.append(
                "`lv_obj_set_pos(controls_, 8, card_height - controls_area - 8)` anchors "
                "the block with the outer card height: the last row lands on the card "
                "border (device: 694, no 17 px inset). Use `content_height - controls_area - 8`")
        else:
            issues.append(
                "the control block must be positioned with "
                "`lv_obj_set_pos(controls_, 8, content_height - controls_area - 8)`")
    if not re.search(r"row\s*\*\s*\(\s*button_height\s*\+\s*row_gap\s*\)", flat):
        issues.append(
            "control rows must be positioned with `row * (button_height + row_gap)` so "
            "consecutive rows keep the same 4 px gap as the columns")
    return issues


FOOTER_LITERAL = re.compile(
    r'constexpr const char \*(kMusicControlsHint(?:Portrait)?)\s*=\s*'
    r'"((?:[^"\\\n]|\\.)*)"\s*;')


def footer_literals(source: str) -> dict[str, str]:
    """`name -> C string body` for the two shortcut-row literals."""
    return dict(FOOTER_LITERAL.findall(strip_comments(source)))


def footer_issues(source: str, header_source: str = "") -> list[str]:
    """The shortcut row must be a fixed footer label, not status text.

    The hint used to be appended to the status string of both render branches
    of `cyberdeck_ui.cpp`; the view never turned it into a widget, so it was
    dead text and could drift from the real key bindings. The footer is created
    after `list_` (children order stays `title_, now_playing_, list_, footer_`),
    painted with the palette MUTED grey and anchored in the bottom band; both
    cards are measured against `footer_y` so neither can overlap it.

    The band holds one row in landscape and two rows in portrait. The flat hint
    is 71 characters, which needs 767.7 px at the real `cyberdeck_font` advance
    (adv_w 173/16 = 10.8125 px/char), while the portrait surface only offers
    680 px of content; the device truncated "Esc close". The break is therefore
    explicit and driven by the existing `landscape` flag, never by runtime text
    metrics, so the host shim font (9 px/char) cannot hide the defect again.

    `layout()` also runs on every status refresh (~4x/s) and the footer text only
    depends on the orientation, so the setter is gated by the orientation that is
    already applied (`footer_landscape_`). Re-applying the same string makes
    LVGL free/re-allocate the label text and invalidate the whole band on every
    tick; `destroy()` resets the sentinel so a recreated label is painted.
    """
    issues: list[str] = []
    header = header_source or VIEW_HEADER.read_text(encoding="utf-8")
    try:
        create = strip_comments(function_body(source, "bool view::create("))
        layout = strip_comments(function_body(source, "void view::layout()"))
        destroy = strip_comments(function_body(source, "void view::destroy()"))
    except (AssertionError, ValueError):
        return ["player view create/layout/destroy function not found"]
    created = re.search(r"footer_\s*=\s*lv_label_create\(\s*surface_\s*\)", create)
    if not created:
        issues.append("the controls hint must be a footer label on the surface")
    elif not re.search(r"list_\s*=\s*lv_obj_create\(\s*surface_\s*\)", create) or \
            created.start() < create.index("list_ = lv_obj_create(surface_)"):
        issues.append("the footer must be created after list_ so the children "
                      "order stays title_, now_playing_, list_, footer_")
    if not re.search(r"style\(\s*footer_\s*,\s*kMuted\s*\)", create):
        issues.append("the footer must use kMuted (#8A8A8A) from the palette")
    if "const lv_color_t kMuted = lv_color_hex(0x8A8A8A);" not in source:
        issues.append("kMuted must stay lv_color_hex(0x8A8A8A)")
    if not re.search(r"lv_label_set_long_mode\(\s*footer_\s*,\s*"
                     r"LV_LABEL_LONG_MODE_DOTS\s*\)", create):
        issues.append("the footer must keep LV_LABEL_LONG_MODE_DOTS")
    literals = footer_literals(source)
    flat = literals.get("kMusicControlsHint")
    if flat is None:
        issues.append("the landscape hint must stay a named kMusicControlsHint literal")
    elif flat != MUSIC_HINT:
        issues.append(f"kMusicControlsHint must keep the exact shortcuts {MUSIC_HINT!r}, "
                      f"found {flat!r}")
    portrait = literals.get("kMusicControlsHintPortrait")
    if portrait is None:
        issues.append("the portrait hint must be a named kMusicControlsHintPortrait "
                      "literal so the break does not depend on runtime metrics")
    elif portrait.replace("\\n", "\n") != MUSIC_HINT_PORTRAIT:
        issues.append("kMusicControlsHintPortrait must repeat the same shortcuts "
                      f"split once after 'seek', found {portrait!r}")
    if re.search(r"lv_label_set_text\(\s*footer_", create):
        issues.append("the footer text must be applied by layout(), the only place "
                      "that knows the orientation; setting it in create() would be "
                      "overwritten and hide the missing portrait branch")
    flat_layout = re.sub(r"\s+", " ", layout)
    if not re.search(r"lv_label_set_text\(\s*footer_\s*,\s*landscape\s*\?\s*"
                     r"kMusicControlsHint\s*:\s*kMusicControlsHintPortrait\s*\)",
                     flat_layout):
        issues.append("layout() must apply kMusicControlsHint in landscape and "
                      "kMusicControlsHintPortrait in portrait, keyed by the existing "
                      "`landscape` flag")
    # The setter must sit inside the orientation guard: `layout()` is also called
    # by every status refresh, so an unconditional `lv_label_set_text` re-allocates
    # the text and invalidates the whole band ~4x/s.
    if not re.search(r"if\s*\(\s*footer_landscape_\s*!=\s*\w+\s*\)\s*\{\s*"
                     r"lv_label_set_text\(\s*footer_\s*,\s*landscape\s*\?\s*"
                     r"kMusicControlsHint\s*:\s*kMusicControlsHintPortrait\s*\)\s*;"
                     r"\s*footer_landscape_\s*=", flat_layout):
        issues.append("layout() must reapply the footer text only when the "
                      "orientation changes: the `lv_label_set_text(footer_, ...)` "
                      "call must be the body of `if (footer_landscape_ != ...)`, "
                      "which then stores the applied orientation")
    if not re.search(r"std::int8_t\s+footer_landscape_\s*=\s*-1\s*;",
                     strip_comments(header)):
        issues.append("the view header must keep `std::int8_t footer_landscape_ = "
                      "-1;` as the never-applied sentinel (-1 leaves the text "
                      "unset for a fresh/recreated label)")
    if not re.search(r"footer_landscape_\s*=\s*-1\s*;", destroy):
        issues.append("destroy() must reset footer_landscape_ to -1, otherwise a "
                      "recreated view in the same orientation never paints the "
                      "new footer label")
    if "lv_txt_get_width" in layout:
        issues.append("the footer break must be orientation-driven; measuring the "
                      "text at runtime reintroduces the shim-only coverage")
    if "const int footer_y = height - pad - footer_height;" not in flat_layout:
        issues.append("the footer band must be reserved as "
                      "`const int footer_y = height - pad - footer_height;` "
                      "before the cards are measured")
    if not re.search(r"const int footer_height = landscape \? line : 2 \* line \+ 2;",
                     flat_layout):
        issues.append("the footer band must reserve `line` in landscape and "
                      "`2 * line + 2` in portrait, otherwise the portrait row is "
                      "clipped again")
    if not re.search(r"lv_obj_set_pos\(\s*footer_\s*,\s*pad\s*,\s*footer_y\s*\)",
                     flat_layout):
        issues.append("the footer must be anchored at `(pad, footer_y)`")
    if not re.search(r"lv_obj_set_size\(\s*footer_\s*,\s*width - pad \* 2\s*,\s*"
                     r"footer_height\s*\)", flat_layout):
        issues.append("the footer must span the surface width minus the two pads")
    if not re.search(r"const int card_height = landscape \? footer_y - pad - y : "
                     r"\(footer_y - y - 2 \* pad\) / 2;", flat_layout):
        issues.append("card_height must stop at `footer_y` in both orientations so "
                      "neither card grows into the reserved footer band")
    return issues


def derivation_pattern(name: str) -> str:
    """Regex for the AC-MUSIC-13 derivation of `name` from a card content area.

    Formatting-tolerant on purpose: the mutation proofs replace the fixed
    derivation with the legacy arithmetic, so the pattern must not depend on
    where the line breaks land after a reformat.
    """
    return (rf"const int {name} =\s*std::max\(\s*1\s*,\s*"
            rf"static_cast<int>\(\s*lv_obj_get_content_width\(\s*\w+\s*\)\s*\)\s*-\s*"
            r"(?:2\s*\*\s*kCardInset|kCardInset\s*\*\s*2)\s*\)")


def raises(call) -> bool:
    """True when `call` rejects its input instead of returning a value."""
    try:
        call()
    except (AssertionError, ValueError):
        return True
    return False


def main() -> int:
    failures: list[str] = []

    def require(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    covered = font_coverage(FONT.read_text(encoding="utf-8"))
    documented_samples = tuple(
        sample for low, high in DOCUMENTED_RANGES
        for sample in (low, (low + high) // 2, high))
    require(all(sample in covered for sample in documented_samples),
            "font coverage must contain the ASCII and Latin-1 ranges used by the player")
    require(not any(code in covered for code in UNCOVERED_SAMPLES),
            "the uncovered sample code points must stay outside the font coverage")

    try:
        symbols = load_symbols(SYMBOL_MAP)
    except (AssertionError, ValueError) as error:
        failures.append(f"lv_symbol_codepoints.json is invalid: {error}")
        symbols = {}
    coherence = map_issues(symbols)
    require(not coherence,
            "lv_symbol_codepoints.json must stay coherent: "
            + "; ".join(coherence))
    missing = [name for name in REQUIRED_COVERED_SYMBOLS
               if symbols.get(name) not in covered]
    require(not missing,
            "these symbols must stay covered by cyberdeck_font: "
            + ", ".join(f"{name} (U+{symbols.get(name, 0):04X})" for name in missing))
    require(symbols.get("LV_SYMBOL_PLAY") not in covered,
            "LV_SYMBOL_PLAY (U+F04B) must stay outside the font; use ASCII '>'")

    # Non-vacuity: the versioned map and its coherence checks must stay live, so
    # a truncated or hand-edited JSON cannot silently weaken the symbol scan.
    require(map_issues({}) != [], "an empty symbol map must be rejected")
    require(map_issues({"LV_SYMBOL_BOGUS": 0x0041}) != [],
            "a symbol outside the Font Awesome block must be rejected")
    require(map_issues({"not_a_symbol": 0xF001}) != [],
            "a malformed symbol name must be rejected")
    require(map_issues({"LV_SYMBOL_BULLET": 0xF001}) != [],
            "LV_SYMBOL_BULLET must keep its plain Unicode code point")
    duplicate = ('{"symbols": {"LV_SYMBOL_OK": {"codepoint": 61452, '
                 '"hex": "U+F00C"}, "LV_SYMBOL_OK": {"codepoint": 61452, '
                 '"hex": "U+F00C"}}}')
    require(raises(lambda: parse_map(duplicate)),
            "a duplicated symbol name must be rejected")
    empty_hex = ('{"symbols": {"LV_SYMBOL_OK": {"codepoint": 61452, '
                 '"hex": ""}}}')
    require(raises(lambda: parse_map(empty_hex)),
            "an empty hex label must be rejected")
    wrong_hex = ('{"symbols": {"LV_SYMBOL_OK": {"codepoint": 61452, '
                 '"hex": "U+F00D"}}}')
    require(raises(lambda: parse_map(wrong_hex)),
            "a hex label disagreeing with the code point must be rejected")
    require(parse_map(render_json(symbols)) == symbols,
            "the map must round-trip through its own generator")

    literal_sources = sorted(DISPLAY.glob("*.cpp")) + [APP]
    require({VIEW, COMMAND_INPUT} <= set(literal_sources),
            "the literal scan must include the player view and command input sources")
    for path in literal_sources:
        for code, literal in uncovered_literals(path.read_text(encoding="utf-8"),
                                                covered):
            name = UNCOVERED_SAMPLES.get(code, "uncovered code point")
            failures.append(
                f"{path.name}: U+{code:04X} ({name}) in UI literal {literal!r} "
                "renders as a box")

    ui_source = DISPLAY.joinpath("cyberdeck_ui.cpp").read_text(encoding="utf-8")
    # The fixed shortcut row is a footer widget in the player view. The UI used
    # to append `kMusicControlsHint` to the status text of both render branches,
    # where it never became a real row: the view must own the literal, and the
    # UI must not resurrect the concatenation.
    require("kMusicControlsHint" not in ui_source,
            "the player controls hint must live in the view footer, not in "
            "cyberdeck_ui.cpp")
    require(MUSIC_HINT not in ui_source,
            "the player controls hint literal must not be concatenated into the "
            "status text rendered by cyberdeck_ui.cpp")

    symbol_sources = [path for path in literal_sources
                      if "LV_SYMBOL_" in path.read_text(encoding="utf-8")]
    rendered: set[str] = set()
    for path in symbol_sources:
        issues, found = scan_symbols(path.name, path.read_text(encoding="utf-8"),
                                     symbols, covered)
        failures.extend(issues)
        rendered |= found
    require({"LV_SYMBOL_BLUETOOTH", "LV_SYMBOL_CHARGE",
             "LV_SYMBOL_BATTERY_FULL", "LV_SYMBOL_MINUS"} <= rendered,
            "the symbol scan must see the rendered header glyphs, not an empty set")

    # Non-vacuity: the scanner must reject the two defects observed in this
    # round and must ignore comments.
    historic = 'const char *a = "-10s";'
    require(uncovered_literals(historic, covered) == [],
            "scanner must accept ASCII replacements")
    minus = 'const char *a = "\u221210s"; const char *b = "Vol \u2212";'
    require(sorted({code for code, _ in uncovered_literals(minus, covered)}) == [0x2212],
            "scanner must reject U+2212 MINUS SIGN")
    dash = 'text += " \u2014 " + std::string(artist);'
    require([code for code, _ in uncovered_literals(dash, covered)] == [0x2014],
            "scanner must reject U+2014 EM DASH")
    arrow_hint = ('text += "Enter play/pause  \u2190/\u2192 seek  \u2191/\u2193 '
                  'volume  N/P track  Esc close";')
    require(sorted({code for code, _ in uncovered_literals(arrow_hint, covered)})
            == [0x2190, 0x2191, 0x2192, 0x2193],
            "scanner must reject the arrow-glyph controls hint")
    play_issues, _ = scan_symbols(
        "mutation.cpp", "lv_label_set_text(icon, LV_SYMBOL_PLAY);", symbols, covered)
    require(play_issues != [],
            "mutation rendering LV_SYMBOL_PLAY must be rejected")
    keyboard_issues, _ = scan_symbols(
        "mutation.cpp",
        "void changed(void) {\n"
        "    const char *text = lv_keyboard_get_button_text(keyboard, button);\n"
        "    if (strcmp(text, LV_SYMBOL_LEFT) == 0) return;\n"
        "}\n", symbols, covered)
    require(keyboard_issues == [],
            "virtual keyboard button text must stay out of the font contract")
    stray_issues, _ = scan_symbols(
        "mutation.cpp",
        "void changed(void) {\n"
        "    if (strcmp(label, LV_SYMBOL_LEFT) == 0) return;\n"
        "}\n", symbols, covered)
    require(stray_issues != [],
            "a symbol comparison outside the keyboard path must be rejected")

    # The keyboard exemption must stay narrow: only the compared token is
    # exempt, never a render that shares a function/statement with it.
    inside_keyboard = (
        "void changed(void) {\n"
        "    const char *text = lv_keyboard_get_button_text(keyboard, button);\n"
    )
    abuse_render, _ = scan_symbols(
        "mutation.cpp",
        inside_keyboard + "    lv_label_set_text(icon, LV_SYMBOL_PLAY);\n}\n",
        symbols, covered)
    require(abuse_render != [],
            "a rendered symbol inside the virtual keyboard handler must still "
            "be validated against cyberdeck_font")
    abuse_same_statement, _ = scan_symbols(
        "mutation.cpp",
        inside_keyboard
        + '    if (strcmp(text, "x") == 0) lv_label_set_text(icon, LV_SYMBOL_PLAY);\n'
        "}\n", symbols, covered)
    require(abuse_same_statement != [],
            "a render sharing a statement with a keyboard-text comparison must "
            "not inherit the exemption")
    abuse_ternary, _ = scan_symbols(
        "mutation.cpp",
        inside_keyboard
        + '    lv_label_set_text(icon, strcmp(text, "x") == 0 ? LV_SYMBOL_PLAY '
        ": LV_SYMBOL_CHARGE);\n}\n", symbols, covered)
    require(abuse_ternary != [],
            "a symbol rendered by a ternary next to a keyboard comparison must "
            "not inherit the exemption")
    abuse_other_operand, _ = scan_symbols(
        "mutation.cpp",
        inside_keyboard
        + "    if (strcmp(other, LV_SYMBOL_PLAY) == 0) return;\n"
        "}\n", symbols, covered)
    require(abuse_other_operand != [],
            "a comparison on an unrelated operand inside the keyboard handler "
            "must not inherit the exemption")
    abuse_member_function, _ = scan_symbols(
        "mutation.cpp",
        "void changed(void) {\n"
        "    const char *text = lv_keyboard_get_button_text(keyboard, button);\n"
        "}\n"
        "class Panel {\n"
        "    void draw(void) {\n"
        "        if (strcmp(text, LV_SYMBOL_PLAY) == 0) return;\n"
        "    }\n"
        "};\n", symbols, covered)
    require(abuse_member_function != [],
            "the keyboard exemption must not leak to another function of the "
            "same translation unit")
    abuse_string_mention, _ = scan_symbols(
        "mutation.cpp",
        'void draw(void) {\n'
        '    log_line("lv_keyboard_get_button_text( was removed");\n'
        "    if (strcmp(text, LV_SYMBOL_PLAY) == 0) return;\n"
        "}\n", symbols, covered)
    require(abuse_string_mention != [],
            "a mere mention of the keyboard accessor in a string must not grant "
            "the exemption")
    covered_render, _ = scan_symbols(
        "mutation.cpp", "lv_label_set_text(icon, LV_SYMBOL_CHARGE);", symbols, covered)
    require(covered_render == [],
            "a rendered symbol with a real glyph must not be reported")
    commented = '// historical label: "\u221210s" and " \u2014 "\nconst char *a = "-";'
    require(uncovered_literals(commented, covered) == [],
            "scanner must ignore code points that only appear in comments")
    require(uncovered_literals(
        '/* was LV_SYMBOL_PLAY */ lv_label_set_text(icon, ">");', covered) == [],
        "a commented LV_SYMBOL_PLAY must not become a literal")

    view_source = VIEW.read_text(encoding="utf-8")
    require(view_source.count(MUSIC_HINT) == 1,
            "the player view must define the controls hint exactly once, as the "
            "footer literal")
    require((ui_source + view_source).count(MUSIC_HINT) == 1,
            "the player controls hint literal must exist exactly once across "
            "cyberdeck_ui.cpp and cyberdeck_music_view.cpp")
    footer_problems = footer_issues(view_source)
    require(footer_problems == [],
            "the player view must render the controls hint as a fixed footer: "
            + "; ".join(footer_problems))
    require(track_create_issues(view_source) == [],
            "track rows must zero every theme padding and chrome channel")
    require(track_layout_issues(view_source) == [],
            "track labels must be inset and vertically centred inside the row")
    progress_issues = progress_style_issues(view_source)
    require(progress_issues == [],
            "the progress bar and control buttons must stay flat and monochrome: "
            + "; ".join(progress_issues))
    inset_issues = card_inset_issues(view_source)
    require(inset_issues == [],
            "the player cards must keep the symmetric card inset (AC-MUSIC-13): "
            + "; ".join(inset_issues))
    grid_issues = controls_grid_issues(view_source)
    require(grid_issues == [],
            "the control grid must keep the vertical row gap and the content-height "
            "anchor (AC-MUSIC-13): " + "; ".join(grid_issues))

    header_source = HEADER.read_text(encoding="utf-8")
    require(header_music_issues(header_source) == [],
            "the header music button must zero the theme radius and shadow")

    # Mutation proof: the legacy implementation must be rejected by the same
    # checks, so the contract cannot pass vacuously.
    legacy_padding = view_source.replace("        lv_obj_set_style_pad_row(track, 0, 0);\n", "")
    require(track_create_issues(legacy_padding) != [],
            "mutation without pad_row must be rejected")
    legacy_radius = view_source.replace("        lv_obj_set_style_radius(track, 0, 0);\n", "")
    require(track_create_issues(legacy_radius) != [],
            "mutation restoring the theme radius on a track row must be rejected")
    legacy_shadow = view_source.replace("        lv_obj_set_style_shadow_width(track, 0, 0);\n", "")
    require(track_create_issues(legacy_shadow) != [],
            "mutation restoring the theme shadow on a track row must be rejected")
    legacy_shadow_opa = view_source.replace(
        "        lv_obj_set_style_shadow_opa(track, LV_OPA_TRANSP, 0);\n", "")
    require(track_create_issues(legacy_shadow_opa) != [],
            "mutation leaving the shadow opaque on a track row must be rejected")

    legacy_header_shadow = header_source.replace(
        "    lv_obj_set_style_shadow_width(s_music_button, 0, 0);\n", "")
    require(header_music_issues(legacy_header_shadow) != [],
            "mutation restoring the theme shadow on the header button must be rejected")
    legacy_header_radius = header_source.replace(
        "    lv_obj_set_style_radius(s_music_button, 0, 0);\n", "")
    require(header_music_issues(legacy_header_radius) != [],
            "mutation restoring the theme radius on the header button must be rejected")
    legacy_header_size = header_source.replace(
        "    lv_obj_set_size(s_music_button, 26, 36);", "    lv_obj_set_size(s_music_button, 26, 44);")
    require(header_music_issues(legacy_header_size) != [],
            "mutation that grows the header target must be rejected")
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

    # AC-MUSIC-13 mutation proof: every part override and the flat button chrome
    # must be load-bearing, so the style contract cannot pass vacuously.
    legacy_indicator_main = view_source.replace(
        "    lv_obj_set_style_bg_color(progress_, kWhite, LV_PART_INDICATOR);",
        "    lv_obj_set_style_bg_color(progress_, kWhite, LV_PART_MAIN);")
    require(progress_style_issues(legacy_indicator_main) != [],
            "mutation painting the indicator through LV_PART_MAIN must be rejected")
    missing_indicator = view_source.replace(
        "    lv_obj_set_style_bg_color(progress_, kWhite, LV_PART_INDICATOR);\n", "")
    require(progress_style_issues(missing_indicator) != [],
            "mutation dropping the indicator override must be rejected")
    opaque_knob = view_source.replace(
        "    lv_obj_set_style_bg_opa(progress_, LV_OPA_TRANSP, LV_PART_KNOB);\n", "")
    require(progress_style_issues(opaque_knob) != [],
            "mutation leaving the slider knob visible must be rejected")
    rounded_bar = view_source.replace(
        "    lv_obj_set_style_radius(progress_, 0, LV_PART_MAIN);\n", "")
    require(progress_style_issues(rounded_bar) != [],
            "mutation restoring the theme radius on the bar must be rejected")
    muted_track = view_source.replace(
        "    lv_obj_set_style_bg_color(progress_, kBorder, LV_PART_MAIN);\n", "")
    require(progress_style_issues(muted_track) != [],
            "mutation leaving the theme primary_muted track must be rejected")
    invisible_track = view_source.replace(
        "    lv_obj_set_style_bg_color(progress_, kBorder, LV_PART_MAIN);",
        "    lv_obj_set_style_bg_color(progress_, kSurface, LV_PART_MAIN);")
    require(progress_style_issues(invisible_track) != [],
            "mutation painting the track with kSurface (invisible rail) must be rejected")
    # The theme paints MAIN at LV_OPA_20; without the explicit COVER the border
    # grey is blended over the card and the rail vanishes (device: #080A08).
    transparent_track = view_source.replace(
        "    lv_obj_set_style_bg_opa(progress_, LV_OPA_COVER, LV_PART_MAIN);\n", "")
    require(progress_style_issues(transparent_track) != [],
            "mutation dropping the MAIN bg_opa COVER must be rejected because the "
            "theme paints the track at LV_OPA_20 (invisible rail)")
    bordered_button = view_source.replace(
        "        lv_obj_set_style_border_width(buttons_[index], 0, 0);",
        "        lv_obj_set_style_border_width(buttons_[index], 1, 0);")
    require(progress_style_issues(bordered_button) != [],
            "mutation restoring the control border must be rejected")
    # The control cells must not fall back to `kBlack`, which is darker than the
    # card they sit on; the elevated grey is `kSelected` (#303030).
    black_buttons = view_source.replace(
        "        lv_obj_set_style_bg_color(buttons_[index], kSelected, 0);",
        "        lv_obj_set_style_bg_color(buttons_[index], kBlack, 0);")
    require(black_buttons != view_source,
            "the control button bg_color must stay replaceable for the mutation proof")
    black_button_issues = progress_style_issues(black_buttons)
    require(black_button_issues != [],
            "mutation painting the control buttons with kBlack must be rejected")
    require(any("kSelected" in issue for issue in black_button_issues),
            "the kBlack mutation must be reported as the missing kSelected grey")
    require(any("kBlack" in issue for issue in black_button_issues),
            "the kBlack mutation must name kBlack as the rejected colour")
    # A later MAIN write wins in LVGL but no other target notices it: the shim
    # folds every part into one slot, so `test_display_views` still reads the
    # KNOB transparency (bg_opa) or the INDICATOR white (bg_color). Only the
    # last-write check keeps the rail opaque and unrounded.
    later_opa_write = view_source.replace(
        "    lv_obj_set_style_bg_opa(progress_, LV_OPA_COVER, LV_PART_MAIN);\n",
        "    lv_obj_set_style_bg_opa(progress_, LV_OPA_COVER, LV_PART_MAIN);\n"
        "    lv_obj_set_style_bg_opa(progress_, 51, LV_PART_MAIN);\n")
    require(progress_style_issues(later_opa_write) != [],
            "mutation adding a later MAIN bg_opa write must be rejected")
    later_track_write = view_source.replace(
        "    lv_obj_set_style_bg_color(progress_, kBorder, LV_PART_MAIN);\n",
        "    lv_obj_set_style_bg_color(progress_, kBorder, LV_PART_MAIN);\n"
        "    lv_obj_set_style_bg_color(progress_, kSurface, LV_PART_MAIN);\n")
    require(progress_style_issues(later_track_write) != [],
            "mutation adding a later MAIN bg_color write must be rejected")
    later_radius_write = view_source.replace(
        "    lv_obj_set_style_radius(progress_, 0, LV_PART_MAIN);\n",
        "    lv_obj_set_style_radius(progress_, 0, LV_PART_MAIN);\n"
        "    lv_obj_set_style_radius(progress_, 6, LV_PART_MAIN);\n")
    require(progress_style_issues(later_radius_write) != [],
            "mutation adding a later MAIN radius write must be rejected")
    # A setter that survives only as a comment is not a style.
    commented_cover = view_source.replace(
        "    lv_obj_set_style_bg_opa(progress_, LV_OPA_COVER, LV_PART_MAIN);\n",
        "    /* lv_obj_set_style_bg_opa(progress_, LV_OPA_COVER, LV_PART_MAIN); */\n")
    require(progress_style_issues(commented_cover) != [],
            "mutation keeping the MAIN bg_opa COVER only as a comment must be rejected")

    # AC-MUSIC-13 (symmetric card inset) mutation proof: the legacy arithmetic
    # measured the outer card width, which is 18 px wider than the content area
    # minus the inset, so the bar/right column/rows overflow the card by 1 px.
    # Only meaningful while the fixed derivation is present; otherwise the
    # primary check above already reports the defect.
    if not inset_issues:
        legacy_inner_width = re.sub(
            derivation_pattern("inner_width"),
            "const int inner_width = std::max(1, now_width - 16);",
            view_source, count=1)
        require(legacy_inner_width != view_source,
                "the inner_width derivation must stay replaceable for the mutation proof")
        legacy_inner = card_inset_issues(legacy_inner_width)
        require(legacy_inner != [],
                "mutation back to `inner_width = now_width - 16` must be rejected")
        require(any("now_width - 16" in issue for issue in legacy_inner),
                "the now_width - 16 mutation must be reported with the offending "
                "expression, not only as a missing derivation")
        legacy_browser_width = re.sub(
            derivation_pattern("browser_inner_width"),
            "const int browser_inner_width = std::max(1, browser_width - 16);",
            view_source, count=1)
        require(legacy_browser_width != view_source,
                "the browser_inner_width derivation must stay replaceable for the "
                "mutation proof")
        legacy_browser = card_inset_issues(legacy_browser_width)
        require(legacy_browser != [],
                "mutation back to `browser_inner_width = browser_width - 16` must be "
                "rejected")
        require(any("browser_width - 16" in issue for issue in legacy_browser),
                "the browser_width - 16 mutation must be reported with the offending "
                "expression, not only as a missing derivation")
        # The named constant carries the card() contract; a bare literal would let
        # a later pad change silently reintroduce the asymmetric inset.
        unnamed_inset = view_source.replace("constexpr int kCardInset = 8;",
                                            "constexpr int kCardPadding = 8;")
        require(card_inset_issues(unnamed_inset) != [],
                "mutation dropping the kCardInset name must be rejected")
        wrong_inset = view_source.replace("constexpr int kCardInset = 8;",
                                          "constexpr int kCardInset = 7;")
        require(card_inset_issues(wrong_inset) != [],
                "mutation that desynchronises kCardInset from card()'s pad_all must "
                "be rejected")
        single_inset = re.sub(
            derivation_pattern("inner_width"),
            "const int inner_width = std::max(1, static_cast<int>("
            "lv_obj_get_content_width(now_playing_)) - kCardInset);",
            view_source, count=1)
        require(card_inset_issues(single_inset) != [],
                "mutation subtracting the inset only once must be rejected")
        outer_width_derivation = re.sub(
            derivation_pattern("browser_inner_width"),
            "const int browser_inner_width = std::max(1, browser_width - 16);",
            view_source, count=1)
        require(card_inset_issues(outer_width_derivation) != [],
                "mutation deriving browser_inner_width from the outer card width must "
                "be rejected")

    # AC-MUSIC-13 (control grid) mutation proof: the glued rows and the outer
    # card anchor are the two measured device defects, so both must be rejected
    # with an explicit message instead of only "missing derivation".
    if not grid_issues:
        glued_rows = view_source.replace(
            "const int controls_height = button_height * control_rows + "
            "row_gap * (control_rows - 1);",
            "const int controls_height = button_height * control_rows;").replace(
            "row * (button_height + row_gap)", "row * button_height")
        require(glued_rows != view_source,
                "the row_gap derivation and the row position must stay replaceable "
                "for the mutation proof")
        glued_issues = controls_grid_issues(glued_rows)
        require(glued_issues != [],
                "mutation back to `controls_height = button_height * control_rows` "
                "must be rejected")
        require(any("row_gap" in issue or "glue" in issue for issue in glued_issues),
                "the glued-rows mutation must be reported as the missing row_gap")
        outer_anchor = view_source.replace(
            "lv_obj_set_pos(controls_, 8, content_height - controls_area - 8);",
            "lv_obj_set_pos(controls_, 8, card_height - controls_area - 8);")
        require(outer_anchor != view_source,
                "the control anchor must stay replaceable for the mutation proof")
        anchor_issues = controls_grid_issues(outer_anchor)
        require(anchor_issues != [],
                "mutation back to `card_height - controls_area - 8` must be rejected")
        require(any("content_height" in issue for issue in anchor_issues),
                "the outer-anchor mutation must name the content-height anchor, not "
                "only report a missing position")
        renamed_gap = view_source.replace("const int row_gap = pad;",
                                          "const int row_gap = 0;")
        require(controls_grid_issues(renamed_gap) != [],
                "mutation that keeps the name but drops the gap value must be rejected")
        unanchored_area = view_source.replace(
            "std::max(line, content_height - content_y - pad)",
            "std::max(line, card_height - content_y - pad)")
        require(controls_grid_issues(unanchored_area) != [],
                "mutation deriving controls_area from card_height must be rejected")

    # Footer mutation proof: appending the hint to the status text instead of
    # creating a widget, letting the cards occupy the footer band and shrinking
    # the band back to one row in portrait were the defects this contract
    # replaces, so all of them must be rejected.
    if not footer_problems:
        no_footer = view_source.replace("    footer_ = lv_label_create(surface_);\n", "")
        require(no_footer != view_source,
                "the footer creation must stay replaceable for the mutation proof")
        require(footer_issues(no_footer) != [],
                "mutation dropping the footer label (hint only as status text) "
                "must be rejected")
        full_height_cards = view_source.replace(
            "const int card_height = landscape ? footer_y - pad - y : "
            "(footer_y - y - 2 * pad) / 2;",
            "const int card_height = landscape ? height - y - pad : "
            "(height - y - 3 * pad) / 2;")
        require(full_height_cards != view_source,
                "the footer-aware card_height must stay replaceable for the "
                "mutation proof")
        require(footer_issues(full_height_cards) != [],
                "mutation back to the full-height cards (footer painted over the "
                "library) must be rejected")
        unanchored_footer = view_source.replace(
            "lv_obj_set_pos(footer_, pad, footer_y);",
            "lv_obj_set_pos(footer_, pad, 0);")
        require(unanchored_footer != view_source,
                "the footer anchor must stay replaceable for the mutation proof")
        require(footer_issues(unanchored_footer) != [],
                "mutation moving the footer to the top of the surface must be "
                "rejected")
        # The measured device defect: 767.7 px of text in a 680 px portrait
        # surface. A one-row band clips "Esc close" and a landscape-only text
        # keeps the truncation, so both mutations must fail even though the host
        # shim font (9 px/char) would fit the row.
        single_row_band = view_source.replace(
            "const int footer_height = landscape ? line : 2 * line + 2;",
            "const int footer_height = line;")
        require(single_row_band != view_source,
                "the orientation-dependent footer band must stay replaceable for "
                "the mutation proof")
        clipping_issues = footer_issues(single_row_band)
        require(clipping_issues != [],
                "mutation reserving a single row in portrait must be rejected")
        require(any("2 * line + 2" in issue for issue in clipping_issues),
                "the single-row mutation must be reported as the missing "
                "`2 * line + 2` reservation, not only as a missing setter")
        flat_text = view_source.replace(
            "lv_label_set_text(footer_, landscape ? kMusicControlsHint : "
            "kMusicControlsHintPortrait);",
            "lv_label_set_text(footer_, kMusicControlsHint);")
        require(flat_text != view_source,
                "the conditional footer text must stay replaceable for the mutation "
                "proof")
        require(footer_issues(flat_text) != [],
                "mutation applying the flat hint in portrait must be rejected")
        unbroken_portrait = view_source.replace(
            "left/right seek\\nup/down volume", "left/right seek  up/down volume")
        require(unbroken_portrait != view_source,
                "the portrait break must stay replaceable for the mutation proof")
        require(footer_issues(unbroken_portrait) != [],
                "mutation dropping the portrait line break must be rejected")
        create_text = view_source.replace(
            "    footer_ = lv_label_create(surface_);\n",
            "    footer_ = lv_label_create(surface_);\n"
            "    lv_label_set_text(footer_, kMusicControlsHint);\n")
        require(create_text != view_source,
                "the footer creation must stay replaceable for the mutation proof")
        require(footer_issues(create_text) != [],
                "mutation setting the footer text in create() must be rejected")
        unguarded_text = view_source.replace(
            "    if (footer_landscape_ != footer_landscape) {\n"
            "        lv_label_set_text(footer_, landscape ? kMusicControlsHint : "
            "kMusicControlsHintPortrait);\n"
            "        footer_landscape_ = footer_landscape;\n"
            "    }",
            "    lv_label_set_text(footer_, landscape ? kMusicControlsHint : "
            "kMusicControlsHintPortrait);")
        require(unguarded_text != view_source,
                "the footer orientation guard must stay replaceable for the "
                "mutation proof")
        require(footer_issues(unguarded_text) != [],
                "mutation re-applying the footer text on every layout() must be "
                "rejected")
        stale_sentinel = view_source.replace("    footer_landscape_ = -1;\n", "", 1)
        require(stale_sentinel != view_source,
                "the footer sentinel reset must stay replaceable for the mutation "
                "proof")
        require(footer_issues(stale_sentinel) != [],
                "mutation leaving footer_landscape_ uncleared by destroy() must be "
                "rejected")

    for failure in failures:
        print(f"FAIL: {failure}")
    if failures:
        return 1
    print("PASS: player UI literals and rendered LV_SYMBOL_* stay inside the real "
          "cyberdeck_font coverage, the controls hint is a single ASCII footer "
          "owned by the view (never status text in cyberdeck_ui.cpp) with one row "
          "in landscape and the exact two-row break in portrait, reapplied only "
          "when the orientation changes (footer_landscape_ sentinel, reset by "
          "destroy()), track "
          "rows and the header button zero the theme padding, radius and shadow, "
          "the track label stays inset and centred, the card children derive their "
          "width from the real content area with a symmetric kCardInset, the control "
          "grid keeps the vertical row_gap equal to the column gap and is anchored "
          "by the card content height (AC-MUSIC-13), and the "
          "progress bar/controls keep the monochrome flat style (opaque kBorder "
          "track with a visible rail, white indicator, hidden knob, no border) and "
          "the control cells use the elevated kSelected grey")
    return 0


if __name__ == "__main__":
    sys.exit(main())
