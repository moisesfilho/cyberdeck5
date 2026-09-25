#!/usr/bin/env python3
"""RED structural contract for the battery UI/header integration.

The UI is ESP-IDF/LVGL code and cannot be linked on the host.  This test checks
the actual UI, Wi-Fi icon, and boot sources, preserving the approved header
contract: 30/40/30 direct grid, Wi-Fi before battery, compact non-growing
children, right-aligned content, numeric percentage plus exactly one semantic
state icon, no battery-level glyph, failure hiding, resize/state callbacks,
non-fatal boot composition, and a snapshot-only UI timer with no direct
I2C/NVS/expander access.

Traceability:
  REQ-BAT-002 -> absent/unavailable data must not become a fabricated zero.
  REQ-BAT-003 -> numeric percentage + one semantic state icon, no level icon.
  REQ-BAT-004 -> boot remains non-fatal and the UI has no raw charger I/O.
  REQ-BAT-006 -> docs and code-map retain the battery test traceability.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
WIFI_ICON = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_wifi_icon.cpp"
WIFI_ICON_HDR = ROOT / "components/cyberdeck/include/platform/display/cyberdeck_wifi_icon.h"
APP = ROOT / "main/app_main.cpp"
MAKEFILE_PATH = ROOT / "tests/host/keymap/Makefile"
CODEMAP = ROOT / "code-map.md"

TRACEABILITY = (
    "REQ-BAT-002 -> test_battery_ui_contract.py",
    "REQ-BAT-003 -> test_battery_ui_contract.py",
    "REQ-BAT-004 -> test_battery_ui_contract.py",
    "REQ-BAT-006 -> test_battery_ui_contract.py + code-map.md",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def strip_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", source)


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{re.escape(name)}\s*\([^;{{]*\)\s*\{{", source)
    require(match is not None, f"UI must define {name}()")
    assert match is not None
    opening = source.find("{", match.start())
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    raise AssertionError(f"unterminated body for {name}()")


def split_call_arguments(arguments: str) -> list[str]:
    """Split a C++ call's arguments without splitting nested expressions."""
    result: list[str] = []
    start = 0
    paren_depth = 0
    bracket_depth = 0
    brace_depth = 0
    quote: str | None = None
    escaped = False
    for index, character in enumerate(arguments):
        if quote is not None:
            if escaped:
                escaped = False
            elif character == "\\\\":
                escaped = True
            elif character == quote:
                quote = None
            continue
        if character in "'\\\"":
            quote = character
        elif character == "(":
            paren_depth += 1
        elif character == ")":
            paren_depth -= 1
        elif character == "[":
            bracket_depth += 1
        elif character == "]":
            bracket_depth -= 1
        elif character == "{":
            brace_depth += 1
        elif character == "}":
            brace_depth -= 1
        elif (character == "," and paren_depth == 0 and
              bracket_depth == 0 and brace_depth == 0):
            result.append(arguments[start:index].strip())
            start = index + 1
    result.append(arguments[start:].strip())
    return result


def call_arguments(source: str, function: str):
    """Yield argument lists for calls of a simple function name."""
    pattern = re.compile(rf"\b{re.escape(function)}\s*\(")
    for match in pattern.finditer(source):
        opening = match.end() - 1
        depth = 0
        quote: str | None = None
        escaped = False
        closing = None
        for index in range(opening, len(source)):
            character = source[index]
            if quote is not None:
                if escaped:
                    escaped = False
                elif character == "\\\\":
                    escaped = True
                elif character == quote:
                    quote = None
                continue
            if character in "'\\\"":
                quote = character
            elif character == "(":
                depth += 1
            elif character == ")":
                depth -= 1
                if depth == 0:
                    closing = index
                    break
        if closing is not None:
            yield split_call_arguments(source[opening + 1:closing])


def calls_for_object(source: str, function: str, object_name: str):
    expected = re.sub(r"\s+", "", object_name)
    for arguments in call_arguments(source, function):
        if arguments and re.sub(r"\s+", "", arguments[0]) == expected:
            yield arguments


def is_zero_literal(expression: str) -> bool:
    compact = re.sub(r"\s+", "", expression)
    return re.fullmatch(r"[+-]?0+(?:[uUlL]+)?", compact) is not None


