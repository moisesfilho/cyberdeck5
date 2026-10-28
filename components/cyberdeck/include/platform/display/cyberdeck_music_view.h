#pragma once

#include <string_view>

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
    void render(std::string_view text);

  private:
    static void clicked(lv_event_t *event);
    lv_obj_t *surface_ = nullptr;
    lv_obj_t *status_ = nullptr;
    callback callback_ = nullptr;
    void *context_ = nullptr;
    bool visible_ = false;
};
} // namespace cyberdeck_music_view
