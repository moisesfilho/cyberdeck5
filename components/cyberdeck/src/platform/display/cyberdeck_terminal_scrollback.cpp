#include "platform/display/cyberdeck_terminal_scrollback.h"

#include <cstring>

namespace cyberdeck_terminal_scrollback {

model::model()
{
    s_text.reserve(k_capacity);
}

std::size_t model::valid_start_offset(const std::string &text, std::size_t offset)
{
    if (offset >= text.size()) return text.size();
    while (offset < text.size() &&
           (static_cast<unsigned char>(text[offset]) & 0xC0U) == 0x80U) {
        ++offset;
    }
    return offset;
}

std::string model::tail_utf8(const std::string &text, std::size_t capacity)
{
    if (capacity == 0 || text.empty()) return {};
    if (text.size() <= capacity) return text;
    return text.substr(valid_start_offset(text, text.size() - capacity));
}

void model::append(const char *data, std::size_t length)
{
    if (data == nullptr || length == 0) return;
    if (length >= k_capacity) {
        std::size_t offset = length - k_capacity;
        while (offset < length &&
               (static_cast<unsigned char>(data[offset]) & 0xC0U) == 0x80U) {
            ++offset;
        }
        s_text.assign(data + offset, length - offset);
        return;
    }
    if (s_text.size() + length > k_capacity) {
        const std::size_t excess = s_text.size() + length - k_capacity;
        s_text.erase(0, valid_start_offset(s_text, excess));
    }
    s_text.append(data, length);
}

void model::clear()
{
    s_text.clear();
}

std::size_t model::copy(char *buffer, std::size_t capacity) const
{
    if (buffer == nullptr || capacity == 0) return 0;
    const std::string snapshot = tail_utf8(s_text, capacity);
    std::memcpy(buffer, snapshot.data(), snapshot.size());
    return snapshot.size();
}

std::string model::text() const
{
    return s_text;
}

std::string model::viewport(std::size_t capacity) const
{
    return tail_utf8(s_text, capacity);
}

} // namespace cyberdeck_terminal_scrollback
