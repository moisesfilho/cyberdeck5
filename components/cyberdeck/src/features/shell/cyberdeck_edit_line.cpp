#include "features/shell/cyberdeck_edit_line.h"

#include <algorithm>

namespace {

size_t next_codepoint(const std::string &s, size_t pos)
{
    if (pos >= s.size()) return s.size();
    const unsigned char c = static_cast<unsigned char>(s[pos]);
    size_t width = 1;
    if ((c & 0xe0) == 0xc0) width = 2;
    else if ((c & 0xf0) == 0xe0) width = 3;
    else if ((c & 0xf8) == 0xf0) width = 4;
    return std::min(s.size(), pos + width);
}

size_t previous_codepoint(const std::string &s, size_t pos)
{
    if (pos == 0) return 0;
    size_t start = pos - 1;
    while (start > 0 && (static_cast<unsigned char>(s[start]) & 0xc0) == 0x80) --start;
    return start;
}

} // namespace

void cyberdeck_edit_line::set_session(cyberdeck_session_state state)
{
    m_session = state;
}

bool cyberdeck_edit_line::insert(const char *text, size_t len)
{
    if (text == nullptr && len != 0) return false;
    if (m_line.size() + len > limit) return false;
    if (len != 0) m_line.insert(m_cursor, text, len);
    m_cursor += len;
    return true;
}

bool cyberdeck_edit_line::insert_physical(const char *text, size_t len)
{
    return insert(text, len);
}

bool cyberdeck_edit_line::insert_virtual(const char *text, size_t len)
{
    return insert(text, len);
}

void cyberdeck_edit_line::backspace()
{
    if (m_cursor == 0) return;
    const size_t start = previous_codepoint(m_line, m_cursor);
    m_line.erase(start, m_cursor - start);
    m_cursor = start;
}

void cyberdeck_edit_line::del()
{
    if (m_cursor >= m_line.size()) return;
    const size_t end = next_codepoint(m_line, m_cursor);
    m_line.erase(m_cursor, end - m_cursor);
}

void cyberdeck_edit_line::cursor_left()
{
    m_cursor = previous_codepoint(m_line, m_cursor);
}

void cyberdeck_edit_line::cursor_right()
{
    m_cursor = next_codepoint(m_line, m_cursor);
}

void cyberdeck_edit_line::cursor_home() { m_cursor = 0; }
void cyberdeck_edit_line::cursor_end() { m_cursor = m_line.size(); }

size_t cyberdeck_edit_line::utf8_length() const
{
    size_t count = 0;
    for (size_t pos = 0; pos < m_line.size(); ++count) pos = next_codepoint(m_line, pos);
    return count;
}

std::string cyberdeck_edit_line::visible_line() const
{
    if (m_session == cyberdeck_session_state::PASSWORD) return std::string(m_line.size(), '*');
    return m_line;
}

cyberdeck_enter_result cyberdeck_edit_line::enter(bool line_already_sent)
{
    cyberdeck_enter_result result;
    const bool blank = m_line.find_first_not_of(" \t") == std::string::npos;
    switch (m_session) {
    case cyberdeck_session_state::HOST_KEY:
        result.action = cyberdeck_enter_action::ACCEPT_HOST_KEY;
        break;
    case cyberdeck_session_state::CONNECTED:
        if (m_line.empty() || line_already_sent) {
            result.action = cyberdeck_enter_action::SEND_NEWLINE;
            result.payload = "\n";
        } else {
            result.action = cyberdeck_enter_action::SEND_LINE_NEWLINE;
            result.payload = m_line;
            result.payload.push_back('\n');
        }
        break;
    case cyberdeck_session_state::PASSWORD:
        if (!blank) {
            result.action = cyberdeck_enter_action::SEND_PASSWORD;
            result.payload = m_line;
        }
        break;
    case cyberdeck_session_state::MENU:
        if (!blank) {
            result.action = cyberdeck_enter_action::LOCAL_COMMAND;
            result.payload = m_line;
            result.echo = "$ " + m_line + "\n";
        }
        break;
    }
    clear();
    return result;
}

void cyberdeck_edit_line::clear()
{
    std::fill(m_line.begin(), m_line.end(), '\0');
    m_line.clear();
    m_cursor = 0;
}
