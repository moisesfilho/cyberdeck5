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

void append_frame(std::string &tag, unsigned version, std::string_view id, std::string_view text) {
    const std::string payload = std::string(1, '\x03') + std::string(text);
    tag.append(id);
    if (version == 2) {
        const auto size = static_cast<std::uint32_t>(payload.size());
        tag.push_back(static_cast<char>((size >> 16) & 0xff));
        tag.push_back(static_cast<char>((size >> 8) & 0xff));
        tag.push_back(static_cast<char>(size & 0xff));
    } else {
        const auto size = static_cast<std::uint32_t>(payload.size());
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

std::string make_tag(unsigned version, std::string_view title, std::string_view artist) {
    std::string frames;
    append_frame(frames, version, version == 2 ? "TT2" : "TIT2", title);
    append_frame(frames, version, version == 2 ? "TP1" : "TPE1", artist);
    std::string bytes = "ID3";
    bytes.push_back(static_cast<char>(version));
    bytes.push_back('\0');
    bytes.push_back('\0');
    append_syncsafe(bytes, static_cast<std::uint32_t>(frames.size()));
    bytes += frames;
    return bytes;
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

    std::string oversized("ID3\x04\0\0", 6);
    append_syncsafe(oversized, 64 * 1024 + 1);
    oversized.append(64 * 1024 + 1, 'x');
    CHECK(!cyberdeck_music::parse_id3v2(oversized, unchanged));
    std::string max_tag("ID3\x04\0\0", 6);
    append_syncsafe(max_tag, 64 * 1024);
    max_tag.append(64 * 1024, '\0');
    CHECK(cyberdeck_music::parse_id3v2(max_tag, unchanged));
}

void test_id3_text_encoding_and_limits() {
    auto utf8 = make_tag(4, "Caf\xc3\xa9", "Beyonc\xc3\xa9");
    track parsed{};
    CHECK(cyberdeck_music::parse_id3v2(utf8, parsed));
    CHECK(std::string_view(parsed.title.data()) == "Caf\xc3\xa9");
    CHECK(std::string_view(parsed.artist.data()) == "Beyonc\xc3\xa9");

    auto invalid_utf8 = make_tag(3, std::string("ok\xc0\xaf", 4), "artist");
    CHECK(cyberdeck_music::parse_id3v2(invalid_utf8, parsed));
    CHECK(std::string_view(parsed.title.data()) == "ok");

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

    auto utf16_be = make_encoded_tag(1, std::string("\xfe\xff\0A\0B\0\0", 8));
    CHECK(cyberdeck_music::parse_id3v2(utf16_be, parsed));
    CHECK(std::string_view(parsed.title.data()) == "AB");

    auto utf16_le_bom = make_encoded_tag(1, std::string("\xff\xfe" "A\0B\0", 6));
    CHECK(cyberdeck_music::parse_id3v2(utf16_le_bom, parsed));
    CHECK(std::string_view(parsed.title.data()) == "AB");

    auto utf16_le = make_encoded_tag(2, std::string("A\0B\0", 4));
    CHECK(cyberdeck_music::parse_id3v2(utf16_le, parsed));
    CHECK(std::string_view(parsed.title.data()) == "AB");

    auto unsupported = make_encoded_tag(4, "ignored");
    CHECK(cyberdeck_music::parse_id3v2(unsupported, parsed));
    CHECK(parsed.title[0] == '\0');

    auto utf16_odd_tail = make_encoded_tag(2, std::string("A\0Z", 3));
    CHECK(cyberdeck_music::parse_id3v2(utf16_odd_tail, parsed));
    CHECK(std::string_view(parsed.title.data()) == "A");

    auto utf16_surrogate = make_encoded_tag(2, std::string("\x00\xd8", 2));
    CHECK(cyberdeck_music::parse_id3v2(utf16_surrogate, parsed));
    CHECK(std::string_view(parsed.title.data()) == "?");

    auto empty_utf16 = make_encoded_tag(1, "");
    CHECK(cyberdeck_music::parse_id3v2(empty_utf16, parsed));
    CHECK(parsed.title[0] == '\0');

    auto invalid_utf8_continuation = make_tag(4, std::string("\xe2(A", 3), "artist");
    CHECK(cyberdeck_music::parse_id3v2(invalid_utf8_continuation, parsed));
    CHECK(parsed.title[0] == '\0');

    auto truncated_utf8 = make_tag(4, std::string("\xe2\x82", 2), "artist");
    CHECK(cyberdeck_music::parse_id3v2(truncated_utf8, parsed));
    CHECK(parsed.title[0] == '\0');
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
    test_id3_text_encoding_and_limits();
    test_id3_legacy_text_encodings();
    test_clear_and_empty_state();
    std::cout << checks - failures << '/' << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
