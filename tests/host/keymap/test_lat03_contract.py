#!/usr/bin/env python3
"""Structural acceptance contracts for REQ-LAT-03's non-linkable UI seam."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
UI = (ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp").read_text()
VIEW = (ROOT / "components/cyberdeck/src/platform/display/cyberdeck_terminal_view.cpp").read_text()
MODEL_H = (ROOT / "components/cyberdeck/include/platform/display/cyberdeck_terminal_scrollback.h").read_text()
MODEL_CPP = (ROOT / "components/cyberdeck/src/platform/display/cyberdeck_terminal_scrollback.cpp").read_text()
MAKE = (ROOT / "tests/host/keymap/Makefile").read_text()


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    display_test = (ROOT / "tests/host/keymap/test_display_views.cpp").read_text()
    for marker in ("TEST-TERM-SCROLL-01", "TEST-TERM-SCROLL-02",
                   "TEST-TERM-SCROLL-03", "TEST-TERM-SCROLL-04",
                   "TEST-TERM-SCROLL-06"):
        require(marker in display_test,
                f"missing touch scroll scenario: {marker}")

    # TEST-LAT03-01/02/03: one bounded model owns bytes and cuts only at UTF-8
    # boundaries; clear is the only reset operation.
    require("k_capacity = 12288" in MODEL_H, "scrollback capacity changed")
    require("s_text.reserve(k_capacity)" in MODEL_CPP, "model storage is not reserved")
    require("valid_start_offset" in MODEL_CPP and "0xC0U" in MODEL_CPP,
            "UTF-8 boundary pruning disappeared")
    require("s_text.clear();" in MODEL_CPP, "clear must reset the model")
    require("s_text.append(data, length);" in MODEL_CPP,
            "fragmented append must reach the same model")

    # TEST-LAT03-04/09: scrollback goes to the bounded continuous surface. The
    # textarea receives the composed editor line, never the retained model.
    require("viewport_bytes = 4096" in UI, "viewport bound is not 4096 bytes")
    render_start = UI.index("void render_terminal() {")
    render_end = UI.index("void append_output(const char *data, size_t len, bool repaint) {",
                          render_start)
    render = UI[render_start:render_end]
    require("s_terminal_view.render(visual);" in render,
            "bounded viewport is not rendered by the continuous surface")
    require("s_scrollback.viewport(viewport)" in UI,
            "bounded viewport is not selected from scrollback")
    require("lv_textarea_set_text(s_terminal, editor.c_str());" in render,
            "textarea must render prompt/editor only")
    require("s_scrollback.text()" not in render[render.index("RenderGuard") :],
            "full scrollback must not be sent to textarea render path")
    require("std::string visual = output + editor;" in render,
            "scrollback and editor are not composed on one terminal surface")

    # TEST-LAT03-05/06: behavioral tests already exercise UTF-8 cursor,
    # password masking/backspace and Enter; keep the acceptance wiring alive.
    edit_test = (ROOT / "tests/host/keymap/test_edit_line.cpp").read_text()
    for marker in ("test_arrows_utf8_and_clamp", "test_backspace_utf8_and_middle",
                   "test_visible_line_password_mask", "test_echo_password_no_echo",
                   "test_enter_menu", "test_enter_connected_sends_command_once"):
        require(marker in edit_test, f"missing REQ-LAT-03-05/06 scenario: {marker}")

    # TEST-LAT03-07: ANSI/CRLF, menus and SSH are preserved by their existing
    # contracts, rather than being reimplemented in the scrollback model.
    filter_test = (ROOT / "tests/host/keymap/test_terminal_filter.cpp").read_text()
    for marker in ("test_csi_sequences_removed", "test_crlf_fragmented",
                   "test_utf8_preserved", "test_remote_prompt_is_preserved_literally"):
        require(marker in filter_test, f"missing ANSI/SSH preservation test: {marker}")
    require("s_wifi_search_menu.render()" in UI and "s_wifi_saved_menu.render()" in UI,
            "menu overlays are not preserved in composed output")

    # TEST-LAT03-08: term.dump remains a separate contract and must be present
    # alongside the new scrollback target.
    dump_test = (ROOT / "tests/host/keymap/test_term_dump_contract.py").read_text()
    require("term.dump" in dump_test and "k_term_dump_max_bytes" in dump_test,
            "term.dump contract was not retained")

    # TEST-LAT03-10: the previous bounded dispatcher FIFO/wake regression is
    # still part of the focused acceptance set.
    dispatch_test = (ROOT / "tests/host/keymap/test_keyboard_dispatch.cpp").read_text()
    dispatch_contract = (ROOT / "tests/host/keymap/test_keyboard_input_contract.py").read_text()
    require("fifo" in dispatch_test and "lv_shim_wake_count" in dispatch_test,
            "FIFO/wake behavioral regression missing")
    require("lvgl_port_task_wake" in dispatch_contract,
            "explicit wake structural regression missing")

    # TEST-LAT03-11: bounded object contract is host-visible and compiled from
    # the real production model, not a test duplicate.
    require("test_terminal_scrollback" in MAKE and "test_lat03_contract" in MAKE,
            "REQ-LAT-03 tests are not wired in Makefile")
    print("PASS: REQ-LAT-03 structural acceptance contract")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, OSError, UnicodeError, ValueError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
