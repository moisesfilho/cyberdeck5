#!/usr/bin/env python3
"""Structural host contract for the device-only terminal output seam."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
UI_PATH = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
FILTER_TEST = ROOT / "tests/host/keymap/test_terminal_filter.cpp"


def body(source: str, signature: str) -> str:
    start = 0
    while True:
        start = source.find(signature, start)
        assert start >= 0, f"missing {signature}"
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


def main() -> int:
    source = UI_PATH.read_text(encoding="utf-8")
    append = body(source, "void append_output(const char *data, size_t len, bool repaint)")
    process = body(source, "void process_terminal_output(lv_timer_t *)")
    ssh_data = body(source, "void on_ssh_data(const char *data, size_t length)")
    ssh_state = body(source, "void on_ssh_state(ssh_client_state_t state, const char *message)")
    init = body(source, "extern \"C\" esp_err_t cyberdeck_ui_init(void)")

    # T-COAL-01/02, REQ-1/AC-1.
    assert "append_output(displayed.data(), displayed_size, false);" in ssh_data
    assert "s_output.append(data, len);" in append
    assert "s_terminal_output_dirty = true;" in append
    assert "if (repaint) render_terminal();" in append
    assert process.count("render_terminal();") == 1
    assert "if (s_terminal_output_dirty) render_terminal();" in process
    assert "lv_timer_create(process_terminal_output, 100, nullptr);" in init

    # T-BOUND-01/02, REQ-2/AC-2.
    assert "constexpr size_t TERMINAL_LIMIT = 12288;" in source
    assert "if (s_output.size() > TERMINAL_LIMIT)" in append
    assert "s_output.erase(0, safe_offset);" in append
    assert "utf8_valid_start_offset(s_output, excess)" in append
    assert "return str.substr(utf8_valid_start_offset(str, drop_bytes));" in source

    # T-CTX-01/02, REQ-3/AC-3.
    assert "bsp_display_lock" not in process
    keyboard = body(source, "void on_keyboard_event(const char *text, size_t length, uint8_t modifier,")
    assert "lv_async_call" in source
    assert "bsp_display_lock()" not in keyboard

    # T-FLUSH-01/02, REQ-4/AC-4.
    assert "append_line(status);" in ssh_state
    assert "append_line(\"\\n\");" in ssh_state
    assert "s_ssh_output_filter.flush(pending, sizeof(pending))" in ssh_state
    assert "s_ssh_line_composer.flush(&retained[0], retained.size())" in ssh_state
    assert ssh_state.rfind("render_terminal();") > ssh_state.find("s_ssh_line_composer.flush")
    assert "void shell_session_host::clear_output() { s_output.clear(); }" in source
    assert "void shell_session_host::render() { render_terminal(); }" in source

    # T-FILTER-01, REQ-5/AC-5.
    assert "s_ssh_output_filter.feed(data, length" in ssh_data
    assert "s_ssh_line_composer.feed(filtered.data(), written" in ssh_data
    filter_source = FILTER_TEST.read_text(encoding="utf-8")
    for marker in ("test_csi_sequences_removed", "test_osc_bel_terminated",
                   "test_osc_st_terminated", "test_crlf_fragmented",
                   "test_utf8_preserved"):
        assert marker in filter_source, f"missing filter scenario {marker}"

    # T-REG-01: rendering preserves the session-owned editing line/cursor.
    render = body(source, "void render_terminal()")
    assert "s_shell_session.cursor()" in render
    assert "lv_textarea_set_cursor_pos(s_terminal, char_pos);" in render
    assert "s_shell_session.line()" in render

    print("PASS: terminal output structural contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
