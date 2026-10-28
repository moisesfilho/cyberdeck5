#include "platform/display/cyberdeck_header_view.h"
#include "platform/display/cyberdeck_terminal_view.h"
#include "platform/display/cyberdeck_editor_view.h"
#include "platform/display/cyberdeck_music_view.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

/* The host fixture only supplies the font symbol.  Its metrics are not a
 * contract: the test compares the clock and percentage labels directly. */
extern const lv_font_t cyberdeck_font{18};

static void noop(lv_event_t *) {}

static int geometry_events = 0;
static void geometry_changed(lv_event_t *) { ++geometry_events; }
static std::string editor_action;
static void editor_changed(std::string_view action, void *) { editor_action = std::string(action); }
struct music_action_log {
    std::vector<std::string> actions;
    void *expected_context{};
    int context_calls{};
};
static void record_music_action(std::string_view action, void *context)
{
    auto *log = static_cast<music_action_log *>(context);
    log->actions.emplace_back(action);
    if (context == log->expected_context) ++log->context_calls;
}

static void assert_visible_lines_fit(const cyberdeck_terminal_view::view &terminal,
                                     int32_t width)
{
    const auto *surface = terminal.scrollback();
    for (const auto *line : surface->children) {
        if (!line->hidden)
            assert(lv_shim_text_width(&cyberdeck_font, line->text) <= width);
    }
}

static std::string last_visible_text(const cyberdeck_terminal_view::view &terminal)
{
    const auto *surface = terminal.scrollback();
    for (auto it = surface->children.rbegin(); it != surface->children.rend(); ++it)
        if (!(*it)->hidden) return (*it)->text;
    return {};
}

static std::vector<std::string> visible_line_texts(
    const cyberdeck_terminal_view::view &terminal, const lv_obj_t *cursor)
{
    std::vector<std::string> lines;
    for (const auto *line : terminal.scrollback()->children)
        if (line != cursor && !line->hidden) lines.push_back(line->text);
    return lines;
}

static const lv_obj_t *visible_line_with_text(
    const cyberdeck_terminal_view::view &terminal, const std::string &text)
{
    for (const auto *line : terminal.scrollback()->children)
        if (!line->hidden && line->text == text) return line;
    return nullptr;
}

static const lv_obj_t *cursor_overlay(const cyberdeck_terminal_view::view &terminal)
{
    for (const auto *child : terminal.scrollback()->children)
        if (child->bg_opa == LV_OPA_COVER) return child;
    return nullptr;
}

static void touch_swipe(cyberdeck_terminal_view::view &terminal, lv_indev_t &touch,
                        int32_t start_y, int32_t end_y)
{
    lv_shim_set_pointer(&touch, start_y);
    lv_shim_emit_event(terminal.scrollback(), LV_EVENT_PRESSED);
    lv_shim_set_pointer(&touch, end_y);
    lv_shim_emit_event(terminal.scrollback(), LV_EVENT_RELEASED);
}

