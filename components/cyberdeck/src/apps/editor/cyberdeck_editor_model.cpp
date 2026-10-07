#include "apps/editor/cyberdeck_editor_model.h"

#include <algorithm>
#include <new>

namespace cyberdeck_editor {
namespace {
bool valid_utf8(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t n =
            c < 0x80 ? 1
                     : (c >= 0xC2 && c <= 0xDF ? 2 : (c >= 0xE0 && c <= 0xEF ? 3 : (c >= 0xF0 && c <= 0xF4 ? 4 : 0)));
        if (!n || i + n > s.size())
            return false;
        for (std::size_t j = 1; j < n; ++j)
            if ((static_cast<unsigned char>(s[i + j]) & 0xC0) != 0x80)
                return false;
        if ((n == 3 && ((c == 0xE0 && static_cast<unsigned char>(s[i + 1]) < 0xA0) ||
                        (c == 0xED && static_cast<unsigned char>(s[i + 1]) >= 0xA0))) ||
            (n == 4 && ((c == 0xF0 && static_cast<unsigned char>(s[i + 1]) < 0x90) ||
                        (c == 0xF4 && static_cast<unsigned char>(s[i + 1]) > 0x8F))))
            return false;
        i += n;
    }
    return true;
}
void append_utf8(std::string &out, std::uint32_t cp) {
    if (cp <= 0x7f)
        out += char(cp);
    else if (cp <= 0x7ff) {
        out += char(0xc0 | (cp >> 6));
        out += char(0x80 | (cp & 63));
    } else if (cp <= 0xffff) {
        out += char(0xe0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 63));
        out += char(0x80 | (cp & 63));
    } else {
        out += char(0xf0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 63));
        out += char(0x80 | ((cp >> 6) & 63));
        out += char(0x80 | (cp & 63));
    }
}
std::uint32_t cp1252(unsigned char c) {
    static constexpr std::uint16_t x[] = {0x20ac, 0,      0x201a, 0x192,  0x201e, 0x2026, 0x2020, 0x2021,
                                          0x2c6,  0x2030, 0x160,  0x2039, 0x152,  0,      0x17d,  0,
                                          0,      0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
                                          0x2dc,  0x2122, 0x161,  0x203a, 0x153,  0,      0x17e,  0x178};
    return c >= 0x80 && c <= 0x9f ? (x[c - 0x80] ? x[c - 0x80] : 0xffffffffu) : c;
}
bool append_cp1252(std::string &out, std::uint32_t cp) {
    if (cp <= 0x7f || (cp >= 0xa0 && cp <= 0xff)) {
        out.push_back(static_cast<char>(cp));
        return true;
    }
    static constexpr unsigned char codes[] = {0x80, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
                                              0x8a, 0x8b, 0x8c, 0x8e, 0x91, 0x92, 0x93, 0x94, 0x95,
                                              0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9e, 0x9f};
    static constexpr std::uint32_t values[] = {0x20ac, 0x201a, 0x192, 0x201e, 0x2026, 0x2020, 0x2021, 0x2c6,  0x2030,
                                               0x160,  0x2039, 0x152, 0x17d,  0x2018, 0x2019, 0x201c, 0x201d, 0x2022,
                                               0x2013, 0x2014, 0x2dc, 0x2122, 0x161,  0x203a, 0x153,  0x17e,  0x178};
    for (std::size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        if (values[i] == cp) {
            out.push_back(static_cast<char>(codes[i]));
            return true;
        }
    return false;
}
bool next_utf8(std::string_view s, std::size_t &pos, std::uint32_t &cp) {
    if (pos >= s.size())
        return false;
    const unsigned char c = static_cast<unsigned char>(s[pos]);
    const std::size_t n = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
    cp = c & (n == 1 ? 0x7f : n == 2 ? 0x1f : n == 3 ? 0x0f : 0x07);
    for (std::size_t i = 1; i < n; ++i)
        cp = (cp << 6) | (static_cast<unsigned char>(s[pos + i]) & 0x3f);
    pos += n;
    return true;
}

std::size_t previous_codepoint_bounded(std::string_view s, std::size_t pos, std::size_t lower) {
    pos = std::min(pos, s.size());
    lower = std::min(lower, pos);
    if (pos == lower)
        return lower;
    --pos;
    while (pos > lower && (static_cast<unsigned char>(s[pos]) & 0xc0) == 0x80)
        --pos;
    return pos;
}

std::size_t next_codepoint_bounded(std::string_view s, std::size_t pos, std::size_t upper) {
    pos = std::min(pos, s.size());
    upper = std::min(upper, s.size());
    if (pos >= upper)
        return upper;
    ++pos;
    while (pos < upper && (static_cast<unsigned char>(s[pos]) & 0xc0) == 0x80)
        ++pos;
    return pos;
}
} // namespace

