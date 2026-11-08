#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cyberdeck_music {
inline constexpr std::size_t k_max_tracks = 64;
inline constexpr std::size_t k_max_path_bytes = 256;
inline constexpr std::size_t k_max_title_bytes = 96;
inline constexpr std::size_t k_max_artist_bytes = 64;
inline constexpr std::size_t k_max_album_bytes = 64;
inline constexpr std::uint32_t k_max_track_bytes = 32U * 1024U * 1024U;

enum class repeat_mode : std::uint8_t { off, all, one };
enum class playback_state : std::uint8_t { stopped, playing, paused, error };
struct scan_measurement {
    bool completed = false;
    bool succeeded = false;
    std::uint32_t duration_ms = 0;
    std::uint32_t stack_high_water_bytes = 0;
};
struct scan_measurements {
    scan_measurement queue;
    scan_measurement browser;
};
enum class error_stage : std::uint8_t { none, open, id3, decode, codec_start, codec_write, read, io };

struct track {
    std::array<char, k_max_path_bytes + 1> path{};
    std::array<char, k_max_title_bytes + 1> title{};
    std::array<char, k_max_artist_bytes + 1> artist{};
    std::array<char, k_max_album_bytes + 1> album{};
    std::uint32_t size_bytes = 0;
    std::uint32_t duration_ms = 0;
    /* True only when `title` is the sanitized file name because the tag carried
     * no usable TIT2/TT2 value; it is the machine-readable marker of the
     * fallback, so a real title that happens to equal the file name keeps it
     * false. */
    bool title_from_filename = false;
};

/* Decoded ID3v2 text without the queue fields. It keeps the metadata read off
 * the scan task's stack: the parser and `read_metadata` only ever touch this
 * bounded struct, and the owners copy title/artist/album into a `track`
 * afterwards. */
struct metadata {
    std::array<char, k_max_title_bytes + 1> title{};
    std::array<char, k_max_artist_bytes + 1> artist{};
    std::array<char, k_max_album_bytes + 1> album{};
};

struct snapshot {
    playback_state state = playback_state::stopped;
    error_stage failure_stage = error_stage::none;
    std::size_t selected = 0;
    std::size_t track_count = 0;
    std::uint32_t position_ms = 0;
    std::uint8_t volume = 20;
    repeat_mode repeat = repeat_mode::off;
    bool shuffle = false;
    track current{};
};

class model final {
  public:
    bool replace_queue(const track *tracks, std::size_t count);
    bool select(std::size_t index);
    bool next();
    bool previous();
    bool seek(std::uint32_t position_ms);
    void set_state(playback_state state) { state_ = state; }
    void set_position(std::uint32_t position_ms) { position_ms_ = position_ms; }
    void set_volume(std::uint8_t volume);
    void toggle_shuffle() { shuffle_ = !shuffle_; }
    void cycle_repeat();
    void clear();
    const track *current() const;
    const track *at(std::size_t index) const;
    snapshot status() const;
  private:
    std::array<track, k_max_tracks> tracks_{};
    std::size_t count_ = 0;
    std::size_t selected_ = 0;
    std::uint32_t position_ms_ = 0;
    std::uint8_t volume_ = 20;
    playback_state state_ = playback_state::stopped;
    repeat_mode repeat_ = repeat_mode::off;
    bool shuffle_ = false;
};

bool is_mp3_path(std::string_view path);

/* ID3v2 framing shared by the in-memory parser and the streaming reader, so a
 * tag is accepted or rejected by one set of rules regardless of how its bytes
 * were obtained. The sizes and windows those rules use live in
 * `apps/music/cyberdeck_music_metadata.h`. */
enum class id3v2_header_status : std::uint8_t { absent, accepted, rejected };
enum class id3v2_field : std::uint8_t { none, title, artist, album };

struct id3v2_header {
    std::uint8_t version = 0;
    /* Syncsafe `size` field: the payload, excluding the ten header bytes. */
    std::size_t payload_bytes = 0;
};

struct id3v2_frame_header {
    std::uint8_t header_bytes = 0;
    std::uint32_t payload_bytes = 0;
    /* False when the frame carries any flag; such a frame cannot be decoded at
     * the declared offset, so a target frame rejects the whole tag. */
    bool flags_clear = true;
    id3v2_field field = id3v2_field::none;
};

/* Classifies the ten-byte tag header: `absent` when the data does not start
 * with an ID3v2 tag (nothing to publish), `rejected` for a truncated, malformed,
 * unsupported or over-ceiling tag, which must be ignored whole. */
id3v2_header_status parse_id3v2_header(const unsigned char *bytes, std::size_t count, id3v2_header &out);
/* Parses one frame header. Returns false for a truncated header or a size field
 * that cannot be trusted (zero size, syncsafe byte with the high bit set); the
 * caller still has to reject a payload that does not fit the declared tag end. */
bool parse_id3v2_frame_header(const unsigned char *bytes, std::size_t count, std::uint8_t version,
                              id3v2_frame_header &out);
/* Decodes one textual frame payload into `dst` (`max` bytes plus the NUL) with
 * the encoding and transliteration rules shared by both read paths. */
void decode_id3v2_text(std::string_view payload, char *dst, std::size_t max);

/* Fills `out.title`/`out.artist`/`out.album` for the frames the tag carries; a
 * present but empty frame clears the field, while an absent frame leaves it
 * untouched. */
bool parse_id3v2(std::string_view bytes, metadata &out);
/* Track-level overload kept for callers that already own a `track`; it only
 * replaces the fields whose frame is present and never copies the whole track. */
bool parse_id3v2(std::string_view bytes, track &out);
/* Re-encodes arbitrary text (file names included) exactly like ID3 UTF-8 text:
 * controls become spaces, malformed sequences become one '?' per maximal
 * subpart, typographic marks are transliterated and truncation happens between
 * code points. `dst` must hold `max` bytes plus the NUL terminator. */
void sanitize_display_text(std::string_view source, char *dst, std::size_t max);
}
