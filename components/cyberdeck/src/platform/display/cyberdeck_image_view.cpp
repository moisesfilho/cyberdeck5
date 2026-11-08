#include "platform/display/cyberdeck_image_view.h"

#include "apps/image/cyberdeck_image_decode.h"
#include "lvgl.h"

#include <algorithm>
#include <cstdlib>
#include <new>
#include <string>
#include <utility>

extern const lv_font_t cyberdeck_font;

namespace cyberdeck_image_view {
namespace {

constexpr int kMinimumLineHeight = 18;
constexpr int kInset = 4;
constexpr int kBandGap = 2;
const char *kFooterHint = "left/right browse  Esc close";
const lv_color_t kBlack = lv_color_hex(0x000000);
const lv_color_t kWhite = lv_color_hex(0xF2F2F2);
const lv_color_t kMuted = lv_color_hex(0x8A8A8A);

void style(lv_obj_t *object, lv_color_t text) {
    lv_obj_set_style_bg_color(object, kBlack, 0);
    lv_obj_set_style_text_color(object, text, 0);
    lv_obj_set_style_text_font(object, &cyberdeck_font, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
}

void disable_scrolling(lv_obj_t *object) {
    lv_obj_set_scroll_dir(object, LV_DIR_NONE);
    lv_obj_set_scroll_chain(object, false);
    lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
}

} // namespace

bool view::create(lv_obj_t *parent) {
    if (parent == nullptr)
        return false;
    if (surface_ != nullptr)
        return true;
    parent_ = parent;
    lv_obj_add_event_cb(parent_, parent_resized, LV_EVENT_SIZE_CHANGED, this);
    surface_ = lv_obj_create(parent);
    if (surface_ == nullptr) {
        destroy();
        return false;
    }
    disable_scrolling(surface_);
    lv_obj_set_ignore_layout(surface_, true);
    lv_obj_set_pos(surface_, 0, 0);
    lv_obj_set_size(surface_, lv_obj_get_width(parent_), lv_obj_get_height(parent_));
    lv_obj_set_layout(surface_, LV_LAYOUT_NONE);
    lv_obj_set_style_pad_all(surface_, 0, 0);
    lv_obj_set_style_bg_color(surface_, kBlack, 0);
    lv_obj_set_style_border_width(surface_, 0, 0);
    lv_obj_add_event_cb(surface_, resized, LV_EVENT_SIZE_CHANGED, this);

    title_ = lv_label_create(surface_);
    position_ = lv_label_create(surface_);
    footer_ = lv_label_create(surface_);
    image_ = lv_image_create(surface_);
    if (title_ == nullptr || position_ == nullptr || footer_ == nullptr || image_ == nullptr) {
        destroy();
        return false;
    }
    style(title_, kWhite);
    style(position_, kWhite);
    style(footer_, kMuted);
    lv_label_set_text(title_, "");
    lv_label_set_text(position_, "");
    lv_label_set_text(footer_, kFooterHint);
    lv_label_set_long_mode(title_, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_long_mode(position_, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_long_mode(footer_, LV_LABEL_LONG_MODE_DOTS);
    disable_scrolling(title_);
    disable_scrolling(position_);
    disable_scrolling(footer_);
    /* The default (light) theme paints a background, border and radius on
     * every widget; the bitmap must reach the panel untouched, so every
     * decoration and every padding is zeroed and the alpha stays opaque. */
    lv_obj_set_style_bg_opa(image_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(image_, 0, 0);
    lv_obj_set_style_radius(image_, 0, 0);
    lv_obj_set_style_outline_width(image_, 0, 0);
    lv_obj_set_style_shadow_width(image_, 0, 0);
    lv_obj_set_style_pad_all(image_, 0, 0);
    disable_scrolling(image_);
    lv_obj_set_layout(surface_, LV_LAYOUT_NONE);
    layout();
    return true;
}

void view::destroy() {
    if (parent_ != nullptr)
        lv_obj_remove_event_cb(parent_, parent_resized);
    /* No pointer to the frame may survive the widget: repoint first, then
     * release, then delete the tree. */
    if (image_ != nullptr)
        lv_image_set_src(image_, nullptr);
    if (surface_ != nullptr)
        lv_obj_delete(surface_);
    surface_ = title_ = position_ = footer_ = image_ = nullptr;
    parent_ = nullptr;
    clear_image();
    if (descriptor_ != nullptr) {
        delete static_cast<lv_image_dsc_t *>(descriptor_);
        descriptor_ = nullptr;
    }
    status_text_.clear();
    placement_ = fit_result{};
}

void view::clear_image() {
    if (pixels_ != nullptr) {
        cyberdeck_image::decoded_image owned{};
        owned.pixels = pixels_;
        owned.pixel_bytes = pixel_bytes_;
        owned.width = image_width_;
        owned.height = image_height_;
        cyberdeck_image::release_decoded(owned);
    }
    pixels_ = nullptr;
    pixel_bytes_ = 0;
    image_width_ = 0;
    image_height_ = 0;
}

bool view::set_image(cyberdeck_image::published_image &image) {
    if (surface_ == nullptr || image_ == nullptr || image.pixels == nullptr || image.width == 0 || image.height == 0)
        return false;
    if (descriptor_ == nullptr) {
        descriptor_ = new (std::nothrow) lv_image_dsc_t{};
        if (descriptor_ == nullptr)
            return false;
    }
    auto *descriptor = static_cast<lv_image_dsc_t *>(descriptor_);
    /* Repoint and drop the previous frame: after `lv_image_set_src()` the
     * widget no longer references the old buffer. */
    lv_image_set_src(image_, nullptr);
    clear_image();

    std::fill_n(reinterpret_cast<std::uint8_t *>(descriptor), sizeof(lv_image_dsc_t), 0);
    descriptor->header.magic = LV_IMAGE_HEADER_MAGIC;
    descriptor->header.cf = LV_COLOR_FORMAT_RGB565;
    descriptor->header.flags = 0;
    descriptor->header.w = static_cast<std::uint32_t>(image.width);
    descriptor->header.h = static_cast<std::uint32_t>(image.height);
    descriptor->header.stride = static_cast<std::uint32_t>(image.width * 2u);
    descriptor->data_size = static_cast<std::uint32_t>(image.pixel_bytes);
    descriptor->data = reinterpret_cast<const std::uint8_t *>(image.pixels);
    descriptor->reserved = nullptr;
    descriptor->reserved_2 = nullptr;

    pixels_ = image.pixels;
    pixel_bytes_ = image.pixel_bytes;
    image_width_ = image.width;
    image_height_ = image.height;
    /* Ownership moved: the caller must not release this frame any more. */
    image.pixels = nullptr;
    image.pixel_bytes = 0;

    lv_image_set_src(image_, descriptor);
    lv_image_set_scale(image_, cyberdeck_image::k_zoom_identity);
    layout();
    return true;
}

void view::set_status(std::string_view name, std::string_view position) {
    if (title_ == nullptr)
        return;
    status_text_.assign(name.data(), name.size());
    const std::string text(name);
    lv_label_set_text(title_, text.c_str());
    const std::string indicator(position);
    lv_label_set_text(position_, indicator.c_str());
    layout();
}

void view::layout() {
    if (surface_ == nullptr)
        return;
    const int width = static_cast<int>(std::max<int32_t>(1, lv_obj_get_content_width(surface_)));
    const int height = static_cast<int>(std::max<int32_t>(1, lv_obj_get_content_height(surface_)));
    const int line = static_cast<int>(std::max<int32_t>(kMinimumLineHeight, lv_font_get_line_height(&cyberdeck_font)));
    const int top = kInset;
    const int bottom_y = height - kInset - line;
    lv_obj_set_pos(title_, kInset, top);
    lv_obj_set_size(title_, std::max(1, width - 2 * kInset - 96), line);
    lv_obj_set_pos(position_, std::max(kInset, width - kInset - 96), top);
    lv_obj_set_size(position_, 96, line);
    lv_obj_set_pos(footer_, kInset, bottom_y);
    lv_obj_set_size(footer_, std::max(1, width - 2 * kInset), line);

    if (!has_image()) {
        lv_obj_set_pos(image_, 0, 0);
        lv_obj_set_size(image_, 0, 0);
        placement_ = fit_result{};
        return;
    }
    const int area_x = kInset;
    const int area_y = top + line + kBandGap;
    const int area_w = std::max(1, width - 2 * kInset);
    const int area_h = std::max(1, bottom_y - kBandGap - area_y);
    placement_ = cyberdeck_image::fit_to_area(image_width_, image_height_, area_x, area_y, area_w, area_h);
    if (!placement_.valid) {
        lv_obj_set_pos(image_, 0, 0);
        lv_obj_set_size(image_, 0, 0);
        return;
    }
    lv_obj_set_pos(image_, placement_.object_x, placement_.object_y);
    lv_obj_set_size(image_, static_cast<std::int32_t>(placement_.object_width),
                    static_cast<std::int32_t>(placement_.object_height));
    lv_image_set_scale(image_, placement_.zoom);
}

void view::resized(lv_event_t *event) {
    auto *self = static_cast<view *>(lv_event_get_user_data(event));
    if (self != nullptr)
        self->layout();
}

void view::parent_resized(lv_event_t *event) {
    auto *self = static_cast<view *>(lv_event_get_user_data(event));
    if (self == nullptr || self->surface_ == nullptr || self->parent_ == nullptr)
        return;
    lv_obj_set_pos(self->surface_, 0, 0);
    lv_obj_set_size(self->surface_, lv_obj_get_width(self->parent_), lv_obj_get_height(self->parent_));
    self->layout();
}

} // namespace cyberdeck_image_view
