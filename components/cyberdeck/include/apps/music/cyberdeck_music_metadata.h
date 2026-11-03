#pragma once

#include "apps/music/cyberdeck_music_model.h"

#include <cstddef>

namespace cyberdeck_music {
/* Reads the leading ID3v2 tag behind an already open descriptor and replaces
 * `out.title`/`out.artist` only when the tag carries a non-empty value. `out`
 * is the bounded metadata struct, so the scan task never puts a whole `track`
 * on its stack, and the tag buffer lives on the heap. `size` is the verified
 * file size; the read is bounded to the metadata limit. */
void read_metadata(int fd, std::size_t size, metadata &out);
} // namespace cyberdeck_music
