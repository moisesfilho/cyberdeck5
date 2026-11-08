#include "apps/music/cyberdeck_music_metadata.h"
#include "apps/music/cyberdeck_music_model.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

namespace {
int checks = 0;
int failures = 0;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": " #condition "\n"; \
    } \
} while (false)

using cyberdeck_music::k_max_album_bytes;
using cyberdeck_music::k_max_artist_bytes;
using cyberdeck_music::k_max_path_bytes;
using cyberdeck_music::k_max_title_bytes;
using cyberdeck_music::k_max_track_bytes;
using cyberdeck_music::model;
using cyberdeck_music::repeat_mode;
using cyberdeck_music::track;

track make_track(std::string_view path, std::uint32_t size = 1, std::uint32_t duration = 10000) {
    track result{};
    if (path.size() <= k_max_path_bytes)
        std::copy(path.begin(), path.end(), result.path.begin());
    result.size_bytes = size;
    result.duration_ms = duration;
    return result;
}

void append_syncsafe(std::string &out, std::uint32_t value) {
    out.push_back(static_cast<char>((value >> 21) & 0x7f));
    out.push_back(static_cast<char>((value >> 14) & 0x7f));
    out.push_back(static_cast<char>((value >> 7) & 0x7f));
    out.push_back(static_cast<char>(value & 0x7f));
}

/* Strict UTF-8 validation: `copy_text` feeds LVGL, which decodes by code point,
 * so every parsed title/artist must be a decodable sequence. */
bool valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        if (lead < 0x80) {
            ++i;
            continue;
        }
        const std::size_t bytes = lead >= 0xc2 && lead <= 0xdf   ? 2
                                  : lead >= 0xe0 && lead <= 0xef ? 3
                                  : lead >= 0xf0 && lead <= 0xf4 ? 4
                                                                 : 0;
        if (bytes == 0 || i + bytes > text.size())
            return false;
        for (std::size_t j = 1; j < bytes; ++j)
            if ((static_cast<unsigned char>(text[i + j]) & 0xc0) != 0x80)
                return false;
        if (bytes >= 3) {
            const auto second = static_cast<unsigned char>(text[i + 1]);
            if (bytes == 3 && ((lead == 0xe0 && second < 0xa0) || (lead == 0xed && second >= 0xa0)))
                return false;
            if (bytes == 4 && ((lead == 0xf0 && second < 0x90) || (lead == 0xf4 && second >= 0x90)))
                return false;
        }
        i += bytes;
    }
    return true;
}

void append_raw_frame(std::string &tag, unsigned version, std::string_view id, std::string_view payload) {
    tag.append(id);
    const auto size = static_cast<std::uint32_t>(payload.size());
    if (version == 2) {
        tag.push_back(static_cast<char>((size >> 16) & 0xff));
        tag.push_back(static_cast<char>((size >> 8) & 0xff));
        tag.push_back(static_cast<char>(size & 0xff));
    } else {
        if (version == 4) append_syncsafe(tag, size);
        else {
            tag.push_back(static_cast<char>((size >> 24) & 0xff));
            tag.push_back(static_cast<char>((size >> 16) & 0xff));
            tag.push_back(static_cast<char>((size >> 8) & 0xff));
            tag.push_back(static_cast<char>(size & 0xff));
        }
        tag.append(2, '\0');
    }
    tag += payload;
}

void append_frame(std::string &tag, unsigned version, std::string_view id, std::string_view text) {
    const std::string payload = std::string(1, '\x03') + std::string(text);
    append_raw_frame(tag, version, id, payload);
}

std::string wrap_tag(unsigned version, std::string_view frames) {
    std::string bytes = "ID3";
    bytes.push_back(static_cast<char>(version));
    bytes.push_back('\0');
    bytes.push_back('\0');
    append_syncsafe(bytes, static_cast<std::uint32_t>(frames.size()));
    bytes += frames;
    return bytes;
}

std::string make_tag(unsigned version, std::string_view title, std::string_view artist,
                     std::string_view album = {}) {
    std::string frames;
    append_frame(frames, version, version == 2 ? "TT2" : "TIT2", title);
    append_frame(frames, version, version == 2 ? "TP1" : "TPE1", artist);
    if (!album.empty())
        append_frame(frames, version, version == 2 ? "TAL" : "TALB", album);
    return wrap_tag(version, frames);
}

/* Total bytes of one accepted tag: the ten-byte header plus the payload the
 * syncsafe size field declares. */
constexpr std::size_t k_tag_total_cap = cyberdeck_music::k_id3_max_tag_bytes;
/* Historical envelope of the previous parser; tags on both sides of it are
 * accepted now and keep their frames. Provenance of the superseded boundary,
 * not a production limit. */
constexpr std::size_t k_legacy_tag_payload = 64 * 1024;

/* Rewrites the size field of an already framed tag and zero-pads it so the
 * declared payload is exactly `payload_bytes`. */
std::string tag_with_payload(std::string tag, std::size_t payload_bytes) {
    const std::size_t frames = tag.size() - cyberdeck_music::k_id3v2_header_bytes;
    if (payload_bytes < frames)
        return tag;
    tag.append(payload_bytes - frames, '\0');
    const auto size = static_cast<std::uint32_t>(payload_bytes);
    tag[6] = static_cast<char>((size >> 21) & 0x7f);
    tag[7] = static_cast<char>((size >> 14) & 0x7f);
    tag[8] = static_cast<char>((size >> 7) & 0x7f);
    tag[9] = static_cast<char>(size & 0x7f);
    return tag;
}

/* One frame the metadata never consumes (artwork-like) with a zeroed payload.
 * The parser must skip it by its declared size instead of loading it. */
std::string filler_frame(unsigned version, std::string_view id, std::size_t payload_bytes) {
    std::string frame;
    append_raw_frame(frame, version, id, std::string(payload_bytes, '\0'));
    return frame;
}

void test_path_policy() {
    CHECK(cyberdeck_music::is_mp3_path("/song.mp3"));
    CHECK(cyberdeck_music::is_mp3_path("/Music/SONG.Mp3"));
    CHECK(!cyberdeck_music::is_mp3_path("song.mp3"));
    CHECK(!cyberdeck_music::is_mp3_path("/"));
    CHECK(!cyberdeck_music::is_mp3_path("/music//song.mp3"));
    CHECK(!cyberdeck_music::is_mp3_path("/music/./song.mp3"));
    CHECK(!cyberdeck_music::is_mp3_path("/music/../song.mp3"));
    CHECK(!cyberdeck_music::is_mp3_path("/sdcard/music/song.mp3"));
    CHECK(!cyberdeck_music::is_mp3_path("/music/song.mp3/"));
    CHECK(!cyberdeck_music::is_mp3_path("/music/song.mp3x"));
    const std::string longest = "/" + std::string(k_max_path_bytes - 5, 'a') + ".mp3";
    const std::string too_long = "/" + std::string(k_max_path_bytes - 4, 'a') + ".mp3";
    CHECK(longest.size() == k_max_path_bytes && cyberdeck_music::is_mp3_path(longest));
    CHECK(too_long.size() == k_max_path_bytes + 1 && !cyberdeck_music::is_mp3_path(too_long));
}

