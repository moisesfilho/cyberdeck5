#include "platform/display/cyberdeck_music_header_visibility.h"

#include <cassert>
#include <cstdio>

using cyberdeck_music::playback_state;

int main() {
    using cyberdeck_music_header_visibility::should_show;

    assert(should_show(false, playback_state::playing) == false);
    assert(should_show(false, playback_state::paused) == false);
    assert(should_show(false, playback_state::stopped) == false);
    assert(should_show(false, playback_state::error) == false);
    assert(should_show(true, playback_state::playing) == true);
    assert(should_show(true, playback_state::paused) == true);
    assert(should_show(true, playback_state::stopped) == false);
    assert(should_show(true, playback_state::error) == false);

    // A successful close switches foreground before the next timer tick. A
    // stale playing snapshot must not keep the header indicator visible.
    assert(should_show(false, playback_state::playing) == false);

    // A failed close retains the foreground, so active playback remains visible.
    assert(should_show(true, playback_state::playing) == true);
    assert(should_show(true, playback_state::paused) == true);

    std::puts("PASS: music header visibility follows foreground and playback state");
}
