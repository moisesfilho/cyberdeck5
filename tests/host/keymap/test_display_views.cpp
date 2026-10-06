#include "platform/display/cyberdeck_header_view.h"
#include "platform/display/cyberdeck_terminal_view.h"

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
    const cyberdeck_terminal_view::view &terminal)
{
    std::vector<std::string> lines;
    for (const auto *line : terminal.scrollback()->children)
        if (!line->hidden) lines.push_back(line->text);
    return lines;
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
    header.update_wifi(false); header.update_wifi(true);
    cyberdeck_battery_view::presentation presentation{};
    header.update_battery(presentation);
    assert(right->children.size() == 3);
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
    const std::vector<std::string> wrapped_lines = visible_line_texts(terminal);
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
    std::vector<std::string> oldest = visible_line_texts(terminal);
    assert(oldest.size() == 2);
    assert(oldest[0] == "zero" && oldest[1] == "one");
    touch_swipe(terminal, touch, -120, 120);
    assert(visible_line_texts(terminal) == oldest);
    touch_swipe(terminal, touch, 120, -120);
    const std::vector<std::string> newest = visible_line_texts(terminal);
    assert(newest.size() == 2);
    assert(newest[0] == "eight" && newest[1] == "nine");
    touch_swipe(terminal, touch, 120, -120);
    assert(visible_line_texts(terminal) == newest);

    /* TEST-TERM-SCROLL-02/03: UTF-8 remains intact in the navigable window,
     * while the input target stays hidden and the visible prompt surface is
     * unaffected by touch navigation. */
    terminal.render("prompt: \xC3\xA9\neditor: \xE7\x8C\xAB\nend");
    touch_swipe(terminal, touch, -120, 120);
    const std::vector<std::string> utf8_window = visible_line_texts(terminal);
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
    cyberdeck_terminal_view::view no_callbacks;
    assert(no_callbacks.create(&screen, &content, 0, {}));
    std::cout << "display view tests passed\n";
}
