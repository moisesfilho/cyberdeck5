#include "apps/music/cyberdeck_music_manager.h"

namespace cyberdeck_music_manager {
void manager::bind(action_callback perform, close_callback close, void *context) {
    perform_ = perform;
    close_ = close;
    context_ = context;
}
bool manager::open() {
    active_ = true;
    return true;
}
bool manager::close() {
    if (!active_)
        return false;
    // The close adapter performs app stop/join before returning to the shell.
    if (close_ == nullptr || !close_(context_))
        return false;
    active_ = false;
    return true;
}
bool manager::handle(key value) {
    if (!active_)
        return false;
    if (value == key::escape)
        return close();
    if (perform_ == nullptr)
        return true;
    switch (value) {
    case key::enter: (void)perform_(action::toggle, context_); break;
    case key::left: (void)perform_(action::seek_back, context_); break;
    case key::right: (void)perform_(action::seek_forward, context_); break;
    case key::up: (void)perform_(action::volume_up, context_); break;
    case key::down: (void)perform_(action::volume_down, context_); break;
    case key::next: (void)perform_(action::next, context_); break;
    case key::previous: (void)perform_(action::previous, context_); break;
    case key::escape:
    case key::other: break;
    }
    return true;
}
void manager::deactivate() { active_ = false; }
} // namespace cyberdeck_music_manager
