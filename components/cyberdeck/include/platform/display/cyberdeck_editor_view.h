#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

struct _lv_obj_t;
using lv_obj_t = _lv_obj_t;
struct _lv_event_t;
using lv_event_t = _lv_event_t;

namespace cyberdeck_editor_view {

class view final {
  public:
    using action_callback = void (*)(std::string_view action, void *context);

    bool create(lv_obj_t *parent, action_callback callback, void *context);
    void destroy();
    void set_visible(bool visible);
    void set_bottom_inset(int pixels);
    bool visible() const {
        return surface_ != nullptr && visible_;
    }
    void render(std::string_view document, std::size_t cursor, bool dirty, std::string_view status);
    void gesture_scroll(int pixels);
    lv_obj_t *surface() const {
        return surface_;
    }

  private:
    static constexpr std::size_t k_max_lines = 64;
    static void pressed(lv_event_t *event);
    static void released(lv_event_t *event);
    static void button_clicked(lv_event_t *event);
    static void resized(lv_event_t *event);
    void begin_touch();
    void finish_touch();
    void notify(std::string_view action);
    void layout();

    lv_obj_t *surface_ = nullptr;
    lv_obj_t *parent_ = nullptr;
    lv_obj_t *status_ = nullptr;
    std::array<lv_obj_t *, k_max_lines> lines_{};
    action_callback callback_ = nullptr;
    void *context_ = nullptr;
    bool visible_ = false;
    bool touching_ = false;
    int touch_y_ = 0;
    std::size_t first_line_ = 0;
    int bottom_inset_ = 0;
    std::size_t visible_lines_ = 1;
    std::size_t total_lines_ = 1;
};

} // namespace cyberdeck_editor_view