load_result decode(std::string_view b) {
    load_result r;
    r.info = {};
    if (b.size() > k_max_document_bytes) {
        r.status = load_status::too_large;
        return r;
    }
    std::size_t p = 0;
    bool utf8_bom = false;
    if (b.size() >= 3 && b.substr(0, 3) == "\xef\xbb\xbf") {
        r.info.codec = encoding::utf8;
        r.info.bom = true;
        utf8_bom = true;
        p = 3;
    } else if (b.size() >= 2 && static_cast<unsigned char>(b[0]) == 0xff && static_cast<unsigned char>(b[1]) == 0xfe) {
        r.info.codec = encoding::utf16_le;
        r.info.bom = true;
        p = 2;
    } else if (b.size() >= 2 && static_cast<unsigned char>(b[0]) == 0xfe && static_cast<unsigned char>(b[1]) == 0xff) {
        r.info.codec = encoding::utf16_be;
        r.info.bom = true;
        p = 2;
    }
    if (b.find('\0', p) != std::string_view::npos && r.info.codec == encoding::utf8) {
        r.status = load_status::binary;
        return r;
    }
    if (r.info.codec == encoding::utf8) {
        if (!valid_utf8(b.substr(p))) {
            // A byte sequence that is not UTF-8 is accepted only as the
            // explicitly supported single-byte legacy encoding. NUL and the
            // undefined C1 slots remain binary/invalid rather than guessed.
            if (utf8_bom) {
                r.status = load_status::invalid_encoding;
                return r;
            }
            r.info.codec = encoding::windows_1252;
            for (p = 0; p < b.size(); ++p) {
                auto cp = cp1252(static_cast<unsigned char>(b[p]));
                if (cp == 0xffffffffu || cp == 0) {
                    r.status = load_status::invalid_encoding;
                    return r;
                }
                append_utf8(r.text, cp);
            }
        } else
            r.text.assign(b.substr(p));
    } else if (r.info.codec == encoding::windows_1252) {
        for (; p < b.size(); ++p) {
            auto cp = cp1252(static_cast<unsigned char>(b[p]));
            if (cp == 0xffffffffu || cp == 0) {
                r.status = load_status::invalid_encoding;
                return r;
            }
            append_utf8(r.text, cp);
        }
    } else {
        if ((b.size() - p) % 2) {
            r.status = load_status::invalid_encoding;
            return r;
        }
        for (; p < b.size(); p += 2) {
            const std::uint16_t u = r.info.codec == encoding::utf16_le
                                        ? (unsigned char)b[p] | ((unsigned char)b[p + 1] << 8)
                                        : ((unsigned char)b[p + 1] | ((unsigned char)b[p] << 8));
            if (u == 0) {
                r.status = load_status::binary;
                return r;
            }
            std::uint32_t cp = u;
            if (u >= 0xd800 && u <= 0xdbff) {
                if (p + 3 >= b.size()) {
                    r.status = load_status::invalid_encoding;
                    return r;
                }
                const std::uint16_t low = r.info.codec == encoding::utf16_le
                                              ? (unsigned char)b[p + 2] | ((unsigned char)b[p + 3] << 8)
                                              : (unsigned char)b[p + 3] | ((unsigned char)b[p + 2] << 8);
                if (low < 0xdc00 || low > 0xdfff) {
                    r.status = load_status::invalid_encoding;
                    return r;
                }
                cp = 0x10000u + ((u - 0xd800u) << 10) + (low - 0xdc00u);
                p += 2;
            } else if (u >= 0xdc00 && u <= 0xdfff) {
                r.status = load_status::invalid_encoding;
                return r;
            }
            append_utf8(r.text, cp);
        }
    }
    for (std::size_t i = 0; i < r.text.size(); ++i) {
        if (r.text[i] == '\r') {
            r.info.eol = i + 1 < r.text.size() && r.text[i + 1] == '\n' ? line_ending::crlf : line_ending::cr;
            break;
        }
        if (r.text[i] == '\n') {
            r.info.eol = line_ending::lf;
            break;
        }
    }
    r.status = load_status::ok;
    return r;
}

