#pragma once

#include "apps/music/cyberdeck_music_model.h"

namespace cyberdeck_music_header_visibility {

bool should_show(bool music_is_foreground, cyberdeck_music::playback_state state);

}
