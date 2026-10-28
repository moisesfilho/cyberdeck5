#include "platform/display/cyberdeck_music_view.h"
#include "lvgl.h"

#include <string>

extern const lv_font_t cyberdeck_font;

namespace cyberdeck_music_view {
namespace {
const lv_color_t kBlack = lv_color_hex(0x000000);
const lv_color_t kBorder = lv_color_hex(0x2A2A2A);
const lv_color_t kWhite = lv_color_hex(0xF2F2F2);
constexpr const char *kActions[] = {"prev", "toggle", "next", "seek_back", "seek_forward", "volume_down", "volume_up"};
constexpr const char *kLabels[] = {"Previous", "Play/Pause", "Next", "Seek -10s", "Seek +10s", "Volume -", "Volume +"};
}

bool view::create(lv_obj_t *parent, callback on_action, void *context) {
    if (parent == nullptr)
        return false;
    if (surface_ != nullptr)
        return true;
    callback_ = on_action;
    context_ = context;
    surface_ = lv_obj_create(parent);
    if (surface_ == nullptr)
        return false;
    lv_obj_set_size(surface_, LV_PCT(94), LV_PCT(72));
    lv_obj_center(surface_);
    lv_obj_set_style_bg_color(surface_, kBlack, 0);
    lv_obj_set_style_border_color(surface_, kBorder, 0);
    lv_obj_set_style_text_color(surface_, kWhite, 0);
    lv_obj_set_style_text_font(surface_, &cyberdeck_font, 0);
    lv_obj_set_flex_flow(surface_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(surface_, 12, 0);
    status_ = lv_label_create(surface_);
    if (status_ == nullptr) {
        destroy();
        return false;
    }
    lv_obj_set_width(status_, LV_PCT(100));
    lv_label_set_long_mode(status_, LV_LABEL_LONG_WRAP);
    for (std::size_t i = 0; i < sizeof(kActions) / sizeof(kActions[0]); ++i) {
        lv_obj_t *button = lv_label_create(surface_);
        if (button == nullptr) {
            destroy();
            return false;
        }
        lv_label_set_text(button, kLabels[i]);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(button, clicked, LV_EVENT_CLICKED, this);
    }
    set_visible(false);
    return true;
}

void view::destroy() {
    if (surface_ != nullptr)
        lv_obj_delete(surface_);
    surface_ = nullptr;
    status_ = nullptr;
    callback_ = nullptr;
    context_ = nullptr;
    visible_ = false;
}

void view::set_visible(bool visible) {
    visible_ = visible;
    if (surface_ != nullptr)
        lv_obj_set_hidden(surface_, !visible);
}

void view::render(std::string_view text) {
    if (status_ != nullptr)
        lv_label_set_text(status_, std::string(text).c_str());
}

void view::clicked(lv_event_t *event) {
    auto *self = static_cast<view *>(lv_event_get_user_data(event));
    if (self == nullptr || self->callback_ == nullptr)
        return;
    auto *button = static_cast<lv_obj_t *>(lv_event_get_target(event));
    const char *label = lv_label_get_text(button);
    for (std::size_t i = 0; i < sizeof(kActions) / sizeof(kActions[0]); ++i) {
        if (label != nullptr && std::string_view(label) == kLabels[i]) {
            self->callback_(kActions[i], self->context_);
            return;
        }
    }
}
} // namespace cyberdeck_music_view