void test_queue_bounds_and_atomic_replace() {
    model queue;
    std::array<track, cyberdeck_music::k_max_tracks + 1> tracks{};
    for (std::size_t i = 0; i < tracks.size(); ++i) tracks[i] = make_track("/" + std::to_string(i) + ".mp3");
    CHECK(!queue.replace_queue(tracks.data(), tracks.size()));
    CHECK(queue.status().track_count == 0);
    CHECK(!queue.replace_queue(nullptr, 1));
    CHECK(queue.replace_queue(tracks.data(), cyberdeck_music::k_max_tracks));
    CHECK(queue.status().track_count == cyberdeck_music::k_max_tracks);
    CHECK(queue.status().selected == 0);
    CHECK(queue.replace_queue(nullptr, 0));
    CHECK(queue.status().track_count == 0);

    track valid = make_track("/safe.mp3");
    CHECK(queue.replace_queue(&valid, 1));
    track invalid = make_track("/safe/../escape.mp3");
    CHECK(!queue.replace_queue(&invalid, 1));
    CHECK(queue.status().track_count == 1);
    CHECK(std::string_view(queue.current()->path.data()) == "/safe.mp3");
}

void test_track_validation_and_selection() {
    model queue;
    std::array<track, 2> tracks{make_track("/one.mp3"), make_track("/two.mp3")};
    tracks[0].duration_ms = 0;
    CHECK(queue.replace_queue(tracks.data(), tracks.size()));
    CHECK(queue.current() != nullptr);
    CHECK(queue.at(1) != nullptr);
    CHECK(queue.at(2) == nullptr);
    CHECK(!queue.select(2));
    CHECK(queue.select(1));
    CHECK(queue.status().selected == 1);

    auto invalid = tracks[0];
    invalid.path.back() = 'x';
    CHECK(!queue.replace_queue(&invalid, 1));
    invalid = tracks[0]; invalid.path.fill('x');
    CHECK(!queue.replace_queue(&invalid, 1));
    invalid = tracks[0]; invalid.size_bytes = 0;
    CHECK(!queue.replace_queue(&invalid, 1));
    invalid = tracks[0]; invalid.size_bytes = k_max_track_bytes + 1;
    CHECK(!queue.replace_queue(&invalid, 1));
    /* The album is transported by the same bounded struct, so it carries the
     * same NUL-termination guard as title/artist. A dedicated queue keeps the
     * selection state of `queue` untouched. */
    model album_queue;
    track with_album = make_track("/album.mp3");
    std::strcpy(with_album.album.data(), "Album");
    CHECK(album_queue.replace_queue(&with_album, 1));
    CHECK(std::string_view(album_queue.current()->album.data()) == "Album");
    with_album.album.back() = 'x';
    CHECK(!album_queue.replace_queue(&with_album, 1));
    CHECK(queue.status().selected == 1);
}

void test_navigation_repeat_shuffle_and_seek() {
    model queue;
    std::array<track, 2> tracks{make_track("/one.mp3"), make_track("/two.mp3")};
    CHECK(queue.replace_queue(tracks.data(), tracks.size()));
    CHECK(!queue.previous());
    CHECK(queue.next());
    CHECK(queue.status().selected == 1);
    CHECK(!queue.next());
    queue.cycle_repeat();
    CHECK(queue.status().repeat == repeat_mode::all);
    CHECK(queue.next());
    CHECK(queue.status().selected == 0);
    CHECK(queue.previous());
    CHECK(queue.status().selected == 1);
    queue.cycle_repeat();
    CHECK(queue.status().repeat == repeat_mode::one);
    CHECK(queue.next());
    CHECK(queue.status().selected == 1);
    queue.cycle_repeat();
    CHECK(queue.status().repeat == repeat_mode::off);
    CHECK(queue.seek(0));
    CHECK(queue.select(0));
    CHECK(queue.seek(tracks[0].duration_ms));
    CHECK(queue.status().position_ms == tracks[0].duration_ms);
    CHECK(!queue.seek(tracks[0].duration_ms + 1));
    CHECK(queue.select(1));
    queue.set_position(4001);
    CHECK(queue.previous());
    CHECK(queue.status().selected == 1);
    CHECK(queue.status().position_ms == 0);
    queue.set_position(3000);
    CHECK(queue.previous());
    CHECK(queue.status().selected == 0);
    CHECK(queue.status().selected == 0);
    queue.set_volume(255);
    CHECK(queue.status().volume == 50);
    queue.set_volume(0);
    CHECK(queue.status().volume == 0);
    queue.toggle_shuffle();
    CHECK(queue.status().shuffle);
    queue.toggle_shuffle();
    CHECK(!queue.status().shuffle);
}

void test_id3_versions_and_malformed_tags() {
    for (unsigned version : {2U, 3U, 4U}) {
        auto bytes = make_tag(version, "Title", "Artist");
        track parsed{};
        CHECK(cyberdeck_music::parse_id3v2(bytes, parsed));
        CHECK(std::string_view(parsed.title.data()) == "Title");
        CHECK(std::string_view(parsed.artist.data()) == "Artist");
    }
    track unchanged{};
    std::strcpy(unchanged.title.data(), "unchanged");
    CHECK(cyberdeck_music::parse_id3v2("not-an-id3-tag", unchanged));
    CHECK(std::string_view(unchanged.title.data()) == "unchanged");

    auto bad_version = make_tag(3, "x", "y"); bad_version[3] = 1;
    CHECK(!cyberdeck_music::parse_id3v2(bad_version, unchanged));
    auto bad_reserved_version = make_tag(4, "x", "y"); bad_reserved_version[4] = static_cast<char>(0xff);
    CHECK(!cyberdeck_music::parse_id3v2(bad_reserved_version, unchanged));
    auto bad_syncsafe = make_tag(4, "x", "y"); bad_syncsafe[6] = static_cast<char>(0x80);
    CHECK(!cyberdeck_music::parse_id3v2(bad_syncsafe, unchanged));
    auto truncated = make_tag(3, "long", "artist"); truncated.pop_back();
    CHECK(!cyberdeck_music::parse_id3v2(truncated, unchanged));

    /* The ceiling counts the whole tag (header plus payload) and is derived from
     * the production constant: a tag whose total equals it is accepted, one byte
     * more is rejected whole (TEST-MUSIC-META-ID3-CAP). */
    std::string oversized("ID3\x04\0\0", 6);
    append_syncsafe(oversized, static_cast<std::uint32_t>(k_tag_total_cap - cyberdeck_music::k_id3v2_header_bytes + 1));
    oversized.append(k_tag_total_cap - cyberdeck_music::k_id3v2_header_bytes + 1, 'x');
    CHECK(!cyberdeck_music::parse_id3v2(oversized, unchanged));
    std::string max_tag("ID3\x04\0\0", 6);
    append_syncsafe(max_tag, static_cast<std::uint32_t>(k_tag_total_cap - cyberdeck_music::k_id3v2_header_bytes));
    max_tag.append(k_tag_total_cap - cyberdeck_music::k_id3v2_header_bytes, '\0');
    CHECK(cyberdeck_music::parse_id3v2(max_tag, unchanged));

    /* A payload above the historical 64 KiB window keeps its frames: the old
     * boundary is superseded, not weakened, so the values stay exact. */
    std::string above_old_frames;
    append_frame(above_old_frames, 3, "TIT2", "Wide title");
    append_frame(above_old_frames, 3, "TPE1", "Wide artist");
    append_frame(above_old_frames, 3, "TALB", "Wide album");
    const std::string above_old = tag_with_payload(wrap_tag(3, above_old_frames), k_legacy_tag_payload + 1);
    cyberdeck_music::metadata wide{};
    CHECK(cyberdeck_music::parse_id3v2(above_old, wide));
    CHECK(std::string_view(wide.title.data()) == "Wide title");
    CHECK(std::string_view(wide.artist.data()) == "Wide artist");
    CHECK(std::string_view(wide.album.data()) == "Wide album");
}

