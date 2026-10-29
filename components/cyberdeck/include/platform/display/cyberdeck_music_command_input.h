#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace cyberdeck_music_command_input {
enum class key : std::uint8_t { escape, enter, backspace, del, left, right, home, end, other };
enum class key_result : std::uint8_t { ignored, edited, closed_line, closed_player, executed };

using execute_callback = std::string (*)(std::string_view command, void *context);
using close_callback = void (*)(void *context);

class controller final {
  public:
    static constexpr std::size_t max_bytes = 96;

    void open();
    bool is_open() const { return open_; }
    std::string_view text() const { return text_; }
    std::size_t cursor() const { return cursor_; }
    std::string_view feedback() const { return feedback_; }
    void insert_text(std::string_view text);
    key_result handle(key input, execute_callback execute, void *execute_context,
                      close_callback close_player, void *close_context);

  private:
    static std::size_t previous_boundary(std::string_view text, std::size_t position);
    static std::size_t next_boundary(std::string_view text, std::size_t position);
    std::string text_;
    std::string feedback_;
    std::size_t cursor_ = 0;
    bool open_ = false;
};
} // namespace cyberdeck_music_command_input
