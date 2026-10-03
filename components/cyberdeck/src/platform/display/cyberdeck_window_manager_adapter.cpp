#include "platform/display/cyberdeck_window_manager_adapter.h"

#include "lvgl.h"

namespace cyberdeck_window_manager_adapter {
namespace {
const lv_color_t k_black = lv_color_hex(0x000000);
const lv_color_t k_white = lv_color_hex(0xF2F2F2);

void disable_scrolling(lv_obj_t *object)
{
    lv_obj_set_scroll_dir(object, LV_DIR_NONE);
    lv_obj_set_scroll_chain(object, false);
    lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
}
}

bool adapter::init()
{
    if (screen_ != nullptr) return true;
    screen_ = lv_scr_act();
    if (screen_ == nullptr) return false;
    lv_obj_set_style_bg_color(screen_, k_black, 0);
    lv_obj_set_style_text_color(screen_, k_white, 0);
    lv_obj_set_style_pad_all(screen_, 12, 0);
    lv_obj_set_layout(screen_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(screen_, LV_FLEX_FLOW_COLUMN);
    system_bar_ = lv_obj_create(screen_);
    content_ = lv_obj_create(screen_);
    if (system_bar_ == nullptr || content_ == nullptr) {
        deinit();
        return false;
    }
    lv_obj_set_style_bg_color(system_bar_, k_black, 0);
    lv_obj_set_style_text_color(system_bar_, k_white, 0);
    lv_obj_set_style_border_width(system_bar_, 0, 0);
    lv_obj_set_style_pad_all(system_bar_, 0, 0);
    lv_obj_set_style_pad_row(system_bar_, 0, 0);
    lv_obj_set_style_pad_column(system_bar_, 0, 0);
    disable_scrolling(system_bar_);
    lv_obj_set_width(system_bar_, LV_PCT(100));
    lv_obj_set_height(system_bar_, 42);
    lv_obj_set_flex_grow(system_bar_, 0);
    lv_obj_set_width(content_, LV_PCT(100));
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    return true;
}

void adapter::deinit()
{
    /* LVGL owns the screen; only the adapter's children are ours. */
    if (content_ != nullptr) lv_obj_del(content_);
    if (system_bar_ != nullptr) lv_obj_del(system_bar_);
    content_ = nullptr;
    system_bar_ = nullptr;
    screen_ = nullptr;
    policy_.reset();
}

adapter &global()
{
    static adapter instance;
    return instance;
}
} // namespace cyberdeck_window_manager_adapter
