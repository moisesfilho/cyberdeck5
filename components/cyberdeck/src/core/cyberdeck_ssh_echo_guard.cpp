#include "cyberdeck_ssh_echo_guard.h"

#include <algorithm>

cyberdeck_ssh_echo_guard::cyberdeck_ssh_echo_guard() = default;

bool cyberdeck_ssh_echo_guard::arm(const char *payload, size_t len)
{
    if (!payload || len == 0 || m_armed) return false;
    m_payload.assign(payload, len);
    m_matched = 0;
    m_armed = true;
    return true;
}

size_t cyberdeck_ssh_echo_guard::feed(const char *data, size_t len,
                                      char *out, size_t out_cap)
{
    if (!data || len == 0) return 0;

    size_t written = 0;
    const auto emit = [&](char c) {
        if (out && written < out_cap) out[written++] = c;
    };

    size_t pos = 0;
    while (pos < len) {
        if (!m_armed) {
            while (pos < len) emit(data[pos++]);
            break;
        }

        const char c = data[pos];
        if (c == m_payload[m_matched]) {
            ++m_matched;
            ++pos;
            if (m_matched == m_payload.size()) {
                m_payload.clear();
                m_matched = 0;
                m_armed = false;
            }
            continue;
        }

        /* The retained prefix is real output when the stream is not an echo. */
        for (size_t i = 0; i < m_matched; ++i) emit(m_payload[i]);
        emit(c);
        ++pos;
        m_payload.clear();
        m_matched = 0;
        m_armed = false;
        while (pos < len) emit(data[pos++]);
    }
    return written;
}

size_t cyberdeck_ssh_echo_guard::flush(char *out, size_t out_cap)
{
    size_t written = 0;
    if (m_armed) {
        const size_t count = std::min(m_matched, out_cap);
        if (out && count != 0) {
            for (size_t i = 0; i < count; ++i) out[i] = m_payload[i];
            written = count;
        }
    }
    m_payload.clear();
    m_matched = 0;
    m_armed = false;
    return written;
}

bool cyberdeck_ssh_echo_guard::armed() const
{
    return m_armed;
}