int main()
{
    lv_obj_t screen{};
    cyberdeck_header_view::view header;
    assert(!header.create(nullptr));
    assert(header.create(&screen));
    assert(screen.children.size() == 1);
    const auto *header_bar = screen.children[0];
    assert(header_bar->height == 42);
    assert(header_bar->children.size() == 3);
    const auto *title = header_bar->children[0];
    const auto *clock = header_bar->children[1];
    const auto *right = header_bar->children[2];
    assert(title->width == LV_PCT(30));
    assert(clock->width == LV_PCT(40));
    assert(right->width == LV_PCT(30) && right->height == 42);
    assert(header_bar->flex_main == LV_FLEX_ALIGN_START);
    assert(header_bar->flex_cross == LV_FLEX_ALIGN_CENTER);
    assert(header_bar->flex_track == LV_FLEX_ALIGN_CENTER);
    assert(clock->text_align == LV_TEXT_ALIGN_CENTER);
    assert(clock->text_font == &cyberdeck_font);
    assert(clock->long_mode == LV_LABEL_LONG_CLIP);
    /* REQ-HEADER-FONT-01 / AC-HEADER-FONT-01: every header text label uses
     * the exact shared cyberdeck_font object. */
    assert(title->text_font == &cyberdeck_font);
    assert(header_bar->children[2]->children[0]->children[0]->text_font ==
           &cyberdeck_font);
    assert(lv_font_get_line_height(title->text_font) ==
           lv_font_get_line_height(clock->text_font));
    assert(lv_font_get_line_height(
               header_bar->children[2]->children[0]->children[0]->text_font) ==
           lv_font_get_line_height(clock->text_font));
    const auto geometry_before_long_text = std::array<int32_t, 6>{
        title->width, clock->width, right->width, right->height,
        clock->x, clock->y};
    header.update_clock(nullptr);
    header.update_clock("12:34");
    header.update_clock("2026-10-04 23:59:59 UTC+00:00 / LONG STATUS TEXT");
    const auto geometry_after_long_text = std::array<int32_t, 6>{
        title->width, clock->width, right->width, right->height,
        clock->x, clock->y};
    assert(geometry_after_long_text == geometry_before_long_text);
    header.update_ble(false); header.update_ble(true); header.update_ble(false);
    /* TEST-AUDIO-26/27: real header view keeps the global music action hidden
     * while idle, exposes it for active/paused playback, and dispatches the
     * registered action exactly once. */
    assert(right->children.size() == 4);
    const auto *music_button = right->children[3];
    assert(music_button->hidden);
    assert(music_button->width == 26 && music_button->height == 36);
    assert(music_button->children.size() == 1);
    assert(!music_button->children[0]->text.empty());
    int music_action_calls = 0;
    const auto music_action = [](void *context) { ++*static_cast<int *>(context); };
    header.set_music_action(music_action, &music_action_calls);
    lv_shim_emit_event(const_cast<lv_obj_t *>(music_button), LV_EVENT_CLICKED);
    assert(music_action_calls == 1);
    header.update_music(false);
    assert(music_button->hidden);
    header.update_music(true);
    assert(!music_button->hidden);
    header.dispatch_music_action();
    assert(music_action_calls == 2);
    header.update_music(false);
    assert(music_button->hidden);
    header.update_wifi(false); header.update_wifi(true);
    cyberdeck_battery_view::presentation presentation{};
    header.update_battery(presentation);
    assert(right->children.size() == 4);
    assert(right->children[0]->hidden);
    assert(right->children[1]->children.size() == 4);
    assert(right->children[2]->hidden);
    presentation.visible = true; presentation.show_percentage = true;
    presentation.percentage = 87; presentation.glyph = cyberdeck_battery_view::power_glyph::battery;
    header.update_battery(presentation);
    assert(!right->children[2]->hidden);
    assert(right->children[2]->children[0]->text == "BAT");
    assert(right->children[2]->children[1]->text == "87%");
    const auto *battery_percentage = right->children[2]->children[1];
    const auto *battery_symbol = right->children[2]->children[0];
    /* REQ-HEADER-FONT-02 / AC-HEADER-FONT-01: battery symbol and percentage
     * share the exact font object and line-height with the clock. */
    assert(battery_symbol->text_font == &cyberdeck_font);
    assert(battery_percentage->text_font == &cyberdeck_font);
    assert(battery_symbol->text_font == clock->text_font);
    assert(battery_percentage->text_font == clock->text_font);
    assert(lv_font_get_line_height(battery_symbol->text_font) ==
           lv_font_get_line_height(clock->text_font));
    assert(lv_font_get_line_height(battery_percentage->text_font) ==
           lv_font_get_line_height(clock->text_font));
    /* REQ-HEADER-FONT-03 / AC-HEADER-FONT-01: bitmap A4 fonts use the real
     * supported overlay emulation. The full shim exposes no outline API, so
     * an outline-based implementation cannot compile this contract. */
    assert(right->children[2]->children.size() == 3);
    const auto *battery_percentage_bold = right->children[2]->children[2];
    assert(battery_percentage_bold->text == "87%");
    assert(battery_percentage_bold->text_font == battery_percentage->text_font);
    assert(battery_percentage_bold->ignore_layout);
    assert(battery_percentage_bold->x == battery_percentage->x + 1);
    assert(battery_percentage_bold->y == battery_percentage->y);
    assert(!battery_percentage_bold->hidden);
    /* AC-HEADER-FONT-01: preserve text, visibility and header layout. */
    presentation.show_percentage = false; presentation.glyph = cyberdeck_battery_view::power_glyph::external;
    header.update_battery(presentation);
    assert(right->children[2]->children[0]->text == "-");
    assert(right->children[2]->children[1]->text.empty());
    assert(right->children[2]->children[2]->text.empty());
    assert(right->children[2]->children[2]->hidden);

    cyberdeck_terminal_view::view terminal;
    /* TEST-REG-8-BAR: the composed root is a flex column, so the terminal
     * textarea lives in the content row and the virtual keyboard is a floating
     * overlay on the root.  These run the real production statements, so they
     * fail if the keyboard stops being excluded from the column. */
    lv_obj_t content{};
    content.width = LV_PCT(100);
    content.height = 100;
    assert(!terminal.create(nullptr, &screen, 64, {}));
    cyberdeck_terminal_view::callbacks callbacks{noop, noop, noop, noop, noop,
                                                  geometry_changed};
    assert(terminal.create(&screen, &content, 256, callbacks));
    assert(terminal.textarea() != nullptr && terminal.keyboard() != nullptr);
    /* REQ-LAYOUT-01 / AC-LAYOUT-01: the visible terminal surface has no
     * border, while its hidden LVGL input target keeps the textarea style. */
    assert(terminal.scrollback()->border_width == 0);
    assert(terminal.textarea()->parent == &content);
    assert(terminal.textarea()->hidden);
    assert(terminal.textarea()->border_width == 1);
    assert(terminal.textarea()->pad_top == 12);
    assert(terminal.textarea()->pad_bottom == 12);
    assert(terminal.textarea()->pad_left == 12);
    assert(terminal.textarea()->pad_right == 12);
    assert(terminal.keyboard()->parent == &screen);
    assert(lv_obj_is_ignore_layout(terminal.keyboard()));
    assert(terminal.keyboard()->hidden);
    /* Showing the overlay must not make it take a row or a size of the column. */
    assert(terminal.keyboard()->width == 0 && terminal.keyboard()->height == 0);
    assert(terminal.textarea()->scroll_dir == LV_DIR_ALL);
    assert(!terminal.textarea()->scroll_chain);
    assert(terminal.textarea()->scrollbar_mode == LV_SCROLLBAR_MODE_OFF);
    assert(terminal.keyboard()->scroll_dir == LV_DIR_NONE);

    /* TEST-CURSOR-01/02: cursor starts visible, uses a block with inverted
     * glyph colors, and blink toggles visibility without altering input text. */
    const auto *cursor = cursor_overlay(terminal);
    assert(cursor != nullptr);
    auto *blink_timer = lv_shim_timers().back();
    terminal.scrollback()->width = 72;
    terminal.scrollback()->height = 120;
    terminal.render("ABCD", 0);
    const auto cursor_line_y = [&]() {
        for (const auto *line : terminal.scrollback()->children)
            if (line != cursor && !line->hidden && line->text == "ABCD") return line->y;
        return -1;
    };
    assert(!cursor->hidden && cursor->text == "A");
    assert(cursor->x == 0 && cursor->y == cursor_line_y() && cursor->width == 9);
    assert(cursor->color == 0xF2F2F2 && cursor->text_color == 0x0A0A0A);
    assert(cursor->bg_opa == LV_OPA_COVER);
    terminal.toggle_cursor();
    assert(cursor->hidden);
    terminal.toggle_cursor();
    assert(!cursor->hidden);
    lv_shim_fire_timer(blink_timer);
    assert(cursor->hidden);
    lv_shim_fire_timer(blink_timer);
    assert(!cursor->hidden && terminal.textarea()->text.empty());

    /* TEST-CURSOR-03: cursor follows cyberdeck_font advance at start, within a
     * line, and after UTF-8 glyphs; reflow uses codepoint boundaries. */
    terminal.render("ABCD", 2);
    assert(cursor->x == 18 && cursor->y == cursor_line_y());
    terminal.render("ABCD", 4);
    assert(cursor->x == 36 && cursor->y == cursor_line_y() && cursor->text == " ");
    terminal.render("ABCDE\xC3\xA9" "Z", 5);
    assert(cursor->text == u8"é" && cursor->x == 0 && cursor->y == 72);
    terminal.render("ABCDE\xC3\xA9" "Z", 7);
    assert(cursor->text == "Z" && cursor->x == 18 && cursor->y == 72);
    terminal.render("AB\nCD", 3);
    assert(cursor->text == "C" && cursor->x == 0 && cursor->y == 72);

    /* TEST-CURSOR-04: end-of-line at the right margin wraps to the next row;
     * an out-of-range cursor is hidden and cannot leave stale overlay visible. */
    terminal.scrollback()->width = 60;
    terminal.render("ABCDE", 4);
    assert(cursor->x == 0 && cursor->y == 72);
    terminal.render("ABCDE", 99);
    assert(cursor->hidden);
    terminal.render("input", std::string::npos);
    assert(cursor->hidden);

    /* TEST-TERM-ROT-01/04: both portrait and landscape geometries reflow a
     * long unbroken word and UTF-8 without exposing a clipped line. */
    terminal.scrollback()->width = 72;
    terminal.scrollback()->height = 80;
    terminal.render("supercalifragilistic\n\xC3\xA9\xE7\x8C\xAB\nlast");
    assert_visible_lines_fit(terminal, lv_obj_get_content_width(terminal.scrollback()));
    const std::size_t portrait_capacity = terminal.viewport_capacity();
    terminal.scrollback()->width = 240;
    terminal.scrollback()->height = 400;
    lv_shim_emit_event(terminal.scrollback(), LV_EVENT_SIZE_CHANGED);
    assert(geometry_events == 1);
    terminal.render("supercalifragilistic\n\xC3\xA9\xE7\x8C" "\xAB\nlast");
    assert_visible_lines_fit(terminal, lv_obj_get_content_width(terminal.scrollback()));
    assert(terminal.viewport_capacity() > portrait_capacity);
    assert(terminal.viewport_capacity() <= 4096);

    /* BUG-TERM-WRAP-01 / REQ-TERM-WRAP-01: automatic width breaks preserve
     * every byte, including UTF-8 codepoints, while explicit newlines remain
     * separators.  This catches the old end-exclusive calculation that
     * dropped the last character of each automatically wrapped segment. */
    terminal.scrollback()->width = 72;
    terminal.scrollback()->height = 130;
    terminal.render("ABCDE\xC3\xA9" "FG\nHIJ\nKLMNOP");
    const std::vector<std::string> wrapped_lines = visible_line_texts(terminal, cursor);
    const std::vector<std::string> expected_wrapped_lines{
        "ABCDE", "\xC3\xA9" "FG", "HIJ", "KLMNO", "P"};
    assert(wrapped_lines.size() == expected_wrapped_lines.size());
    assert(wrapped_lines == expected_wrapped_lines);
    assert(wrapped_lines[0] + wrapped_lines[1] + "\n" + wrapped_lines[2] +
               "\n" + wrapped_lines[3] + wrapped_lines[4] ==
           "ABCDE\xC3\xA9" "FG\nHIJ\nKLMNOP");

    /* Return to the landscape geometry used by the following viewport tests. */
    terminal.scrollback()->width = 240;

    /* TEST-TERM-ROT-02: resizing recomputes the line count and preserves the
     * complete newest line at the bottom of the viewport. */
    const std::size_t children_before_resize = terminal.scrollback()->children.size();
    terminal.render("one\ntwo\nthree\nfour\nfive\nsix");
    assert(last_visible_text(terminal) == "six");
    terminal.scrollback()->height = 48;
    terminal.render("one\ntwo\nthree\nfour\nfive\nsix");
    assert(last_visible_text(terminal) == "six");
    assert(terminal.scrollback()->children.size() == children_before_resize);

    /* TEST-TERM-ROT-03/04: empty lines, a trailing newline and UTF-8 survive
     * the same render path used after textarea/keyboard overlay activity. */
    terminal.scrollback()->height = 100;
    terminal.render("A\n\n\xE2\x98\x83\n");
    assert(terminal.scrollback()->children[0]->text == "A");
    assert(terminal.scrollback()->children[1]->text.empty());
    assert(terminal.scrollback()->children[2]->text == "\xE2\x98\x83");
    assert(terminal.textarea()->hidden && terminal.keyboard()->hidden);

    /* TEST-TERM-ROT-05: repeated rendering reuses the fixed line objects and
     * keeps bottom anchoring instead of accumulating labels. */
    std::array<const lv_obj_t *, 4> line_objects{};
    for (std::size_t i = 0; i < line_objects.size(); ++i)
        line_objects[i] = terminal.scrollback()->children[i];
    terminal.render("repeat\nrender\nrepeat\nrender\nBOTTOM");
    terminal.render("repeat\nrender\nrepeat\nrender\nBOTTOM");
    assert(terminal.scrollback()->children.size() == children_before_resize);
    assert(last_visible_text(terminal) == "BOTTOM");
    for (std::size_t i = 0; i < line_objects.size(); ++i)
        assert(line_objects[i] == terminal.scrollback()->children[i]);

    terminal.render("one");
    const auto *surface = terminal.scrollback();
    assert(surface->children.size() >= 4);
    /* TEST-LAT04-01: a short transcript is bottom anchored, not top padded. */
    assert(surface->children[3]->text == "one");
    terminal.render("one\ntwo\nthree\nfour\nfive");
    /* TEST-LAT04-02: excess output keeps the newest lines in the fixed window. */
    assert(surface->children[0]->text == "two");
    assert(surface->children[3]->text == "five");

    /* TEST-TERM-SCROLL-01: touch is the only navigation input.  A large
     * downward gesture reaches the oldest bounded window, clamps there, and an
     * upward gesture returns to the bottom anchor. */
    /* TEST-TERM-SCROLL-06: the same downward gesture is the bottom-anchoring
     * regression check at the end of the transcript. */
    terminal.scrollback()->width = 240;
    terminal.scrollback()->height = 72;
    const std::string transcript =
        "zero\none\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine";
    terminal.render(transcript);
    lv_indev_t touch{};
    touch_swipe(terminal, touch, -120, 120);
    std::vector<std::string> oldest = visible_line_texts(terminal, cursor);
    assert(oldest.size() == 2);
    assert(oldest[0] == "zero" && oldest[1] == "one");
    touch_swipe(terminal, touch, -120, 120);
    assert(visible_line_texts(terminal, cursor) == oldest);
    touch_swipe(terminal, touch, 120, -120);
    const std::vector<std::string> newest = visible_line_texts(terminal, cursor);
    assert(newest.size() == 2);
    assert(newest[0] == "eight" && newest[1] == "nine");
    touch_swipe(terminal, touch, 120, -120);
    assert(visible_line_texts(terminal, cursor) == newest);

    /* TEST-AUDIO-28: with preceding shell scrollback, the appended music panel
     * remains at the bottom of the real terminal view and survives navigation
     * away from, then back to, the newest viewport. */
    const std::string panel_transcript =
        "old-0\nold-1\nold-2\nold-3\nold-4\nold-5\n[MUSIC PANEL]\nEscape: return\n";
    terminal.render(panel_transcript);
    std::vector<std::string> panel_bottom = visible_line_texts(terminal, cursor);
    assert(std::find(panel_bottom.begin(), panel_bottom.end(), "Escape: return") != panel_bottom.end());
    touch_swipe(terminal, touch, -120, 120);
    const std::vector<std::string> panel_oldest = visible_line_texts(terminal, cursor);
    assert(!panel_oldest.empty() && panel_oldest.front() == "old-0");
    touch_swipe(terminal, touch, 120, -120);
    panel_bottom = visible_line_texts(terminal, cursor);
    assert(std::find(panel_bottom.begin(), panel_bottom.end(), "Escape: return") != panel_bottom.end());

    /* TEST-CURSOR-05 / REQ-LAT04-08: scrolling away hides the editing cursor, blink ticks
     * cannot resurrect it over historical output, and returning to bottom
     * restores the saved byte position on the current editor line. */
    const std::string editable_transcript = transcript + "\ninput";
    terminal.render(editable_transcript, editable_transcript.size());
    const auto editing_lines = visible_line_texts(terminal, cursor);
    assert(editing_lines.back() == "input");
    const auto *editing_line = static_cast<const lv_obj_t *>(nullptr);
    for (const auto *line : terminal.scrollback()->children)
        if (line != cursor && !line->hidden && line->text == "input") editing_line = line;
    assert(editing_line != nullptr);
    assert(editing_line->text_font == &cyberdeck_font);
    assert(cursor->text_font == &cyberdeck_font);
    assert(!cursor->hidden && cursor->x == 45 && cursor->y == editing_line->y);
    touch_swipe(terminal, touch, -120, 120);
    assert(cursor->hidden);
    for (const auto *child : terminal.scrollback()->children)
        if (!child->hidden) assert(child->bg_opa != LV_OPA_COVER);
    lv_shim_fire_timer(blink_timer);
    lv_shim_fire_timer(blink_timer);
    assert(cursor->hidden);
    assert(visible_line_with_text(terminal, "input") == nullptr);
    touch_swipe(terminal, touch, 120, -120);
    const auto restored_lines = visible_line_texts(terminal, cursor);
    assert(restored_lines.size() == 2 && restored_lines.back() == "input");
    const auto *restored_editing_line = visible_line_with_text(terminal, "input");
    assert(restored_editing_line != nullptr && restored_editing_line != cursor);
    assert(restored_editing_line->text_font == &cyberdeck_font);
    assert(!cursor->hidden && cursor->x == 45 &&
           cursor->y == restored_editing_line->y);
    /* TEST-TERM-SCROLL-02/03: UTF-8 remains intact in the navigable window,
     * while the input target stays hidden and the visible prompt surface is
     * unaffected by touch navigation. */
    terminal.render("prompt: \xC3\xA9\neditor: \xE7\x8C\xAB\nend");
    touch_swipe(terminal, touch, -120, 120);
    const std::vector<std::string> utf8_window = visible_line_texts(terminal, cursor);
    assert(utf8_window.size() == 2);
    assert(utf8_window[0] == "prompt: \xC3\xA9");
    assert(utf8_window[1] == "editor: \xE7\x8C\xAB");
    assert(terminal.textarea()->hidden);
    assert(terminal.keyboard()->hidden);
    assert(terminal.textarea()->text.empty());

    /* TEST-TERM-SCROLL-03: new output and resize preserve bounded slots. */
    terminal.render("prompt: \xC3\xA9\neditor: \xE7\x8C\xAB\nend\nnew output");
    assert(terminal.scrollback()->children.size() >= 2);
    terminal.scrollback()->height = 108;
    lv_shim_emit_event(terminal.scrollback(), LV_EVENT_SIZE_CHANGED);
    terminal.render("prompt: \xC3\xA9\neditor: \xE7\x8C\xAB\nend\nnew output");
    assert_visible_lines_fit(terminal, lv_obj_get_content_width(terminal.scrollback()));
    /* TEST-TERM-SCROLL-04: this visual window is deliberately not the source
     * of term.dump; its complete-output contract is tested independently. */
    assert(terminal.textarea()->text.empty());
    const bool cursor_hidden_before_deinit = cursor->hidden;
    terminal.deinit();
    assert(blink_timer->deleted);
    lv_shim_fire_timer(blink_timer);
    assert(cursor->hidden == cursor_hidden_before_deinit);
    cyberdeck_terminal_view::view no_callbacks;
    assert(no_callbacks.create(&screen, &content, 0, {}));
    /* TEST-EDIT-HOST-01: compile and exercise the real editor view TU. */
    cyberdeck_editor_view::view editor;
    screen.width = 640;
    screen.height = 300;
    assert(!editor.create(nullptr, editor_changed, nullptr));
    assert(editor.create(&screen, editor_changed, nullptr));
    assert(editor.surface()->children.size() == 65);
    editor.set_visible(true);
    assert(editor.visible() && !editor.surface()->hidden);

    /* TEST-EDIT-FOOTER-HOST: one footer row stays below the document and the
     * document capacity is derived from the current surface height. */
    editor.surface()->width = 320;
    editor.surface()->height = 100;
    lv_shim_emit_event(editor.surface(), LV_EVENT_SIZE_CHANGED);
    editor.render("one\ntwo\nthree", 0, false, "status");
    const int footer_line_height = lv_font_get_line_height(&cyberdeck_font);
    const auto *status_label = editor.surface()->children[64];
    assert(lv_obj_get_y(status_label) ==
           lv_obj_get_content_height(editor.surface()) - footer_line_height);
    assert(lv_obj_get_y(editor.surface()->children[1]) + footer_line_height <=
           lv_obj_get_y(status_label));
    assert(editor.surface()->children[4]->hidden);
    assert(std::string(status_label->text) == "  status");

    /* TEST-EDIT-FOOTER-ENCODING: all supported codecs share the single line. */
    for (const std::string encoding : {"UTF-8", "UTF-16LE", "UTF-16BE", "Windows-1252"}) {
        editor.render("one", 0, false, "Ctrl+F Buscar  Ctrl+S Salvar  Ctrl+Q Fechar | " + encoding);
        assert(editor.surface()->children.size() == 65);
        assert(std::string(status_label->text).find("Ctrl+F Buscar") != std::string::npos);
        assert(std::string(status_label->text).find("Ctrl+S Salvar") != std::string::npos);
        assert(std::string(status_label->text).find("Ctrl+Q Fechar") != std::string::npos);
        assert(std::string(status_label->text).find(" | " + encoding) != std::string::npos);
        assert(std::string(status_label->text).find('\n') == std::string::npos);
        assert(std::string(status_label->text).size() <= 256);
    }
    /* TEST-EDIT-FOOTER-BOUNDARY: truncation preserves the encoding suffix. */
    editor.render("one", 0, true, std::string(400, 'x') + " | UTF-16BE");
    assert(std::string(status_label->text).size() <= 256);
    assert(std::string(status_label->text).find(" | UTF-16BE") != std::string::npos);
    assert(std::string(status_label->text).find('\n') == std::string::npos);

    /* REQ-PLAYER-01 / AC-PLAYER-06,12: execute the production music view TU
     * through the same full LVGL shim as the other display surfaces. */
    cyberdeck_music_view::view music_view;
    assert(!music_view.create(nullptr, record_music_action, nullptr));
    assert(!music_view.visible());
    music_action_log music_log;
    music_log.expected_context = &music_log;
    assert(music_view.create(&screen, record_music_action, &music_log));
    assert(music_view.create(&screen, record_music_action, &music_log));
    assert(screen.children.back()->children.size() == 8);
    auto *music_surface = screen.children.back();
    assert(music_surface->width == LV_PCT(94) && music_surface->height == LV_PCT(72));
    assert(music_surface->hidden && !music_view.visible());
    const std::array<std::string, 7> music_labels{
        "Previous", "Play/Pause", "Next", "Seek -10s", "Seek +10s",
        "Volume -", "Volume +"};
    const std::array<std::string, 7> music_actions{
        "prev", "toggle", "next", "seek_back", "seek_forward",
        "volume_down", "volume_up"};
    for (std::size_t i = 0; i < music_labels.size(); ++i) {
        auto *button = music_surface->children[i + 1];
        assert(button->text == music_labels[i]);
        assert(button->event_callbacks.size() == 1);
        lv_shim_emit_event(button, LV_EVENT_CLICKED);
        assert(music_log.actions.back() == music_actions[i]);
    }
    assert(music_log.actions.size() == 7 && music_log.context_calls == 7);
    music_surface->children[1]->text = "unrecognized";
    lv_shim_emit_event(music_surface->children[1], LV_EVENT_CLICKED);
    assert(music_log.actions.size() == 7);
    music_surface->children[1]->text = music_labels[0];
    cyberdeck_music_view::view no_music_callback;
    assert(no_music_callback.create(&screen, nullptr, nullptr));
    auto *no_callback_surface = screen.children.back();
    lv_shim_emit_event(no_callback_surface->children[1], LV_EVENT_CLICKED);
    no_music_callback.destroy();
    music_view.render("Now playing: track.mp3");
    assert(music_surface->children[0]->text == "Now playing: track.mp3");
    music_view.render("");
    assert(music_surface->children[0]->text.empty());
    music_view.set_visible(true);
    assert(music_view.visible() && !music_surface->hidden);
    music_view.set_visible(false);
    assert(!music_view.visible() && music_surface->hidden);
    music_view.destroy();
    assert(!music_view.visible());
    music_view.destroy();
    assert(!music_view.create(nullptr, record_music_action, &music_log));
    assert(music_view.create(&screen, record_music_action, &music_log));
    assert(music_view.visible() == false);
    music_view.destroy();

    /* TEST-EDIT-FOOTER-HOST/CONTRACT: keyboard inset and orientation resize
     * recalculate the footer without overlap or stale document slots. */
    editor.set_bottom_inset(40);
    assert(lv_obj_get_height(editor.surface()) ==
           lv_obj_get_content_height(&screen) - 40);
    editor.render("one\ntwo\nthree\nfour", 0, false, "inset");
    assert(lv_obj_get_y(status_label) + footer_line_height <=
           lv_obj_get_content_height(editor.surface()));
    editor.surface()->width = 640;
    editor.surface()->height = 220;
    lv_shim_emit_event(editor.surface(), LV_EVENT_SIZE_CHANGED);
    editor.set_bottom_inset(0);
    editor.surface()->height = 220;
    lv_shim_emit_event(editor.surface(), LV_EVENT_SIZE_CHANGED);
    editor.render("one\ntwo\nthree\nfour", 0, false, "landscape");
    assert(!editor.surface()->children[3]->hidden);
    assert(lv_obj_get_y(status_label) >=
           lv_obj_get_y(editor.surface()->children[3]) + footer_line_height);

    /* TEST-EDIT-FOOTER-HOST: fixed slots, cursor and scroll are bounded. */
    std::string many_lines;
    for (int line = 0; line < 100; ++line) {
        if (line != 0) many_lines += '\n';
        many_lines += "bounded";
    }
    editor.render(many_lines, many_lines.size() + 1000, true, "bounded");
    assert(editor.surface()->children.size() == 65);
    for (int i = 0; i < 200; ++i) editor.gesture_scroll(-1000);
    editor.render(many_lines, 0, true, "oldest");
    assert(editor.surface()->children[0]->text == "|bounded");
    for (int i = 0; i < 200; ++i) editor.gesture_scroll(1000);
    editor.render(many_lines, many_lines.size(), true, "newest");
    assert(editor.surface()->children[0]->text == "bounded");

    /* TEST-EDIT-FOOTER-SERIAL/REGRESSION: touch still emits scroll and
     * teardown clears ownership without leaving a live view. */
    editor.render("alpha\n\xC3\xA9", 5, true, "saved");
    assert(editor.surface()->children[0]->text == "alpha|");
    assert(editor.surface()->children[1]->text == "\xC3\xA9");
    lv_indev_t editor_touch{};
    lv_shim_set_pointer(&editor_touch, 120);
    lv_shim_emit_event(editor.surface(), LV_EVENT_PRESSED);
    lv_shim_set_pointer(&editor_touch, 0);
    lv_shim_emit_event(editor.surface(), LV_EVENT_RELEASED);
    assert(editor_action == "scroll");
    editor.destroy();
    assert(!editor.visible() && editor.surface() == nullptr);
    std::cout << "display view tests passed\n";
}