bool encode(std::string_view text, document_info info, std::string &out) {
    if (!valid_utf8(text))
        return false;
    std::string normalized;
    normalized.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
            normalized += info.eol == line_ending::cr ? "\r" : "\n";
        } else if (text[i] == '\n')
            normalized += info.eol == line_ending::crlf ? "\r\n" : (info.eol == line_ending::cr ? "\r" : "\n");
        else
            normalized += text[i];
    }
    out.clear();
    if (info.codec == encoding::utf8) {
        if (info.bom)
            out = "\xef\xbb\xbf";
        out += normalized;
    } else {
        if (info.bom && info.codec != encoding::windows_1252)
            out += info.codec == encoding::utf16_le ? "\xff\xfe" : "\xfe\xff";
        for (std::size_t p = 0; p < normalized.size();) {
            std::uint32_t cp = 0;
            if (!next_utf8(normalized, p, cp))
                return false;
            if (info.codec == encoding::windows_1252) {
                if (!append_cp1252(out, cp))
                    return false;
            } else {
                auto put = [&](std::uint16_t u) {
                    out.push_back(static_cast<char>(info.codec == encoding::utf16_le ? u : u >> 8));
                    out.push_back(static_cast<char>(info.codec == encoding::utf16_le ? u >> 8 : u));
                };
                if (cp > 0xffff) {
                    const auto v = cp - 0x10000;
                    put(static_cast<std::uint16_t>(0xd800 + (v >> 10)));
                    put(static_cast<std::uint16_t>(0xdc00 + (v & 0x3ff)));
                } else
                    put(static_cast<std::uint16_t>(cp));
            }
        }
    }
    return out.size() <= k_max_document_bytes;
}

bool model::open(std::string_view bytes) {
    auto r = decode(bytes);
    if (r.status != load_status::ok || r.text.size() > k_max_document_bytes)
        return false;
    text_ = std::move(r.text);
    info_ = r.info;
    cursor_ = selection_start_ = selection_end_ = 0;
    undo_ = {};
    redo_ = {};
    undo_count_ = redo_count_ = 0;
    dirty_ = false;
    save_as_confirmation_ = false;
    return true;
}
std::size_t model::previous_codepoint(std::string_view s, std::size_t p) {
    return previous_codepoint_bounded(s, p, 0);
}
std::size_t model::next_codepoint(std::string_view s, std::size_t p) {
    return next_codepoint_bounded(s, p, s.size());
}
bool model::remember(std::array<std::unique_ptr<snapshot>, k_max_undo> &history, std::size_t &count) {
    auto value =
        std::unique_ptr<snapshot>(new (std::nothrow) snapshot{text_, cursor_, selection_start_, selection_end_});
    if (!value || value->text.size() > k_max_document_bytes)
        return false;
    auto history_bytes = [&]() {
        std::size_t bytes = 0;
        for (std::size_t i = 0; i < count; ++i)
            bytes += history[i] != nullptr ? history[i]->text.size() : 0;
        return bytes;
    };
    while (count != 0 && (count == k_max_undo || history_bytes() + value->text.size() > k_max_history_bytes)) {
        history[0].reset();
        for (std::size_t i = 1; i < k_max_undo; ++i)
            history[i - 1] = std::move(history[i]);
        --count;
    }
    history[count++] = std::move(value);
    return true;
}

