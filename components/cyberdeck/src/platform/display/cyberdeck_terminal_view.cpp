#include "platform/display/cyberdeck_terminal_view.h"

#include <algorithm>
#include <array>
#include <cstdlib>

extern const lv_font_t cyberdeck_font;

namespace cyberdeck_terminal_view {
namespace {

const lv_color_t kSurface = lv_color_hex(0x0A0A0A);
const lv_color_t kBorder = lv_color_hex(0x2A2A2A);
const lv_color_t kWhite = lv_color_hex(0xF2F2F2);
constexpr int32_t kSurfaceBorder = 1;
constexpr int32_t kSurfacePadding = 12;
constexpr std::size_t kMaxLines = 64;

void style_terminal(lv_obj_t *object)
{
    lv_obj_set_style_bg_color(object, kSurface, 0);
    lv_obj_set_style_text_color(object, kWhite, 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_border_color(object, kBorder, 0);
    lv_obj_set_style_pad_all(object, 12, 0);
}

std::size_t visible_line_count(lv_obj_t *surface, lv_obj_t *parent,
                               int32_t line_height)
{
    lv_obj_update_layout(surface);
    int32_t content_height = lv_obj_get_content_height(surface);
    if (content_height <= 0 && parent != nullptr) {
        /* A newly composed flex child can report zero before its first layout
         * pass.  Use the parent's resolved content height so slot placement is
         * deterministic in that window as well as after LVGL lays it out. */
        content_height = lv_obj_get_content_height(parent) - kSurfaceBorder -
                         (2 * kSurfacePadding);
    }
    return std::clamp<std::size_t>(
        static_cast<std::size_t>(content_height > 0 ? content_height / line_height : 1),
        1, kMaxLines);
}

std::size_t codepoint_length(const std::string &text, std::size_t offset)
{
    const unsigned char first = static_cast<unsigned char>(text[offset]);
    if (first < 0x80) return 1;
    if (first >= 0xF0 && offset + 4 <= text.size()) return 4;
    if (first >= 0xE0 && offset + 3 <= text.size()) return 3;
    if (first >= 0xC2 && offset + 2 <= text.size()) return 2;
    return 1;
}

uint32_t codepoint_at(const std::string &text, std::size_t offset)
{
    const std::size_t length = codepoint_length(text, offset);
    const unsigned char first = static_cast<unsigned char>(text[offset]);
    if (length == 1) return first;
    uint32_t codepoint = first & (length == 4 ? 0x07 : length == 3 ? 0x0F : 0x1F);
    for (std::size_t i = 1; i < length; ++i)
        codepoint = (codepoint << 6) | (static_cast<unsigned char>(text[offset + i]) & 0x3F);
    return codepoint;
}

int32_t glyph_advance(uint32_t codepoint, uint32_t next)
{
    lv_font_glyph_dsc_t glyph{};
    if (!lv_font_get_glyph_dsc(&cyberdeck_font, &glyph, codepoint, next)) return 0;
    return glyph.adv_w;
}

void geometry_changed_cb(lv_event_t *event)
{
    auto *terminal = static_cast<view *>(lv_event_get_user_data(event));
    if (terminal != nullptr && terminal->geometry_callback() != nullptr)
        terminal->geometry_callback()(event);
}

void touch_pressed_cb(lv_event_t *event)
{
    auto *terminal = static_cast<view *>(lv_event_get_user_data(event));
    if (terminal == nullptr) return;
    terminal->begin_touch();
}

void touch_released_cb(lv_event_t *event)
{
    auto *terminal = static_cast<view *>(lv_event_get_user_data(event));
    if (terminal == nullptr) return;
    terminal->scroll_from_touch();
}

std::string truncate_left_utf8(const std::string &text, std::size_t limit)
{
    if (text.size() <= limit) return text;
    std::size_t begin = text.size() - limit;
    while (begin < text.size() &&
           (static_cast<unsigned char>(text[begin]) & 0xC0U) == 0x80U) {
        ++begin;
    }
    return text.substr(begin);
}

} // namespace

bool view::create(lv_obj_t *screen, lv_obj_t *parent, std::size_t max_length,
                  const callbacks &callbacks)
{
    if (screen == nullptr || parent == nullptr) return false;

    s_parent = parent;
    s_geometry_changed = callbacks.geometry_changed;
    s_max_length = max_length;
    s_surface = lv_obj_create(parent);
    if (s_surface == nullptr) return false;
    lv_obj_set_width(s_surface, LV_PCT(100));
    lv_obj_set_flex_grow(s_surface, 1);
    lv_obj_set_layout(s_surface, LV_LAYOUT_NONE);
    style_terminal(s_surface);
    lv_obj_set_style_border_width(s_surface, 0, 0);
    lv_obj_add_flag(s_surface, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_surface, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_scroll_dir(s_surface, LV_DIR_NONE);
    lv_obj_set_scroll_chain(s_surface, false);
    lv_obj_set_scrollbar_mode(s_surface, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_surface, geometry_changed_cb, LV_EVENT_SIZE_CHANGED, this);
    lv_obj_add_event_cb(s_surface, touch_pressed_cb, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(s_surface, touch_released_cb, LV_EVENT_RELEASED, this);

    const int32_t line_height = std::max<int32_t>(1, lv_font_get_line_height(&cyberdeck_font));
    s_line_count = visible_line_count(s_surface, s_parent, line_height);
    for (std::size_t i = 0; i < k_max_lines; ++i) {
        s_lines[i] = lv_label_create(s_surface);
        if (s_lines[i] == nullptr) return false;
        lv_obj_set_width(s_lines[i], LV_PCT(100));
        lv_obj_set_height(s_lines[i], line_height);
        lv_obj_set_pos(s_lines[i], 0, static_cast<int32_t>(i) * line_height);
        lv_obj_set_hidden(s_lines[i], i >= s_line_count);
        lv_label_set_long_mode(s_lines[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(s_lines[i], &cyberdeck_font, 0);
        lv_obj_set_style_text_color(s_lines[i], kWhite, 0);
        lv_label_set_text(s_lines[i], "");
    }

    if (callbacks.focused != nullptr)
        lv_obj_add_event_cb(s_surface, callbacks.focused, LV_EVENT_FOCUSED, nullptr);

    s_terminal = lv_textarea_create(parent);
    if (s_terminal == nullptr) return false;
    lv_obj_set_width(s_terminal, LV_PCT(100));
    lv_obj_set_height(s_terminal, LV_SIZE_CONTENT);
    style_terminal(s_terminal);
    /* The textarea is an LVGL input target only.  The terminal surface above
     * owns every visible character, including the prompt and cursor. */
    lv_obj_set_hidden(s_terminal, true);
    lv_textarea_set_one_line(s_terminal, true);
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

std::size_t view::viewport_capacity() const
{
    /* Four bytes per UTF-8 codepoint is a conservative bounded budget for a
     * fixed line slot.  The number of slots comes from the actual font and
     * surface geometry, so resizing changes the retained window. */
    constexpr std::size_t kBytesPerLine = 256;
    return std::min(k_viewport_bytes,
                    std::max<std::size_t>(kBytesPerLine, s_line_count * kBytesPerLine));
}

void view::render(const std::string &text)
{
    if (s_surface == nullptr || s_line_count == 0) return;
    s_rendered_text = text;

    const int32_t line_height = std::max<int32_t>(1, lv_font_get_line_height(&cyberdeck_font));
    const std::size_t visible_lines = visible_line_count(s_surface, s_parent, line_height);
    s_line_count = visible_lines;
    for (std::size_t i = 0; i < k_max_lines; ++i) {
        lv_obj_set_hidden(s_lines[i], i >= visible_lines);
        if (i < visible_lines) {
            lv_obj_set_height(s_lines[i], line_height);
            lv_obj_set_pos(s_lines[i], 0, static_cast<int32_t>(i) * line_height);
        }
    }

    const std::string bounded_text = truncate_left_utf8(text, s_max_length);
    int32_t available_width = lv_obj_get_content_width(s_surface);
    if (available_width <= 0 && s_parent != nullptr)
        available_width = lv_obj_get_content_width(s_parent) - (2 * kSurfacePadding);
    available_width = std::max<int32_t>(1, available_width);
    std::array<std::size_t, k_max_lines + 1> starts{};
    std::array<bool, k_max_lines + 1> explicit_breaks{};
    const auto scan_lines = [&](auto &&on_line) {
        std::size_t line = 0;
        std::size_t line_start = 0;
        int32_t line_width = 0;
        for (std::size_t i = 0; i < bounded_text.size();) {
            if (bounded_text[i] == '\n') {
                on_line(line++, line_start, true);
                line_start = i + 1;
                line_width = 0;
                ++i;
                continue;
            }
            const std::size_t length = codepoint_length(bounded_text, i);
            const uint32_t codepoint = codepoint_at(bounded_text, i);
            const uint32_t next = i + length < bounded_text.size()
                                      ? codepoint_at(bounded_text, i + length)
                                      : 0;
            const int32_t width = glyph_advance(codepoint, next);
            if (line_width != 0 && line_width + width > available_width) {
                on_line(line++, line_start, false);
                line_start = i;
                line_width = 0;
            }
            line_width += width;
            i += length;
        }
        on_line(line, line_start, false);
        return line + 1;
    };
    const std::size_t count = scan_lines([](std::size_t, std::size_t, bool) {});
    const std::size_t maximum_offset = count > visible_lines ? count - visible_lines : 0;
    const bool was_at_bottom = !s_previous_line_total || s_scroll_offset == 0;
    s_scroll_offset = was_at_bottom ? 0 : std::min(s_scroll_offset, maximum_offset);
    s_previous_line_total = count;
    const std::size_t first_line = maximum_offset > s_scroll_offset
                                       ? maximum_offset - s_scroll_offset : 0;
    scan_lines([&](std::size_t line, std::size_t start, bool explicit_break) {
        if (line >= first_line && line - first_line < starts.size()) {
            starts[line - first_line] = start;
            explicit_breaks[line - first_line] = explicit_break;
        }
    });
    const std::size_t first_slot = count < visible_lines ? visible_lines - count : 0;
    for (std::size_t slot = 0; slot < visible_lines; ++slot) {
        if (slot < first_slot) {
            lv_label_set_text(s_lines[slot], "");
            continue;
        }
        const std::size_t source = first_line + slot - first_slot;
        const std::size_t relative_source = source - first_line;
        const std::size_t begin = starts[relative_source];
        const std::size_t end = source + 1 < count
                                    ? starts[relative_source + 1] -
                                          (explicit_breaks[relative_source] ? 1 : 0)
                                    : bounded_text.size();
        lv_label_set_text(s_lines[slot], bounded_text.substr(begin, end - begin).c_str());
    }
    for (std::size_t slot = visible_lines; slot < k_max_lines; ++slot)
        lv_label_set_text(s_lines[slot], "");
}

void view::begin_touch()
{
    lv_indev_t *indev = lv_indev_active();
    if (indev == nullptr || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) return;
    lv_point_t point{};
    lv_indev_get_point(indev, &point);
    s_touch_start_y = point.y;
    s_touch_active = true;
}

void view::scroll_from_touch()
{
    lv_indev_t *indev = lv_indev_active();
    if (indev == nullptr || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) return;
    lv_point_t point{};
    lv_indev_get_point(indev, &point);
    if (!s_touch_active) return;

    const lv_coord_t delta = point.y - s_touch_start_y;
    const int32_t line_height = std::max<int32_t>(1, lv_font_get_line_height(&cyberdeck_font));
    const std::size_t lines = static_cast<std::size_t>(std::abs(delta) / line_height);
    const std::size_t maximum_offset = s_previous_line_total > s_line_count
                                           ? s_previous_line_total - s_line_count : 0;
    if (lines != 0) {
        if (delta > 0) s_scroll_offset = std::min(maximum_offset, s_scroll_offset + lines);
        else s_scroll_offset = s_scroll_offset > lines ? s_scroll_offset - lines : 0;
        s_touch_start_y = point.y;
        render(s_rendered_text);
    }
    s_touch_active = false;
}

} // namespace cyberdeck_terminal_view
