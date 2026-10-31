#include "platform/display/cyberdeck_header_view.h"
#include "platform/display/cyberdeck_terminal_view.h"
#include "platform/display/cyberdeck_editor_view.h"
#include "platform/display/cyberdeck_music_view.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <string>
#include <utility>
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
    lv_obj_set_size(&screen, 720, 1280);
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
    /* The light default theme rounds and shadows every `lv_btn`; the transparent
     * header button must opt out of both or the body paints a capsule. */
    assert(music_button->radius == 0);
    assert(music_button->outline_width == 0);
    assert(music_button->shadow_width == 0);
    assert(music_button->shadow_opa == 0);
    assert(music_button->children.size() == 1);
    /* The button label is painted with the shared font, which is the premise
     * the glyph-coverage contract relies on (`lv_obj_center` is a no-op in the
     * shim, so the centring itself is not asserted here). */
    assert(music_button->children[0]->text_font == &cyberdeck_font);
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
     * supported overlay emulation. The shim only records `outline_width` so a
     * zeroed theme chrome can be asserted; it has no outline renderer, so an
     * outline-based bold emulation is still not covered here. */
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
    lv_obj_set_size(&screen, 720, 1280);
    lv_obj_set_layout(&screen, LV_LAYOUT_FLEX);
    lv_obj_t music_root{};
    lv_obj_set_size(&music_root, 720, 1280);
    lv_obj_set_scroll_dir(&music_root, LV_DIR_NONE);
    lv_obj_set_scroll_chain(&music_root, false);
    lv_obj_set_scrollbar_mode(&music_root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_t system_bar{};
    system_bar.parent = &music_root;
    lv_obj_set_pos(&system_bar, 12, 12);
    lv_obj_set_size(&system_bar, 696, 42);
    lv_obj_set_scroll_dir(&system_bar, LV_DIR_NONE);
    lv_obj_set_scroll_chain(&system_bar, false);
    lv_obj_set_scrollbar_mode(&system_bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_t music_content{};
    music_content.parent = &music_root;
    lv_obj_set_pos(&music_content, 12, 65);
    lv_obj_set_size(&music_content, 696, 1203);
    lv_obj_set_scroll_dir(&music_content, LV_DIR_NONE);
    lv_obj_set_scroll_chain(&music_content, false);
    lv_obj_set_scrollbar_mode(&music_content, LV_SCROLLBAR_MODE_OFF);
    cyberdeck_music_view::view music_view;
    assert(!music_view.create(nullptr, record_music_action, nullptr));
    assert(!music_view.visible());
    music_action_log music_log;
    music_log.expected_context = &music_log;
    assert(music_view.create(&music_content, record_music_action, &music_log));
    assert(music_view.create(&music_content, record_music_action, &music_log));
    auto *music_surface = music_content.children.back();
    assert(music_surface->width > 0 && music_surface->height > 0);
    assert(music_surface->parent == &music_content);
    assert(lv_obj_is_ignore_layout(music_surface));
    assert(music_surface->x == 0 && music_surface->y == 0);
    assert(music_surface->children.size() == 4);
    assert(music_surface->hidden && !music_view.visible());
    auto *now_playing = music_surface->children[1];
    auto *browser = music_surface->children[2];
    auto *music_footer = music_surface->children[3];
    auto *music_status_label = now_playing->children[1];
    auto *progress = now_playing->children[3];
    auto *controls = now_playing->children[6];
    assert(music_surface->color == 0x000000);
    assert(now_playing->color == 0x0A0A0A && browser->color == 0x0A0A0A);
    /* AC-MUSIC-13: the light default theme paints the slider indicator with the
     * primary (blue), rounds MAIN/INDICATOR and draws a knob ball. The shim
     * folds every part selector into one style slot, so the last write per
     * property wins: `color` is the INDICATOR override and `bg_opa` is the KNOB
     * one (LV_OPA_TRANSP), because the KNOB block is written after MAIN.
     * Therefore this `bg_opa` assertion only proves the knob stays invisible in
     * the real call order; it is NOT evidence of the MAIN track opacity (the
     * slot holds the last write). The opaque `kBorder` track is proven by the
     * structural contract `test_music_player_ui_contract.py`. */
    assert(progress->color == 0xF2F2F2);
    assert(progress->radius == 0);
    assert(progress->border_width == 0);
    assert(progress->bg_opa == LV_OPA_TRANSP);
    assert(progress->shadow_width == 0);
    const auto assert_not_scrollable = [](const lv_obj_t *object) {
        assert(lv_obj_get_scroll_dir(object) == LV_DIR_NONE);
        assert(lv_obj_get_scrollbar_mode(object) == LV_SCROLLBAR_MODE_OFF);
        assert(!object->scroll_chain);
    };
    assert_not_scrollable(&music_content);
    assert_not_scrollable(&music_root);
    assert_not_scrollable(&system_bar);
    assert_not_scrollable(music_surface);
    assert_not_scrollable(music_surface->children[0]);
    assert_not_scrollable(now_playing);
    for (std::size_t index = 0; index < now_playing->children.size(); ++index)
        assert_not_scrollable(now_playing->children[index]);
    assert_not_scrollable(browser->children[0]);
    assert_not_scrollable(music_footer);
    /* TEST-MUSIC-FOOTER-TEXT: the shortcut row is a real label on the surface,
     * painted in the palette MUTED grey, not text appended to the status block.
     * The exact per-orientation text is asserted by `assert_music_footer`. */
    assert(!music_footer->text.empty());
    assert(music_footer->text_color == 0x8A8A8A);
    assert(music_footer->text_font == &cyberdeck_font);
    assert(music_footer->long_mode == LV_LABEL_LONG_MODE_DOTS);
    assert_not_scrollable(controls);
    assert(controls->children.size() == 8);
    for (const auto *button : controls->children) {
        const auto *label = button->children[0];
        assert(button->width >= 44 && button->height >= 44);
        assert(label->x >= 0 && label->y >= 0);
        assert(label->x + label->width <= button->width);
        assert(label->y + label->height <= button->height);
        assert(label->long_mode == LV_LABEL_LONG_MODE_DOTS);
        assert(lv_shim_text_width(&cyberdeck_font, label->text) <= label->width);
        /* AC-MUSIC-13: the light theme frames every `lv_btn` with a border,
         * radius and grey shadow; the control must stay a flat cell. */
        assert(button->border_width == 0);
        assert(button->radius == 0);
        assert(button->shadow_width == 0);
        assert(button->shadow_opa == LV_OPA_TRANSP);
        assert(button->color == 0x303030);
    }
    /* AC-MUSIC-13: the control grid keeps the same gap between rows as between
     * columns (`pad` = 4) and anchors the block by the card *content* height.
     * Device defect: the three rows were glued into one continuous block
     * (563..694) and the block was placed with the outer card height, so the
     * last row ended on the card border (694) instead of keeping the 17 px
     * inset measured on the left/right. */
    constexpr int kControlGap = 4;
    const auto assert_control_grid = [&](int columns) {
        const int rows = static_cast<int>(
            (controls->children.size() + static_cast<std::size_t>(columns) - 1) /
            static_cast<std::size_t>(columns));
        const int button_height = controls->children[0]->height;
        for (std::size_t index = static_cast<std::size_t>(columns);
             index < controls->children.size(); ++index) {
            const auto *button = controls->children[index];
            const auto *above = controls->children[index - static_cast<std::size_t>(columns)];
            assert(button->height == button_height);
            assert(button->y == above->y + above->height + kControlGap);
        }
        /* Non-vacuity: the glued layout is `(rows - 1) * gap` px shorter, so
         * this equality cannot hold without the row gap. */
        assert(controls->height == rows * button_height + (rows - 1) * kControlGap);
        assert(controls->height > rows * button_height);
        /* Symmetric inset: the bottom of the block sits 8 px from the bottom of
         * the card *content* area, exactly like the 8 px left/right inset. */
        assert(controls->y + controls->height ==
               lv_obj_get_content_height(now_playing) - 8);
        /* The anchor is the content height, not the outer card height (they
         * differ by `card()`'s border + pad), so the legacy
         * `card_height - controls_area - 8` cannot satisfy the line above. */
        assert(controls->y + controls->height != now_playing->height - 8);
    };
    assert_control_grid(2);
    assert(browser->scroll_dir == LV_DIR_VER);
    assert(browser->scrollbar_mode == LV_SCROLLBAR_MODE_AUTO);
    assert(!browser->scroll_chain);
    for (std::size_t index = 1; index < browser->children.size(); ++index) {
        assert_not_scrollable(browser->children[index]);
        /* Library rows are `lv_btn` instances; the theme radius/shadow would
         * leak as a light arc at the row corners. */
        assert(browser->children[index]->radius == 0);
        assert(browser->children[index]->outline_width == 0);
        assert(browser->children[index]->shadow_width == 0);
        assert(browser->children[index]->shadow_opa == 0);
    }
    const std::array<std::string, 7> music_labels{
        "Previous", "Play / Pause", "Next", "-10s", "+10s", "Vol -", "Vol +"};
    const std::array<std::string, 7> music_actions{
        "prev", "toggle", "next", "seek_back", "seek_forward",
        "volume_down", "volume_up"};
    for (std::size_t i = 0; i < music_labels.size(); ++i) {
        auto *button = controls->children[i];
        assert(button->children[0]->text == music_labels[i]);
        assert(button->event_callbacks.size() == 1);
        lv_shim_emit_event(button, LV_EVENT_CLICKED);
        assert(music_log.actions.back() == music_actions[i]);
    }
    assert(music_log.actions.size() == 7 && music_log.context_calls == 7);
    auto *command_button = controls->children[7];
    assert(command_button->children[0]->text == "Command");
    assert(command_button->color == 0x303030);
    lv_shim_emit_event(command_button, LV_EVENT_CLICKED);
    assert(music_log.actions.back() == "command_open");
    music_view.set_command_open(true);
    const auto *command_field = now_playing->children[5];
    assert(!command_field->hidden);
    assert(command_field->x >= 0 && command_field->y >= 0);
    assert(command_field->x + command_field->width <= now_playing->width);
    assert(command_field->y + command_field->height <= now_playing->height);
    const auto overlaps = [](const lv_obj_t *left, const lv_obj_t *right) {
        return left->x < right->x + right->width && right->x < left->x + left->width &&
               left->y < right->y + right->height && right->y < left->y + left->height;
    };
    /* TEST-MUSIC-FOOTER-TEXT: the shortcut row is one row in landscape and two
     * explicit rows in portrait. The 71-character hint needs 767.7 px at the
     * real `cyberdeck_font` advance (10.8125 px/char) while the portrait surface
     * content is 680 px wide, so the break is orientation-driven and never
     * derived from runtime text metrics. */
    const std::string music_footer_landscape =
        "Enter play/pause  left/right seek  up/down volume  N/P track  Esc close";
    const std::string music_footer_portrait =
        "Enter play/pause  left/right seek\nup/down volume  N/P track  Esc close";
    /* TEST-MUSIC-FOOTER-LAYOUT: the footer owns the bottom `pad + label height`
     * band of the surface and both cards must end above it, in portrait
     * (696x1203) and landscape (1256x643). The old `height - y - pad` card
     * formula filled the whole surface, so the row would have been painted over
     * the library. */
    const int music_footer_line = lv_font_get_line_height(&cyberdeck_font);
    const auto assert_music_footer = [&]() {
        const int surface_width = lv_obj_get_content_width(music_surface);
        const int surface_height = lv_obj_get_content_height(music_surface);
        const bool landscape = surface_width > surface_height;
        const int expected_height =
            landscape ? music_footer_line : 2 * music_footer_line + 2;
        /* The row is only useful while painted: a hidden footer would still
         * satisfy every geometry/text assertion below. The hint stays visible
         * in the normal and in the command overlay mode. */
        assert(!music_footer->hidden);
        assert(music_footer->parent == music_surface);
        assert(music_footer->x == 4);
        assert(music_footer->y == surface_height - 4 - expected_height);
        assert(music_footer->width == surface_width - 8);
        assert(music_footer->height == expected_height);
        assert(music_footer->x >= 0 && music_footer->y >= 0);
        assert(music_footer->x + music_footer->width <= surface_width);
        assert(music_footer->y + music_footer->height <= surface_height);
        /* Landscape keeps the single row; portrait carries the exact break. */
        assert(music_footer->text ==
               (landscape ? music_footer_landscape : music_footer_portrait));
        if (landscape)
            assert(music_footer->text.find('\n') == std::string::npos);
        else
            assert(music_footer->text.find('\n') != std::string::npos &&
                   music_footer->text.find('\n') == music_footer->text.rfind('\n'));
        /* TEST-MUSIC-FOOTER-FIT: `LV_LABEL_LONG_MODE_DOTS` truncates instead of
         * wrapping, so the reserved height alone does not prove the row is
         * readable: a literal that fits the band can still be cut at the label
         * edge. The shim advance is `line_height / 2` = 11 px/char, 1.7% wider
         * than the real `cyberdeck_font` advance (adv_w 173/16 = 10.8125
         * px/char), so this hint measurement is a conservative proxy for the
         * device. It reproduces the original defect scenario (71 x 11 = 781 px
         * inside the 672 px portrait label) whenever the shortcuts grow and the
         * expected literals above are updated together. */
        int rows = 1;
        std::size_t row_begin = 0;
        while (true) {
            const std::size_t row_end = music_footer->text.find('\n', row_begin);
            const std::string row = music_footer->text.substr(
                row_begin,
                row_end == std::string::npos ? std::string::npos : row_end - row_begin);
            assert(lv_shim_text_width(&cyberdeck_font, row) <= music_footer->width);
            if (row_end == std::string::npos) break;
            ++rows;
            row_begin = row_end + 1;
        }
        assert(rows * music_footer_line <= music_footer->height);
        assert(now_playing->y + now_playing->height <= music_footer->y);
        assert(browser->y + browser->height <= music_footer->y);
        assert(!overlaps(music_footer, now_playing));
        assert(!overlaps(music_footer, browser));
    };
    assert_music_footer();
    const std::array<const lv_obj_t *, 4> fixed_content{
        now_playing->children[1], progress, now_playing->children[4], command_field};
    for (const auto *fixed_item : fixed_content)
        assert(!overlaps(fixed_item, controls));
    const std::array<const lv_obj_t *, 6> information_and_controls{
        now_playing->children[1], progress, now_playing->children[2],
        now_playing->children[4], command_field, controls};
    for (std::size_t left = 0; left < information_and_controls.size(); ++left) {
        const auto *item = information_and_controls[left];
        assert(item->x >= 0 && item->y >= 0);
        assert(item->x + item->width <= now_playing->width);
        assert(item->y + item->height <= now_playing->height);
        for (std::size_t right_index = left + 1;
             right_index < information_and_controls.size(); ++right_index)
            assert(!overlaps(item, information_and_controls[right_index]));
    }
    for (std::size_t left = 0; left < controls->children.size(); ++left)
        for (std::size_t right_index = left + 1;
             right_index < controls->children.size(); ++right_index)
            assert(!overlaps(controls->children[left], controls->children[right_index]));
    music_view.set_command_text("help");
    assert(music_view.command_open() && music_view.command_text() == "help");
    music_view.set_command_text(std::string(120, 'x'));
    assert(music_view.command_text().size() == 96);
    music_view.set_command_text("help");
    music_view.set_command_cursor(99);
    music_view.set_command_open(false);
    lv_shim_emit_event(browser->children[1], LV_EVENT_CLICKED);
    assert(music_log.actions.size() == 9 && music_log.actions.back() == "select");
    assert(music_view.selected_track() == 0);
    music_view.render("Now playing: track.mp3");
    assert(music_status_label->text == "Now playing: track.mp3");
    music_view.render("");
    assert(music_status_label->text.empty());
    music_view.render("state: paused\nqueue: 2 tracks\ntrack: 1/2\nmusic queue:\n1. first.mp3\n2. second.mp3\n");
    assert(music_status_label->text == "state: paused\ntrack: 1/2");
    /* REQ-PLAYER-LAYOUT-READABLE: more status content than the three-line
     * viewport must be bounded, and the rendered label itself stays within
     * the now-playing card in both dimensions. */
    music_view.render("state: playing\ntrack: 1/64\nLong readable track title\nartist: Example\nextra status row\n");
    assert(music_status_label->text ==
           "state: playing\ntrack: 1/64\nLong readable track title");
    assert(music_status_label->height == 3 * lv_font_get_line_height(&cyberdeck_font));
    assert(music_status_label->x >= 0 && music_status_label->y >= 0);
    assert(music_status_label->x + music_status_label->width <= now_playing->width);
    assert(music_status_label->y + music_status_label->height <= now_playing->height);
    music_view.render("state: paused\ntrack: 1/2\nmusic queue:\n1. first.mp3\n2. second.mp3\n");
    assert(browser->children[1]->hidden == false);
    assert(browser->children[1]->children[0]->text == "first.mp3");
    assert(browser->children[2]->children[0]->text == "second.mp3");
    std::string full_queue = "state: paused\nprogress: 50s / 100s\nmusic queue:\n";
    for (int index = 1; index <= 64; ++index)
        full_queue += std::to_string(index) + ". track-" + std::to_string(index) + ".mp3\n";
    music_view.render(full_queue);
    assert(!browser->children[64]->hidden);
    assert(browser->children[64]->children[0]->text == "track-64.mp3");
    assert(browser->children[64]->children[0]->width == browser->children[64]->width - 12);
    assert(browser->children[64]->children[0]->x == 6);
    assert(browser->children[64]->children[0]->long_mode == LV_LABEL_LONG_MODE_DOTS);
    const std::string long_track_name(180, 'W');
    music_view.render("music queue:\n1. " + long_track_name + "\n");
    const auto *long_track_label = browser->children[1]->children[0];
    assert(long_track_label->text == long_track_name);
    assert(long_track_label->width < lv_shim_text_width(&cyberdeck_font, long_track_label->text));
    assert(long_track_label->long_mode == LV_LABEL_LONG_MODE_DOTS);
    music_view.render(full_queue);
    assert(progress->slider_value == 500);
    assert(browser->children[64]->y + browser->children[64]->height > browser->height);
    lv_shim_emit_event(progress, LV_EVENT_RELEASED);
    assert(music_log.actions.back() == "seek_ratio");
    for (std::size_t index = 0; index < 64; ++index) {
        const auto *row = browser->children[index + 1];
        assert(row->y >= 0 && row->height > 0);
        if (index > 0) assert(row->y >= browser->children[index]->y + browser->children[index]->height);
    }
    assert(browser->children[1]->y + browser->children[1]->height <= browser->height);
    assert(browser->children[64]->y < 64 * 32);
    assert(now_playing->x >= 0 && now_playing->y >= 0);
    assert(now_playing->x + now_playing->width <= music_surface->width);
    assert(now_playing->y + now_playing->height <= music_surface->height);
    assert(browser->x >= 0 && browser->y >= 0);
    assert(browser->x + browser->width <= music_surface->width);
    assert(browser->y + browser->height <= music_surface->height);
    for (const auto *button : controls->children) {
        assert(button->x >= 0 && button->y >= 0);
        assert(button->x + button->width <= controls->width);
        assert(button->y + button->height <= controls->height);
    }
    const int portrait_y = browser->y;
    music_view.set_command_open(true);
    lv_obj_set_size(&music_content, 696, 1203);
    lv_shim_emit_event(&music_content, LV_EVENT_SIZE_CHANGED);
    assert(music_surface->width == 696 && music_surface->height == 1203);
    assert(browser->x == 4 && browser->y > now_playing->y);
    assert(browser->y >= now_playing->y + now_playing->height);
    assert(now_playing->x + now_playing->width <= music_surface->width);
    assert(browser->x + browser->width <= music_surface->width);
    assert(now_playing->y + now_playing->height <= music_surface->height);
    assert(browser->y + browser->height <= music_surface->height);
    music_view.render("music queue:\n1. " + long_track_name + "\n");
    assert(browser->children[1]->children[0]->text == long_track_name);
    assert(browser->children[1]->children[0]->width <
           lv_shim_text_width(&cyberdeck_font, long_track_name));
    for (const auto *button : controls->children)
        assert(button->width >= 44 && button->height >= 44);
    assert(command_field->x >= 0 && command_field->y >= 0);
    assert(command_field->x + command_field->width <= now_playing->width);
    assert(command_field->y + command_field->height <= now_playing->height);
    assert(!overlaps(command_field, controls));
    lv_obj_set_size(&music_root, 1280, 720);
    lv_obj_set_pos(&music_content, 12, 65);
    lv_obj_set_size(&music_content, 1256, 643);
    lv_shim_emit_event(&music_content, LV_EVENT_SIZE_CHANGED);
    lv_obj_set_size(&system_bar, 1256, 42);
    assert(music_surface->x == 0 && music_surface->y == 0);
    assert(music_surface->width == 1256 && music_surface->height == 643);
    lv_shim_emit_event(music_surface, LV_EVENT_SIZE_CHANGED);
    assert_music_footer();
    assert(browser->y <= portrait_y);
    assert(browser->x > now_playing->x && browser->y == now_playing->y);
    assert(browser->x >= now_playing->x + now_playing->width);
    assert(browser->x + browser->width <= 1256 && browser->y + browser->height <= 643);
    music_view.render("music queue:\n1. " + long_track_name + "\n");
    assert(browser->children[1]->children[0]->text == long_track_name);
    assert(browser->children[1]->children[0]->width <
           lv_shim_text_width(&cyberdeck_font, long_track_name));
    music_view.render(full_queue);
    assert(command_field->x + command_field->width <= now_playing->width);
    assert(command_field->y + command_field->height <= now_playing->height);
    assert(!overlaps(command_field, controls));
    for (const auto *button : controls->children) {
        const auto *label = button->children[0];
        assert(button->width >= 44 && button->height >= 44);
        assert(label->x >= 0 && label->y >= 0);
        assert(label->x + label->width <= button->width);
        assert(label->y + label->height <= button->height);
        assert(label->long_mode == LV_LABEL_LONG_MODE_DOTS);
    }
    /* AC-MUSIC-13: `card()` pads every side by 8 px, and a child at `x = 8` is
     * relative to the card *content* area, which already starts at
     * `border + pad` from the card border. The symmetric inset therefore comes
     * from the real content width (`content - 2 * kCardInset`) and the right
     * edge of the bar, of the rightmost control column and of every library row
     * must stay `kCardInset` inside that content area. The previous
     * `now_width - 16` / `browser_width - 16` measured the outer card and left
     * the right edge 1 px past the card border (device: bar 574 vs card 573).
     * The shim resolves `lv_obj_get_content_width` from border + pad, so the
     * assertion is relative to the same content area the production code reads
     * (the shim subtracts the border once, LVGL subtracts it on both sides, so
     * host widths are 1 px larger than the device; the invariant is identical). */
    constexpr int kCardInset = 8;
    const int now_content = lv_obj_get_content_width(now_playing);
    const int browser_content = lv_obj_get_content_width(browser);
    assert(now_content > 0 && browser_content > 0);
    for (const auto *child : now_playing->children) {
        if (child->width <= 0) continue;
        assert(child->x == kCardInset);
        assert(child->x + child->width == now_content - kCardInset);
    }
    assert(progress->x == kCardInset);
    assert(progress->x + progress->width == now_content - kCardInset);
    /* The rightmost control column is column 2 of 3; the integer division of
     * `button_width` can leave up to `columns - 1` px unused, so the right edge
     * must sit between `content - kCardInset - (columns - 1)` and
     * `content - kCardInset`. */
    for (std::size_t index = 2; index < controls->children.size(); index += 3) {
        const auto *button = controls->children[index];
        const int right = controls->x + button->x + button->width;
        assert(button->x >= 0);
        assert(right <= now_content - kCardInset);
        assert(right >= now_content - kCardInset - 2);
    }
    for (const auto *row : {browser->children[1], browser->children[64]}) {
        assert(row->x == kCardInset);
        assert(row->x + row->width == browser_content - kCardInset);
    }
    /* Non-vacuity: the legacy arithmetic is 18 px wider, which is exactly the
     * overflow measured on the device, so the equality above cannot be
     * satisfied by the previous formula. */
    assert(kCardInset + (now_playing->width - 16) > now_content - kCardInset);
    assert(kCardInset + (browser->width - 16) > browser_content - kCardInset);
    const auto absolute_position = [](const lv_obj_t *object) {
        int x = 0;
        int y = 0;
        for (auto *parent = object; parent; parent = parent->parent) {
            x += parent->x;
            y += parent->y;
        }
        return std::pair<int, int>{x, y};
    };
    for (const auto *panel : {now_playing, browser}) {
        const auto [x, y] = absolute_position(panel);
        assert(x >= 0 && y >= 0);
        assert(x >= 12 && y >= 65);
        assert(y >= system_bar.y + system_bar.height);
        assert(x + panel->width <= 1268 && y + panel->height <= 708);
    }
    for (const auto *button : controls->children) {
        const auto [x, y] = absolute_position(button);
        assert(x >= 0 && y >= 0);
        assert(x >= 12 && y >= 65);
        assert(x + button->width <= 1268 && y + button->height <= 708);
        const auto [label_x, label_y] = absolute_position(button->children[0]);
        assert(label_x >= 0 && label_y >= 0);
        assert(label_x >= 12 && label_y >= 65);
        assert(label_x + button->children[0]->width <= 1268);
        assert(label_y + button->children[0]->height <= 708);
    }
    auto *landscape_list = browser;
    for (std::size_t index = 0; index < controls->children.size(); ++index) {
        const auto *button = controls->children[index];
        assert(button->x >= 0 && button->y >= 0);
        assert(button->x + button->width <= controls->width);
        assert(button->y + button->height <= controls->height);
    }
    /* Same invariants in landscape (3 columns, 3 rows): the vertical gap and the
     * content-height anchor cannot be satisfied by the legacy glued block. */
    assert_control_grid(3);
    /* REQ-PLAYER-LAYOUT-READABLE: repeat the complete information/control
     * matrix in landscape; the portrait matrix above cannot catch regressions
     * caused by the denser horizontal arrangement. */
    const std::array<const lv_obj_t *, 6> landscape_information_and_controls{
        now_playing->children[1], progress, now_playing->children[2],
        now_playing->children[4], command_field, controls};
    for (std::size_t left = 0; left < landscape_information_and_controls.size(); ++left) {
        const auto *item = landscape_information_and_controls[left];
        assert(item->x >= 0 && item->y >= 0);
        assert(item->x + item->width <= now_playing->width);
        assert(item->y + item->height <= now_playing->height);
        for (std::size_t right_index = left + 1;
             right_index < landscape_information_and_controls.size(); ++right_index)
            assert(!overlaps(item, landscape_information_and_controls[right_index]));
    }
    const auto *landscape_title = music_surface->children[0];
    assert(landscape_title->x + landscape_title->width <= music_surface->width);
    assert(landscape_title->y + landscape_title->height <= music_surface->height);
    assert(!overlaps(landscape_title, now_playing));
    assert(!overlaps(landscape_title, browser));
    for (std::size_t index = 1; index <= 64; ++index) {
        const auto *row = browser->children[index];
        const auto *label = row->children[0];
        /* REQ-PLAYER-LAYOUT-READABLE / AC-LIST-AND-CONTROLS-FIT: the theme pads
         * `btn`, so the row must zero pad all sides and pad_row/pad_column
         * explicitly or the glyph is clipped. */
        assert(row->pad_top == 0 && row->pad_bottom == 0);
        assert(row->pad_left == 0 && row->pad_right == 0);
        assert(row->pad_row == 0 && row->pad_column == 0);
        assert(label->x >= 6 && label->x + label->width <= row->width - 6);
        assert(label->y >= 0 && label->y + label->height <= row->height);
        assert(label->height <= row->height);
        assert(label->long_mode == LV_LABEL_LONG_MODE_DOTS);
    }
    /* Exercise the reverse resize as well; return to landscape for subsequent
     * list-scroll and callback assertions. */
    lv_obj_set_size(&music_content, 696, 1203);
    lv_shim_emit_event(&music_content, LV_EVENT_SIZE_CHANGED);
    assert_music_footer();
    assert(browser->y > now_playing->y);
    assert(browser->y >= now_playing->y + now_playing->height);
    assert(now_playing->x + now_playing->width <= music_surface->width);
    assert(browser->x + browser->width <= music_surface->width);
    assert(now_playing->y + now_playing->height <= music_surface->height);
    assert(browser->y + browser->height <= music_surface->height);
    for (const auto *button : controls->children)
        assert(button->width >= 44 && button->height >= 44);
    assert(!overlaps(command_field, controls));
    lv_obj_set_size(&music_content, 1256, 643);
    lv_shim_emit_event(&music_content, LV_EVENT_SIZE_CHANGED);
    assert_music_footer();
    /* TEST-MUSIC-FOOTER-GUARD: `layout()` also runs on every status refresh
     * (~4x/s), and re-applying the same string makes LVGL free/re-allocate it
     * and invalidate the whole band each tick. Planting a marker and forcing a
     * same-orientation layout proves the setter is gated by the orientation; the
     * next orientation change must still restore the matching hint. */
    music_footer->text = "stale-footer";
    music_view.render("state: paused\nmusic queue:\n1. a.mp3\n2. b.mp3\n3. c.mp3\n");
    assert(music_footer->text == "stale-footer");
    lv_obj_set_size(&music_content, 696, 1203);
    lv_shim_emit_event(&music_content, LV_EVENT_SIZE_CHANGED);
    assert(music_footer->text == music_footer_portrait);
    lv_obj_set_size(&music_content, 1256, 643);
    lv_shim_emit_event(&music_content, LV_EVENT_SIZE_CHANGED);
    assert_music_footer();
    assert(browser->x >= now_playing->x + now_playing->width);
    assert(browser->scroll_dir == LV_DIR_VER);
    assert(!browser->scroll_chain);
    music_view.set_command_open(false);
    assert(browser->scrollbar_mode == LV_SCROLLBAR_MODE_AUTO);
    assert_not_scrollable(music_surface);
    assert_not_scrollable(now_playing);
    assert_not_scrollable(controls);
    assert_not_scrollable(&music_content);
    for (std::size_t index = 1; index < browser->children.size(); ++index)
        assert_not_scrollable(browser->children[index]);
    assert(browser->children[64]->y + browser->children[64]->height > browser->height);
    music_view.render("state: paused\ntrack: 2/64\nprogress: 50s / 100s\nmusic queue:\n1. track-1.mp3\n2. track-2.mp3\n");
    lv_shim_emit_event(landscape_list->children[2], LV_EVENT_CLICKED);
    assert(music_log.actions.back() == "select");
    assert(music_view.selected_track() == 1);
    const auto callback_count = music_log.actions.size();
    music_view.set_command_open(true);
    lv_shim_emit_event(controls->children[1], LV_EVENT_CLICKED);
    lv_shim_emit_event(landscape_list->children[1], LV_EVENT_CLICKED);
    assert(music_log.actions.size() == callback_count);
    music_view.set_command_open(false);
    cyberdeck_music_view::view no_music_callback;
    assert(no_music_callback.create(&music_content, nullptr, nullptr));
    auto *no_callback_surface = music_content.children.back();
    lv_shim_emit_event(no_callback_surface->children[2]->children[1], LV_EVENT_CLICKED);
    no_music_callback.destroy();
    auto *terminal_overlay = lv_obj_create(&music_content);
    assert(terminal_overlay != nullptr);
    assert(music_content.children.back() == terminal_overlay);
    assert(music_content.children.back() != music_surface);
    music_view.set_visible(true);
    assert(music_view.visible() && !music_surface->hidden);
    assert(music_content.children.back() == music_surface);
    music_view.set_visible(false);
    assert(!music_view.visible() && music_surface->hidden);
    music_view.destroy();
    assert(!music_view.visible());
    music_view.destroy();
    assert(!music_view.create(nullptr, record_music_action, &music_log));
    assert(music_view.create(&music_content, record_music_action, &music_log));
    assert(music_view.visible() == false);
    /* TEST-MUSIC-FOOTER-GUARD: `destroy()` resets the orientation sentinel, so a
     * recreated view in the same orientation (landscape 1256x643) must paint the
     * hint on the fresh label instead of skipping the gated setter. */
    auto *recreated_surface = music_content.children.back();
    assert(recreated_surface->children.size() == 4);
    assert(recreated_surface->children[3]->text == music_footer_landscape);
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
