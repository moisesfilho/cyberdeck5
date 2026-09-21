#include "cyberdeck_terminal_filter.h"

cyberdeck_terminal_filter::cyberdeck_terminal_filter() = default;

size_t cyberdeck_terminal_filter::feed(const char *data, size_t len, char *out, size_t out_cap)
{
    if (!data || len == 0) return 0;

    size_t written = 0;
    const auto emit = [&](char c) {
        if (out && written < out_cap) {
            out[written++] = c;
        }
    };

    size_t pos = 0;
    while (pos < len) {
        const unsigned char c = static_cast<unsigned char>(data[pos]);

        /* CR is resolved before interpreting the next byte.  LF consumes the
         * pending CR; every other byte is reprocessed normally afterwards. */
        if (m_pending_cr) {
            m_pending_cr = false;
            emit('\n');
            if (c == '\n') {
                ++pos;
                continue;
            }
        }

        bool consumed = true;
        switch (m_seq) {
        case SEQ_GROUND:
            if (c == '\r') {
                m_pending_cr = true;
            } else if (c == 0x1B) {
                m_seq = SEQ_ESC;
            } else if (c == '\n' || c == '\t' || (c >= 0x20 && c <= 0x7E) || c >= 0x80) {
                emit(static_cast<char>(c));
            }
            break;

        case SEQ_ESC:
            if (c == '[') {
                m_seq = SEQ_CSI;
            } else if (c == ']') {
                m_seq = SEQ_OSC;
            } else if (c >= 0x20 && c <= 0x2F) {
                m_seq = SEQ_ESC_INT;
            } else if (c >= 0x30 && c <= 0x7E) {
                m_seq = SEQ_GROUND;
            } else {
                m_seq = SEQ_GROUND;
                consumed = false;
            }
            break;

        case SEQ_ESC_INT:
            if (c >= 0x20 && c <= 0x2F) {
                /* Continue consuming intermediates. */
            } else if (c >= 0x30 && c <= 0x7E) {
                m_seq = SEQ_GROUND;
            } else {
                m_seq = SEQ_GROUND;
                consumed = false;
            }
            break;

        case SEQ_CSI:
            if (c >= 0x40 && c <= 0x7E) {
                m_seq = SEQ_GROUND;
            } else if (c < 0x20 || c == 0x7F || c >= 0x80) {
                m_seq = SEQ_GROUND;
                consumed = false;
            }
            break;

        case SEQ_OSC:
            if (c == 0x07) {
                m_seq = SEQ_GROUND;
            } else if (c == 0x1B) {
                m_seq = SEQ_OSC_ST;
            }
            break;

        case SEQ_OSC_ST:
            if (c == '\\') {
                m_seq = SEQ_GROUND;
            } else {
                m_seq = SEQ_GROUND;
                consumed = false;
            }
            break;
        }

        if (consumed) ++pos;
    }
    return written;
}

size_t cyberdeck_terminal_filter::flush(char *out, size_t out_cap)
{
    const bool emit_lf = m_pending_cr;
    m_pending_cr = false;
    m_seq = SEQ_GROUND;
    if (emit_lf && out && out_cap) {
        out[0] = '\n';
        return 1;
    }
    return 0;
}
