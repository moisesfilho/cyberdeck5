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
HEADER = DISPLAY / "cyberdeck_header_view.cpp"
COMMAND_INPUT = DISPLAY / "cyberdeck_music_command_input.cpp"
APP = ROOT / "components/cyberdeck/src/apps/music/cyberdeck_music_app.cpp"
FONT = DISPLAY / "cyberdeck_font.c"
SYMBOL_MAP = Path(__file__).resolve().with_name("lv_symbol_codepoints.json")

# Player controls hint. ASCII only: U+2190..U+2193 have no glyph in the font.
MUSIC_HINT = ("Enter play/pause  left/right seek  up/down volume  N/P track  "
              "Esc close")

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
    require(ui_source.count("text += kMusicControlsHint;") == 2,
            "both player render branches must reuse the shared kMusicControlsHint")
    require(ui_source.count(MUSIC_HINT) == 1,
            "the player controls hint must be defined once, not duplicated")

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
    require(track_create_issues(view_source) == [],
            "track rows must zero every theme padding and chrome channel")
    require(track_layout_issues(view_source) == [],
            "track labels must be inset and vertically centred inside the row")

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

    for failure in failures:
        print(f"FAIL: {failure}")
    if failures:
        return 1
    print("PASS: player UI literals and rendered LV_SYMBOL_* stay inside the real "
          "cyberdeck_font coverage, the controls hint is ASCII and shared, track "
          "rows and the header button zero the theme padding, radius and shadow, "
          "and the track label stays inset and centred")
    return 0


if __name__ == "__main__":
    sys.exit(main())