void append_flagged_frame(std::string &tag, unsigned version, std::string_view id, std::string_view payload,
                          unsigned char flag1, unsigned char flag2) {
    const std::size_t start = tag.size();
    append_raw_frame(tag, version, id, payload);
    if (version != 2) {
        tag[start + id.size() + 4] = static_cast<char>(flag1);
        tag[start + id.size() + 5] = static_cast<char>(flag2);
    }
}

/* The reported file carries a 252 KiB APIC; walking the frames by their declared
 * size reaches the text frames on either side of it (TEST-MUSIC-META-ID3-ORDER,
 * TEST-MUSIC-META-ID3-ACTUAL geometry, TEST-MUSIC-META-ID3-CAP). */
void test_id3_large_tag_frame_walk() {
    /* Payload of the APIC in the reported file (provenance of the bug, not a
     * production constant). */
    constexpr std::size_t k_artwork_payload = 252112;

    for (unsigned version : {2U, 3U, 4U}) {
        const std::string_view title_id = version == 2 ? "TT2" : "TIT2";
        const std::string_view artist_id = version == 2 ? "TP1" : "TPE1";
        const std::string_view album_id = version == 2 ? "TAL" : "TALB";
        const std::string_view artwork_id = version == 2 ? "PIC" : "APIC";
        const std::string_view unknown_id = version == 2 ? "TP2" : "TPE2";
        const std::string_view encoder_id = version == 2 ? "TSS" : "TSSE";

        /* Texts before the artwork, with frames the metadata ignores between. */
        std::string after_frames;
        append_frame(after_frames, version, title_id, "Walk title");
        after_frames += filler_frame(version, unknown_id, 47);
        append_frame(after_frames, version, artist_id, "Walk artist");
        after_frames += filler_frame(version, encoder_id, 29);
        append_frame(after_frames, version, album_id, "Walk album");
        after_frames += filler_frame(version, artwork_id, k_artwork_payload);
        track after{};
        CHECK(cyberdeck_music::parse_id3v2(wrap_tag(version, after_frames), after));
        CHECK(std::string_view(after.title.data()) == "Walk title");
        CHECK(std::string_view(after.artist.data()) == "Walk artist");
        CHECK(std::string_view(after.album.data()) == "Walk album");

        /* Artwork first, carrying a decoy target frame inside its payload: a
         * parser that scans for the frame id instead of using the declared size
         * would publish the decoy. */
        std::string decoy;
        append_frame(decoy, version, title_id, "Fake");
        decoy.append(k_artwork_payload - decoy.size(), '\0');
        std::string before_frames;
        append_raw_frame(before_frames, version, artwork_id, decoy);
        append_frame(before_frames, version, title_id, "Walk title");
        append_frame(before_frames, version, artist_id, "Walk artist");
        append_frame(before_frames, version, album_id, "Walk album");
        track before{};
        CHECK(cyberdeck_music::parse_id3v2(wrap_tag(version, before_frames), before));
        CHECK(std::string_view(before.title.data()) == "Walk title");
        CHECK(std::string_view(before.artist.data()) == "Walk artist");
        CHECK(std::string_view(before.album.data()) == "Walk album");
    }

    /* Exactly at the ceiling, with the three texts after a nearly ceiling-sized
     * artwork frame: the last bytes inside the tag are still reached. */
    std::string capped_frames;
    append_frame(capped_frames, 3, "TIT2", "Cap title");
    append_frame(capped_frames, 3, "TPE1", "Cap artist");
    append_frame(capped_frames, 3, "TALB", "Cap album");
    const std::size_t text_bytes = capped_frames.size();
    const std::size_t artwork_bytes =
        k_tag_total_cap - cyberdeck_music::k_id3v2_header_bytes - text_bytes - 10 /* APIC header */;
    std::string at_cap_frames = filler_frame(3, "APIC", artwork_bytes);
    at_cap_frames += capped_frames;
    CHECK(at_cap_frames.size() + cyberdeck_music::k_id3v2_header_bytes == k_tag_total_cap);
    const std::string at_cap = wrap_tag(3, at_cap_frames);
    CHECK(at_cap.size() == k_tag_total_cap);
    track capped{};
    CHECK(cyberdeck_music::parse_id3v2(at_cap, capped));
    CHECK(std::string_view(capped.title.data()) == "Cap title");
    CHECK(std::string_view(capped.artist.data()) == "Cap artist");
    CHECK(std::string_view(capped.album.data()) == "Cap album");

    /* One byte above the ceiling rejects the whole tag, even with complete text
     * frames before and after the artwork. */
    std::string over_frames = at_cap_frames;
    over_frames.push_back('\0');
    CHECK(wrap_tag(3, over_frames).size() == k_tag_total_cap + 1);
    cyberdeck_music::metadata over{};
    std::strcpy(over.title.data(), "keep-title");
    std::strcpy(over.artist.data(), "keep-artist");
    std::strcpy(over.album.data(), "keep-album");
    CHECK(!cyberdeck_music::parse_id3v2(wrap_tag(3, over_frames), over));
    CHECK(std::string_view(over.title.data()) == "keep-title");
    CHECK(std::string_view(over.artist.data()) == "keep-artist");
    CHECK(std::string_view(over.album.data()) == "keep-album");
}

/* Frame bounds, padding and flag handling shared by both read paths
 * (TEST-MUSIC-META-ID3-BOUNDS, -ID3-FLAGS, -ID3-ALLOR-NOTHING). */
