#pragma once

#include <cstddef>
#include <string>

namespace cyberdeck_terminal_scrollback {

class model {
public:
    static constexpr std::size_t k_capacity = 12288;

    model();

    void append(const char *data, std::size_t length);
    void clear();
    std::size_t copy(char *buffer, std::size_t capacity) const;
    std::string text() const;
    std::string viewport(std::size_t capacity) const;
    std::size_t size() const { return s_text.size(); }

private:
    static std::size_t valid_start_offset(const std::string &text, std::size_t offset);
    static std::string tail_utf8(const std::string &text, std::size_t capacity);

    std::string s_text;
};

} // namespace cyberdeck_terminal_scrollback
