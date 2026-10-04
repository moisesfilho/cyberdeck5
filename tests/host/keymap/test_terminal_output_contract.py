#!/usr/bin/env python3
"""Structural host contract for the device-only terminal output seam."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
UI_PATH = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
SSH_CLIENT_PATH = ROOT / "components/cyberdeck/src/apps/ssh/ssh_client.cpp"
CONSOLE_PATH = (ROOT / "components/cyberdeck/src/apps/shell/"
                "cyberdeck_shell_console.cpp")
CONSOLE_HEADER = (ROOT / "components/cyberdeck/include/apps/shell/"
                  "cyberdeck_shell_console.h")
FILTER_TEST = ROOT / "tests/host/keymap/test_terminal_filter.cpp"
FILTER_SRC = (ROOT / "components/cyberdeck/src/apps/shell/"
              "cyberdeck_terminal_filter.cpp")
FILTER_HEADER = (ROOT / "components/cyberdeck/include/apps/shell/"
                 "cyberdeck_terminal_filter.h")
COMPOSER_SRC = (ROOT / "components/cyberdeck/src/apps/shell/"
                "cyberdeck_ssh_line_composer.cpp")
COMPOSER_TEST = ROOT / "tests/host/keymap/test_ssh_line_composer.cpp"
SESSION_SRC = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp"
SCROLLBACK_SRC = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_terminal_scrollback.cpp"
SCROLLBACK_HEADER = ROOT / "components/cyberdeck/include/platform/display/cyberdeck_terminal_scrollback.h"
TERMINAL_VIEW_SRC = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_terminal_view.cpp"


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


def declaration_block(source: str, opener: str) -> str:
    """Return `opener ... };` verbatim, tracking brace depth."""
    start = source.index(opener)
    depth = 0
    for index in range(source.index("{", start), len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:source.index(";", index) + 1]
    raise AssertionError(f"unterminated {opener}")


def transport_chunk_limit() -> int:
    """Largest payload `ssh_client` delivers in a single rx callback.

    The UI data slot is relational to this value, not an arbitrary bound: a
    smaller slot would truncate every full-size read the producer legitimately
    makes, and a bigger one would reserve payload the transport cannot carry.
    Both directions are pinned here -- the UI literal must equal the derived
    transport chunk, so moving either side without the other fails the contract.
    """
    ssh_source = SSH_CLIENT_PATH.read_text(encoding="utf-8")
    buffer_size = re.search(r"char\s+rx_buffer\s*\[\s*(\d+)\s*\]\s*;", ssh_source)
    assert buffer_size, "missing the SSH receive buffer in ssh_client.cpp"
    bounds = set(re.findall(
        r"ssh_channel_read_nonblocking\(.*?sizeof\(rx_buffer\)\s*-\s*(\d+)",
        ssh_source, re.DOTALL))
    assert bounds, "the SSH receive is no longer bounded by the receive buffer"
    assert len(bounds) == 1, f"ambiguous SSH receive bound: {sorted(bounds)}"
    return int(buffer_size.group(1)) - int(bounds.pop())


def main() -> int:
    source = UI_PATH.read_text(encoding="utf-8")
    scrollback_source = SCROLLBACK_SRC.read_text(encoding="utf-8")
    scrollback_header = SCROLLBACK_HEADER.read_text(encoding="utf-8")
    terminal_view_source = TERMINAL_VIEW_SRC.read_text(encoding="utf-8")
    CONSOLE_SOURCE = CONSOLE_PATH.read_text(encoding="utf-8")
    console_header = CONSOLE_HEADER.read_text(encoding="utf-8")
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
    # REQ-1/AC-1: output is handed to the bounded model verbatim.  The UI seam
    # must not manufacture a separator, truncate a prefix, or retain a second
    # output buffer whose contents could diverge from term.dump.
    assert "if (!data || !len) return;" in append
    assert "s_scrollback.append(data, len);" in append
    assert "s_output.append" not in append
    assert "s_output.push_back" not in append
    assert "s_terminal_output_dirty = true;" in append
    assert "if (repaint) render_terminal();" in append
    assert process.count("render_terminal();") == 1

    # The SSH queue element is reclaimed by value on the LVGL timer.  A large
    # inline payload overflowed the LVGL task stack and froze the boot right
    # after wifi_mgr_start(), so both the element size and the absence of a
    # stack copy are pinned here.
    #
    # REQ-SSH-OUTPUT-02 / AC-SSH-02: the data slot is the transport chunk and
    # not an arbitrary smaller bound.  `ssh_client` reads at most
    # `sizeof(rx_buffer) - 1` bytes per callback, so an artificial bound below
    # that (the 256 this contract used to pin) would truncate every full-size
    # chunk the producer legitimately delivers.  The relation is derived from
    # the producer, so shrinking the receive buffer or the slot alone fails.
    transport_limit = transport_chunk_limit()
    assert f"k_ssh_event_data_limit = {transport_limit}" in source
    assert "k_ssh_event_state_message_limit = 64" in source
    ssh_ui_event = declaration_block(source, "struct ssh_ui_event {")
    assert "char message[k_ssh_event_state_message_limit]{};" in ssh_ui_event
    assert "char data[k_ssh_event_data_limit]{};" in ssh_ui_event
    # No payload array in the event element is sized by a literal, so the slot
    # can only grow if the transport contract itself is re-derived.
    assert not re.search(r"\[\s*\d+\s*\]\s*\{?\}?\s*;", ssh_ui_event)
    assert "ssh_ui_event s_ssh_event_slot;" in source
    assert "xQueueReceive(s_ssh_event_queue, &s_ssh_event_slot, 0)" in ssh_events
    assert "if (s_ssh_event_slot.generation != s_ssh_expected_generation) continue;" in ssh_events
    assert "ssh_ui_event event{}" not in ssh_events
    assert "if (s_terminal_output_dirty) render_terminal();" in process
    assert "lv_timer_create(process_terminal_output, 100, nullptr);" in init

    # T-BOUND-01/02, REQ-2/AC-2.  Bounded retention is now the model's
    # responsibility; the UI only composes a bounded viewport for LVGL.
    assert "cyberdeck_terminal_scrollback::model s_scrollback;" in source
    assert "#include \"platform/display/cyberdeck_terminal_scrollback.h\"" in source
    assert "static constexpr std::size_t k_capacity = 12288;" in scrollback_header
    assert "if (length >= k_capacity)" in scrollback_source
    assert "s_text.assign(data + offset, length - offset);" in scrollback_source
    assert "if (s_text.size() + length > k_capacity)" in scrollback_source
    assert "s_text.erase(0, valid_start_offset(s_text, excess));" in scrollback_source
    assert "s_text.append(data, length);" in scrollback_source
    assert "constexpr std::size_t k_terminal_limit = cyberdeck_edit_line::limit;" in console_header
    assert "return std::string(text.substr(utf8_valid_start_offset(text, text.size() - max_bytes)));" \
        in CONSOLE_SOURCE
    assert "k_ssh_event_queue_capacity = 8" in source
    assert "s_output.size()" not in append

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
    assert "void shell_session_host::clear_output() { s_scrollback.clear(); }" in source
    assert "void shell_session_host::render() { render_terminal(); }" in source

    # T-FILTER-01, REQ-5/AC-5.
    assert "s_ssh_output_filter.feed(data, length" in ssh_data
    assert "s_ssh_line_composer.feed(filtered.data(), written" in ssh_data
    filter_source = FILTER_TEST.read_text(encoding="utf-8")
    for marker in ("test_csi_sequences_removed", "test_osc_bel_terminated",
                   "test_osc_st_terminated", "test_crlf_fragmented",
                   "test_utf8_preserved", "test_remote_prompt_is_preserved_literally",
                   "test_remote_prompt_survives_partition_invariance"):
        assert marker in filter_source, f"missing filter scenario {marker}"

    # T-REG-02 (REQ-1/AC-1): o prompt remoto e preservado literal pelo fluxo
    # remoto, e nenhum modulo fabrica um substituto fixo para ele.  O console nao
    # declara constante de marcador; o filtro ANSI, o compositor e a sessao nunca
    # conheceram esse texto.  `ssh> ` chegando do remoto e, portanto, texto remoto
    # qualquer, guardado byte a byte por test_terminal_filter e pelos cenarios do
    # compositor, de modo que a regra de eco nunca pode casar um prompt
    # sintetizado.  O mesmo vale para o literal da UI: ela consome a view.
    assert "k_ssh_marker" not in console_header
    assert "k_ssh_marker" not in CONSOLE_SOURCE
    assert "ssh>" not in console_header and "ssh>" not in CONSOLE_SOURCE
    filter_implementation = FILTER_SRC.read_text(encoding="utf-8") + FILTER_HEADER.read_text(encoding="utf-8")
    assert "k_ssh_marker" not in filter_implementation and "ssh>" not in filter_implementation
    composer_implementation = COMPOSER_SRC.read_text(encoding="utf-8")
    assert "k_ssh_marker" not in composer_implementation and "ssh>" not in composer_implementation
    session_source = SESSION_SRC.read_text(encoding="utf-8")
    assert "k_ssh_marker" not in session_source and "ssh>" not in session_source
    assert "k_ssh_marker" not in source and "ssh>" not in source
    # The console synthesises only the password prompt and the local cwd prompt,
    # and the connected state suppresses the local prompt so the remote prompt
    # travelling in the scrollback is the only one on screen.
    assert CONSOLE_SOURCE.count("view.marker =") == 2
    assert 'view.marker = "Password: ";' in CONSOLE_SOURCE
    assert 'view.marker = fit_prompt_marker(state.cwd + "$ ");' in CONSOLE_SOURCE
    assert CONSOLE_SOURCE.count("state.ssh_connected") == 2
    assert "else if (!state.ssh_connected && !state.input_owned_elsewhere)" in CONSOLE_SOURCE
    # The filter and the composer unit tests pin the same boundary behaviourally.
    filter_test_source = filter_source
    for scenario in ("test_remote_prompt_is_preserved_literally",
                     "test_remote_prompt_survives_partition_invariance"):
        assert scenario in filter_test_source, f"missing filter scenario {scenario}"
    composer_test = COMPOSER_TEST.read_text(encoding="utf-8")
    for scenario in ("test_remote_prompt_never_matches_the_echo_guard",):
        assert scenario in composer_test, f"missing composer scenario {scenario}"

    # T-REG-03 (REQ-SSH-01/AC-SSH-01 + REQ-2/AC-2 + AC-4): o scrollback e aparado
    # pelos bytes que a cauda composta realmente reserva e o cursor e contado em
    # codepoints depois desse mesmo scrollback.  Nenhum byte e fabricado: a regra
    # nao acrescenta LF, e o compositor tambem nao injeta separador.  Um LF real
    # do remoto permanece exatamente uma vez e um fluxo sem LF final permanece
    # sem LF; o orcamento e o `available` inteiro, sem reservar byte para uma
    # quebra que nao existe.
    rendered = body(source, "std::string get_rendered_output(const cyberdeck_shell_console::line_view &view,")
    assert "const size_t used = view.reserved();" in rendered
    assert "const size_t available = complete" in rendered
    assert "TERMINAL_LIMIT > used ? TERMINAL_LIMIT - used : 0" in rendered
    assert "truncate_left_utf8(output, available)" in rendered

    # The overlay may only append its own menus; the rule that decides the final
    # band may not create a byte at all.  The two regions have different rights,
    # so the fabrication ban is scoped to the rule instead of the whole function.
    anchor = "std::string output = complete ? s_scrollback.text() : s_scrollback.viewport(viewport);"
    rule_start = "if (!complete && output.size() > available)"
    assert anchor in rendered and rule_start in rendered
    overlay = rendered[rendered.index(anchor) + len(anchor):rendered.index(rule_start)]
    for forbidden in ("push_back", "pop_back", "clear()", "resize", "output = "):
        assert forbidden not in overlay, f"the overlay may only append menus ({forbidden})"
    rule = rendered[rendered.index(rule_start):]
    for forbidden in ("push_back", "+=", "'\\n'", '"\\n"', "append"):
        assert forbidden not in rule, f"the truncation rule must not fabricate bytes ({forbidden})"
    assert rendered.rstrip().endswith("return output;")
    assert rule.rstrip().endswith("truncate_left_utf8(output, available);\n    return output;") or \
        rule.rstrip().endswith("truncate_left_utf8(output, available);return output;"), \
        "the truncation must be the last statement before the return"

    # No separator-aware budget and no closing-LF condition may reappear: both
    # would shrink the real bytes the composed tail leaves free.
    assert "output_limit" not in rendered and "available - 1" not in rendered
    assert "needs_visual_separator" not in rendered
    assert rendered.count("push_back") == 0

    # REQ-SSH-01/AC-SSH-01: the composer carries the same rule -- no pending
    # separator to emit on echo completion, divergence or flush.  Its unit test
    # pins the resulting band behaviourally.
    assert "m_separator_pending" not in composer_implementation
    for scenario in ("test_echo_suppressed_without_artificial_lf",
                     "test_no_echo_output_is_verbatim",
                     "test_output_starting_with_newline",
                     "test_remote_prompt_never_matches_the_echo_guard"):
        assert scenario in composer_test, f"missing composer scenario {scenario}"
    for behaviour in ("feed_ok(g, \"cmd\\n\");", "CHECK_EQ(f, \"\");",
                      "feed_ok(g, \"result\\n\");", "CHECK_EQ(f, \"result\\n\");"):
        assert behaviour in composer_test, \
            f"composer band must stay verbatim/no-fabrication: {behaviour}"

    # REQ-3: no fabricated byte is ever written back into the retained scrollback,
    # so re-rendering cannot accumulate newlines.
    assert "s_output.push_back" not in source
    assert "s_scrollback.append(data, len);" in append
    # O comportamento da regra e executado no host em test_prompt_behavior.py, que
    # compila estas mesmas sentencas de producao contra uma oracle independente.
    prompt_behavior = ROOT / "tests/host/keymap/test_prompt_behavior.py"
    prompt_behavior_source = prompt_behavior.read_text(encoding="utf-8")
    for scenario in ("test_prompt_matrix_executes_production_composition",
                     "test_visual_separator_executes_production_statements"):
        assert scenario in prompt_behavior_source, f"missing behavioural scenario {scenario}"
    # The no-fabrication oracle is stated in that harness, not borrowed from the
    # production statements under test.
    for oracle in ("static std::string expected(size_t available, const std::string &scrollback)",
                   "count_char(once, '\\n') == count_char(scrollback, '\\n')",
                   "rendered(view, once) == once"):
        assert oracle in prompt_behavior_source, f"missing rendered-output oracle {oracle}"

    # T-REG-01: rendering preserves the shell-application-owned line/cursor.
    # The prompt, the fitted line and the cursor now come from the foreground
    # shell application; the view only applies them.
    render = body(source, "void render_terminal()")
    assert "s_shell_app.compose_line(surface)" in body(source, "cyberdeck_shell_console::line_view compose_console_line()")
    assert "view.cursor_chars()" in render
    assert "lv_textarea_set_cursor_pos(s_terminal, char_pos);" in render
    assert "view.text()" in render
    assert "s_shell_app.cursor()" not in render and "s_shell_app.line()" not in render

    # The model is the complete term.dump source.  The visual path is a single
    # bounded surface: scrollback, prompt/editor text, and cursor are composed
    # before the reusable line slots receive the result.  The textarea remains
    # an input target only and must never render scrollback.
    assert "std::string output = get_rendered_output(view);" in render
    assert "const std::string editor = view.text();" in render
    assert "std::string visual = output + editor;" in render
    assert "visual.insert(output.size() + cursor_byte, \"|\");" in render
    assert "s_terminal_view.render(visual);" in render
    assert "lv_textarea_set_text(s_terminal, editor.c_str());" in render
    assert "s_terminal_view.render(output);" not in render
    assert "s_scrollback_view" not in source
    assert "lv_label_set_text(s_scrollback_view" not in terminal_view_source
    assert "std::string output = complete ? s_scrollback.text()" in rendered
    assert "if (!complete && output.size() > available)" in rendered
    assert "constexpr size_t viewport_bytes = 4096;" in source
    term_dump = body(source, "extern \"C\" esp_err_t cyberdeck_ui_term_dump(")
    assert "const std::string rendered = get_rendered_output(view, true) + view.text();" in term_dump
    assert "truncate_left_utf8(rendered, capacity)" in term_dump
    assert "*out_bytes = snapshot.size();" in term_dump
    assert "*out_truncated = rendered.size() > snapshot.size() ? 1 : 0;" in term_dump
    assert "lv_label_create(s_surface)" in terminal_view_source
    assert "lv_textarea_create(parent)" in terminal_view_source
    assert "lv_textarea_set_one_line(s_terminal, true)" in terminal_view_source
    assert "lv_obj_set_hidden(s_terminal, true)" in terminal_view_source
    assert "lv_textarea_set_text(s_terminal, output.c_str())" not in render

    # T-GAP-01/02/03 (REQ-01/AC-01, REQ-02/AC-02, REQ-04/AC-04): a queue gap
    # leaves the ANSI filter holding a half-consumed sequence, so every discard
    # is signalled by a monotonic epoch and the LVGL timer resynchronises the
    # filter when it sees one.  The signal has to survive the whole transport:
    # producer-side overflow, payload truncation, and the eviction performed to
    # make room for a state transition all publish a new epoch, and the state
    # event that evicted data is restamped with the epoch of what it discarded.
    assert "uint32_t discard_epoch = 0;" in source
    assert "std::atomic<uint32_t> s_ssh_discard_epoch{0};" in source
    assert "uint32_t s_ssh_applied_discard_epoch = 0;" in source
    mark = body(source, "uint32_t mark_ssh_event_discarded(std::atomic<uint32_t> &counter)")
    assert "counter.compare_exchange_weak(dropped, dropped + 1, std::memory_order_relaxed)" in mark
    assert "s_ssh_discard_epoch.compare_exchange_weak(epoch, epoch + 1," in mark
    assert "std::numeric_limits<uint32_t>::max()" in mark
    assert "return epoch;" in mark
    assert body(source, "uint32_t current_ssh_discard_epoch()").strip() == \
        "return s_ssh_discard_epoch.load(std::memory_order_relaxed);"
    assert "s_ssh_output_filter.flush(nullptr, 0);" in \
        body(source, "void reset_ssh_output_filter()")
    # Every discard site funnels through the epoch, never through a bare
    # counter: the data producer only ever loses data, while the state producer
    # can lose data (eviction) or a state (state-only overflow).
    assert ssh_data_callback.count("mark_ssh_event_discarded(s_ssh_data_queue_drop_count)") == 3
    assert "s_ssh_state_queue_drop_count" not in ssh_data_callback
    assert ssh_state_callback.count("mark_ssh_event_discarded(s_ssh_data_queue_drop_count)") == 1
    assert ssh_state_callback.count("mark_ssh_event_discarded(s_ssh_state_queue_drop_count)") == 2
    for discarder in (ssh_data_callback, ssh_state_callback):
        assert "compare_exchange_weak" not in discarder
    # Overflow, rejection and eviction each publish a fresh epoch.
    # REQ-SSH-OUTPUT-02: a chunk above the transport contract is rejected
    # fail-closed *before* any copy, so no partial prefix is enqueued and the
    # queue never holds a spliced chunk.  The inert guard runs first, the
    # rejection comes next and returns, and only then is `length` trusted.
    inert = ssh_data_callback.index("data == nullptr || length == 0")
    reject = ssh_data_callback.index("if (length > k_ssh_event_data_limit) {")
    reject_return = ssh_data_callback.index("return;", reject)
    stamp = ssh_data_callback.index("event.discard_epoch = current_ssh_discard_epoch();")
    length_assign = ssh_data_callback.index("event.length = static_cast<uint16_t>(length);")
    copy = ssh_data_callback.index("std::memcpy(event.data, data, event.length)")
    assert inert < reject < reject_return < length_assign < copy
    assert stamp < length_assign
    # The rejection publishes the epoch without stamping an event, because no
    # event is created: one stamp for the delivered chunk only.
    assert ssh_data_callback.count("event.discard_epoch = current_ssh_discard_epoch();") == 1
    # No silent truncation anywhere: the retained prefix of an oversized chunk
    # is exactly what must NOT reach the scrollback.
    assert "static_cast<uint16_t>(k_ssh_event_data_limit)" not in ssh_data_callback
    assert ssh_state_callback.count("event.discard_epoch = current_ssh_discard_epoch();") == 2
    # T-GAP-03: the stale-generation filter runs BEFORE the gap guard, so a
    # stale event can neither reach the scrollback nor consume the epoch.
    skip = ssh_events.index("if (s_ssh_event_slot.generation != s_ssh_expected_generation) continue;")
    guard = ssh_events.index("if (s_ssh_event_slot.discard_epoch > s_ssh_applied_discard_epoch) {")
    assert skip < guard
    assert "reset_ssh_output_filter();" in ssh_events
    assert "s_ssh_applied_discard_epoch = s_ssh_event_slot.discard_epoch;" in ssh_events
    # T-GAP-04: the guard resynchronises through the bounded-tail entry point,
    # once per advance.  The reset alone (flush to GROUND) is not enough: a gap
    # also has to drop the uncertain `[0-9;]*m` remainder, which is what
    # resync_after_gap() does and what keeps `34m` off the screen.  The budget
    # is a header constant, so pinning it here pins the device build too.
    assert ssh_events.count("s_ssh_output_filter.resync_after_gap();") == 1
    assert guard < ssh_events.index("s_ssh_output_filter.resync_after_gap();")
    assert "void resync_after_gap();" in filter_implementation
    assert "static constexpr unsigned char k_gap_parameter_limit = 16;" in filter_implementation
    filter_filter_source = FILTER_SRC.read_text(encoding="utf-8")
    assert "void cyberdeck_terminal_filter::resync_after_gap()" in filter_filter_source
    assert "m_discard_gap_tail = true;" in filter_filter_source
    assert "m_gap_parameter_bytes >= k_gap_parameter_limit" in filter_filter_source
    # The gap resynchronises the ANSI filter only: the echo guard/composer stays
    # armed, because a gap must never suppress or duplicate a legitimate echo.
    assert ssh_events.count("reset_ssh_output_filter()") == 1
    assert "discard_ssh_line_composer" not in ssh_events
    assert "s_ssh_line_composer" not in ssh_events
    # A successful connect adopts the current epoch so a new session never
    # inherits a pending reset; a failed one adopts nothing.
    connect = body(source, "esp_err_t shell_session_host::ssh_connect(")
    assert connect.index("if (result == ESP_OK) {") < \
        connect.index("s_ssh_applied_discard_epoch = current_ssh_discard_epoch();")
    assert connect.count("s_ssh_applied_discard_epoch = current_ssh_discard_epoch();") == 1
    # REQ-03/AC-03: CSI/OSC of 8 bits is not treated by production, so nothing
    # in the filter may claim it.  `C2 9B` stays opaque text, guarded by
    # test_terminal_filter (`test_eight_bit_csi_bytes_stay_opaque_text`) and by
    # the behavioural harness (`test_ssh_output_gap.py`).
    assert "0x9B" not in filter_implementation and "0x9D" not in filter_implementation
    # The behavioural seam harness compiles these same statements against the
    # real filter and composer, so the guard above is not the only evidence.
    gap_behavior = ROOT / "tests/host/keymap/test_ssh_output_gap.py"
    gap_source = gap_behavior.read_text(encoding="utf-8")
    for scenario in ("req01_ansi_prompt_is_lossless",
                     "req02_gap_discards_the_uncertain_ansi_tail",
                     "req02_gap_discards_the_bounded_parameter_tail",
                     "req02_repeated_gaps_rearm_the_tail_discard",
                     "req02_gap_guard_fires_once_per_epoch",
                     "req02_state_eviction_is_a_signalled_gap",
                     "req03_utf8_and_eight_bit_csi_are_opaque_text",
"req04_stale_generation_is_skipped_before_the_gap_guard",
                      "req04_transport_chunks_up_to_the_limit_arrive_intact",
                      "req04_oversized_payload_is_rejected_fail_closed_and_signalled",
                      "req04_full_chunks_respect_the_scrollback_budget",
                      "req04_state_only_queue_keeps_counters_separate",
                     "req04_connect_adopts_the_current_discard_epoch",
                     "req04_gap_reset_preserves_the_payload_and_echo_invariants",
                     "req04_only_the_explicit_flush_disarms_the_composer"):
        assert scenario in gap_source, f"missing gap scenario {scenario}"
    for scenario in ("test_ansi_prompt_is_lossless_and_partition_invariant",
                     "test_eight_bit_csi_bytes_stay_opaque_text",
                     "test_gap_resync_discards_only_the_bounded_ansi_tail"):
        assert scenario in filter_source, f"missing filter scenario {scenario}"

    print("PASS: terminal output structural contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, IndexError, ValueError, OSError, UnicodeError) as error:
        print(f"FAIL: {error or type(error).__name__}")
        raise SystemExit(1)