void test_id3_frame_bounds_flags_and_all_or_nothing() {
    const auto reset = [](cyberdeck_music::metadata &out) {
        std::strcpy(out.title.data(), "keep-title");
        std::strcpy(out.artist.data(), "keep-artist");
        std::strcpy(out.album.data(), "keep-album");
    };
    const auto preserved = [](const cyberdeck_music::metadata &out) {
        return std::string_view(out.title.data()) == "keep-title" &&
               std::string_view(out.artist.data()) == "keep-artist" &&
               std::string_view(out.album.data()) == "keep-album";
    };
    cyberdeck_music::metadata target{};

    /* A zero-sized frame after a valid title rejects the tag without leaking the
     * title that was already decoded. */
    std::string zero_frames;
    append_frame(zero_frames, 3, "TIT2", "Partial");
    zero_frames.append("TALB", 4);
    zero_frames.append(6, '\0');
    reset(target);
    CHECK(!cyberdeck_music::parse_id3v2(wrap_tag(3, zero_frames), target));
    CHECK(preserved(target));

    /* Artwork that crosses the declared tag end rejects the tag as well. */
    std::string crossing_frames;
    append_frame(crossing_frames, 3, "TIT2", "Partial");
    crossing_frames += filler_frame(3, "APIC", 512);
    std::string crossing = wrap_tag(3, crossing_frames);
    crossing.pop_back();
    reset(target);
    CHECK(!cyberdeck_music::parse_id3v2(crossing, target));
    CHECK(preserved(target));

    /* A remainder shorter than a frame header is a truncated frame unless it is
     * zero padding. */
    std::string short_frames;
    append_frame(short_frames, 3, "TIT2", "Title");
    std::string truncated = tag_with_payload(wrap_tag(3, short_frames), short_frames.size() + 4);
    truncated.replace(cyberdeck_music::k_id3v2_header_bytes + short_frames.size(), 4, "xxxx");
    reset(target);
    CHECK(!cyberdeck_music::parse_id3v2(truncated, target));
    CHECK(preserved(target));

    std::string padded = tag_with_payload(wrap_tag(3, short_frames), short_frames.size() + 4);
    reset(target);
    CHECK(cyberdeck_music::parse_id3v2(padded, target));
    CHECK(std::string_view(target.title.data()) == "Title");

    /* A zero byte only opens the padding area: the whole declared remainder
     * must be zero. A valid text frame followed by one zero and non-zero bytes
     * that still fit a frame header is a malformed frame area, so the tag is
     * rejected whole instead of publishing the title (AC-MUSIC-META-27). */
    std::string zero_then_garbage_frames;
    append_frame(zero_then_garbage_frames, 3, "TIT2", "Leaked title");
    zero_then_garbage_frames.append(1, '\0');
    zero_then_garbage_frames.append(cyberdeck_music::k_id3v2_header_bytes + 6, 'x');
    reset(target);
    CHECK(!cyberdeck_music::parse_id3v2(wrap_tag(3, zero_then_garbage_frames), target));
    CHECK(preserved(target));

    /* The non-zero byte may sit far inside the padding area: the entire frame
     * area before the tag end is validated, not only its first bytes. */
    std::string deep_garbage_frames;
    append_frame(deep_garbage_frames, 3, "TIT2", "Leaked title");
    deep_garbage_frames.append(2 * cyberdeck_music::k_id3_read_window_bytes + 3, '\0');
    deep_garbage_frames.back() = 'x';
    reset(target);
    CHECK(!cyberdeck_music::parse_id3v2(wrap_tag(3, deep_garbage_frames), target));
    CHECK(preserved(target));

    /* Zero padding of the same size is padding and keeps the frame. */
    std::string full_padding_frames;
    append_frame(full_padding_frames, 3, "TIT2", "Padded title");
    full_padding_frames.append(2 * cyberdeck_music::k_id3_read_window_bytes + 3, '\0');
    reset(target);
    CHECK(cyberdeck_music::parse_id3v2(wrap_tag(3, full_padding_frames), target));
    CHECK(std::string_view(target.title.data()) == "Padded title");

    /* Header flags: the ones that move the payload and the reserved bits reject,
     * experimental (2.3/2.4) and footer (2.4) are tolerated. */
    std::string plain_frames;
    append_frame(plain_frames, 3, "TIT2", "Flag title");
    for (unsigned char flag : {0x80, 0x40, 0x01}) {
        std::string tagged = wrap_tag(3, plain_frames);
        tagged[5] = static_cast<char>(flag);
        reset(target);
        CHECK(!cyberdeck_music::parse_id3v2(tagged, target));
        CHECK(preserved(target));
    }
    std::string experimental = wrap_tag(3, plain_frames);
    experimental[5] = static_cast<char>(0x20);
    reset(target);
    CHECK(cyberdeck_music::parse_id3v2(experimental, target));
    CHECK(std::string_view(target.title.data()) == "Flag title");

    std::string v4_frames;
    append_frame(v4_frames, 4, "TIT2", "Footer title");
    std::string footer = wrap_tag(4, v4_frames);
    footer[5] = static_cast<char>(0x10);
    reset(target);
    CHECK(cyberdeck_music::parse_id3v2(footer, target));
    CHECK(std::string_view(target.title.data()) == "Footer title");
    std::string v4_reserved = wrap_tag(4, v4_frames);
    v4_reserved[5] = static_cast<char>(0x08);
    reset(target);
    CHECK(!cyberdeck_music::parse_id3v2(v4_reserved, target));
    CHECK(preserved(target));

    std::string v2_frames;
    append_frame(v2_frames, 2, "TT2", "Legacy title");
    for (unsigned char flag : {0x80, 0x40, 0x01}) {
        std::string tagged = wrap_tag(2, v2_frames);
        tagged[5] = static_cast<char>(flag);
        reset(target);
        CHECK(!cyberdeck_music::parse_id3v2(tagged, target));
        CHECK(preserved(target));
    }

    /* A target frame with any flag cannot be decoded at the declared offset; a
     * skipped frame with flags does not stop the following texts. */
    std::string flagged_target;
    append_flagged_frame(flagged_target, 3, "TIT2", std::string(1, '\x03') + "Grouped", 0x00, 0x20);
    reset(target);
    CHECK(!cyberdeck_music::parse_id3v2(wrap_tag(3, flagged_target), target));
    CHECK(preserved(target));

    std::string flagged_artwork;
    append_flagged_frame(flagged_artwork, 3, "APIC", std::string(64, '\0'), 0x00, 0x20);
    append_frame(flagged_artwork, 3, "TIT2", "After artwork");
    reset(target);
    CHECK(cyberdeck_music::parse_id3v2(wrap_tag(3, flagged_artwork), target));
    CHECK(std::string_view(target.title.data()) == "After artwork");

    /* Data that does not start with an ID3v2 tag publishes nothing and stays a
     * successful no-op. */
    reset(target);
    CHECK(cyberdeck_music::parse_id3v2("not-an-id3-tag", target));
    CHECK(preserved(target));
}