bool model::mutate(std::string_view repl, std::size_t b, std::size_t e) {
    if (b > e || e > text_.size() || text_.size() - (e - b) + repl.size() > k_max_document_bytes || !valid_utf8(repl) ||
        !remember(undo_, undo_count_))
        return false;
    redo_ = {};
    redo_count_ = 0;
    text_.replace(b, e - b, repl);
    cursor_ = b + repl.size();
    selection_start_ = selection_end_ = cursor_;
    dirty_ = true;
    return true;
}
bool model::insert(std::string_view s) {
    return mutate(s, selection_start_, selection_end_);
}
bool model::replace_selection(std::string_view s) {
    return insert(s);
}
bool model::handle(key k, std::string_view c) {
    switch (k) {
    case key::left:
        cursor_ = previous_codepoint(text_, cursor_);
        break;
    case key::right:
        cursor_ = next_codepoint(text_, cursor_);
        break;
    case key::up:
        return move_vertical(-1);
    case key::down:
        return move_vertical(1);
    case key::backspace:
        if (cursor_)
            return mutate({}, previous_codepoint(text_, cursor_), cursor_);
        break;
    case key::del:
        if (cursor_ < text_.size())
            return mutate({}, cursor_, next_codepoint(text_, cursor_));
        break;
    case key::enter:
        return insert("\n");
    case key::undo:
        return undo();
    case key::redo:
        return redo();
    case key::character:
        return insert(c);
    default:
        break;
    }
    selection_start_ = selection_end_ = cursor_;
    return true;
}
bool model::find_next(std::string_view n) {
    if (n.empty())
        return false;
    auto p = text_.find(n, cursor_);
    if (p == std::string::npos)
        p = text_.find(n);
    if (p == std::string::npos)
        return false;
    selection_start_ = p;
    selection_end_ = p + n.size();
    cursor_ = selection_end_;
    return true;
}
bool model::move_vertical(int lines) {
    if (lines == 0)
        return true;
    const auto line_start = [this](std::size_t position) {
        while (position && text_[position - 1] != '\n')
            position = previous_codepoint_bounded(text_, position, 0);
        return position;
    };
    const auto column_at = [this](std::size_t start, std::size_t position) {
        std::size_t column = 0;
        for (std::size_t p = start; p < position; ++column)
            p = next_codepoint_bounded(text_, p, position);
        return column;
    };
    const auto advance = [this](std::size_t start, std::size_t end, std::size_t column) {
        while (column-- && start < end)
            start = next_codepoint_bounded(text_, start, end);
        return start;
    };

    const std::size_t column = column_at(line_start(cursor_), cursor_);
    while (lines < 0) {
        const std::size_t current_start = line_start(cursor_);
        if (!current_start)
            break;
        const std::size_t previous_end = current_start - 1;
        cursor_ = advance(line_start(previous_end), previous_end, column);
        ++lines;
    }
    while (lines > 0) {
        const std::size_t current_end = text_.find('\n', cursor_);
        if (current_end == std::string::npos)
            break;
        const std::size_t next_start = current_end + 1;
        const std::size_t next_end = text_.find('\n', next_start);
        cursor_ = advance(next_start, next_end == std::string::npos ? text_.size() : next_end, column);
        --lines;
    }
    selection_start_ = selection_end_ = cursor_;
    return true;
}
bool model::gesture_scroll(int pixel_delta) {
    return move_vertical(pixel_delta < 0 ? 3 : pixel_delta > 0 ? -3 : 0);
}
bool model::undo() {
    if (!undo_count_ || !remember(redo_, redo_count_))
        return false;
    auto value = std::move(undo_[undo_count_ - 1]);
    --undo_count_;
    text_ = std::move(value->text);
    cursor_ = value->cursor;
    selection_start_ = value->start;
    selection_end_ = value->end;
    dirty_ = true;
    return true;
}

bool model::redo() {
    if (!redo_count_ || !remember(undo_, undo_count_))
        return false;
    auto value = std::move(redo_[redo_count_ - 1]);
    --redo_count_;
    text_ = std::move(value->text);
    cursor_ = value->cursor;
    selection_start_ = value->start;
    selection_end_ = value->end;
    dirty_ = true;
    return true;
}
bool model::save(std::string &b) const {
    return encode(text_, info_, b);
}
} // namespace cyberdeck_editor
