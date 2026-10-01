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
    ssh_data_callback = body(source, "void on_ssh_data(ssh_client_generation_t generation, const char *data, size_t length)")
    ssh_state_callback = body(source, "void on_ssh_state(ssh_client_generation_t generation, ssh_client_state_t state, const char *message)")
    ssh_data = body(source, "void process_ssh_data(const char *data, size_t length)")
    ssh_state = body(source, "void process_ssh_state(ssh_client_state_t state, const char *message)")
    ssh_events = body(source, "void process_ssh_events(lv_timer_t *)")
    init = body(source, "extern \"C\" esp_err_t cyberdeck_ui_init(void)")

    # T-COAL-01/02, REQ-1/AC-1.
    assert "append_output(displayed.data(), displayed_size, false);" in ssh_data
    assert "event.generation = generation;" in ssh_data_callback
    assert "event.generation = generation;" in ssh_state_callback
    assert "xQueueSend(s_ssh_event_queue, &event, 0)" in ssh_data_callback
    assert "xQueueSend(s_ssh_event_queue, &event, 0)" in ssh_state_callback
    assert "uxQueueMessagesWaiting(s_ssh_event_queue) >= k_ssh_event_queue_capacity - 1" in ssh_data_callback
    assert "s_ssh_data_queue_drop_count" in ssh_data_callback
    assert "xQueueReceive(s_ssh_event_queue, &discarded, 0)" in ssh_state_callback
    assert "discarded.kind == ssh_ui_event_kind::data" in ssh_state_callback
    assert "s_output.append(data, len);" in append
    assert "s_terminal_output_dirty = true;" in append
    assert "if (repaint) render_terminal();" in append
    assert process.count("render_terminal();") == 1

    # The SSH queue element is reclaimed by value on the LVGL timer.  A large
    # inline payload overflowed the LVGL task stack and froze the boot right
    # after wifi_mgr_start(), so both the element size and the absence of a
    # stack copy are pinned here.
    assert "k_ssh_event_data_limit = 256" in source
    assert "k_ssh_event_state_message_limit = 64" in source
    assert "ssh_ui_event s_ssh_event_slot;" in source
    assert "xQueueReceive(s_ssh_event_queue, &s_ssh_event_slot, 0)" in ssh_events
    assert "if (s_ssh_event_slot.generation != s_ssh_expected_generation) continue;" in ssh_events
    assert "ssh_ui_event event{}" not in ssh_events
    assert "1024" not in source, "no inline 1 KB payload may return to the SSH event"
    assert "if (s_terminal_output_dirty) render_terminal();" in process
    assert "lv_timer_create(process_terminal_output, 100, nullptr);" in init

    # T-BOUND-01/02, REQ-2/AC-2.
    assert "constexpr size_t TERMINAL_LIMIT = 12288;" in source
    assert "k_ssh_event_queue_capacity = 8" in source
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
