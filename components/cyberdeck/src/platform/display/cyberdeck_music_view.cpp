#include "platform/display/cyberdeck_music_view.h"
#include "lvgl.h"

#include <algorithm>
#include <cstdlib>
#include <string>

extern const lv_font_t cyberdeck_font;

namespace cyberdeck_music_view {
namespace {
constexpr const char *kActions[] = {"prev", "toggle", "next", "seek_back", "seek_forward", "volume_down", "volume_up", "command_open"};
constexpr const char *kLabels[] = {"Previous", "Play / Pause", "Next", "-10s", "+10s", "Vol -", "Vol +", "Command"};
constexpr int kMinimumButtonSize = 44;
/* `card()` applies `pad_all = 8` (plus a 1 px border). Children are positioned
 * relative to the card *content* area, which already starts at `border + pad`
 * from the card border, so `x = kCardInset` leaves `border + pad + inset` on
 * the left. The usable width is therefore `content - 2 * kCardInset`, which
 * keeps the same inset on the right; measuring the outer card width instead
 * counts the border/pad once and pushes the child 1 px past the card. */
constexpr int kCardInset = 8;
/* Fixed shortcut row of the player view. ASCII only: `cyberdeck_font` covers
 * 0x20-0x7E, 0xA0-0xFF and the sparse Font Awesome block, so arrow code points
 * (U+2190..U+2193) would be painted as missing-glyph boxes. It is a static
 * footer, so the command overlay reuses the same text.
 *
 * The flat row is the landscape variant. The 71-character hint needs 767.7 px
 * at the real `cyberdeck_font` advance (adv_w 173/16 = 10.8125 px/char) while
 * the portrait surface only offers 680 px of content, so portrait splits the
 * same shortcuts in two rows after "seek". The break is explicit: the layout
 * decides by orientation and never measures text at runtime. */
constexpr const char *kMusicControlsHint =
    "Enter play/pause  left/right seek  up/down volume  N/P track  Esc close";
constexpr const char *kMusicControlsHintPortrait =
    "Enter play/pause  left/right seek\nup/down volume  N/P track  Esc close";
const lv_color_t kBlack = lv_color_hex(0x000000);
const lv_color_t kSurface = lv_color_hex(0x0A0A0A);
const lv_color_t kBorder = lv_color_hex(0x2A2A2A);
const lv_color_t kSelected = lv_color_hex(0x303030);
const lv_color_t kWhite = lv_color_hex(0xF2F2F2);
const lv_color_t kMuted = lv_color_hex(0x8A8A8A);

void style(lv_obj_t *object, lv_color_t text) {
    lv_obj_set_style_bg_color(object, kSurface, 0);
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
void card(lv_obj_t *object) {
    lv_obj_set_style_bg_color(object, kSurface, 0);
    lv_obj_set_style_border_color(object, kBorder, 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_pad_all(object, 8, 0);
}
}

bool view::create(lv_obj_t *parent, callback on_action, void *context) {
    if (!parent) return false;
    if (surface_) return true;
    callback_ = on_action;
    context_ = context;
    parent_ = parent;
    lv_obj_add_event_cb(parent_, parent_resized, LV_EVENT_SIZE_CHANGED, this);
    surface_ = lv_obj_create(parent);
    if (!surface_) { destroy(); return false; }
    disable_scrolling(surface_);
    lv_obj_set_ignore_layout(surface_, true);
    lv_obj_set_pos(surface_, 0, 0);
    lv_obj_set_size(surface_, lv_obj_get_width(parent_), lv_obj_get_height(parent_));
    lv_obj_set_layout(surface_, LV_LAYOUT_NONE);
    lv_obj_set_style_pad_all(surface_, 8, 0);
    lv_obj_set_style_bg_color(surface_, kBlack, 0);
    lv_obj_set_style_border_color(surface_, kBorder, 0);
    lv_obj_add_event_cb(surface_, resized, LV_EVENT_SIZE_CHANGED, this);

    title_ = lv_label_create(surface_);
    now_playing_ = lv_obj_create(surface_);
    list_ = lv_obj_create(surface_);
    footer_ = lv_label_create(surface_);
    if (!title_ || !now_playing_ || !list_ || !footer_) { destroy(); return false; }
    disable_scrolling(title_);
    disable_scrolling(footer_);
    now_title_ = lv_label_create(now_playing_);
    browser_title_ = lv_label_create(list_);
    if (!now_title_ || !browser_title_) { destroy(); return false; }
    card(now_playing_); card(list_);
    disable_scrolling(now_playing_);
    disable_scrolling(now_title_);
    disable_scrolling(browser_title_);
    status_ = lv_label_create(now_playing_);
    time_ = lv_label_create(now_playing_);
    progress_ = lv_slider_create(now_playing_);
    volume_ = lv_label_create(now_playing_);
    command_ = lv_textarea_create(now_playing_);
    controls_ = lv_obj_create(now_playing_);
    if (!status_ || !time_ || !progress_ || !volume_ || !command_ || !controls_) { destroy(); return false; }
    disable_scrolling(status_);
    disable_scrolling(time_);
    disable_scrolling(progress_);
    disable_scrolling(volume_);
    disable_scrolling(command_);
    disable_scrolling(controls_);
    style(title_, kWhite); style(now_title_, kWhite); style(browser_title_, kWhite);
    style(status_, kWhite); style(time_, kWhite); style(volume_, kWhite); style(command_, kWhite);
    style(footer_, kMuted);
    lv_label_set_long_mode(status_, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_long_mode(footer_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_layout(controls_, LV_LAYOUT_NONE);
    lv_obj_set_style_bg_color(controls_, kSurface, 0);
    lv_obj_set_style_border_width(controls_, 0, 0);
    lv_obj_set_style_pad_all(controls_, 0, 0);
    lv_label_set_text(title_, "MUSIC / LOCAL LIBRARY");
    lv_label_set_text(now_title_, "NOW PLAYING");
    lv_label_set_text(browser_title_, "LIBRARY  /  64 TRACKS MAX");
    lv_label_set_text(volume_, "VOLUME");
    lv_label_set_long_mode(status_, LV_LABEL_LONG_WRAP);
    /* The default theme is light: it paints the slider INDICATOR with the
     * primary (blue), rounds MAIN/INDICATOR and draws a KNOB ball bigger than
     * the bar. The player is monochrome and the bar must stay a flat white
     * rectangle. It is still an `lv_slider` on purpose: the knob is invisible,
     * but tapping/dragging the bar still seeks (`progress_released` ->
     * `seek_ratio`), which an `lv_bar` would lose. The unfilled MAIN track uses
     * the card border grey instead of the card surface, so the rail stays
     * visible from start to end even when the indicator is empty. The theme
     * paints MAIN through `bg_color_primary_muted`, which sets `bg_opa` to
     * `LV_OPA_20` (lv_theme_default.c), so the border grey alone would be drawn
     * at 20% over the card and the rail would vanish: MAIN needs an explicit
     * opaque `bg_opa`. The INDICATOR does not need it because the theme already
     * uses `bg_color_primary` with `LV_OPA_COVER`. */
    lv_obj_set_style_bg_color(progress_, kBorder, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(progress_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(progress_, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(progress_, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress_, kWhite, LV_PART_INDICATOR);
    lv_obj_set_style_radius(progress_, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(progress_, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(progress_, 0, LV_PART_KNOB);
    lv_obj_set_style_radius(progress_, 0, LV_PART_KNOB);
    lv_obj_set_style_border_width(progress_, 0, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(progress_, 0, LV_PART_KNOB);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scroll_chain(list_, false);
    lv_slider_set_range(progress_, 0, 1000);
    lv_obj_add_event_cb(progress_, progress_released, LV_EVENT_RELEASED, this);
    lv_textarea_set_one_line(command_, true);
    lv_textarea_set_max_length(command_, 96);
    lv_textarea_set_placeholder_text(command_, "music help");
    lv_obj_set_hidden(command_, true);

    for (auto &track : tracks_) {
        track = lv_btn_create(list_);
        if (!track) { destroy(); return false; }
        disable_scrolling(track);
        lv_obj_t *label = lv_label_create(track);
        if (!label) { destroy(); return false; }
        disable_scrolling(label);
        lv_label_set_text(label, "");
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        style(label, kWhite);
        lv_obj_set_style_bg_color(track, kSurface, 0);
        lv_obj_set_style_border_width(track, 0, 0);
        /* The default (light) theme also rounds and shadows every `lv_btn`;
         * the row must stay a flat rectangle inside the library card. */
        lv_obj_set_style_radius(track, 0, 0);
        lv_obj_set_style_outline_width(track, 0, 0);
        lv_obj_set_style_shadow_width(track, 0, 0);
        lv_obj_set_style_shadow_opa(track, LV_OPA_TRANSP, 0);
        /* The default theme pads `btn` (PAD_SMALL/PAD_DEF), shrinking the
         * content area below the glyph height and clipping the track name. */
        lv_obj_set_style_pad_all(track, 0, 0);
        lv_obj_set_style_pad_row(track, 0, 0);
        lv_obj_set_style_pad_column(track, 0, 0);
        lv_obj_set_clickable(track, true);
        lv_obj_add_event_cb(track, selected, LV_EVENT_CLICKED, this);
    }
    constexpr std::size_t count = sizeof(kActions) / sizeof(kActions[0]);
    for (std::size_t index = 0; index < count; ++index) {
        buttons_[index] = lv_btn_create(controls_);
        if (!buttons_[index]) { destroy(); return false; }
        disable_scrolling(buttons_[index]);
        auto *label = lv_label_create(buttons_[index]);
        if (!label) { destroy(); return false; }
        disable_scrolling(label);
        lv_label_set_text(label, kLabels[index]);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        style(label, kWhite);
        /* Control cells reuse the same elevated grey as the selected library
         * row (`kSelected`) with `kWhite` text; `kBlack` stays the surface
         * background of the view. */
        lv_obj_set_style_bg_color(buttons_[index], kSelected, 0);
        lv_obj_set_style_border_width(buttons_[index], 0, 0);
        /* Same light-theme `btn` chrome as the library rows: the rounded
         * capsule and grey shadow would frame the control in a foreign color. */
        lv_obj_set_style_radius(buttons_[index], 0, 0);
        lv_obj_set_style_shadow_width(buttons_[index], 0, 0);
        lv_obj_set_style_shadow_opa(buttons_[index], LV_OPA_TRANSP, 0);
        lv_obj_set_clickable(buttons_[index], true);
        lv_obj_add_event_cb(buttons_[index], clicked, LV_EVENT_CLICKED, this);
    }
    set_visible(false);
    layout();
    return true;
}

void view::destroy() {
    if (parent_) lv_obj_remove_event_cb(parent_, parent_resized);
    if (surface_) lv_obj_delete(surface_);
    surface_ = title_ = now_title_ = browser_title_ = status_ = now_playing_ = time_ = volume_ = progress_ = command_ = list_ = footer_ = controls_ = nullptr;
    parent_ = nullptr;
    tracks_.fill(nullptr); buttons_.fill(nullptr);
    callback_ = nullptr; context_ = nullptr; visible_ = false; command_open_ = false; selected_track_ = 0;
    footer_landscape_ = -1;
}

void view::set_visible(bool visible) {
    visible_ = visible;
    if (surface_) {
        lv_obj_set_hidden(surface_, !visible);
        if (visible)
            lv_obj_move_foreground(surface_);
    }
}

void view::render(std::string_view text) {
    if (!status_) return;
    const std::string value(text);
    const std::size_t queue_start = value.find("music queue:");
    const std::size_t status_end = queue_start == std::string::npos ? value.size() : queue_start;
    std::string status;
    std::size_t status_lines = 0;
    for (std::size_t begin = 0; begin < status_end && status_lines < 3;) {
        const std::size_t end = value.find('\n', begin);
        const std::size_t bounded_end = end == std::string::npos || end > status_end ? status_end : end;
        const std::string_view row(value.data() + begin, bounded_end - begin);
        begin = bounded_end + (bounded_end < status_end ? 1 : 0);
        if (row.empty() || row == "[ MUSIC / TUI ]" || row.rfind("progress: ", 0) == 0 ||
            row.rfind("volume: ", 0) == 0 || row.rfind("controls: ", 0) == 0 || row.rfind("queue: ", 0) == 0)
            continue;
        if (!status.empty()) status.push_back('\n');
        status.append(row.data(), row.size());
        ++status_lines;
    }
    lv_label_set_text(status_, status.c_str());
    const std::size_t progress_start = value.find("progress: ");
    if (progress_start != std::string::npos) {
        const std::size_t begin = progress_start + 10;
        const std::size_t end = value.find('s', begin);
        if (end != std::string::npos) {
            const int seconds = std::max(0, std::atoi(value.substr(begin, end - begin).c_str()));
            const std::size_t total_begin = value.find(" / ", end);
            const std::size_t total_end = total_begin == std::string::npos ? total_begin : value.find('s', total_begin + 3);
            const int total = total_end == std::string::npos ? 0 :
                std::max(0, std::atoi(value.substr(total_begin + 3, total_end - total_begin - 3).c_str()));
            lv_slider_set_value(progress_, total > 0 ? std::min(1000, seconds * 1000 / total) : 0, LV_ANIM_OFF);
        }
    }
    std::size_t line = 0;
    std::size_t offset = queue_start;
    if (offset != std::string::npos) {
        offset = value.find('\n', offset);
        while (offset != std::string::npos && line < tracks_.size()) {
            const std::size_t end = value.find('\n', offset + 1);
            if (end == std::string::npos) break;
            std::string_view row(value.data() + offset + 1, end - offset - 1);
            if (row.empty() || row.substr(0, 3) == "...") break;
            const std::size_t separator = row.find(". ");
            if (separator == std::string_view::npos) break;
            std::string label(row.substr(separator + 2));
            lv_label_set_text(lv_obj_get_child(tracks_[line], 0), label.c_str());
            lv_obj_set_hidden(tracks_[line], false);
            offset = end;
            ++line;
        }
    }
    for (; line < tracks_.size(); ++line) lv_obj_set_hidden(tracks_[line], true);
    const std::size_t selected_start = value.find("track: ");
    if (selected_start != std::string::npos) {
        const std::size_t begin = selected_start + 7;
        const std::size_t end = value.find('/', begin);
        if (end != std::string::npos) {
            const int selected = std::atoi(value.substr(begin, end - begin).c_str());
            if (selected > 0 && static_cast<std::size_t>(selected) <= line)
                selected_track_ = static_cast<std::size_t>(selected - 1);
        }
    }
    for (std::size_t i = 0; i < tracks_.size(); ++i)
        lv_obj_set_style_bg_color(tracks_[i], i == selected_track_ && !lv_obj_is_hidden(tracks_[i]) ? kSelected : kSurface, 0);
    const std::size_t volume_start = value.find("volume: ");
    if (volume_start != std::string::npos) {
        const std::size_t end = value.find_first_of(" \n", volume_start + 8);
        const std::string volume_text = "VOLUME  " + value.substr(volume_start + 8, end - volume_start - 8);
        lv_label_set_text(volume_, volume_text.c_str());
    }
    const std::size_t elapsed_start = value.find("progress: ");
    if (elapsed_start != std::string::npos) {
        const std::size_t begin = elapsed_start + 10;
        const std::size_t separator = value.find(" / ", begin);
        const std::size_t end = separator == std::string::npos ? separator : value.find('s', separator + 3);
        const std::string elapsed = separator == std::string::npos ? "--" : value.substr(begin, separator - begin);
        const std::string total = separator == std::string::npos || end == std::string::npos ? "--" : value.substr(separator + 3, end - separator - 3);
        const std::string time_text = elapsed + " / " + total + " s";
        lv_label_set_text(time_, time_text.c_str());
    } else {
        lv_label_set_text(time_, "-- / -- s");
    }
    layout();
}

void view::set_command_open(bool open) {
    command_open_ = open;
    if (command_) lv_obj_set_hidden(command_, !open);
    layout();
}
bool view::command_open() const { return command_open_; }
void view::set_command_text(std::string_view text) {
    if (!command_) return;
    const std::string bounded(text.substr(0, 96));
    lv_textarea_set_text(command_, bounded.c_str());
}
void view::set_command_cursor(std::size_t position) {
    if (command_) lv_textarea_set_cursor_pos(command_, static_cast<int32_t>(std::min(position, command_text().size())));
}
std::string_view view::command_text() const {
    const char *text = command_ ? lv_textarea_get_text(command_) : nullptr;
    return text ? std::string_view(text) : std::string_view{};
}
int view::progress_value() const { return progress_ ? lv_slider_get_value(progress_) : 0; }

void view::layout() {
    if (!surface_) return;
    const int width = static_cast<int>(std::max<int32_t>(1, lv_obj_get_content_width(surface_)));
    const int height = static_cast<int>(std::max<int32_t>(1, lv_obj_get_content_height(surface_)));
    const bool landscape = width > height;
    const int pad = 4;
    const int line = static_cast<int>(std::max<int32_t>(18, lv_font_get_line_height(&cyberdeck_font)));
    const int title_height = line;
    const int columns = landscape ? 3 : 2;
    const int control_rows = static_cast<int>((buttons_.size() + columns - 1) / columns);
    const int button_height = std::max(line, kMinimumButtonSize);
    /* Rows are separated by the same `pad` used between columns: without it the
     * three rows touch vertically and read as one continuous block (device:
     * 563..694) instead of three distinct cells. */
    const int row_gap = pad;
    const int controls_height = button_height * control_rows + row_gap * (control_rows - 1);
    const int command_height = command_open_ ? line + 8 : 0;
    const int progress_height = 20;
    int y = pad;
    lv_obj_set_pos(title_, pad, y); lv_obj_set_size(title_, width - pad * 2, title_height);
    lv_label_set_long_mode(title_, LV_LABEL_LONG_MODE_DOTS);
    y += title_height + pad;
    /* Fixed footer at the bottom of the surface. Both cards are measured against
     * `footer_y` so neither can grow into the reserved band; the previous
     * `height - y - pad` card formula would paint the shortcut row over the
     * library rows and the control block. The band is one text row in landscape
     * and two rows (plus the 2 px line slack) in portrait, where the flat hint
     * does not fit; the matching text is chosen by the same `landscape` flag. */
    const int footer_height = landscape ? line : 2 * line + 2;
    const int footer_y = height - pad - footer_height;
    const int gap = 8;
    const int now_width = landscape ? (width - 3 * pad - gap) * 45 / 100 : width - 2 * pad;
    const int card_height = landscape ? footer_y - pad - y : (footer_y - y - 2 * pad) / 2;
    const int browser_x = landscape ? pad + now_width + gap : pad;
    const int browser_y = landscape ? y : y + card_height + pad;
    const int browser_width = landscape ? width - browser_x - pad : width - 2 * pad;
    lv_obj_set_pos(now_playing_, pad, y); lv_obj_set_size(now_playing_, now_width, card_height);
    lv_obj_set_pos(list_, browser_x, browser_y); lv_obj_set_size(list_, browser_width, card_height);
    lv_obj_set_pos(footer_, pad, footer_y);
    lv_obj_set_size(footer_, width - pad * 2, footer_height);
    /* The footer text only depends on the orientation, and this function runs on
     * every status refresh (~4x/s). Re-applying the same string makes LVGL free
     * and re-allocate the label text and invalidate the 1232x22 px band on every
     * tick, so the setter is gated by the orientation already applied. */
    const std::int8_t footer_landscape = landscape ? 1 : 0;
    if (footer_landscape_ != footer_landscape) {
        lv_label_set_text(footer_, landscape ? kMusicControlsHint : kMusicControlsHintPortrait);
        footer_landscape_ = footer_landscape;
    }
    /* Derive the child width from the real card content area (AC-MUSIC-13):
     * the previous `now_width - 16` / `browser_width - 16` measured the outer
     * card and ignored that `x = 8` is relative to the content area, so the
     * progress bar, the right control column and the library rows ended 1 px
     * past the card border (device: bar right edge 574 vs card 573). */
    const int inner_width = std::max(1, static_cast<int>(lv_obj_get_content_width(now_playing_)) - 2 * kCardInset);
    const int browser_inner_width = std::max(1, static_cast<int>(lv_obj_get_content_width(list_)) - 2 * kCardInset);
    lv_obj_set_pos(now_title_, 8, 8); lv_obj_set_size(now_title_, inner_width, line);
    lv_label_set_long_mode(now_title_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(browser_title_, 8, 8); lv_obj_set_size(browser_title_, browser_inner_width, line);
    lv_label_set_long_mode(browser_title_, LV_LABEL_LONG_MODE_DOTS);
    int content_y = line + 12;
    lv_obj_set_pos(status_, 8, content_y); lv_obj_set_size(status_, inner_width, line * 3); content_y += line * 3 + pad;
    lv_obj_set_pos(progress_, 8, content_y); lv_obj_set_size(progress_, inner_width, progress_height); content_y += progress_height + pad;
    lv_obj_set_pos(time_, 8, content_y); lv_obj_set_size(time_, inner_width, line); content_y += line + pad;
    lv_obj_set_pos(volume_, 8, content_y); lv_obj_set_size(volume_, inner_width, line); content_y += line + pad;
    if (command_open_) { lv_obj_set_pos(command_, 8, content_y); lv_obj_set_size(command_, inner_width, command_height); content_y += command_height + pad; }
    else lv_obj_set_size(command_, 0, 0);
    /* Anchor the block by the card *content* height (AC-MUSIC-13): `x = 8` is
     * relative to the content area, so the bottom inset must be measured there
     * too. The previous `card_height - controls_area - 8` mixed the outer card
     * height with a content-relative y and pushed the last row onto the card
     * border (device: last row ended at 694, i.e. no 17 px inset, while the
     * left/right inset was 17 px). `card_height` still sizes the cards. */
    const int content_height = static_cast<int>(lv_obj_get_content_height(now_playing_));
    const int controls_area = std::min(controls_height, std::max(line, content_height - content_y - pad));
    lv_obj_set_pos(controls_, 8, content_height - controls_area - 8);
    lv_obj_set_size(controls_, inner_width, controls_area);
    const int button_width = std::max(1, (inner_width - pad * (columns - 1)) / columns);
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        const int row = static_cast<int>(i) / columns;
        const int col = static_cast<int>(i) % columns;
        lv_obj_set_pos(buttons_[i], col * (button_width + pad), row * (button_height + row_gap));
        lv_obj_set_size(buttons_[i], button_width, button_height);
        lv_obj_t *label = lv_obj_get_child(buttons_[i], 0);
        lv_obj_set_pos(label, 0, 0);
        lv_obj_set_size(label, button_width, button_height);
    }
    const int row_height = line;
    constexpr int kTrackInset = 6;
    const int track_label_height =
        std::min(row_height, static_cast<int>(lv_font_get_line_height(&cyberdeck_font)));
    const int track_label_y = (row_height - track_label_height) / 2;
    for (std::size_t i = 0; i < tracks_.size(); ++i) {
        lv_obj_set_pos(tracks_[i], 8, line + 12 + static_cast<int>(i) * row_height);
        lv_obj_set_size(tracks_[i], browser_inner_width, row_height);
        lv_obj_t *label = lv_obj_get_child(tracks_[i], 0);
        lv_obj_set_pos(label, kTrackInset, track_label_y);
        lv_obj_set_size(label, std::max(1, browser_inner_width - kTrackInset * 2), track_label_height);
    }
}

void view::resized(lv_event_t *event) {
    auto *self = static_cast<view *>(lv_event_get_user_data(event));
    if (self) self->layout();
}
void view::parent_resized(lv_event_t *event) {
    auto *self = static_cast<view *>(lv_event_get_user_data(event));
    if (!self || !self->surface_ || !self->parent_) return;
    lv_obj_set_pos(self->surface_, 0, 0);
    lv_obj_set_size(self->surface_, lv_obj_get_width(self->parent_), lv_obj_get_height(self->parent_));
    self->layout();
}
void view::clicked(lv_event_t *event) {
    auto *self = static_cast<view *>(lv_event_get_user_data(event));
    if (!self || !self->callback_) return;
    auto *target = lv_event_get_target(event);
    for (std::size_t i = 0; i < self->buttons_.size(); ++i)
        if (self->buttons_[i] == target) {
            if (self->command_open_ && i != 7) return;
            self->callback_(kActions[i], self->context_);
            return;
        }
}
void view::selected(lv_event_t *event) {
    auto *self = static_cast<view *>(lv_event_get_user_data(event));
    if (!self || !self->callback_) return;
    auto *target = lv_event_get_target(event);
    if (self->command_open_) return;
    for (std::size_t i = 0; i < self->tracks_.size(); ++i)
        if (self->tracks_[i] == target && !lv_obj_is_hidden(static_cast<lv_obj_t *>(target))) {
            self->selected_track_ = i;
            self->callback_("select", self->context_);
            return;
        }
}
void view::progress_released(lv_event_t *event) {
    auto *self = static_cast<view *>(lv_event_get_user_data(event));
    if (self && self->callback_ && !self->command_open_) self->callback_("seek_ratio", self->context_);
}
} // namespace cyberdeck_music_view
