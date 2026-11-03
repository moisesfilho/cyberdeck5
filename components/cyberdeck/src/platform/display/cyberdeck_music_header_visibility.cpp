#include "platform/display/cyberdeck_music_header_visibility.h"

namespace cyberdeck_music_header_visibility {

bool should_show(bool music_is_foreground, cyberdeck_music::playback_state state) {
    return music_is_foreground &&
           (state == cyberdeck_music::playback_state::playing || state == cyberdeck_music::playback_state::paused);
}

} // namespace cyberdeck_music_header_visibility
