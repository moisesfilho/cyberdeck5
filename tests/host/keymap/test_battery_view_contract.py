#!/usr/bin/env python3
"""Structural contract for the pure battery power-indicator view.

`cyberdeck_battery_view` and the LVGL UI cannot both be linked in one host
binary: the view is pure and host-linkable, while the UI needs ESP-IDF/LVGL.
This companion therefore checks the parts that source inspection can prove,
and the behaviour itself is covered by the linked test_battery_view.cpp:

  * the view is a pure presentation layer (no ESP-IDF, FreeRTOS, LVGL, I2C,
    NVS or BSP dependency) and is registered in the firmware build;
  * the LVGL layer holds no business rule: it consumes the adapter's pure
    policy snapshot and delegates the mapping to
    `cyberdeck_battery_view::from_snapshot` / `::resolve`;
  * the semantic glyph is a fixed table over the view's power_glyph, never a
    function of the percentage, and `absent` renders the external glyph with an
    empty percentage label;
  * the group is hidden when the view says the source is not visible;
  * the LVGL layer performs no direct I2C/NVS/reader/expander access.

It reads production source only.  It never opens hardware, a simulator or the
Serial Automation Bridge.

Traceability:
  REQ-BAT-UI-001 -> test_battery_view.cpp + test_battery_protection.cpp
  REQ-BAT-UI-002 -> test_battery_view.cpp + test_battery_protection.cpp
  REQ-BAT-UI-003 -> this contract (pure view, no ESP-IDF/FreeRTOS/LVGL)
  REQ-BAT-UI-004 -> this contract (UI applies the view, no business rule)
  REQ-BAT-UI-005 -> this contract + test_battery_view.cpp
  REQ-BAT-UI-006 -> this contract (glyph table is not level-based)
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
VIEW_SRC = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_battery_view.cpp"
VIEW_HDR = ROOT / "components/cyberdeck/include/platform/display/cyberdeck_battery_view.h"
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
POLICY_HDR = ROOT / "components/cyberdeck/include/platform/sensors/cyberdeck_battery_protection.h"
ADAPTER_HDR = ROOT / "components/cyberdeck/include/platform/sensors/battery_protection.h"
CMAKE = ROOT / "components/cyberdeck/CMakeLists.txt"
MAKEFILE_PATH = ROOT / "tests/host/keymap/Makefile"
CODEMAP = ROOT / "code-map.md"
CONTRACT_HDR = Path(__file__).resolve().parent / "contracts" / "cyberdeck_battery_view.h"
VIEW_TEST = Path(__file__).resolve().parent / "test_battery_view.cpp"

TRACEABILITY = (
    "REQ-BAT-UI-001 -> test_battery_view.cpp + test_battery_protection.cpp",
    "REQ-BAT-UI-002 -> test_battery_view.cpp + test_battery_protection.cpp",
    "REQ-BAT-UI-003 -> test_battery_view_contract.py + test_battery_view.cpp",
    "REQ-BAT-UI-004 -> test_battery_view_contract.py + test_battery_ui_contract.py",
    "REQ-BAT-UI-005 -> test_battery_view.cpp + test_battery_view_contract.py",
    "AC-BAT-UI-001 -> test_battery_protection.cpp (valid INA + invalid CHG_STAT)",
    "AC-BAT-UI-002 -> test_battery_protection.cpp (invalid INA fabricates nothing)",
    "AC-BAT-UI-003 -> test_battery_view.cpp (stuck-low CHG_STAT after votes)",
    "AC-BAT-UI-004 -> test_battery_view.cpp (total state/signal matrix)",
    "AC-BAT-UI-005 -> test_battery_view_contract.py (two labels, 30/40/30 grid)",
    "AC-BAT-UI-006 -> test_battery_view.cpp (absent has no percentage/level glyph)",
)

# Anything that would make the view non-pure or give it a second owner of the
# business rule.  The font glyph macro names are intentionally absent here.
FORBIDDEN_IN_VIEW = (
    "esp_", "freertos", "FreeRTOS", "xQueue", "xSemaphore", "vTaskDelay",
    "task.h", "lvgl", "lv_obj", "lv_label", "lv_timer", "LV_SYMBOL_",
    "i2c_master", "bsp_i2c", "esp_io_expander", "bsp_io_expander",
    "nvs_", "NVS", "esp_log", "ESP_LOG", "printf", "std::string",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def strip_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", source)


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{re.escape(name)}\s*\([^;{{]*\)\s*\{{", source)
    require(match is not None, f"source must define {name}()")
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


def check_pure_view(view_src: str, view_hdr: str) -> None:
    """REQ-BAT-UI-003 / AC-BAT-UI-004: the view is a pure mapping layer."""
    combined = f"{view_hdr}\n{view_src}"
    for token in FORBIDDEN_IN_VIEW:
        require(token not in combined,
                f"the pure battery view must not reference {token!r}")

    # It may only depend on the policy header and the C++ standard library.
    includes = re.findall(r"^\s*#\s*include\s+([^\n]+)", view_src, re.M)
    for include in includes:
        require(include.strip() == '"platform/display/cyberdeck_battery_view.h"',
                f"the view source may only include its own header, found {include.strip()!r}")

    header_includes = re.findall(r"^\s*#\s*include\s+([^\n]+)", view_hdr, re.M)
    for include in header_includes:
        stripped = include.strip()
        require(stripped in ('<cstdint>', '"platform/sensors/cyberdeck_battery_protection.h"'),
                f"the view header may only include cstdint and the policy header, "
                f"found {stripped!r}")

    # The approved seam must exist in both files.
    for symbol in ("power_glyph", "struct input", "struct presentation",
                   "clamp_percentage", "from_snapshot", "resolve"):
        require(symbol in combined,
                f"the view contract must expose {symbol}")

    # A level-encoded glyph would mean picking a pictogram from the percentage.
    require(re.search(r"battery_level", combined) is None,
            "the view must not define a level-based battery glyph")
    require(re.search(r"percentage\s*(?:<|>|>=|<=|==|!=)\s*"
                      r"\w*\s*\?\s*power_glyph", combined) is None,
            "the view glyph must never be selected by comparing the percentage")


def check_view_registration(cmake: str) -> None:
    require("src/platform/display/cyberdeck_battery_view.cpp" in cmake,
            "the firmware build must compile the pure battery view")


def check_ui_delegates_to_the_view(ui: str) -> None:
    """REQ-BAT-UI-004 / AC-BAT-UI-005: the LVGL layer only applies the view."""
    require(re.search(r'#\s*include\s*"platform/display/cyberdeck_battery_view\.h"', ui)
            is not None,
            "the UI must include the pure battery view header")

    refresh = function_body(ui, "refresh_battery_status")

    # The UI consumes the adapter's pure policy snapshot, not the shell-facing
    # charge_class projection, and never the reader itself.
    require(re.search(r"\bbattery_protection_get_policy_snapshot\s*\(", refresh)
            is not None,
            "the UI battery refresh must consume the pure policy snapshot")
    require(re.search(r"\bbattery_protection_started\s*\(", refresh) is not None,
            "the UI must consult the adapter's started state")

    # The mapping is delegated, not re-implemented.
    require("cyberdeck_battery_view::from_snapshot" in refresh,
            "the UI must project the snapshot through the pure view")
    require(re.search(r"cyberdeck_battery_view::resolve\s*\(", refresh) is not None,
            "the UI must resolve the presentation through the pure view")

    # No business rule may remain in the LVGL layer: the old charge_class
    # branching and the availability/percentage rendering decisions are gone.
    # Only the published *values* are banned here, so the widget null-guard on
    # `s_battery_percentage` is not mistaken for a rendering decision.
    for banned in ("charge_class::", "battery_state::", "charge_signal::",
                   "power_glyph::"):
        require(banned not in refresh,
                f"the UI must not classify the battery itself ({banned})")
    # `visible`/`show_percentage` are the *decision* the UI must apply, so they
    # are exempt; the raw policy inputs are what must never be re-read here.
    for field in ("available", "percentage", "state", "charge"):
        require(re.search(rf"\bif\s*\([^)]*(?<![\w]){re.escape(field)}\b", refresh)
                is None,
                f"the UI must not branch on the published {field!r} itself")

    # The result is applied as-is: hidden when not visible, empty percentage
    # when the view withholds it.
    require(re.search(r"!\s*\w+\.visible", refresh) is not None,
            "the UI must hide the group when the view is not visible")
    hidden_return = re.search(
        r"!\s*\w+\.visible\s*\)\s*\{[^}]*LV_OBJ_FLAG_HIDDEN", refresh, re.DOTALL)
    require(hidden_return is not None,
            "a not-visible presentation must use the explicit LVGL hidden state")
    require(re.search(
        r"lv_label_set_text\s*\([^;]*?\.show_percentage\s*\?", refresh) is not None,
        "the percentage label must be gated by the view's show_percentage")
    require(re.search(
        r"\?\s*percentage_text\s*:\s*\"\"", refresh) is not None,
        "an absent source must render an empty percentage label")
    require(re.search(r"lv_obj_clear_flag\s*\([^;]*LV_OBJ_FLAG_HIDDEN", refresh)
            is not None,
            "a visible presentation must clear the hidden state")

    # Exactly one semantic glyph label and one numeric percentage label, and no
    # textual state word.
    for state_word in ("charging", "discharging", "neutral", "unavailable", "absent"):
        require(f'"{state_word}"' not in refresh and f"'{state_word}'" not in refresh,
                f"the UI must not render the textual state {state_word!r}")
    require(re.search(r"snprintf\s*\([^;]*%d%%", refresh) is not None,
            "the UI must format the numeric percentage supplied by the view")


def check_glyph_table(ui: str) -> None:
    """REQ-BAT-UI-005 / AC-BAT-UI-006: a fixed semantic glyph table."""
    table = function_body(ui, "battery_indicator_symbol")

    require("cyberdeck_battery_view::power_glyph" in table,
            "the glyph selector must switch on the view's semantic glyph")
    for case in ("charging", "battery", "external", "none"):
        require(f"cyberdeck_battery_view::power_glyph::{case}" in table,
                f"the glyph table must handle power_glyph::{case}")

    # One pictogram per semantic case, and nothing derived from the level.
    icons = re.findall(r"\bLV_SYMBOL_[A-Z0-9_]+\b", table)
    require(icons == ["LV_SYMBOL_CHARGE", "LV_SYMBOL_BATTERY_FULL", "LV_SYMBOL_MINUS"],
            f"the glyph table must be exactly charge/battery-full/minus, found {icons}")
    require(re.search(r"LV_SYMBOL_BATTERY_(?:EMPTY|ONE_QUARTER|HALF|THREE_QUARTERS|"
                      r"RED|FULL_CUSTOM)?", table) is not None
            and not re.search(r"LV_SYMBOL_BATTERY_(?:EMPTY|ONE_QUARTER|HALF|"
                              r"THREE_QUARTERS|RED)\b", table),
            "no battery level pictogram may be rendered")
    for banned in ("percentage", "absent", "available", "charge_class",
                   "battery_state", "charge_signal"):
        require(banned not in table,
                f"the glyph table must not depend on {banned!r}")

    # The glyph table is the single source of pictograms: the refresh path may
    # not hardcode one of its own.
    refresh = function_body(ui, "refresh_battery_status")
    require(not re.search(r"\bLV_SYMBOL_[A-Z0-9_]+\b", refresh),
            "refresh_battery_status must use the glyph table, not a literal symbol")
    require("battery_indicator_symbol" in refresh,
            "refresh_battery_status must apply the shared glyph table")


def check_ui_has_no_raw_access(ui: str) -> None:
    """REQ-BAT-UI-004: no I2C/NVS/reader/expander access in the LVGL layer."""
    for pattern in (r"\bi2c_master_", r"\bbsp_i2c_", r"\besp_io_expander",
                    r"\bbsp_io_expander", r"\bina226_reader_", r"\bnvs_"):
        require(re.search(pattern, ui, re.IGNORECASE) is None,
                f"the LVGL layer must not perform {pattern} directly")


def check_test_suite_wiring(makefile: str, codemap: str) -> None:
    for target in ("test_battery_view", "test_battery_view_contract"):
        require(target in makefile,
                f"the Makefile must register the {target} target")
    require("test_battery_view.cpp" in makefile,
            "the Makefile must build test_battery_view.cpp")
    require(CONTRACT_HDR.exists(),
            "the test-owned view contract header must exist")
    require(VIEW_TEST.exists(), "test_battery_view.cpp must exist")

    for requirement in ("REQ-BAT-UI-001", "REQ-BAT-UI-002", "REQ-BAT-UI-003",
                        "REQ-BAT-UI-004", "REQ-BAT-UI-005"):
        require(requirement in codemap,
                f"code-map.md must document {requirement} traceability")
    for acceptance in ("AC-BAT-UI-001", "AC-BAT-UI-002", "AC-BAT-UI-003",
                       "AC-BAT-UI-004", "AC-BAT-UI-005", "AC-BAT-UI-006"):
        require(acceptance in codemap,
                f"code-map.md must document {acceptance} traceability")
    require("cyberdeck_battery_view" in codemap,
            "code-map.md must document the pure battery view module")
    require("test_battery_view" in codemap,
            "code-map.md must document the battery view host test")


def main() -> int:
    view_src = strip_comments(VIEW_SRC.read_text(encoding="utf-8"))
    view_hdr = strip_comments(VIEW_HDR.read_text(encoding="utf-8"))
    ui = strip_comments(UI.read_text(encoding="utf-8"))
    cmake = strip_comments(CMAKE.read_text(encoding="utf-8"))
    makefile = strip_comments(MAKEFILE_PATH.read_text(encoding="utf-8"))
    codemap = strip_comments(CODEMAP.read_text(encoding="utf-8"))

    # The policy snapshot seam the view consumes must exist.
    policy = strip_comments(POLICY_HDR.read_text(encoding="utf-8"))
    require("charge_signal charge" in policy,
            "the policy snapshot must publish the decoded charge signal")
    require(re.search(r"charge_signal\s+charge\{charge_signal::unknown\}", policy)
            is not None,
            "an unreadable charger must default to `unknown`, never `not_charging`")
    adapter = strip_comments(ADAPTER_HDR.read_text(encoding="utf-8"))
    require("battery_protection_get_policy_snapshot" in adapter,
            "the adapter must publish the pure policy snapshot for the header")

    check_pure_view(view_src, view_hdr)
    check_view_registration(cmake)
    check_ui_delegates_to_the_view(ui)
    check_glyph_table(ui)
    check_ui_has_no_raw_access(ui)
    check_test_suite_wiring(makefile, codemap)

    for line in TRACEABILITY:
        print(f"TRACE {line}")
    print("PASS: battery pure-view structural contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