def state_window(source: str, state: str) -> str:
    """Return the balanced source block associated with one state branch."""
    marker = f"charge_class::{state}"
    start = source.find(marker)
    require(start >= 0, f"battery UI must branch on {state}")

    # The current seam uses if/else-if blocks.  Prefer the balanced block after
    # the condition so a following default branch cannot be counted as part of
    # the selected state's icon.
    opening = source.find("{", start)
    if opening >= 0 and opening - start <= 800:
        depth = 0
        for index in range(opening, len(source)):
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
                if depth == 0:
                    return source[opening:index + 1]

    # Also support a switch/case or a compact one-line branch.
    following = [
        position for position in
         (source.find(f"charge_class::{candidate}", start + len(marker))
          for candidate in ("charging", "discharging", "neutral", "absent", "unavailable"))

        if position >= 0
    ]
    end = min(following) if following else len(source)
    return source[start:end]


def symbol_tokens(source: str) -> list[str]:
    return re.findall(r"\bLV_SYMBOL_[A-Z0-9_]+\b", source)


def small_style_value(arguments: list[str], description: str, maximum: int = 4) -> None:
    require(len(arguments) >= 2, f"{description} must provide a value")
    value = re.sub(r"\s+", "", arguments[1])
    match = re.fullmatch(r"(\d+)(?:[uUlL]+)?", value)
    require(match is not None, f"{description} must use an integer pixel value")
    assert match is not None
    require(0 <= int(match.group(1)) <= maximum,
            f"{description} must remain a small gap (0..{maximum}px)")


def geometry_expressions(expression: str, source: str) -> list[str]:
    """Return direct or named-constant expressions tied to icon geometry."""
    candidates = [expression]
    identifiers = re.findall(r"\b[A-Za-z_]\w*\b", expression)
    for identifier in identifiers:
        declaration = re.search(
            rf"\b{re.escape(identifier)}\s*=\s*([^;]+);", source)
        if declaration is not None:
            candidates.append(declaration.group(1))
    return [candidate for candidate in candidates
            if "CYBERDECK_WIFI_ICON_RADIUS_2" in candidate and
            "CYBERDECK_WIFI_ICON_DEFAULT_THICKNESS" in candidate]


