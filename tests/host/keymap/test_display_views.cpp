#include "platform/display/cyberdeck_header_view.h"
#include "platform/display/cyberdeck_terminal_view.h"

#include <array>
#include <cassert>
#include <iostream>

/* Previous header typography was 16 px; the production cyberdeck_font used by
 * the clock is intentionally larger and remains a single shared font object. */
extern const lv_font_t cyberdeck_font{18};

static void noop(lv_event_t *) {}

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
    assert(lv_font_get_line_height(clock->text_font) > 16);
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
    presentation.show_percentage = false; presentation.glyph = cyberdeck_battery_view::power_glyph::external;
    header.update_battery(presentation);
    assert(right->children[2]->children[0]->text == "-");
    assert(right->children[2]->children[1]->text.empty());

    cyberdeck_terminal_view::view terminal;
    /* TEST-REG-8-BAR: the composed root is a flex column, so the terminal
     * textarea lives in the content row and the virtual keyboard is a floating
     * overlay on the root.  These run the real production statements, so they
     * fail if the keyboard stops being excluded from the column. */
    lv_obj_t content{};
    content.width = LV_PCT(100);
    content.height = 100;
    assert(!terminal.create(nullptr, &screen, 64, {}));
    cyberdeck_terminal_view::callbacks callbacks{noop, noop, noop, noop, noop};
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
    terminal.render("one");
    const auto *surface = terminal.scrollback();
    assert(surface->children.size() >= 4);
    /* TEST-LAT04-01: a short transcript is bottom anchored, not top padded. */
    assert(surface->children[3]->text == "one");
    terminal.render("one\ntwo\nthree\nfour\nfive");
    /* TEST-LAT04-02: excess output keeps the newest lines in the fixed window. */
    assert(surface->children[0]->text == "two");
    assert(surface->children[3]->text == "five");
    cyberdeck_terminal_view::view no_callbacks;
    assert(no_callbacks.create(&screen, &content, 0, {}));
    std::cout << "display view tests passed\n";
}
