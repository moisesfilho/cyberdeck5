#include "apps/music/cyberdeck_music_metadata.h"

#include <algorithm>
#include <memory>
#include <new>
#include <string_view>
#include <sys/types.h>
#include <unistd.h>

namespace cyberdeck_music {
namespace {
static_assert(k_id3_read_window_bytes >= k_id3v2_header_bytes,
              "the read window must hold one tag header and one frame header");

/* Offset-based exact read. A short count means the file is shorter than the
 * tag declares (or the descriptor cannot deliver the window), and the whole tag
 * is rejected instead of being parsed from partial bytes. `pread` never touches
 * the descriptor position, so the caller keeps whatever offset it had. */
bool read_exact(int fd, unsigned char *dst, std::size_t length, std::size_t offset) {
    if (length == 0)
        return true;
    return pread(fd, dst, length, static_cast<off_t>(offset)) == static_cast<ssize_t>(length);
}

/* True when every byte in [offset, end) is zero. The area is validated in
 * chunks that never exceed the fixed read window, so a padding as large as the
 * tag ceiling costs bounded reads and no extra allocation, while a single
 * non-zero byte rejects the tag at the point where it is found. */
bool read_zero_padding(int fd, std::size_t offset, std::size_t end, unsigned char *scratch) {
    while (offset < end) {
        const std::size_t chunk = std::min<std::size_t>(end - offset, k_id3_read_window_bytes);
        if (!read_exact(fd, scratch, chunk, offset))
            return false;
        for (std::size_t i = 0; i < chunk; ++i)
            if (scratch[i] != 0)
                return false;
        offset += chunk;
    }
    return true;
}
} // namespace

void read_metadata(int fd, std::size_t size, metadata &out) {
    if (fd < 0 || size < k_id3v2_header_bytes)
        return;
    unsigned char header[k_id3v2_header_bytes]{};
    if (!read_exact(fd, header, sizeof(header), 0))
        return;
    id3v2_header tag{};
    if (parse_id3v2_header(header, sizeof(header), tag) != id3v2_header_status::accepted)
        return;
    const std::size_t tag_bytes = k_id3v2_header_bytes + tag.payload_bytes;
    if (tag_bytes > size)
        return;
    /* One fixed read window for the whole tag: the allocation never grows with
     * the tag, with a frame or with the artwork of an APIC, and a failed
     * allocation rejects the metadata instead of publishing nothing useable. */
    std::unique_ptr<unsigned char[]> window(new (std::nothrow) unsigned char[k_id3_read_window_bytes]);
    if (!window)
        return;
    const std::size_t frame_header_bytes = tag.version == 2 ? 6 : k_id3v2_header_bytes;
    metadata decoded{};
    std::size_t offset = k_id3v2_header_bytes;
    while (offset + frame_header_bytes <= tag_bytes) {
        unsigned char raw[k_id3v2_header_bytes]{};
        if (!read_exact(fd, raw, frame_header_bytes, offset))
            return;
        if (raw[0] == 0)
            break;
        id3v2_frame_header frame{};
        if (!parse_id3v2_frame_header(raw, frame_header_bytes, tag.version, frame) ||
            frame.payload_bytes > tag_bytes - offset - frame.header_bytes)
            return;
        if (frame.field != id3v2_field::none) {
            /* A target frame whose flags move or re-encode the payload cannot be
             * decoded at the declared offset, so the tag is rejected. A frame
             * the metadata does not consume is skipped by its declared size
             * without reading a single payload byte (APIC artwork included). */
            if (!frame.flags_clear)
                return;
            const std::size_t want = std::min<std::size_t>(frame.payload_bytes, k_id3_read_window_bytes);
            if (!read_exact(fd, window.get(), want, offset + frame.header_bytes))
                return;
            const std::string_view text(reinterpret_cast<const char *>(window.get()), want);
            switch (frame.field) {
            case id3v2_field::title:
                decode_id3v2_text(text, decoded.title.data(), k_max_title_bytes);
                break;
            case id3v2_field::artist:
                decode_id3v2_text(text, decoded.artist.data(), k_max_artist_bytes);
                break;
            case id3v2_field::album:
                decode_id3v2_text(text, decoded.album.data(), k_max_album_bytes);
                break;
            case id3v2_field::none:
                break;
            }
        }
        offset += frame.header_bytes + frame.payload_bytes;
    }
    /* Everything between the last complete frame and the declared tag end must
     * be zero: a zero byte at the start of the area only opens padding, it does
     * not end the validation. A non-zero byte there means leftover garbage or a
     * truncated frame, which rejects the tag whole instead of publishing the
     * frames that came before it (AC-MUSIC-META-27). */
    if (!read_zero_padding(fd, offset, tag_bytes, window.get()))
        return;
    if (decoded.title[0] != '\0')
        out.title = decoded.title;
    if (decoded.artist[0] != '\0')
        out.artist = decoded.artist;
    if (decoded.album[0] != '\0')
        out.album = decoded.album;
}
} // namespace cyberdeck_music
