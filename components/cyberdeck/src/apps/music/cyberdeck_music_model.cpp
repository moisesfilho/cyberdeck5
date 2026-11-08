#include "apps/music/cyberdeck_music_model.h"

#include "apps/music/cyberdeck_music_metadata.h"

#include <algorithm>
#include <cstring>

namespace cyberdeck_music {
namespace {
bool safe_path(std::string_view path) {
    if (path.size() < 5 || path.size() > k_max_path_bytes || path.front() != '/' || path.find("//") != path.npos)
        return false;
    std::size_t begin = 1;
    while (begin <= path.size()) {
        const std::size_t end = path.find('/', begin);
        const auto part = path.substr(begin, end == path.npos ? path.size() - begin : end - begin);
        if (part == "." || part == ".." || part == "sdcard")
            return false;
        if (end == path.npos)
            break;
        begin = end + 1;
    }
    const std::string_view ext = path.substr(path.size() - 4);
    return ext[0] == '.' && (ext[1] == 'm' || ext[1] == 'M') && (ext[2] == 'p' || ext[2] == 'P') && ext[3] == '3';
}

/* Encodes one code point as UTF-8 inside the `cyberdeck_font` repertoire.
 * `CONFIG_LV_TXT_ENC_UTF8` makes LVGL decode the text and look the glyph up by
 * code point, so a raw Latin-1 byte (e.g. 0xE9) would be read as the lead of a
 * three-byte sequence and swallow the next character. The font covers printable
 * ASCII and U+00A0-U+00FF; everything outside becomes '?' and the typographic
 * marks it does not cover are transliterated. Returns false when the bounded
 * buffer has no room for the whole sequence, so the caller stops instead of
 * writing a partial, undecodable code point. */
bool append_codepoint(char *target, std::size_t &out, std::size_t max, std::uint32_t codepoint) {
    char encoded[3]{};
    std::size_t length = 0;
    switch (codepoint) {
    case 0x2018:
    case 0x2019:
        encoded[length++] = '\'';
        break;
    case 0x201c:
    case 0x201d:
        encoded[length++] = '"';
        break;
    case 0x2013:
    case 0x2014:
        encoded[length++] = '-';
        break;
    case 0x2026: {
        /* Three ASCII dots carry no UTF-8 continuation, so filling the remaining
         * room (instead of dropping the whole ellipsis) is safe. */
        const std::size_t dots = std::min<std::size_t>(3, max - out);
        for (std::size_t i = 0; i < dots; ++i)
            target[out++] = '.';
        return dots == 3;
    }
    default:
        if (codepoint < 0x20 || codepoint == 0xa0)
            encoded[length++] = ' ';
        else if (codepoint <= 0x7e)
            encoded[length++] = static_cast<char>(codepoint);
        else if (codepoint >= 0xa1 && codepoint <= 0xff) {
            encoded[length++] = static_cast<char>(0xc0 | (codepoint >> 6));
            encoded[length++] = static_cast<char>(0x80 | (codepoint & 0x3f));
        } else
            encoded[length++] = '?';
        break;
    }
    if (out + length > max)
        return false;
    for (std::size_t i = 0; i < length; ++i)
        target[out++] = encoded[i];
    return true;
}

/* Validates and re-encodes UTF-8 text. It is shared by the ID3 `UTF-8`
 * payloads and by the file-name fallback, so both always land in the
 * `cyberdeck_font` repertoire without a raw, undecodable byte. */
void copy_utf8_text(char *target, std::size_t max, const unsigned char *data, std::size_t length) {
    std::size_t out = 0;
    for (std::size_t i = 0; i < length && out < max;) {
        const unsigned char lead = data[i];
        if (lead == 0)
            break;
        if (lead < 0x80) {
            if (!append_codepoint(target, out, max, lead))
                break;
            ++i;
            continue;
        }
        const std::size_t bytes = lead >= 0xc2 && lead <= 0xdf   ? 2
                                  : lead >= 0xe0 && lead <= 0xef ? 3
                                  : lead >= 0xf0 && lead <= 0xf4 ? 4
                                                                 : 0;
        /* Consume the maximal subpart of a malformed sequence so each broken
         * character yields one '?' instead of one per stray byte. */
        std::size_t span = 0;
        if (bytes != 0) {
            span = 1;
            while (span < bytes && i + span < length && (data[i + span] & 0xc0) == 0x80)
                ++span;
        }
        bool valid = bytes != 0 && span == bytes;
        if (valid && bytes == 3 && ((lead == 0xe0 && data[i + 1] < 0xa0) || (lead == 0xed && data[i + 1] >= 0xa0)))
            valid = false;
        if (valid && bytes == 4 && ((lead == 0xf0 && data[i + 1] < 0x90) || (lead == 0xf4 && data[i + 1] >= 0x90)))
            valid = false;
        if (!valid) {
            if (!append_codepoint(target, out, max, '?'))
                break;
            i += span == 0 ? 1 : span;
            continue;
        }
        std::uint32_t codepoint = bytes == 2 ? (lead & 0x1fU) : bytes == 3 ? (lead & 0x0fU) : (lead & 0x07U);
        for (std::size_t j = 1; j < bytes; ++j)
            codepoint = (codepoint << 6) | (data[i + j] & 0x3fU);
        i += bytes;
        if (!append_codepoint(target, out, max, codepoint))
            break;
    }
    target[out] = '\0';
}

/* Frames actually present in the tag, so the `track` overload preserves the
 * historical "a present but empty frame clears the field" behavior. */
struct frame_fields {
    bool title = false;
    bool artist = false;
    bool album = false;
};

bool parse_tag(std::string_view bytes, metadata &out, frame_fields &seen) {
    const auto *p = reinterpret_cast<const unsigned char *>(bytes.data());
    id3v2_header tag{};
    const id3v2_header_status status = parse_id3v2_header(p, bytes.size(), tag);
    if (status == id3v2_header_status::absent)
        return true;
    if (status == id3v2_header_status::rejected)
        return false;
    const std::size_t end = k_id3v2_header_bytes + tag.payload_bytes;
    if (end > bytes.size())
        return false;
    /* Frames are decoded into a scratch value and only merged into `out` once
     * the whole tag parsed, so a rejected tag never publishes a partial album
     * (or a partial title/artist) from the frames before the failure. */
    metadata decoded{};
    std::size_t offset = k_id3v2_header_bytes;
    const std::size_t frame_header_bytes = tag.version == 2 ? 6 : k_id3v2_header_bytes;
    while (offset + frame_header_bytes <= end) {
        if (p[offset] == 0)
            break;
        id3v2_frame_header frame{};
        if (!parse_id3v2_frame_header(p + offset, end - offset, tag.version, frame) ||
            frame.payload_bytes > end - offset - frame.header_bytes)
            return false;
        if (frame.field != id3v2_field::none) {
            if (!frame.flags_clear)
                return false;
            const auto *payload = reinterpret_cast<const char *>(p + offset + frame.header_bytes);
            const std::string_view text(payload, frame.payload_bytes);
            switch (frame.field) {
            case id3v2_field::title:
                decode_id3v2_text(text, decoded.title.data(), k_max_title_bytes);
                seen.title = true;
                break;
            case id3v2_field::artist:
                decode_id3v2_text(text, decoded.artist.data(), k_max_artist_bytes);
                seen.artist = true;
                break;
            case id3v2_field::album:
                decode_id3v2_text(text, decoded.album.data(), k_max_album_bytes);
                seen.album = true;
                break;
            case id3v2_field::none:
                break;
            }
        }
        offset += frame.header_bytes + frame.payload_bytes;
    }
    /* Everything between the last complete frame and the declared tag end must
     * be zero. A remainder shorter than a frame header is only padding when it
     * is zero, and a zero byte that ends the frames area only opens padding
     * when the whole area after it is zero too: a non-zero byte there is
     * leftover garbage or a truncated frame, which rejects the tag whole
     * instead of publishing the frames that came before it. The scan is over
     * the tag bytes already in memory, so it never waits on I/O. */
    for (std::size_t i = offset; i < end; ++i)
        if (p[i] != 0)
            return false;
    if (seen.title)
        out.title = decoded.title;
    if (seen.artist)
        out.artist = decoded.artist;
    if (seen.album)
        out.album = decoded.album;
    return true;
}
} // namespace

bool is_mp3_path(std::string_view path) {
    return safe_path(path);
}

id3v2_header_status parse_id3v2_header(const unsigned char *bytes, std::size_t count, id3v2_header &out) {
    if (bytes == nullptr || count < 3 || std::memcmp(bytes, "ID3", 3) != 0)
        return id3v2_header_status::absent;
    if (count < k_id3v2_header_bytes)
        return id3v2_header_status::rejected;
    const std::uint8_t version = bytes[3];
    if (version < 2 || version > 4 || bytes[4] == 0xff || (bytes[6] | bytes[7] | bytes[8] | bytes[9]) & 0x80)
        return id3v2_header_status::rejected;
    /* Unsynchronisation and the extended header move or re-encode the payload,
     * so those tags are rejected instead of being walked at the wrong offsets.
     * Experimental (2.3/2.4) and footer (2.4) do not move it and are tolerated.
     * ID3v2.2 has no tolerated flag: unsync, compression and the reserved bits
     * all mean the frames cannot be trusted. */
    const std::uint8_t flags = bytes[5];
    const std::uint8_t reserved = version == 2 ? 0x3f : version == 3 ? 0x1f : 0x0f;
    if (version == 2 ? flags != 0 : (flags & (0x80 | 0x40 | reserved)) != 0)
        return id3v2_header_status::rejected;
    const std::size_t payload_bytes = (std::size_t(bytes[6]) << 21) | (std::size_t(bytes[7]) << 14) |
                                      (std::size_t(bytes[8]) << 7) | bytes[9];
    if (payload_bytes + k_id3v2_header_bytes > k_id3_max_tag_bytes)
        return id3v2_header_status::rejected;
    out.version = version;
    out.payload_bytes = payload_bytes;
    return id3v2_header_status::accepted;
}

bool parse_id3v2_frame_header(const unsigned char *bytes, std::size_t count, std::uint8_t version,
                              id3v2_frame_header &out) {
    const std::size_t header_bytes = version == 2 ? 6 : k_id3v2_header_bytes;
    if (bytes == nullptr || count < header_bytes)
        return false;
    std::uint32_t size = 0;
    if (version == 2) {
        size = (std::uint32_t(bytes[3]) << 16) | (std::uint32_t(bytes[4]) << 8) | bytes[5];
    } else if (version == 4) {
        if ((bytes[4] | bytes[5] | bytes[6] | bytes[7]) & 0x80)
            return false;
        size = (std::uint32_t(bytes[4]) << 21) | (std::uint32_t(bytes[5]) << 14) |
               (std::uint32_t(bytes[6]) << 7) | bytes[7];
    } else {
        size = (std::uint32_t(bytes[4]) << 24) | (std::uint32_t(bytes[5]) << 16) |
               (std::uint32_t(bytes[6]) << 8) | bytes[7];
    }
    if (size == 0)
        return false;
    const char *id = reinterpret_cast<const char *>(bytes);
    out.header_bytes = static_cast<std::uint8_t>(header_bytes);
    out.payload_bytes = size;
    out.flags_clear = version == 2 || (bytes[8] == 0 && bytes[9] == 0);
    out.field = id3v2_field::none;
    if ((version == 2 && std::memcmp(id, "TT2", 3) == 0) || (version != 2 && std::memcmp(id, "TIT2", 4) == 0))
        out.field = id3v2_field::title;
    else if ((version == 2 && std::memcmp(id, "TP1", 3) == 0) || (version != 2 && std::memcmp(id, "TPE1", 4) == 0))
        out.field = id3v2_field::artist;
    else if ((version == 2 && std::memcmp(id, "TAL", 3) == 0) || (version != 2 && std::memcmp(id, "TALB", 4) == 0))
        out.field = id3v2_field::album;
    return true;
}

void decode_id3v2_text(std::string_view payload, char *dst, std::size_t max) {
    if (dst == nullptr || max == 0)
        return;
    dst[0] = '\0';
    if (payload.empty())
        return;
    const auto *data = reinterpret_cast<const unsigned char *>(payload.data());
    const std::size_t length = payload.size();
    const unsigned encoding = data[0];
    if (encoding == 3) {
        copy_utf8_text(dst, max, data + 1, length - 1);
        return;
    }
    std::size_t out = 0;
    if (encoding == 0) {
        for (std::size_t i = 1; i < length && out < max; ++i) {
            const unsigned char c = data[i];
            if (c == 0)
                break;
            if (!append_codepoint(dst, out, max, c))
                break;
        }
    } else if ((encoding == 1 || encoding == 2) && length >= 3) {
        /* ID3v2.4: encoding 2 is UTF-16BE without a BOM; encoding 1 carries the
         * BOM (FF FE little-endian, FE FF big-endian) and falls back to
         * little-endian when it is missing. */
        bool little = true;
        std::size_t i = 1;
        if (encoding == 2) {
            little = false;
        } else if (data[1] == 0xff && data[2] == 0xfe) {
            i = 3;
        } else if (data[1] == 0xfe && data[2] == 0xff) {
            i = 3;
            little = false;
        }
        const auto unit_at = [&](std::size_t index) {
            return little ? (std::uint32_t(data[index]) | (std::uint32_t(data[index + 1]) << 8))
                          : ((std::uint32_t(data[index]) << 8) | std::uint32_t(data[index + 1]));
        };
        for (; i + 1 < length && out < max; i += 2) {
            std::uint32_t codepoint = unit_at(i);
            if (codepoint == 0)
                break;
            if (codepoint >= 0xd800 && codepoint <= 0xdbff && i + 3 < length) {
                const std::uint32_t low = unit_at(i + 2);
                if (low >= 0xdc00 && low <= 0xdfff) {
                    codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                    i += 2;
                }
            }
            if (!append_codepoint(dst, out, max, codepoint))
                break;
        }
    }
    dst[out] = '\0';
}

bool parse_id3v2(std::string_view bytes, metadata &out) {
    frame_fields seen{};
    return parse_tag(bytes, out, seen);
}

bool parse_id3v2(std::string_view bytes, track &out) {
    metadata tags{};
    frame_fields seen{};
    if (!parse_tag(bytes, tags, seen))
        return false;
    if (seen.title)
        out.title = tags.title;
    if (seen.artist)
        out.artist = tags.artist;
    if (seen.album)
        out.album = tags.album;
    return true;
}

void sanitize_display_text(std::string_view source, char *dst, std::size_t max) {
    if (dst == nullptr)
        return;
    copy_utf8_text(dst, max, reinterpret_cast<const unsigned char *>(source.data()), source.size());
}

bool model::replace_queue(const track *items, std::size_t count) {
    if (count > k_max_tracks || (items == nullptr && count != 0))
        return false;
    for (std::size_t i = 0; i < count; ++i) {
        if (items[i].path.back() != '\0' || !safe_path(items[i].path.data()) || items[i].title.back() != '\0' ||
            items[i].artist.back() != '\0' || items[i].album.back() != '\0' || items[i].size_bytes == 0 ||
            items[i].size_bytes > k_max_track_bytes)
            return false;
    }
    tracks_ = {};
    if (count != 0)
        std::copy(items, items + count, tracks_.begin());
    count_ = count;
    selected_ = 0;
    position_ms_ = 0;
    state_ = playback_state::stopped;
    return true;
}
bool model::select(std::size_t index) {
    if (index >= count_)
        return false;
    selected_ = index;
    position_ms_ = 0;
    return true;
}
bool model::next() {
    if (!count_)
        return false;
    if (repeat_ == repeat_mode::one)
        return true;
    if (shuffle_ && count_ > 1)
        selected_ = (selected_ + 1 + (position_ms_ % (count_ - 1))) % count_;
    else if (selected_ + 1 < count_)
        ++selected_;
    else if (repeat_ == repeat_mode::all)
        selected_ = 0;
    else
        return false;
    position_ms_ = 0;
    return true;
}
bool model::previous() {
    if (!count_)
        return false;
    if (position_ms_ > 3000) {
        position_ms_ = 0;
        return true;
    }
    if (selected_ > 0)
        --selected_;
    else if (repeat_ == repeat_mode::all)
        selected_ = count_ - 1;
    else
        return false;
    position_ms_ = 0;
    return true;
}
bool model::seek(std::uint32_t value) {
    const track *item = current();
    if (item == nullptr || item->duration_ms == 0 || value > item->duration_ms)
        return false;
    position_ms_ = value;
    return true;
}
void model::set_volume(std::uint8_t value) {
    volume_ = std::min<std::uint8_t>(value, 50);
}
void model::cycle_repeat() {
    repeat_ = repeat_ == repeat_mode::off   ? repeat_mode::all
              : repeat_ == repeat_mode::all ? repeat_mode::one
                                            : repeat_mode::off;
}
void model::clear() {
    tracks_ = {};
    count_ = selected_ = position_ms_ = 0;
    state_ = playback_state::stopped;
}
const track *model::current() const {
    return at(selected_);
}
const track *model::at(std::size_t index) const {
    return index < count_ ? &tracks_[index] : nullptr;
}
snapshot model::status() const {
    snapshot out{};
    out.state = state_;
    out.selected = selected_;
    out.track_count = count_;
    out.position_ms = position_ms_;
    out.volume = volume_;
    out.repeat = repeat_;
    out.shuffle = shuffle_;
    if (const track *item = current())
        out.current = *item;
    return out;
}
} // namespace cyberdeck_music
