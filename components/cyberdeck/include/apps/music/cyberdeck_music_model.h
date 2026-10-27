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
inline constexpr std::uint32_t k_max_track_bytes = 32U * 1024U * 1024U;

enum class repeat_mode : std::uint8_t { off, all, one };
enum class playback_state : std::uint8_t { stopped, playing, paused, error };
enum class error_stage : std::uint8_t { none, open, id3, decode, codec_start, codec_write, read, io };

struct track {
    std::array<char, k_max_path_bytes + 1> path{};
    std::array<char, k_max_title_bytes + 1> title{};
    std::array<char, k_max_artist_bytes + 1> artist{};
    std::uint32_t size_bytes = 0;
    std::uint32_t duration_ms = 0;
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
bool parse_id3v2(std::string_view bytes, track &out);
}
