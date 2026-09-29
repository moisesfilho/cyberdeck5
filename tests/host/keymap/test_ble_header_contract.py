#!/usr/bin/env python3
"""Structural host contract for the Bluetooth header indicator.

The LVGL UI is device-only, so this inspects the production source without
starting LVGL, a simulator, a radio, or the Serial Automation Bridge.
"""

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
FONT = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_font.c"


def function_body(source: str, signature: str) -> str:
    start = 0
    while True:
        start = source.find(signature, start)
        if start < 0:
            raise AssertionError(f"missing {signature}")
        opening = source.find("{", start)
        semicolon = source.find(";", start)
        if opening >= 0 and (semicolon < 0 or opening < semicolon):
            break
        start += len(signature)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated {signature}")


def assert_case_dispatch(body: str, event: str, call: str) -> None:
    """Accept equivalent C++ whitespace and an optional case block."""
    compact_call = re.sub(r"\s+", "", call)
    match = re.search(
        rf"case\s+{re.escape(event)}\s*:(?P<case_body>.*?\bbreak\s*;)",
        body,
        re.DOTALL,
    )
    assert match, f"missing {event} case with break"
    compact_case = re.sub(r"[\s{}]", "", match.group("case_body"))
    assert f"{compact_call};" in compact_case, (
        f"missing {event} dispatch to {call}"
    )


def main() -> int:
    source = UI.read_text(encoding="utf-8")
    font = FONT.read_text(encoding="utf-8")
    process = function_body(source, "void process_ble_events(lv_timer_t *)")
    refresh = function_body(source, "void refresh_ble_status()")
    init = function_body(source, "esp_err_t cyberdeck_ui_init(void)")

    # Pure model predicate: both link events reach the model, and refresh_ble_status
    # is performed after queue processing on every 100 ms BLE timer tick.
    assert_case_dispatch(
        process,
        "BLE_MGR_EVT_CONNECTED",
        "s_ble_model.connection_finished(event.token, true)",
    )
    assert_case_dispatch(
        process,
        "BLE_MGR_EVT_DISCONNECTED",
        "s_ble_model.connection_finished(event.token, false)",
    )
    assert process.count("s_ble_model.advance_time(100)") == 1
    assert process.index("s_ble_model.advance_time(100)") < process.index("while (s_ble_event_queue")
    assert process.index("refresh_ble_status();") > process.index("while (s_ble_event_queue")

    # The UI must not infer visibility from screen/ownership or duplicate BLE
    # state.  It only applies the read-only connection predicate to both the
    # wrapper and its label, preserving the wrapper's geometry when hidden.
    assert "if (s_ble_model.is_connected())" in refresh
    assert "lv_obj_clear_flag(s_ble_status, LV_OBJ_FLAG_HIDDEN);" in refresh
    assert "lv_obj_clear_flag(s_ble_cell, LV_OBJ_FLAG_HIDDEN);" in refresh
    assert "lv_obj_add_flag(s_ble_status, LV_OBJ_FLAG_HIDDEN);" in refresh
    assert "lv_obj_add_flag(s_ble_cell, LV_OBJ_FLAG_HIDDEN);" in refresh
    assert "current_screen" not in refresh
    assert "owns_input" not in refresh

    # The named offset is the regression fix: the content-sized glyph is
    # positioned inside a fixed-height, layout-free wrapper rather than using
    # a 42 px-tall label whose text box produced the y=14..32 mismatch.
    assert "constexpr int32_t CYBERDECK_BLE_HEADER_Y_OFFSET = 10;" in source
    ble_start = init.index("lv_obj_t *ble_cell = lv_obj_create(header_right);")
    ble_end = init.index("s_wifi_status = cyberdeck_wifi_icon_create", ble_start)
    ble = init[ble_start:ble_end]
    assert "s_ble_cell = ble_cell;" in ble
    assert "lv_obj_set_size(ble_cell, LV_SIZE_CONTENT, 42);" in ble
    assert "lv_obj_set_layout(ble_cell, LV_LAYOUT_NONE);" in ble
    assert "lv_obj_set_flex_grow(ble_cell, 0);" in ble
    assert "lv_obj_set_style_pad_all(ble_cell, 0, 0);" in ble
    assert "lv_obj_set_style_bg_opa(ble_cell, LV_OPA_TRANSP, 0);" in ble
    assert "s_ble_status = lv_label_create(ble_cell);" in ble
    assert "lv_label_set_text(s_ble_status, LV_SYMBOL_BLUETOOTH);" in ble
    assert "lv_obj_set_size(s_ble_status, LV_SIZE_CONTENT, LV_SIZE_CONTENT);" in ble
    assert "lv_obj_set_y(s_ble_status, CYBERDECK_BLE_HEADER_Y_OFFSET);" in ble
    assert "lv_obj_add_flag(s_ble_status, LV_OBJ_FLAG_HIDDEN);" in ble
    assert "lv_obj_add_flag(ble_cell, LV_OBJ_FLAG_HIDDEN);" in ble
    assert "lv_obj_set_style_pad_right(ble_cell, 4, 0);" in ble
    assert "lv_obj_set_style_bg_opa(s_ble_status, LV_OPA_TRANSP, 0);" in ble
    assert ble.index("s_ble_status = lv_label_create(ble_cell)") < ble.index("lv_obj_set_y(s_ble_status, CYBERDECK_BLE_HEADER_Y_OFFSET)")

    # Header order and non-growing content-sized children prevent the new item
    # from stealing width from Wi-Fi or battery at the right edge.
    assert ble_end < init.index("s_battery_status = lv_obj_create(header_right)")
    assert init.index("lv_obj_t *ble_cell = lv_obj_create(header_right);") < init.index("s_wifi_status = cyberdeck_wifi_icon_create")
    assert init.index("s_wifi_status = cyberdeck_wifi_icon_create") < init.index("s_battery_status = lv_obj_create(header_right)")
    assert "lv_obj_set_flex_grow(s_wifi_status, 0);" in init
    assert "lv_obj_set_flex_grow(s_battery_status, 0);" in init
    battery_start = init.index("s_battery_status = lv_obj_create(header_right)")
    battery_end = init.index("s_battery_symbol = lv_label_create(s_battery_status);", battery_start)
    battery = init[battery_start:battery_end]
    assert "lv_obj_set_style_pad_left(s_battery_status, 11, 0);" in battery
    assert "lv_obj_set_style_pad_left(s_battery_status, 4, 0);" not in battery
    assert "lv_obj_set_flex_align(header_right, LV_FLEX_ALIGN_END" in init
    assert "lv_obj_set_style_pad_column(header_right, 2, 0);" in init
    assert "lv_timer_create(process_ble_events, 100, nullptr);" in init

    # The committed LVGL font contains the requested Bluetooth codepoint, not
    # merely a macro reference in the UI.
    assert "0xF293" in font
    assert '/* U+F293 "' in font

    print("PASS: Bluetooth header indicator contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