void test_id3_text_encoding_and_limits() {
    auto utf8 = make_tag(4, "Caf\xc3\xa9", "Beyonc\xc3\xa9");
    track parsed{};
    CHECK(cyberdeck_music::parse_id3v2(utf8, parsed));
    /* LVGL decodes UTF-8 (`CONFIG_LV_TXT_ENC_UTF8`) and looks the glyph up by
     * code point, so valid sequences stay verbatim: `Caf\xc3\xa9` renders as
     * `Café`. A Latin-1 byte would be read as a 3-byte lead and swallow the
     * following character. */
    CHECK(std::string_view(parsed.title.data()) == "Caf\xc3\xa9");
    CHECK(std::string_view(parsed.artist.data()) == "Beyonc\xc3\xa9");
    CHECK(valid_utf8(parsed.title.data()) && valid_utf8(parsed.artist.data()));

    auto utf8_punctuation = make_tag(4, "\xe2\x80\x9cHi\xe2\x80\x9d", "\xe2\x80\x93x\xe2\x80\x94y\xe2\x80\xa6");
    CHECK(cyberdeck_music::parse_id3v2(utf8_punctuation, parsed));
    CHECK(std::string_view(parsed.title.data()) == "\"Hi\"");
    CHECK(std::string_view(parsed.artist.data()) == "-x-y...");

    auto utf8_apostrophes = make_tag(4, "It\xe2\x80\x99s", "a\xe2\x80\x98" "b");
    CHECK(cyberdeck_music::parse_id3v2(utf8_apostrophes, parsed));
    CHECK(std::string_view(parsed.title.data()) == "It's");
    CHECK(std::string_view(parsed.artist.data()) == "a'b");

    auto utf8_outside_font = make_tag(4, "\xe6\xbc\xa2", "\xf0\x9f\x8e\xb5");
    CHECK(cyberdeck_music::parse_id3v2(utf8_outside_font, parsed));
    CHECK(std::string_view(parsed.title.data()) == "?");
    CHECK(std::string_view(parsed.artist.data()) == "?");

    /* U+00A0 renders as a space; U+00FF stays a two-byte UTF-8 sequence. */
    auto utf8_latin1_supplement = make_tag(4, "\xc2\xa0" "A\xc3\xbf", "b");
    CHECK(cyberdeck_music::parse_id3v2(utf8_latin1_supplement, parsed));
    CHECK(std::string_view(parsed.title.data()) == " A\xc3\xbf");
    CHECK(valid_utf8(parsed.title.data()));

    /* An ellipsis is three glyphs: the bounded buffer must drop it instead of
     * writing past the last byte or splitting it. */
    auto ellipsis_limit = make_tag(
        4, std::string(k_max_title_bytes - 2, 'a') + "\xe2\x80\xa6", "artist");
    CHECK(cyberdeck_music::parse_id3v2(ellipsis_limit, parsed));
    CHECK(std::strlen(parsed.title.data()) == k_max_title_bytes);
    CHECK(std::string_view(parsed.title.data()).substr(k_max_title_bytes - 2) == "..");
    CHECK(parsed.title.back() == '\0');

    /* Truncation is per code point: a two-byte `ç` that does not fit is dropped
     * whole, so the output never ends with a dangling lead byte. */
    auto split_limit = make_tag(
        4, std::string(k_max_title_bytes - 1, 'a') + "\xc3\xa7", "artist");
    CHECK(cyberdeck_music::parse_id3v2(split_limit, parsed));
    CHECK(std::strlen(parsed.title.data()) == k_max_title_bytes - 1);
    CHECK(std::string_view(parsed.title.data()) == std::string(k_max_title_bytes - 1, 'a'));
    CHECK(valid_utf8(parsed.title.data()));

    /* A sequence that fits exactly is kept, with the NUL one byte later. */
    auto exact_limit = make_tag(
        4, std::string(k_max_title_bytes - 2, 'a') + "\xc3\xa7", "artist");
    CHECK(cyberdeck_music::parse_id3v2(exact_limit, parsed));
    CHECK(std::strlen(parsed.title.data()) == k_max_title_bytes);
    CHECK(std::string_view(parsed.title.data()).substr(k_max_title_bytes - 2) == "\xc3\xa7");
    CHECK(parsed.title[k_max_title_bytes] == '\0');

    /* Malformed input becomes '?', never a truncated or ambiguous sequence. */
    auto invalid_utf8 = make_tag(3, std::string("ok\xc0\xaf", 4), "artist");
    CHECK(cyberdeck_music::parse_id3v2(invalid_utf8, parsed));
    CHECK(std::string_view(parsed.title.data()) == "ok??");
    CHECK(valid_utf8(parsed.title.data()));

    auto long_text = make_tag(3, std::string(k_max_title_bytes + 20, 't'),
                              std::string(k_max_artist_bytes + 20, 'a'));
    CHECK(cyberdeck_music::parse_id3v2(long_text, parsed));
    CHECK(std::strlen(parsed.title.data()) == k_max_title_bytes);
    CHECK(std::strlen(parsed.artist.data()) == k_max_artist_bytes);
    CHECK(parsed.title.back() == '\0' && parsed.artist.back() == '\0');
}

