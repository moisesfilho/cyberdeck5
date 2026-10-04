#include "platform/display/cyberdeck_terminal_view.h"

#include <algorithm>
#include <array>

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
    s_surface = lv_obj_create(parent);
    if (s_surface == nullptr) return false;
    lv_obj_set_width(s_surface, LV_PCT(100));
    lv_obj_set_flex_grow(s_surface, 1);
    lv_obj_set_layout(s_surface, LV_LAYOUT_NONE);
    style_terminal(s_surface);
    lv_obj_set_style_border_width(s_surface, 0, 0);

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

    const std::string bounded_text = truncate_left_utf8(text, k_viewport_bytes);
    std::array<std::size_t, k_max_lines + 1> starts{};
    std::size_t count = 1;
    starts[0] = 0;
    for (std::size_t i = 0; i < bounded_text.size(); ++i) {
        if (bounded_text[i] != '\n') continue;
        if (count < starts.size()) {
            starts[count++] = i + 1;
        } else {
            std::move(starts.begin() + 1, starts.end(), starts.begin());
            starts.back() = i + 1;
        }
    }
    const std::size_t first_line = count > visible_lines ? count - visible_lines : 0;
    const std::size_t first_slot = count < visible_lines ? visible_lines - count : 0;
    for (std::size_t slot = 0; slot < visible_lines; ++slot) {
        if (slot < first_slot) {
            lv_label_set_text(s_lines[slot], "");
            continue;
        }
        const std::size_t source = first_line + slot - first_slot;
        const std::size_t begin = starts[source];
        const std::size_t end = source + 1 < count ? starts[source + 1] - 1 : bounded_text.size();
        lv_label_set_text(s_lines[slot], bounded_text.substr(begin, end - begin).c_str());
    }
    for (std::size_t slot = visible_lines; slot < k_max_lines; ++slot)
        lv_label_set_text(s_lines[slot], "");
}

} // namespace cyberdeck_terminal_view
