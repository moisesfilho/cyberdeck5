#pragma once

#include <cstdint>
#include <string_view>

namespace cyberdeck_music_manager {
enum class key : std::uint8_t { enter, left, right, up, down, next, previous, escape, other };
enum class action : std::uint8_t { toggle, seek_back, seek_forward, volume_up, volume_down, next, previous };
class manager final {
  public:
    using action_callback = bool (*)(action value, void *context);
    using close_callback = bool (*)(void *context);
    void bind(action_callback perform, close_callback close, void *context);
    bool open();
    bool activate();
    bool close();
    bool handle(key value);
    void deactivate();
    bool active() const { return active_; }
  private:
    action_callback perform_ = nullptr;
    close_callback close_ = nullptr;
    void *context_ = nullptr;
    bool active_ = false;
    bool ready_ = false;
};
} // namespace cyberdeck_music_manager