def main() -> int:
    ui = strip_comments(UI.read_text(encoding="utf-8"))
    wifi_icon = strip_comments(WIFI_ICON.read_text(encoding="utf-8"))
    wifi_icon_header = strip_comments(WIFI_ICON_HDR.read_text(encoding="utf-8"))
    app = strip_comments(APP.read_text(encoding="utf-8"))
    makefile = strip_comments(MAKEFILE_PATH.read_text(encoding="utf-8"))
    codemap = strip_comments(CODEMAP.read_text(encoding="utf-8"))

    init = function_body(ui, "cyberdeck_ui_init")

    # The header must remain a direct three-cell row, with the approved ratio.
    require(re.search(r"LV_FLEX_FLOW_ROW", init), "header must use a direct row layout")
    require(re.search(r"LV_PCT\s*\(\s*30\s*\)", init), "header must define the 30% cell")
    require(re.search(r"LV_PCT\s*\(\s*40\s*\)", init), "header must define the 40% cell")
    require(len(re.findall(r"LV_PCT\s*\(\s*30\s*\)", init)) == 2,
            "header must define two direct 30% cells")
    header_start = init.find("header")
    header_end = init.find("s_terminal", header_start)
    header_block = init[header_start:header_end if header_end >= 0 else len(init)]
    widths = [int(value) for value in re.findall(r"LV_PCT\s*\(\s*(\d+)\s*\)", header_block)]
    require(widths[:3] == [30, 40, 30],
            "header direct grid must be exactly 30/40/30 in order")

    # Wi-Fi must precede the battery in source/child order.
    wifi = init.find("cyberdeck_wifi_icon_create")
    battery = init.find("battery")
    require(wifi >= 0, "header must retain the Wi-Fi icon")
    require(battery >= 0, "header must integrate the battery indicator")
    require(wifi < battery, "Wi-Fi must be created before the battery in the header")

    # The Wi-Fi child must be compact and intrinsic to the icon geometry.  A
    # fixed 216px cell (or another 30% allocation) reintroduces the spacing
    # regression even when the outer header grid still has the right ratio.
    for token in ("CYBERDECK_WIFI_ICON_RADIUS_2",
                  "CYBERDECK_WIFI_ICON_DEFAULT_THICKNESS"):
        require(token in wifi_icon_header,
                f"Wi-Fi header width must use the public {token} constant")
    require(not re.search(r"\bHEADER_WIFI_CELL_WIDTH\s*=\s*216\b", ui),
            "header must not retain HEADER_WIFI_CELL_WIDTH=216")
    wifi_width_calls = list(calls_for_object(init, "lv_obj_set_width", "s_wifi_status"))
    require(wifi_width_calls, "Wi-Fi child must have an explicit compact width")
    for width_call in wifi_width_calls:
        width_expression = width_call[1] if len(width_call) > 1 else ""
        require(not re.search(r"\b216\b", width_expression),
                "Wi-Fi width must not be the old fixed 216px value")
        require(re.search(r"\b(?:LV_PCT|lv_pct)\s*\(", width_expression) is None,
                "Wi-Fi width must not be a percentage of the right cell")
        derived = geometry_expressions(width_expression, ui)
        require(derived,
                "Wi-Fi width must derive from radius 2 and default thickness")
        for candidate in derived:
            require(re.search(r"(?:\*\s*2(?:\.0)?(?:f)?|2(?:\.0)?(?:f)?\s*\*)",
                              candidate) is not None,
                    "Wi-Fi width must account for the icon's two-sided diameter")
            numeric_literals = re.findall(
                r"(?<![A-Za-z_])\d+(?:\.\d+)?", candidate)
            require(all(float(value) <= 64.0 for value in numeric_literals),
                    "Wi-Fi width expression must remain compact (<64px literals)")

    # Neither child may consume the remaining right-cell width.  Explicit
    # zeroes are part of the contract so a future theme/default cannot restore
    # flex growth implicitly.
    for object_name, description in (("s_wifi_status", "Wi-Fi"),
                                     ("s_battery_status", "battery")):
        grow_calls = list(calls_for_object(init, "lv_obj_set_flex_grow", object_name))
        require(grow_calls, f"{description} must explicitly set flex_grow")
        for grow_call in grow_calls:
            require(len(grow_call) >= 2 and is_zero_literal(grow_call[1]),
                    f"{description} must use flex_grow=0")

    # The right cell owns the compact cluster and aligns it at its end; the
    # battery remains intrinsic rather than filling the cell.
    align_calls = list(calls_for_object(init, "lv_obj_set_flex_align", "header_right"))
    require(len(align_calls) == 1,
            "header_right must have one explicit flex alignment")
    if align_calls:
        align_args = align_calls[0]
        require(len(align_args) >= 2 and align_args[1] == "LV_FLEX_ALIGN_END",
                "header_right must use LV_FLEX_ALIGN_END")
        require(len(align_args) >= 4 and
                align_args[2] == "LV_FLEX_ALIGN_CENTER" and
                align_args[3] == "LV_FLEX_ALIGN_CENTER",
                "header_right must keep centered cross-axis alignment")

    battery_width_calls = list(calls_for_object(init, "lv_obj_set_width", "s_battery_status"))
    battery_size_calls = list(calls_for_object(init, "lv_obj_set_size", "s_battery_status"))
    battery_width_expressions = [call[1] for call in battery_width_calls]
    battery_width_expressions += [call[1] for call in battery_size_calls]
    require(battery_width_expressions,
            "battery must have an explicit intrinsic width")
    require(all("LV_SIZE_CONTENT" in expression
                for expression in battery_width_expressions),
            "battery width must use LV_SIZE_CONTENT")
    require(not any(re.search(r"LV_PCT\s*\(\s*30\s*\)", expression)
                    for expression in battery_width_expressions),
            "battery must not reclaim a 30% cell")

    # Keep the nested flex gap bounded; large theme/default padding would move
    # the compact cluster back to the old, visibly sparse header.
    for api, object_name, description in (
            ("lv_obj_set_style_pad_column", "header_right", "header_right gap"),
            ("lv_obj_set_style_pad_row", "header_right", "header_right row gap"),
            ("lv_obj_set_style_pad_left", "s_battery_status", "battery left pad"),
            ("lv_obj_set_style_pad_column", "s_battery_status", "battery column gap")):
        style_calls = list(calls_for_object(init, api, object_name))
        require(style_calls, f"{description} must be explicit")
        for style_call in style_calls:
            small_style_value(style_call, description)

    # Layout is updated before the icon's explicit width-based repositioning;
    # state and LV_EVENT_SIZE_CHANGED callbacks remain part of the integration.
    update_header = re.search(
        r"lv_obj_update_layout\s*\(\s*header\s*\)", init)
    update_right = re.search(
        r"lv_obj_update_layout\s*\(\s*header_right\s*\)", init)
    icon_update = re.search(r"cyberdeck_wifi_icon_update_layout", init)
    require(update_header is not None and update_right is not None and
            icon_update is not None,
            "header/right-cell layout and Wi-Fi update_layout must be retained")
    require(update_header.start() < update_right.start() < icon_update.start(),
            "Wi-Fi update_layout must run after both parent layout updates")
    require(re.search(
        r"cyberdeck_wifi_icon_update_layout\s*\(\s*s_wifi_status\s*,\s*"
        r"lv_obj_get_width\s*\(\s*s_wifi_status\s*\)\s*\)", init, re.DOTALL) is not None,
        "Wi-Fi resize must use the actual child width")
    require(re.search(r"wifi_mgr_set_state_callback\s*\(\s*on_wifi_state\s*,",
                      init) is not None,
            "Wi-Fi state callback must remain registered")
    require("LV_EVENT_SIZE_CHANGED" in wifi_icon,
            "Wi-Fi icon must retain its LVGL resize callback")
    require("wifi_icon_size_changed_cb" in wifi_icon and
            "cyberdeck_wifi_icon_update_layout" in wifi_icon,
            "Wi-Fi resize callback must invoke update_layout")

    # REQ-BAT-003: the visual contract is one semantic state icon plus the
    # numeric percentage.  Battery-level glyphs and multi-glyph charge text
    # are explicitly forbidden.
    refresh = function_body(ui, "refresh_battery_status")
    require("battery_level_symbol" not in ui,
            "battery UI must not select a glyph from percentage level")
    require(re.search(r"\bLV_SYMBOL_BATTERY(?:_[A-Z0-9_]+)?\b", ui) is None,
            "battery UI must not render an LV_SYMBOL_BATTERY level icon")

    battery_label_updates = re.findall(
        r"lv_label_set_text(?:_fmt)?\s*\(\s*(s_battery_[A-Za-z0-9_]+)",
        refresh)
    require(len(battery_label_updates) >= 2,
            "battery refresh must update one icon label and one percentage label")
    icon_updates = [handle for handle in battery_label_updates
                    if "percentage" not in handle.lower()]
    percentage_updates = [handle for handle in battery_label_updates
                          if "percentage" in handle.lower()]
    require(len(icon_updates) == 1,
            "battery refresh must update exactly one semantic icon label")
    require(percentage_updates,
            "battery refresh must update a dedicated numeric percentage label")

    for state in ("charging", "discharging"):
        branch = state_window(refresh + "\n", state)
        icons = symbol_tokens(branch)
        require(len(icons) == 1,
                f"{state} state must select exactly one semantic LVGL icon")
        require(all(not icon.startswith("LV_SYMBOL_BATTERY") for icon in icons),
                f"{state} state must not select a battery-level icon")
    require(re.search(r"charge_class::discharging", refresh) is not None,
            "battery UI must distinguish discharging from charging")
    require(re.search(r"charge_class::charging", refresh) is not None,
            "battery UI must retain the charging state branch")
    require(re.search(r"charge_class::neutral", refresh) is not None,
            "battery UI must retain an explicit neutral state branch")
    neutral_branch = state_window(refresh + "\n", "neutral")
    neutral_icons = symbol_tokens(neutral_branch)
    require(len(neutral_icons) == 0,
            "neutral state must not render a battery/state glyph")
    require(re.search(r"LV_OBJ_FLAG_HIDDEN|lv_obj_add_flag|lv_obj_set_hidden",
                      neutral_branch) is None,
            "neutral state must keep the battery group visible")
    require(re.search(r"LV_SYMBOL_WIFI", ui) or "cyberdeck_wifi_icon" in ui,
            "header must retain the LVGL Wi-Fi indicator")
    battery_label_creates = re.findall(
        r"lv_label_create\s*\(\s*s_battery_status\s*\)", init)
    require(len(battery_label_creates) == 2,
            "battery group must contain only one icon label and one percentage label")

    # A percentage label is required, with a real numeric render/update path;
    # no state word is rendered as a third textual element.
    require(re.search(r"battery.*percent|percent.*battery|percentage", ui,
                      re.IGNORECASE),
            "header battery path must include percentage state")
    require(re.search(r"snprintf\s*\([^;]*%|%[0-9]*d", refresh),
            "header must format a numeric battery percentage")
    for state_word in ("charging", "discharging", "neutral", "unavailable", "absent"):
        require(f'"{state_word}"' not in refresh and
                f"'{state_word}'" not in refresh,
                f"battery UI must not render the textual state {state_word!r}")

    # A failed read/absent battery must hide the group, not display stale or
    # fabricated zero data.  A subsequent valid snapshot may reveal it again.
    require(re.search(r"(?:battery|charge)[^;]*(?:hidden|hide)|lv_obj_(?:add_flag|set_hidden)[^;]*battery",
                      ui, re.IGNORECASE | re.DOTALL),
            "battery failure path must hide the battery indicator")
    require(re.search(r"value\.available|charge_class::(?:absent|unavailable)",
                      refresh),
            "UI must branch on battery availability/validity/state")
    require(re.search(r"lv_obj_add_flag\s*\([^;]*LV_OBJ_FLAG_HIDDEN|lv_obj_set_hidden\s*\([^;]*true",
                      ui, re.DOTALL),
            "failure must use an explicit LVGL hidden state")

    # The reader is composed separately and remains sensor-only.  app_main
    # starts the protection adapter, while the UI consumes only the adapter's
    # synchronized snapshot; a direct reader dependency must not be required
    # (or reintroduced) in the LVGL layer.
    require(re.search(r"\bbattery_protection_get_snapshot\s*\(", ui) is not None,
            "UI must consume the battery-protection snapshot")
    require(re.search(r"\bbattery_protection_started\s*\(", ui) is not None,
            "UI must use the adapter's started state")
    require(re.search(r"\bina226_reader_(?:get_snapshot|get_raw_sample|started|start|init)\s*\(", ui) is None,
            "UI must not call the INA226 reader directly")
    app_main = function_body(app, "app_main")
    start = re.search(r"\bbattery_protection_(?:start|init)\s*\(", app_main)
    require(start is not None, "app_main must contain battery protection startup")
    if start is not None:
        tail = app_main[start.end():]
        require(not re.search(r"ESP_ERROR_CHECK\s*\(\s*battery_protection_", tail),
                "battery protection startup must not abort app_main")
        require(re.search(r"ESP_LOG[EWI]\s*\(", tail),
                "battery protection startup failure must be logged")
        require(re.search(r"(?:tab5_keyboard|screenshot|wifi_mgr|bridge_start)", tail),
                "boot must continue after a non-fatal protection startup failure")
        require(not re.search(r"\bina226_reader_(?:start|init)\s*\(", app_main),
                "app_main must start the reader through the protection adapter")

    # REQ-BAT-004/REQ-BAT-010: the LVGL layer consumes a copied battery
    # protection snapshot.  It may route the shell commands to the adapter,
    # but it must not own raw INA226 I2C, expander GPIO, or NVS persistence.
    # app_main is allowed to initialize the system NVS before composing the
    # adapter; the battery startup path itself must remain free of NVS calls.
    for pattern in (r"\bi2c_master_", r"\bbsp_i2c_", r"\besp_io_expander",
                    r"\bbsp_io_expander"):
        require(re.search(pattern, "\n".join((ui, app_main)), re.IGNORECASE) is None,
                f"battery UI/boot flow must not perform {pattern} directly")
    require(re.search(r"\bnvs_", ui, re.IGNORECASE) is None,
            "battery UI must not perform NVS directly")
    require(start is not None and re.search(r"\bnvs_", tail, re.IGNORECASE) is None,
            "battery protection startup path must not perform NVS directly")
    require(re.search(r"\bbattery_protection_get_snapshot\s*\(", ui) is not None,
            "UI must consume the synchronized battery protection snapshot")
    require("lv_timer_create" in ui and re.search(
        r"lv_timer_create\s*\([^;]{0,500}1000", ui, re.S) is not None,
        "UI must refresh the battery snapshot from a one-second LVGL timer")

    require("test_battery_ui_contract" in makefile,
            "Makefile must register the battery UI structural target")
    require("battery" in codemap.lower() and "ina226" in codemap.lower(),
            "code-map.md must document the battery UI/reader seam")
    for requirement in ("REQ-BAT-001", "REQ-BAT-002", "REQ-BAT-003",
                        "REQ-BAT-004", "REQ-BAT-005", "REQ-BAT-006"):
        require(requirement in codemap,
                f"code-map.md must document {requirement} traceability")
    require("neutral" in codemap.lower(),
            "code-map.md must document the neutral battery state")

    for line in TRACEABILITY:
        print(f"TRACE {line}")
    print("PASS: battery UI/header structural contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
