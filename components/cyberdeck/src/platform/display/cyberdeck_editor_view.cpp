#include "platform/display/cyberdeck_editor_view.h"

#include "lvgl.h"

#include <algorithm>
#include <cstdlib>
#include <string>

extern const lv_font_t cyberdeck_font;

namespace cyberdeck_editor_view {
namespace {
constexpr std::size_t kLineLimit = 256;
const lv_color_t kSurface = lv_color_hex(0x0A0A0A);
const lv_color_t kWhite = lv_color_hex(0xF2F2F2);
const lv_color_t kMuted = lv_color_hex(0x8A8A8A);

void style(lv_obj_t *object, lv_color_t color) {
    lv_obj_set_style_bg_color(object, kSurface, 0);
    lv_obj_set_style_text_color(object, color, 0);
    lv_obj_set_style_text_font(object, &cyberdeck_font, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
}

std::string bounded_line(std::string_view text) {
    if (text.size() <= kLineLimit)
        return std::string(text);
    std::size_t end = kLineLimit;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U)
        --end;
    return std::string(text.substr(0, end));
}
} // namespace

bool view::create(lv_obj_t *parent, action_callback callback, void *context) {
    if (parent == nullptr || surface_ != nullptr)
        return surface_ != nullptr;
    callback_ = callback;
    context_ = context;
    surface_ = lv_obj_create(parent);
    if (surface_ == nullptr)
        return false;
    lv_obj_set_width(surface_, LV_PCT(100));
    lv_obj_set_height(surface_, LV_PCT(100));
    lv_obj_set_flex_grow(surface_, 1);
    lv_obj_set_layout(surface_, LV_LAYOUT_NONE);
    lv_obj_set_style_pad_all(surface_, 10, 0);
    style(surface_, kWhite);
    lv_obj_add_flag(surface_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(surface_, pressed, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(surface_, released, LV_EVENT_RELEASED, this);

    const int line_height = std::max<int>(1, lv_font_get_line_height(&cyberdeck_font));
    for (std::size_t index = 0; index < k_max_lines; ++index) {
        lines_[index] = lv_label_create(surface_);
        if (lines_[index] == nullptr)
            return false;
        style(lines_[index], kWhite);
        lv_obj_set_width(lines_[index], LV_PCT(100));
        lv_obj_set_height(lines_[index], line_height);
        lv_obj_set_pos(lines_[index], 0, 20 + static_cast<int>(index) * line_height);
        lv_label_set_long_mode(lines_[index], LV_LABEL_LONG_CLIP);
        lv_label_set_text(lines_[index], "");
    }
    status_ = lv_label_create(surface_);
    prompt_ = lv_label_create(surface_);
    if (status_ == nullptr || prompt_ == nullptr)
        return false;
    style(status_, kMuted);
    style(prompt_, kWhite);
    lv_label_set_text(status_, "");
    lv_label_set_text(prompt_, "");
    lv_obj_set_pos(status_, 0, 0);
    lv_obj_set_pos(prompt_, 0, 20);
    set_visible(false);
    return true;
}

void view::destroy() {
    surface_ = nullptr;
    status_ = nullptr;
    prompt_ = nullptr;
    lines_.fill(nullptr);
    callback_ = nullptr;
    context_ = nullptr;
    visible_ = false;
    touching_ = false;
}

void view::set_visible(bool visible) {
    visible_ = visible;
    if (surface_ != nullptr)
        lv_obj_set_hidden(surface_, !visible);
}

void view::render(std::string_view document, std::size_t cursor, bool dirty, std::string_view status) {
    if (surface_ == nullptr)
        return;
    const std::size_t bounded_cursor = std::min(cursor, document.size());
    std::size_t cursor_line = 0;
    for (std::size_t index = 0; index < bounded_cursor; ++index)
        if (document[index] == '\n')
            ++cursor_line;
    const int line_height = std::max<int>(1, lv_font_get_line_height(&cyberdeck_font));
    const std::size_t visible_lines = k_max_lines;
    if (cursor_line < first_line_)
        first_line_ = cursor_line;
    if (cursor_line >= first_line_ + visible_lines)
        first_line_ = cursor_line - visible_lines + 1;
    std::size_t line = 0;
    std::size_t start = 0;
    std::size_t rendered = 0;
    while (rendered < k_max_lines) {
        const std::size_t end = document.find('\n', start);
        const bool last = end == std::string_view::npos;
        if (line >= first_line_) {
            const std::size_t slot = line - first_line_;
            if (slot >= k_max_lines)
                break;
            std::string value = bounded_line(document.substr(start, (last ? document.size() : end) - start));
            if (line == cursor_line) {
                const std::size_t column = bounded_cursor >= start ? bounded_cursor - start : 0;
                const std::size_t marker = std::min(column, value.size());
                value.insert(marker, "|");
            }
            lv_label_set_text(lines_[slot], value.c_str());
            lv_obj_set_pos(lines_[slot], 0, 20 + static_cast<int>(slot) * line_height);
            lv_obj_set_hidden(lines_[slot], false);
            ++rendered;
        }
        if (last)
            break;
        start = end + 1;
        ++line;
    }
    for (std::size_t slot = rendered; slot < k_max_lines; ++slot)
        lv_obj_set_hidden(lines_[slot], true);
    std::string state = dirty ? "* " : "  ";
    state += status;
    if (status_ != nullptr)
        lv_label_set_text(status_, state.c_str());
    if (prompt_ != nullptr)
        lv_label_set_text(prompt_, "Documento UTF-8 bounded | Ctrl+F buscar");
}

void view::gesture_scroll(int pixels) {
    if (pixels == 0)
        return;
    const std::size_t amount = static_cast<std::size_t>(std::abs(pixels) / 12 + 1);
    if (pixels < 0)
        first_line_ += amount;
    else
        first_line_ = first_line_ > amount ? first_line_ - amount : 0;
    notify("scroll");
}

void view::pressed(lv_event_t *event) {
    auto *self = static_cast<view *>(lv_event_get_user_data(event));
    if (self != nullptr)
        self->begin_touch();
}
void view::released(lv_event_t *event) {
    auto *self = static_cast<view *>(lv_event_get_user_data(event));
    if (self != nullptr)
        self->finish_touch();
}
void view::button_clicked(lv_event_t *event) {
    (void)event;
}
void view::begin_touch() {
    if (lv_indev_get_act() == nullptr)
        return;
    lv_point_t point{};
    lv_indev_get_point(lv_indev_get_act(), &point);
    touch_y_ = point.y;
    touching_ = true;
}
void view::finish_touch() {
    if (!touching_)
        return;
    if (lv_indev_get_act() == nullptr) {
        touching_ = false;
        return;
    }
    lv_point_t point{};
    lv_indev_get_point(lv_indev_get_act(), &point);
    gesture_scroll(point.y - touch_y_);
    touching_ = false;
}
void view::notify(std::string_view action) {
    if (callback_ != nullptr)
        callback_(action, context_);
}
} // namespace cyberdeck_editor_view
