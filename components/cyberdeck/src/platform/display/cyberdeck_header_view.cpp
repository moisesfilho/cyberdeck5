#include "platform/display/cyberdeck_header_view.h"

#include "esp_app_desc.h"
#include "platform/display/cyberdeck_wifi_icon.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

extern const lv_font_t cyberdeck_font;

namespace cyberdeck_header_view {
namespace {

constexpr int32_t CYBERDECK_BLE_HEADER_Y_OFFSET = 10;
constexpr int32_t CYBERDECK_PERCENTAGE_BOLD_OFFSET = 1;
const lv_color_t kBlack = lv_color_hex(0x000000);
const lv_color_t kWhite = lv_color_hex(0xF2F2F2);
const lv_color_t kMuted = lv_color_hex(0x8A8A8A);

bool is_firmware_version_supported(const char *version, std::size_t field_size) {
    if (version == nullptr || field_size == 0)
        return false;
    const auto is_digit = [](char value) { return value >= '0' && value <= '9'; };
    std::size_t index = 0;
    for (int segment = 0; segment < 3; ++segment) {
        const std::size_t segment_start = index;
        if (index >= field_size)
            return false;
        if (!is_digit(version[index]))
            return false;
        while (index < field_size && is_digit(version[index]))
            ++index;
        if (version[segment_start] == '0' && index - segment_start > 1)
            return false;
        if (segment < 2) {
            if (index >= field_size || version[index] != '.')
                return false;
            ++index;
        }
    }
    return index < field_size && version[index] == '\0';
}

bool title_fits_portrait(const char *text) {
    constexpr uint32_t kMinimumPortraitWidth = 720;
    constexpr uint32_t kTitleWidthPercent = 30;
    constexpr uint32_t kGlyphWidthFraction = 16;
    constexpr uint32_t kMaximumTitleWidth = kMinimumPortraitWidth * kTitleWidthPercent * kGlyphWidthFraction / 100;

    uint32_t width = 0;
    for (std::size_t index = 0; text[index] != '\0'; ++index) {
        const uint32_t next = text[index + 1] == '\0' ? 0 : static_cast<unsigned char>(text[index + 1]);
        width += lv_font_get_glyph_width(&cyberdeck_font, static_cast<unsigned char>(text[index]), next);
        if (width > kMaximumTitleWidth)
            return false;
    }
    return true;
}

void disable_scrolling(lv_obj_t *object)
{
    lv_obj_set_scroll_dir(object, LV_DIR_NONE);
    lv_obj_set_scroll_chain(object, false);
    lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
}

void music_clicked(lv_event_t *event) {
    auto *view = static_cast<cyberdeck_header_view::view *>(lv_event_get_user_data(event));
    if (view != nullptr) view->dispatch_music_action();
}

void style_base(lv_obj_t *object, lv_color_t background, lv_color_t text)
{
    lv_obj_set_style_bg_color(object, background, 0);
    lv_obj_set_style_text_color(object, text, 0);
    lv_obj_set_style_border_width(object, 0, 0);
}

void apply_header_font(lv_obj_t *object)
{
    // REQ-HEADER-FONT-01/02 / AC-HEADER-FONT-01: every header text label,
    // including the battery labels, uses the shared bitmap font.
    lv_obj_set_style_text_font(object, &cyberdeck_font, 0);
}

const char *battery_indicator_symbol(cyberdeck_battery_view::power_glyph glyph)
{
    switch (glyph) {
    case cyberdeck_battery_view::power_glyph::charging:
        return LV_SYMBOL_CHARGE;
    case cyberdeck_battery_view::power_glyph::battery:
        return LV_SYMBOL_BATTERY_FULL;
    case cyberdeck_battery_view::power_glyph::external:
        return LV_SYMBOL_MINUS;
    case cyberdeck_battery_view::power_glyph::none:
    default:
        return "";
    }
}

} // namespace

bool view::create(lv_obj_t *parent)
{
    if (parent == nullptr) return false;

    const esp_app_desc_t *app_description = esp_app_get_description();
    if (app_description == nullptr ||
        !is_firmware_version_supported(app_description->version, sizeof(app_description->version)))
        return false;
    char title_text[32] = {};
    const int title_length = std::snprintf(title_text, sizeof(title_text), "CYBERDECK5 %s", app_description->version);
    if (title_length < 0 || static_cast<std::size_t>(title_length) >= sizeof(title_text)) {
        return false;
    }
    if (!title_fits_portrait(title_text))
        return false;

    lv_obj_t *header = lv_obj_create(parent);
    if (header == nullptr) return false;
    lv_obj_set_size(header, lv_pct(100), 42);
    style_base(header, kBlack, kWhite);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_pad_column(header, 0, 0);
    lv_obj_set_style_pad_row(header, 0, 0);
    lv_obj_set_layout(header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    disable_scrolling(header);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, title_text);
    lv_obj_set_width(title, LV_PCT(30));
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
    style_base(title, kBlack, kWhite);
    apply_header_font(title);

    s_clock_status = lv_label_create(header);
    lv_label_set_text(s_clock_status, "");
    lv_obj_set_width(s_clock_status, LV_PCT(40));
    lv_obj_set_style_text_align(s_clock_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_clock_status, &cyberdeck_font, 0);
    lv_label_set_long_mode(s_clock_status, LV_LABEL_LONG_CLIP);
    style_base(s_clock_status, kBlack, kMuted);

    lv_obj_t *right = lv_obj_create(header);
    lv_obj_set_width(right, LV_PCT(30));
    lv_obj_set_height(right, 42);
    style_base(right, kBlack, kWhite);
    lv_obj_set_style_pad_all(right, 0, 0);
    lv_obj_set_style_pad_column(right, 2, 0);
    lv_obj_set_style_pad_row(right, 0, 0);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    disable_scrolling(right);

    s_ble_cell = lv_obj_create(right);
    lv_obj_set_size(s_ble_cell, LV_SIZE_CONTENT, 42);
    lv_obj_set_layout(s_ble_cell, LV_LAYOUT_NONE);
    lv_obj_set_flex_grow(s_ble_cell, 0);
    lv_obj_set_style_pad_all(s_ble_cell, 0, 0);
    lv_obj_set_style_pad_right(s_ble_cell, 4, 0);
    style_base(s_ble_cell, kBlack, kWhite);
    lv_obj_set_style_bg_opa(s_ble_cell, LV_OPA_TRANSP, 0);
    disable_scrolling(s_ble_cell);

    s_ble_status = lv_label_create(s_ble_cell);
    lv_label_set_text(s_ble_status, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_size(s_ble_status, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    style_base(s_ble_status, kBlack, kWhite);
    apply_header_font(s_ble_status);
    lv_obj_set_style_bg_opa(s_ble_status, LV_OPA_TRANSP, 0);
    lv_obj_set_y(s_ble_status, CYBERDECK_BLE_HEADER_Y_OFFSET);
    lv_obj_set_hidden(s_ble_status, true);
    lv_obj_set_hidden(s_ble_cell, true);

    s_wifi_status = cyberdeck_wifi_icon_create(right);
    if (s_wifi_status == nullptr) return false;
    lv_obj_set_width(s_wifi_status, static_cast<int32_t>(
        (CYBERDECK_WIFI_ICON_RADIUS_2 + CYBERDECK_WIFI_ICON_DEFAULT_THICKNESS) * 2.0f));
    lv_obj_set_flex_grow(s_wifi_status, 0);

    s_battery_status = lv_obj_create(right);
    lv_obj_set_size(s_battery_status, LV_SIZE_CONTENT, 42);
    lv_obj_set_flex_grow(s_battery_status, 0);
    style_base(s_battery_status, kBlack, kWhite);
    lv_obj_set_style_bg_opa(s_battery_status, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_battery_status, 0, 0);
    lv_obj_set_style_pad_left(s_battery_status, 11, 0);
    lv_obj_set_flex_flow(s_battery_status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_battery_status, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    disable_scrolling(s_battery_status);

    s_battery_symbol = lv_label_create(s_battery_status);
    lv_label_set_text(s_battery_symbol, "");
    style_base(s_battery_symbol, kBlack, kMuted);
    apply_header_font(s_battery_symbol);
    s_battery_percentage = lv_label_create(s_battery_status);
    lv_label_set_text(s_battery_percentage, "");
    lv_obj_set_style_pad_column(s_battery_status, 3, 0);
    style_base(s_battery_percentage, kBlack, kWhite);
    apply_header_font(s_battery_percentage);

    /* REQ-HEADER-FONT-03 / AC-HEADER-FONT-01: keep the real percentage label
     * unchanged and paint the same bitmap label once more one pixel to the
     * right. The overlay is ignored by flex layout, so this adds weight
     * without changing the font metrics or header geometry. */
    s_battery_percentage_bold = lv_label_create(s_battery_status);
    lv_label_set_text(s_battery_percentage_bold, "");
    style_base(s_battery_percentage_bold, kBlack, kWhite);
    apply_header_font(s_battery_percentage_bold);
    lv_obj_set_ignore_layout(s_battery_percentage_bold, true);
    lv_obj_set_hidden(s_battery_percentage_bold, true);
    lv_obj_set_hidden(s_battery_status, true);

    s_music_button = lv_btn_create(right);
    if (s_music_button == nullptr)
        return false;
    lv_obj_set_size(s_music_button, 26, 36);
    lv_obj_set_style_bg_color(s_music_button, kBlack, 0);
    lv_obj_set_style_bg_opa(s_music_button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_music_button, 0, 0);
    lv_obj_set_style_radius(s_music_button, 0, 0);
    lv_obj_set_style_outline_width(s_music_button, 0, 0);
    /* The default theme is light (`CONFIG_LV_THEME_DEFAULT_DARK` is unset), so
     * every `lv_btn` inherits a grey drop shadow even with a transparent fill.
     * Left alone it paints a capsule around the glyph. */
    lv_obj_set_style_shadow_width(s_music_button, 0, 0);
    lv_obj_set_style_shadow_opa(s_music_button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_music_button, 0, 0);
    lv_obj_set_hidden(s_music_button, true);
    lv_obj_add_event_cb(s_music_button, music_clicked, LV_EVENT_CLICKED, this);
    lv_obj_t *music_icon = lv_label_create(s_music_button);
    if (music_icon == nullptr)
        return false;
    /* ASCII on purpose: `cyberdeck_font` only covers 0x20-0x7E, 0xA0-0xFF and
     * the sparse Font Awesome block, so LV_SYMBOL_PLAY (U+F04B) would render
     * as a missing-glyph box. */
    lv_label_set_text(music_icon, ">");
    style_base(music_icon, kBlack, kWhite);
    apply_header_font(music_icon);
    lv_obj_center(music_icon);

    lv_obj_update_layout(header);
    lv_obj_update_layout(right);
    cyberdeck_wifi_icon_update_layout(s_wifi_status, lv_obj_get_width(s_wifi_status));
    return true;
}

void view::update_clock(const char *text)
{
    if (s_clock_status != nullptr)
        lv_label_set_text(s_clock_status, text != nullptr ? text : "");
}

void view::update_ble(bool connected)
{
    if (s_ble_cell == nullptr || s_ble_status == nullptr) return;
    if (connected) {
        lv_obj_set_hidden(s_ble_status, false);
        lv_obj_set_hidden(s_ble_cell, false);
    } else {
        lv_obj_set_hidden(s_ble_status, true);
        lv_obj_set_hidden(s_ble_cell, true);
    }
}

void view::update_wifi(bool lit)
{
    if (s_wifi_status != nullptr) {
        cyberdeck_wifi_icon_set_color(s_wifi_status, lit ? 0xF2F2F2 : 0x8A8A8A);
    }
}

void view::update_battery(const cyberdeck_battery_view::presentation &presentation)
{
    if (s_battery_status == nullptr || s_battery_symbol == nullptr ||
        s_battery_percentage == nullptr || s_battery_percentage_bold == nullptr)
        return;
    if (!presentation.visible) {
        lv_obj_set_hidden(s_battery_status, true);
        return;
    }
    char percentage[8] = {};
    std::snprintf(percentage, sizeof(percentage), "%d%%",
                  static_cast<int>(presentation.percentage));
    lv_label_set_text(s_battery_symbol, battery_indicator_symbol(presentation.glyph));
    lv_label_set_text(s_battery_percentage,
                      presentation.show_percentage ? percentage : "");
    lv_label_set_text(s_battery_percentage_bold,
                      presentation.show_percentage ? percentage : "");
    lv_obj_update_layout(s_battery_status);
    lv_obj_set_pos(s_battery_percentage_bold,
                   lv_obj_get_x(s_battery_percentage) + CYBERDECK_PERCENTAGE_BOLD_OFFSET,
                   lv_obj_get_y(s_battery_percentage));
    lv_obj_set_hidden(s_battery_percentage_bold, !presentation.show_percentage);
    lv_obj_set_hidden(s_battery_status, false);
}

void view::update_music(bool active) {
    if (s_music_button != nullptr)
        lv_obj_set_hidden(s_music_button, !active);
}

void view::set_music_action(void (*action)(void *), void *context) {
    music_action_ = action;
    music_context_ = context;
}

void view::dispatch_music_action() {
    if (music_action_ != nullptr)
        music_action_(music_context_);
}

} // namespace cyberdeck_header_view
