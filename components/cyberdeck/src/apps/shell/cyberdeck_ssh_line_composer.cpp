#include "apps/shell/cyberdeck_ssh_line_composer.h"

#include <algorithm>

cyberdeck_ssh_line_composer::cyberdeck_ssh_line_composer() = default;

size_t cyberdeck_ssh_line_composer::begin(const char *command, size_t len,
                                          char *out, size_t out_cap)
{
    if (command == nullptr || m_active) return 0;

    m_payload.assign(command, len);
    m_payload.push_back('\n');
    m_matched = 0;
    m_active = true;

    const size_t written = std::min(len, out_cap);
    if (out != nullptr) {
        for (size_t i = 0; i < written; ++i) out[i] = command[i];
    }
    return written;
}

size_t cyberdeck_ssh_line_composer::feed(const char *data, size_t len,
                                         char *out, size_t out_cap)
{
    if (data == nullptr || len == 0) return 0;

    size_t written = 0;
    const auto emit = [&](char c) {
        if (out != nullptr && written < out_cap) out[written] = c;
        if (written < out_cap) ++written;
    };
    size_t pos = 0;
    while (pos < len) {
        if (!m_active) {
            emit(data[pos++]);
            continue;
        }

        const char c = data[pos];
        if (c == m_payload[m_matched]) {
            ++m_matched;
            ++pos;
            if (m_matched == m_payload.size()) {
                m_active = false;
                m_payload.clear();
                m_matched = 0;
            }
            continue;
        }

        /* Release the retained prefix and the divergent byte verbatim. */
        for (size_t i = 0; i < m_matched; ++i) emit(m_payload[i]);
        emit(c);
        ++pos;
        m_active = false;
        m_payload.clear();
        m_matched = 0;
    }

    return written;
}

size_t cyberdeck_ssh_line_composer::flush(char *, size_t)
{
    if (!m_active) return 0;

    size_t written = 0;
    m_active = false;
    m_payload.clear();
    m_matched = 0;
    return written;
}

bool cyberdeck_ssh_line_composer::active() const
{
    return m_active;
}
