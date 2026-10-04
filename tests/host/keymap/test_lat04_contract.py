#!/usr/bin/env python3
"""Structural acceptance contract for REQ-LAT-04 terminal composition."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
UI = (ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp").read_text()
VIEW_H = (ROOT / "components/cyberdeck/include/platform/display/cyberdeck_terminal_view.h").read_text()
VIEW = (ROOT / "components/cyberdeck/src/platform/display/cyberdeck_terminal_view.cpp").read_text()
MAKE = (ROOT / "tests/host/keymap/Makefile").read_text()


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    # TEST-LAT04-01/02/03: fixed reusable slots, bounded at 64, and one
    # continuous surface.  The behavioral test also checks short/long output.
    require("k_max_lines = 64" in VIEW_H, "terminal slot bound changed")
    require("s_lines[k_max_lines]" in VIEW_H, "line slots are not fixed")
    require("std::array<std::size_t, k_max_lines + 1>" in VIEW,
            "render path is not bounded")
    require("lv_obj_set_hidden(s_terminal, true)" in VIEW,
            "textarea is a visible duplicate")
    require("s_terminal_view.render(visual)" in UI,
            "UI does not render through the continuous surface")
    require("lv_textarea_set_text(s_terminal, editor.c_str())" in UI,
            "hidden textarea is not synchronized with editor state")

    # TEST-LAT04-04/05: cursor and UTF-8 editing remain owned by the real
    # shell/session contracts; do not replace them with a view-only fake.
    edit = (ROOT / "tests/host/keymap/test_edit_line.cpp").read_text()
    for marker in ("test_arrows_utf8_and_clamp", "test_backspace_utf8_and_middle",
                   "test_enter_menu", "test_enter_connected_sends_command_once"):
        require(marker in edit, f"missing editing scenario: {marker}")
    session = (ROOT / "tests/host/keymap/test_shell_session.cpp").read_text()
    for marker in ("local", "SSH", "password", "BLE", "Wi-Fi"):
        require(marker.lower() in session.lower(), f"missing mode coverage: {marker}")

    # TEST-LAT04-06/07: resize/clear/output ownership and the complete dump
    # are separate from the bounded visual viewport.
    require("s_line_count = visible_lines" in VIEW,
            "resize does not recalculate visible slots")
    require("s_scrollback.clear()" in UI, "clear does not clear scrollback model")
    dump = (ROOT / "tests/host/keymap/test_term_dump_contract.py").read_text()
    require("term.dump" in dump and "k_term_dump_max_bytes" in dump,
            "integral term.dump contract missing")

    # TEST-LAT04-08/09: ownership, physical/virtual input and regressions.
    require("lv_keyboard_set_textarea(s_keyboard, s_terminal)" in VIEW,
            "virtual keyboard target contract missing")
    require("cyberdeck_keyboard_dispatch" in UI,
            "physical keyboard dispatcher contract missing")
    require("test_lat03_contract" in MAKE and "test_display_views" in MAKE,
            "LAT03/display regression targets are not wired")
    print("PASS: REQ-LAT-04 structural acceptance contract")


if __name__ == "__main__":
    main()