void test_id3_legacy_text_encodings() {
    auto make_encoded_tag = [](unsigned encoding, std::string_view encoded) {
        std::string payload(1, static_cast<char>(encoding));
        payload.append(encoded);
        std::string frames;
        frames += "TIT2";
        append_syncsafe(frames, static_cast<std::uint32_t>(payload.size()));
        frames.append(2, '\0');
        frames += payload;
        std::string tag = "ID3";
        tag.push_back('\x04');
        tag.append(2, '\0');
        append_syncsafe(tag, static_cast<std::uint32_t>(frames.size()));
        tag += frames;
        return tag;
    };

    track parsed{};
    auto latin1 = make_encoded_tag(0, std::string("A\x01" "B\x80", 4));
    CHECK(cyberdeck_music::parse_id3v2(latin1, parsed));
    CHECK(std::string_view(parsed.title.data()) == "A B?");

    /* ISO-8859-1 bytes are transcoded to UTF-8: accented letters become two
     * bytes, U+00A0 becomes a space and C1 controls become '?'. */
    auto latin1_accents = make_encoded_tag(0, std::string("\x7f\x9f\xa0\xc0\xff", 5));
    CHECK(cyberdeck_music::parse_id3v2(latin1_accents, parsed));
    CHECK(std::string_view(parsed.title.data()) == "?? \xc3\x80\xc3\xbf");
    CHECK(valid_utf8(parsed.title.data()));

    auto latin1_cafe = make_encoded_tag(0, std::string("Caf\xe9", 4));
    CHECK(cyberdeck_music::parse_id3v2(latin1_cafe, parsed));
    CHECK(std::string_view(parsed.title.data()) == "Caf\xc3\xa9");
    CHECK(valid_utf8(parsed.title.data()));

    auto latin1_space = make_encoded_tag(0, std::string("S\xe3o Paulo", 9));
    CHECK(cyberdeck_music::parse_id3v2(latin1_space, parsed));
    CHECK(std::string_view(parsed.title.data()) == "S\xc3\xa3o Paulo");
    CHECK(valid_utf8(parsed.title.data()));

    auto utf16_be = make_encoded_tag(1, std::string("\xfe\xff\0A\0B\0\0", 8));
    CHECK(cyberdeck_music::parse_id3v2(utf16_be, parsed));
    CHECK(std::string_view(parsed.title.data()) == "AB");

    auto utf16_le_bom = make_encoded_tag(1, std::string("\xff\xfe" "A\0B\0", 6));
    CHECK(cyberdeck_music::parse_id3v2(utf16_le_bom, parsed));
    CHECK(std::string_view(parsed.title.data()) == "AB");

    /* Encoding 1 without a BOM uses the documented little-endian default. */
    auto utf16_no_bom = make_encoded_tag(1, std::string("A\x00" "B\x00", 4));
    CHECK(cyberdeck_music::parse_id3v2(utf16_no_bom, parsed));
    CHECK(std::string_view(parsed.title.data()) == "AB");

    /* Encoding 2 is UTF-16BE without a BOM per ID3v2.4, not little-endian. */
    auto utf16_be_spec = make_encoded_tag(2, std::string("\x00" "A\x00" "B", 4));
    CHECK(cyberdeck_music::parse_id3v2(utf16_be_spec, parsed));
    CHECK(std::string_view(parsed.title.data()) == "AB");

    /* Code units are transcoded to UTF-8: Latin-1 accents become two bytes,
     * typographic punctuation becomes ASCII and an ellipsis becomes three dots. */
    auto utf16_accent = make_encoded_tag(2, std::string("\x00" "A\x00\xe9", 4));
    CHECK(cyberdeck_music::parse_id3v2(utf16_accent, parsed));
    CHECK(std::string_view(parsed.title.data()) == "A\xc3\xa9");
    CHECK(valid_utf8(parsed.title.data()));

    auto utf16_punctuation = make_encoded_tag(2, std::string("\x20\x18\x20\x26", 4));
    CHECK(cyberdeck_music::parse_id3v2(utf16_punctuation, parsed));
    CHECK(std::string_view(parsed.title.data()) == "'...");

    auto utf16_space = make_encoded_tag(2, std::string("\x00\xa0", 2));
    CHECK(cyberdeck_music::parse_id3v2(utf16_space, parsed));
    CHECK(std::string_view(parsed.title.data()) == " ");

    auto unsupported = make_encoded_tag(4, "ignored");
    CHECK(cyberdeck_music::parse_id3v2(unsupported, parsed));
    CHECK(parsed.title[0] == '\0');

    auto utf16_odd_tail = make_encoded_tag(2, std::string("\x00" "A" "Z", 3));
    CHECK(cyberdeck_music::parse_id3v2(utf16_odd_tail, parsed));
    CHECK(std::string_view(parsed.title.data()) == "A");

    /* U+00D8 is a printable Latin-1 glyph and must reach LVGL as UTF-8; a lone
     * or paired surrogate is outside the font and becomes '?'. */
    auto utf16_latin1_byte = make_encoded_tag(2, std::string("\x00\xd8", 2));
    CHECK(cyberdeck_music::parse_id3v2(utf16_latin1_byte, parsed));
    CHECK(std::string_view(parsed.title.data()) == "\xc3\x98");
    CHECK(valid_utf8(parsed.title.data()));

    auto utf16_surrogate = make_encoded_tag(2, std::string("\xd8\x00\x00\x00", 4));
    CHECK(cyberdeck_music::parse_id3v2(utf16_surrogate, parsed));
    CHECK(std::string_view(parsed.title.data()) == "?");

    /* U+1F600 arrives as a surrogate pair; it has no glyph, so one '?'. */
    auto utf16_surrogate_pair = make_encoded_tag(2, std::string("\xd8\x3d\xde\x00", 4));
    CHECK(cyberdeck_music::parse_id3v2(utf16_surrogate_pair, parsed));
    CHECK(std::string_view(parsed.title.data()) == "?");
    CHECK(valid_utf8(parsed.title.data()));

    auto empty_utf16 = make_encoded_tag(1, "");
    CHECK(cyberdeck_music::parse_id3v2(empty_utf16, parsed));
    CHECK(parsed.title[0] == '\0');

    auto invalid_utf8_continuation = make_tag(4, std::string("\xe2(A", 3), "artist");
    CHECK(cyberdeck_music::parse_id3v2(invalid_utf8_continuation, parsed));
    CHECK(std::string_view(parsed.title.data()) == "?(A");
    CHECK(valid_utf8(parsed.title.data()));

    /* A truncated multi-byte sequence collapses to a single '?' (maximal
     * subpart), never to a dangling lead byte. */
    auto truncated_utf8 = make_tag(4, std::string("\xe2\x82", 2), "artist");
    CHECK(cyberdeck_music::parse_id3v2(truncated_utf8, parsed));
    CHECK(std::string_view(parsed.title.data()) == "?");
    CHECK(valid_utf8(parsed.title.data()));

    auto overlong_utf8 = make_tag(4, std::string("\xe0\x80\xaf", 3), "artist");
    CHECK(cyberdeck_music::parse_id3v2(overlong_utf8, parsed));
    CHECK(std::string_view(parsed.title.data()) == "?");
    CHECK(valid_utf8(parsed.title.data()));
}

