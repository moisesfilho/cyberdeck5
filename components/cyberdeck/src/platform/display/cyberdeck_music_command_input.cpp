#include "platform/display/cyberdeck_music_command_input.h"

#include <algorithm>

namespace cyberdeck_music_command_input {
namespace {
bool continuation(unsigned char byte) { return (byte & 0xC0U) == 0x80U; }

/* Bounded copy that never splits a UTF-8 codepoint, mirroring the command
 * buffer limit so the rendered feedback stays a single bounded line. */
void assign_bounded(std::string &target, std::string_view message) {
    std::size_t length = std::min(message.size(), controller::max_bytes);
    while (length > 0 && length < message.size() && continuation(static_cast<unsigned char>(message[length])))
        --length;
    target.assign(message.data(), length);
}
}

void controller::open() {
    text_.clear();
    cursor_ = 0;
    feedback_.clear();
    open_ = true;
}

void controller::set_feedback(std::string_view message) { assign_bounded(feedback_, message); }

void controller::insert_text(std::string_view input) {
    if (!open_) return;
    for (std::size_t offset = 0; offset < input.size() && text_.size() < max_bytes;) {
        const unsigned char lead = static_cast<unsigned char>(input[offset]);
        std::size_t length = lead < 0x80U ? 1 : (lead >= 0xC2U && lead <= 0xDFU) ? 2
            : (lead >= 0xE0U && lead <= 0xEFU) ? 3 : (lead >= 0xF0U && lead <= 0xF4U) ? 4 : 0;
        if (length == 0 || offset + length > input.size() || text_.size() + length > max_bytes) break;
        bool valid = true;
        for (std::size_t i = 1; i < length; ++i)
            valid = valid && continuation(static_cast<unsigned char>(input[offset + i]));
        if (!valid || lead < 0x20U || lead == 0x7FU) { ++offset; continue; }
        text_.insert(cursor_, input.data() + offset, length);
        cursor_ += length;
        offset += length;
    }
}

std::size_t controller::previous_boundary(std::string_view text, std::size_t position) {
    if (position == 0) return 0;
    --position;
    while (position > 0 && continuation(static_cast<unsigned char>(text[position]))) --position;
    return position;
}

std::size_t controller::next_boundary(std::string_view text, std::size_t position) {
    if (position >= text.size()) return text.size();
    ++position;
    while (position < text.size() && continuation(static_cast<unsigned char>(text[position]))) ++position;
    return position;
}

key_result controller::handle(key input, execute_callback execute, void *execute_context,
                               close_callback close_player, void *close_context) {
    if (input == key::escape) {
        if (open_) {
            open_ = false;
            text_.clear();
            cursor_ = 0;
            return key_result::closed_line;
        }
        if (close_player) close_player(close_context);
        return key_result::closed_player;
    }
    if (!open_) return key_result::ignored;
    switch (input) {
    case key::enter: {
        std::string_view command(text_);
        if (command.substr(0, 6) == "music ") command.remove_prefix(6);
        const auto first = command.find_first_not_of(" \t\r\n");
        if (first != std::string_view::npos) command.remove_prefix(first);
        else command = {};
        feedback_ = command.empty() || !execute
            ? "music: enter a subcommand\n" : execute(command, execute_context);
        open_ = false;
        text_.clear();
        cursor_ = 0;
        return key_result::executed;
    }
    case key::backspace:
        if (cursor_ > 0) { const auto previous = previous_boundary(text_, cursor_); text_.erase(previous, cursor_ - previous); cursor_ = previous; }
        break;
    case key::del:
        if (cursor_ < text_.size()) text_.erase(cursor_, next_boundary(text_, cursor_) - cursor_);
        break;
    case key::left: cursor_ = previous_boundary(text_, cursor_); break;
    case key::right: cursor_ = next_boundary(text_, cursor_); break;
    case key::home: cursor_ = 0; break;
    case key::end: cursor_ = text_.size(); break;
    case key::escape: case key::other: return key_result::ignored;
    }
    return key_result::edited;
}
} // namespace cyberdeck_music_command_input
