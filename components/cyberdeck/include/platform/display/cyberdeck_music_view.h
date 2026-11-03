#pragma once

#include <string_view>
#include <array>
#include <cstdint>
#include <string>
#include "apps/music/cyberdeck_music_browser.h"

struct _lv_obj_t;
using lv_obj_t = _lv_obj_t;
struct _lv_event_t;
using lv_event_t = _lv_event_t;

namespace cyberdeck_music_view {
class view final {
  public:
    using callback = void (*)(std::string_view action, void *context);
    bool create(lv_obj_t *parent, callback on_action, void *context);
    void destroy();
    void set_visible(bool visible);
    bool visible() const { return surface_ != nullptr && visible_; }
    bool exists() const { return surface_ != nullptr; }
    void render(std::string_view text);
    void render_browser(const cyberdeck_music::browser_snapshot &snapshot, bool scanning);
    void set_command_open(bool open);
    bool command_open() const;
    void set_command_text(std::string_view text);
    void set_command_cursor(std::size_t position);
    std::string_view command_text() const;
    std::size_t selected_track() const { return selected_track_; }
    bool move_selection(int direction);
    bool move_browser_selection(int direction);
    std::size_t browser_selection() const {
        return browser_selection_;
    }
    std::string_view browser_directory() const {
        return browser_directory_;
    }
    bool browser_parent_selected() const {
        return browser_parent_visible_ && browser_selection_ == 0;
    }
    int progress_value() const;

  private:
    static void clicked(lv_event_t *event);
    static void selected(lv_event_t *event);
    static void progress_released(lv_event_t *event);
    static void resized(lv_event_t *event);
    static void parent_resized(lv_event_t *event);
    void layout();
    lv_obj_t *parent_ = nullptr;
    lv_obj_t *surface_ = nullptr;
    lv_obj_t *title_ = nullptr;
    lv_obj_t *now_title_ = nullptr;
    lv_obj_t *browser_title_ = nullptr;
    lv_obj_t *status_ = nullptr;
    lv_obj_t *now_playing_ = nullptr;
    lv_obj_t *time_ = nullptr;
    lv_obj_t *volume_ = nullptr;
    lv_obj_t *progress_ = nullptr;
    lv_obj_t *command_ = nullptr;
    lv_obj_t *list_ = nullptr;
    lv_obj_t *footer_ = nullptr;
    lv_obj_t *controls_ = nullptr;
    std::array<lv_obj_t *, 65> tracks_{};
    std::array<lv_obj_t *, 9> buttons_{};
    callback callback_ = nullptr;
    void *context_ = nullptr;
    bool visible_ = false;
    bool command_open_ = false;
    bool selection_pending_sync_ = false;
    /* Orientation whose footer text is already applied: -1 = never applied
     * (fresh label). `layout()` runs on every status refresh, so the footer
     * setter is gated by this value instead of re-writing the same string. */
    std::int8_t footer_landscape_ = -1;
    std::size_t selected_track_ = 0;
    std::size_t browser_selection_ = 0;
    std::size_t browser_entry_count_ = 0;
    bool browser_parent_visible_ = false;
    bool browser_mode_ = false;
    std::string browser_directory_;
};
} // namespace cyberdeck_music_view
