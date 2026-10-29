#pragma once

#include "apps/music/cyberdeck_music_model.h"

#include <cstddef>

namespace cyberdeck_music {
/* Reads the leading ID3v2 tag behind an already open descriptor and replaces
 * `item.title`/`item.artist` only when the tag carries a non-empty value.
 * `size` is the verified file size; the read is bounded to the metadata limit
 * and the tag buffer lives on the heap, so the scan task keeps a small stack. */
void read_metadata(int fd, std::size_t size, track &item);
} // namespace cyberdeck_music
