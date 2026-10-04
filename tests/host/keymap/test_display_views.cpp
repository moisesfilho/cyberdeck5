#include "platform/display/cyberdeck_header_view.h"
#include "platform/display/cyberdeck_terminal_view.h"

#include <cassert>
#include <iostream>

extern const lv_font_t cyberdeck_font{16};

static void noop(lv_event_t *) {}

int main()
{
    lv_obj_t screen{};
    cyberdeck_header_view::view header;
    assert(!header.create(nullptr));
    assert(header.create(&screen));
    header.update_clock(nullptr);
    header.update_clock("12:34");
    header.update_ble(false); header.update_ble(true); header.update_ble(false);
    header.update_wifi(false); header.update_wifi(true);
    cyberdeck_battery_view::presentation presentation{};
    header.update_battery(presentation);
    presentation.visible = true; presentation.show_percentage = true;
    presentation.percentage = 87; presentation.glyph = cyberdeck_battery_view::power_glyph::battery;
    header.update_battery(presentation);
    presentation.show_percentage = false; presentation.glyph = cyberdeck_battery_view::power_glyph::external;
    header.update_battery(presentation);

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
