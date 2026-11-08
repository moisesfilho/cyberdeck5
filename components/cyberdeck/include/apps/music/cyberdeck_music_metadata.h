#pragma once

#include "apps/music/cyberdeck_music_model.h"

#include <cstddef>

namespace cyberdeck_music {
/* Length of the ID3v2 tag header: "ID3", version, revision, flags and the
 * four-byte syncsafe size of the payload that follows. */
inline constexpr std::size_t k_id3v2_header_bytes = 10;

/* Ceiling for one tag, counted as the header above plus the payload declared by
 * the syncsafe size field: a tag whose total is exactly this value is read, one
 * byte more is rejected whole. The same ceiling bounds `parse_id3v2`, so the
 * in-memory and the streaming paths never disagree about an accepted tag. */
inline constexpr std::size_t k_id3_max_tag_bytes = 1024 * 1024;

/* Largest single read the streaming reader issues. A target frame payload is
 * read up to this window only: UTF-16 text spends two bytes per code point and
 * the widest replacement still fits, so `k_max_title_bytes` of output is always
 * reachable without loading the rest of the frame or any artwork. */
inline constexpr std::size_t k_id3_read_window_bytes = 2 * k_max_title_bytes + 4;

/* Reads the leading ID3v2 tag behind an already open descriptor and replaces
 * `out.title`/`out.artist`/`out.album` only when the tag carries a non-empty
 * value. `out` is the bounded metadata struct, so the scan task never puts a
 * whole `track` on its stack. The tag is walked frame by frame with `pread` at
 * explicit offsets: the descriptor position and ownership are untouched, frames
 * the metadata does not consume (APIC artwork included) are skipped by their
 * declared size, and the only heap block is one fixed read window. A malformed
 * header or frame, a non-zero byte in the frame area left after the last frame
 * (padding must be all zero), a tag above `k_id3_max_tag_bytes`, a tag that does
 * not fit
 * `size` and any short or failed read reject the whole tag, leaving `out`
 * untouched. */
void read_metadata(int fd, std::size_t size, metadata &out);
} // namespace cyberdeck_music
