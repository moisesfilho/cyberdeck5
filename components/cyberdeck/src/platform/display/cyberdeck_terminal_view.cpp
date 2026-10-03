#include "platform/display/cyberdeck_terminal_view.h"

namespace cyberdeck_terminal_view {
namespace {

const lv_color_t kSurface = lv_color_hex(0x0A0A0A);
const lv_color_t kBorder = lv_color_hex(0x2A2A2A);
const lv_color_t kWhite = lv_color_hex(0xF2F2F2);

void style_terminal(lv_obj_t *object)
{
    lv_obj_set_style_bg_color(object, kSurface, 0);
    lv_obj_set_style_text_color(object, kWhite, 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_border_color(object, kBorder, 0);
    lv_obj_set_style_pad_all(object, 12, 0);
}

} // namespace

bool view::create(lv_obj_t *screen, lv_obj_t *parent, std::size_t max_length,
                  const callbacks &callbacks)
{
    if (screen == nullptr || parent == nullptr) return false;

    s_terminal = lv_textarea_create(parent);
    if (s_terminal == nullptr) return false;
    lv_obj_set_width(s_terminal, LV_PCT(100));
    lv_obj_set_flex_grow(s_terminal, 1);
    style_terminal(s_terminal);
    lv_textarea_set_one_line(s_terminal, false);
    lv_textarea_set_max_length(s_terminal, static_cast<uint32_t>(max_length));
    lv_obj_set_scroll_dir(s_terminal, LV_DIR_ALL);
    lv_obj_set_scroll_chain(s_terminal, false);
    lv_obj_set_scrollbar_mode(s_terminal, LV_SCROLLBAR_MODE_OFF);
    if (callbacks.focused != nullptr)
        lv_obj_add_event_cb(s_terminal, callbacks.focused, LV_EVENT_FOCUSED, nullptr);
    if (callbacks.inserted != nullptr)
        lv_obj_add_event_cb(s_terminal, callbacks.inserted, LV_EVENT_INSERT, nullptr);
    if (callbacks.changed != nullptr)
        lv_obj_add_event_cb(s_terminal, callbacks.changed, LV_EVENT_VALUE_CHANGED, nullptr);
    if (callbacks.key != nullptr)
        lv_obj_add_event_cb(s_terminal, callbacks.key, LV_EVENT_KEY, nullptr);

    s_keyboard = lv_keyboard_create(screen);
    if (s_keyboard == nullptr) return false;
    lv_obj_set_ignore_layout(s_keyboard, true);
    lv_obj_set_hidden(s_keyboard, true);
    lv_keyboard_set_textarea(s_keyboard, s_terminal);
    if (callbacks.virtual_keyboard_changed != nullptr) {
        lv_obj_add_event_cb(s_keyboard, callbacks.virtual_keyboard_changed,
                            LV_EVENT_VALUE_CHANGED, nullptr);
    }
    lv_obj_set_scroll_dir(s_keyboard, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(s_keyboard, LV_SCROLLBAR_MODE_OFF);
    return true;
}

} // namespace cyberdeck_terminal_view
