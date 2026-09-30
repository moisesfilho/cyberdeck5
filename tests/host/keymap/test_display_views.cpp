#include "platform/display/cyberdeck_header_view.h"
#include "platform/display/cyberdeck_terminal_view.h"

#include <cassert>
#include <iostream>

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
    assert(!terminal.create(nullptr, &screen, 64, {}));
    cyberdeck_terminal_view::callbacks callbacks{noop, noop, noop, noop, noop};
    assert(terminal.create(&screen, &screen, 256, callbacks));
    assert(terminal.textarea() != nullptr && terminal.keyboard() != nullptr);
    cyberdeck_terminal_view::view no_callbacks;
    assert(no_callbacks.create(&screen, &screen, 0, {}));
    std::cout << "display view tests passed\n";
}