void test_metadata_struct_and_display_text() {
    /* The bounded metadata overload fills only the frames the tag carries, so
     * the parser and the scan never need a whole `track` on the stack. */
    std::string frames;
    append_frame(frames, 4, "TIT2", "Only title");
    std::string title_only = "ID3";
    title_only.push_back('\x04');
    title_only.append(2, '\0');
    append_syncsafe(title_only, static_cast<std::uint32_t>(frames.size()));
    title_only += frames;

    cyberdeck_music::metadata tags{};
    std::strcpy(tags.artist.data(), "keep");
    CHECK(cyberdeck_music::parse_id3v2(title_only, tags));
    CHECK(std::string_view(tags.title.data()) == "Only title");
    CHECK(std::string_view(tags.artist.data()) == "keep");

    /* The track overload still leaves a field untouched when its frame is
     * absent, while a present but empty frame keeps clearing it. */
    track partial{};
    std::strcpy(partial.artist.data(), "keep");
    CHECK(cyberdeck_music::parse_id3v2(title_only, partial));
    CHECK(std::string_view(partial.title.data()) == "Only title");
    CHECK(std::string_view(partial.artist.data()) == "keep");

    std::array<char, k_max_title_bytes + 1> buffer{};

    /* A valid file name is preserved byte for byte. */
    cyberdeck_music::sanitize_display_text("alpha.mp3", buffer.data(), k_max_title_bytes);
    CHECK(std::string_view(buffer.data()) == "alpha.mp3");
    CHECK(valid_utf8(buffer.data()));

    /* An invalid byte becomes a single '?' (maximal subpart) without losing the
     * rest of the name. */
    cyberdeck_music::sanitize_display_text(std::string_view("bad\xff.mp3", 9), buffer.data(), k_max_title_bytes);
    CHECK(std::string_view(buffer.data()) == "bad?.mp3");
    CHECK(valid_utf8(buffer.data()));

    /* Controls become spaces and typographic marks are transliterated, exactly
     * like the ID3 UTF-8 path. */
    cyberdeck_music::sanitize_display_text(std::string_view("a\x01" "b\xe2\x80\xa6", 6), buffer.data(),
                                           k_max_title_bytes);
    CHECK(std::string_view(buffer.data()) == "a b...");

    /* A two-byte sequence that does not fit is dropped whole, so the truncated
     * name still ends on a code point boundary. */
    const std::string long_name = std::string(k_max_title_bytes - 1, 'a') + "\xc3\xa7" + ".mp3";
    cyberdeck_music::sanitize_display_text(long_name, buffer.data(), k_max_title_bytes);
    CHECK(std::strlen(buffer.data()) == k_max_title_bytes - 1);
    CHECK(std::string_view(buffer.data()) == std::string(k_max_title_bytes - 1, 'a'));
    CHECK(buffer[k_max_title_bytes - 1] == '\0' && buffer[k_max_title_bytes] == '\0');
    CHECK(valid_utf8(buffer.data()));

    /* A sequence that fits exactly is kept and NUL-terminated one byte later. */
    const std::string exact_name = std::string(k_max_title_bytes - 2, 'a') + "\xc3\xa7";
    cyberdeck_music::sanitize_display_text(exact_name, buffer.data(), k_max_title_bytes);
    CHECK(std::strlen(buffer.data()) == k_max_title_bytes);
    CHECK(buffer[k_max_title_bytes] == '\0');
    CHECK(valid_utf8(buffer.data()));

    /* Degenerate inputs stay terminated and never write past `max`. */
    cyberdeck_music::sanitize_display_text("", buffer.data(), k_max_title_bytes);
    CHECK(buffer[0] == '\0');
    cyberdeck_music::sanitize_display_text("abc", buffer.data(), 0);
    CHECK(buffer[0] == '\0');
    cyberdeck_music::sanitize_display_text("abc", nullptr, k_max_title_bytes);
}

/* AC-MUSIC-META-01/02: `TALB` (v2.3/v2.4) and `TAL` (v2.2) fill the album
 * through the same decode path as title/artist, a duplicated frame follows the
 * same last-frame-wins rule, and a present but empty frame clears the field. */
void test_id3_album_versions_and_duplicates() {
    for (unsigned version : {2U, 3U, 4U}) {
        const std::string id = version == 2 ? "TAL" : "TALB";
        std::string frames;
        append_frame(frames, version, version == 2 ? "TT2" : "TIT2", "Title");
        append_frame(frames, version, version == 2 ? "TP1" : "TPE1", "Artist");
        append_frame(frames, version, id, "Album");
        const std::string bytes = wrap_tag(version, frames);

        track parsed{};
        CHECK(cyberdeck_music::parse_id3v2(bytes, parsed));
        CHECK(std::string_view(parsed.title.data()) == "Title");
        CHECK(std::string_view(parsed.artist.data()) == "Artist");
        CHECK(std::string_view(parsed.album.data()) == "Album");

        cyberdeck_music::metadata tags{};
        CHECK(cyberdeck_music::parse_id3v2(bytes, tags));
        CHECK(std::string_view(tags.album.data()) == "Album");

        /* The frame ids are version-specific: a control frame that is not the
         * album id must leave the album empty for every version. */
        const std::string control_id = version == 2 ? "TCO" : "TCON";
        std::string control_frames;
        append_raw_frame(control_frames, version, control_id, std::string(1, '\x03') + "Album");
        track control{};
        CHECK(cyberdeck_music::parse_id3v2(wrap_tag(version, control_frames), control));
        CHECK(control.album[0] == '\0');

        /* Duplicate album frames: the last one wins, exactly like title/artist. */
        std::string duplicates;
        append_frame(duplicates, version, id, "First");
        append_frame(duplicates, version, id, "Second");
        track last{};
        CHECK(cyberdeck_music::parse_id3v2(wrap_tag(version, duplicates), last));
        CHECK(std::string_view(last.album.data()) == "Second");

        /* A present but empty frame clears instead of inheriting "First". */
        std::string cleared;
        append_frame(cleared, version, id, "First");
        append_frame(cleared, version, id, "");
        track empty{};
        std::strcpy(empty.album.data(), "stale");
        CHECK(cyberdeck_music::parse_id3v2(wrap_tag(version, cleared), empty));
        CHECK(empty.album[0] == '\0');
    }
}

/* AC-MUSIC-META-01/02: the album decodes through every supported encoding with
 * the same transliteration as title/artist; an unsupported encoding clears. */
void test_id3_album_encodings() {
    const auto album_tag = [](unsigned version, std::string payload) {
        std::string frames;
        append_raw_frame(frames, version, version == 2 ? "TAL" : "TALB", payload);
        return wrap_tag(version, frames);
    };
    track parsed{};

    CHECK(cyberdeck_music::parse_id3v2(album_tag(4, std::string(1, '\x03') + "Caf\xc3\xa9"), parsed));
    CHECK(std::string_view(parsed.album.data()) == "Caf\xc3\xa9");
    CHECK(valid_utf8(parsed.album.data()));

    CHECK(cyberdeck_music::parse_id3v2(album_tag(3, std::string(1, '\x00') + "Caf\xe9"), parsed));
    CHECK(std::string_view(parsed.album.data()) == "Caf\xc3\xa9");
    CHECK(valid_utf8(parsed.album.data()));

    CHECK(cyberdeck_music::parse_id3v2(
        album_tag(4, std::string(1, '\x01') + std::string("\xfe\xff\0A\0B", 6)), parsed));
    CHECK(std::string_view(parsed.album.data()) == "AB");
    CHECK(cyberdeck_music::parse_id3v2(
        album_tag(4, std::string(1, '\x01') + std::string("\xff\xfe" "A\0B\0", 6)), parsed));
    CHECK(std::string_view(parsed.album.data()) == "AB");
    CHECK(cyberdeck_music::parse_id3v2(
        album_tag(4, std::string(1, '\x02') + std::string("\x00" "A\x00" "B", 4)), parsed));
    CHECK(std::string_view(parsed.album.data()) == "AB");

    /* Typographic marks are transliterated and out-of-font code points become a
     * single '?', never a dangling byte. */
    CHECK(cyberdeck_music::parse_id3v2(
        album_tag(4, std::string(1, '\x03') + "\xe2\x80\x9cHi\xe2\x80\x9d\xe2\x80\xa6"), parsed));
    CHECK(std::string_view(parsed.album.data()) == "\"Hi\"...");
    CHECK(cyberdeck_music::parse_id3v2(
        album_tag(4, std::string(1, '\x03') + "\xe6\xbc\xa2"), parsed));
    CHECK(std::string_view(parsed.album.data()) == "?");
    CHECK(valid_utf8(parsed.album.data()));

    /* Unsupported encodings (and a payload with no encoding byte at all) clear
     * the field instead of keeping whatever it held before. */
    std::strcpy(parsed.album.data(), "stale");
    CHECK(cyberdeck_music::parse_id3v2(album_tag(4, std::string(1, '\x04') + "ignored"), parsed));
    CHECK(parsed.album[0] == '\0');
    std::strcpy(parsed.album.data(), "stale");
    CHECK(cyberdeck_music::parse_id3v2(album_tag(4, std::string(1, '\x03')), parsed));
    CHECK(parsed.album[0] == '\0');
    /* The `metadata` overload leaves an absent album untouched. */
    std::string title_only;
    append_frame(title_only, 4, "TIT2", "Only title");
    cyberdeck_music::metadata tags{};
    std::strcpy(tags.album.data(), "keep");
    CHECK(cyberdeck_music::parse_id3v2(wrap_tag(4, title_only), tags));
    CHECK(std::string_view(tags.album.data()) == "keep");
}

/* AC-MUSIC-META-03/04: the album is bounded by a single named constant, output
 * stays valid UTF-8 and NUL-terminated, truncation happens between code points
 * and a failed tag publishes nothing at all. */
void test_id3_album_limits_and_fail_closed() {
    const auto album_tag = [](std::string text) {
        std::string frames;
        append_raw_frame(frames, 4, "TALB", std::string(1, '\x03') + text);
        return wrap_tag(4, frames);
    };
    const std::size_t capacity = cyberdeck_music::k_max_album_bytes;
    track parsed{};

    CHECK(cyberdeck_music::parse_id3v2(album_tag(std::string(capacity, 'a')), parsed));
    CHECK(std::strlen(parsed.album.data()) == capacity);
    CHECK(parsed.album[capacity] == '\0');

    CHECK(cyberdeck_music::parse_id3v2(album_tag(std::string(capacity + 1, 'a')), parsed));
    CHECK(std::strlen(parsed.album.data()) == capacity);
    CHECK(parsed.album[capacity] == '\0');

    /* A two-byte code point that does not fit is dropped whole. */
    CHECK(cyberdeck_music::parse_id3v2(
        album_tag(std::string(capacity - 1, 'a') + "\xc3\xa7"), parsed));
    CHECK(std::strlen(parsed.album.data()) == capacity - 1);
    CHECK(std::string_view(parsed.album.data()) == std::string(capacity - 1, 'a'));
    CHECK(valid_utf8(parsed.album.data()));

    /* ... and one that fits exactly is kept with the NUL one byte later. */
    CHECK(cyberdeck_music::parse_id3v2(
        album_tag(std::string(capacity - 2, 'a') + "\xc3\xa7"), parsed));
    CHECK(std::strlen(parsed.album.data()) == capacity);
    CHECK(parsed.album[capacity] == '\0');
    CHECK(valid_utf8(parsed.album.data()));

    /* A four-byte code point with no glyph collapses to '?', never a partial
     * sequence. */
    CHECK(cyberdeck_music::parse_id3v2(
        album_tag(std::string(capacity - 4, 'a') + "\xf0\x9f\x8e\xb5"), parsed));
    CHECK(std::string_view(parsed.album.data()) == std::string(capacity - 4, 'a') + "?");
    CHECK(valid_utf8(parsed.album.data()));

    /* Fail closed: the album must not be published when the tag is rejected. */
    std::string frames;
    append_frame(frames, 4, "TALB", "Album");
    std::string bad_version = wrap_tag(4, frames);
    bad_version[3] = 5;
    track rejected{};
    std::strcpy(rejected.album.data(), "unchanged");
    CHECK(!cyberdeck_music::parse_id3v2(bad_version, rejected));
    CHECK(std::string_view(rejected.album.data()) == "unchanged");

    std::string bad_syncsafe = wrap_tag(4, frames);
    bad_syncsafe[6] = static_cast<char>(0x80);
    CHECK(!cyberdeck_music::parse_id3v2(bad_syncsafe, rejected));
    CHECK(std::string_view(rejected.album.data()) == "unchanged");

    std::string oversized("ID3\x04\0\0", 6);
    append_syncsafe(oversized, static_cast<std::uint32_t>(k_tag_total_cap - cyberdeck_music::k_id3v2_header_bytes + 1));
    oversized.append(k_tag_total_cap - cyberdeck_music::k_id3v2_header_bytes + 1, 'x');
    CHECK(!cyberdeck_music::parse_id3v2(oversized, rejected));
    CHECK(std::string_view(rejected.album.data()) == "unchanged");

    /* A frame that overruns the declared tag size rejects the whole tag. */
    std::string overrun = wrap_tag(4, frames);
    CHECK(!cyberdeck_music::parse_id3v2(overrun.substr(0, overrun.size() - 1), rejected));
    CHECK(std::string_view(rejected.album.data()) == "unchanged");

    /* A zero-sized frame is rejected after a valid one, and the earlier frame
     * must not leak into the output. */
    std::string zero_frames;
    append_frame(zero_frames, 4, "TIT2", "Partial");
    std::string zero_frame("TALB", 4);
    zero_frame.append(6, '\0');
    zero_frames += zero_frame;
    std::strcpy(rejected.title.data(), "unchanged-title");
    CHECK(!cyberdeck_music::parse_id3v2(wrap_tag(4, zero_frames), rejected));
    CHECK(std::string_view(rejected.album.data()) == "unchanged");
    CHECK(std::string_view(rejected.title.data()) == "unchanged-title");

    /* The `track` overload also stays fail-closed. */
    track track_rejected{};
    std::strcpy(track_rejected.album.data(), "keep");
    CHECK(!cyberdeck_music::parse_id3v2(bad_version, track_rejected));
    CHECK(std::string_view(track_rejected.album.data()) == "keep");
}

void test_clear_and_empty_state() {
    model queue;
    track item = make_track("/single.mp3");
    CHECK(queue.replace_queue(&item, 1));
    CHECK(queue.select(0));
    CHECK(queue.seek(5000));
    queue.set_state(cyberdeck_music::playback_state::playing);
    queue.clear();
    const auto state = queue.status();
    CHECK(state.track_count == 0);
    CHECK(state.selected == 0);
    CHECK(state.position_ms == 0);
    CHECK(state.state == cyberdeck_music::playback_state::stopped);
    CHECK(queue.current() == nullptr);
    CHECK(!queue.next());
    CHECK(!queue.previous());
    CHECK(!queue.seek(0));
}
}

int main() {
    test_path_policy();
    test_queue_bounds_and_atomic_replace();
    test_track_validation_and_selection();
    test_navigation_repeat_shuffle_and_seek();
    test_id3_versions_and_malformed_tags();
    test_id3_large_tag_frame_walk();
    test_id3_frame_bounds_flags_and_all_or_nothing();
    test_id3_text_encoding_and_limits();
    test_id3_legacy_text_encodings();
    test_metadata_struct_and_display_text();
    test_id3_album_versions_and_duplicates();
    test_id3_album_encodings();
    test_id3_album_limits_and_fail_closed();
    test_clear_and_empty_state();
    std::cout << checks - failures << '/' << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
