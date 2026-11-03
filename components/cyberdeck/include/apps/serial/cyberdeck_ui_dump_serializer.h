#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace cyberdeck_serial {
namespace ui_dump {

constexpr std::size_t k_max_text_bytes = 120;
constexpr std::size_t k_max_nodes = 64;
constexpr std::size_t k_max_nodes_bytes = 3500;

struct clamped_text {
    std::string value;
    bool truncated = false;
};

inline bool is_utf8_continuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

inline std::size_t valid_utf8_sequence_length(const std::string &value, std::size_t offset) {
    const auto byte = [&](std::size_t index) { return static_cast<unsigned char>(value[index]); };
    const unsigned char first = byte(offset);
    const std::size_t remaining = value.size() - offset;
    std::size_t length = 0;
    unsigned char second_min = 0x80;
    unsigned char second_max = 0xBF;
    if (first >= 0xC2 && first <= 0xDF)
        length = 2;
    else if (first == 0xE0) {
        length = 3;
        second_min = 0xA0;
    } else if (first >= 0xE1 && first <= 0xEC)
        length = 3;
    else if (first == 0xED) {
        length = 3;
        second_max = 0x9F;
    } else if (first >= 0xEE && first <= 0xEF)
        length = 3;
    else if (first == 0xF0) {
        length = 4;
        second_min = 0x90;
    } else if (first >= 0xF1 && first <= 0xF3)
        length = 4;
    else if (first == 0xF4) {
        length = 4;
        second_max = 0x8F;
    } else
        return 0;
    if (remaining < length || byte(offset + 1) < second_min || byte(offset + 1) > second_max)
        return 0;
    for (std::size_t index = 2; index < length; ++index) {
        if (!is_utf8_continuation(value[offset + index]))
            return 0;
    }
    return length;
}

inline clamped_text clamp_text(const char *text) {
    std::string value = text != nullptr ? text : "";
    if (value.size() <= k_max_text_bytes)
        return {std::move(value), false};
    std::size_t cut = k_max_text_bytes;
    for (std::size_t offset = 0; offset < cut;) {
        const unsigned char byte = static_cast<unsigned char>(value[offset]);
        if (byte < 0x80) {
            ++offset;
            continue;
        }
        const std::size_t sequence_length = valid_utf8_sequence_length(value, offset);
        if (sequence_length == 0) {
            ++offset;
            continue;
        }
        if (offset + sequence_length > cut) {
            cut = offset;
            break;
        }
        offset += sequence_length;
    }
    value.resize(cut);
    return {std::move(value), true};
}

inline void append_json_string(std::string &out, const std::string &value) {
    static constexpr char hex[] = "0123456789abcdef";
    out.push_back('"');
    for (std::size_t index = 0; index < value.size();) {
        const unsigned char c = static_cast<unsigned char>(value[index]);
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(static_cast<char>(c));
        } else if (c < 0x20 || c == 0x7f) {
            out += "\\u00";
            out.push_back(hex[(c >> 4) & 0xf]);
            out.push_back(hex[c & 0xf]);
        } else if (c >= 0x80) {
            const std::size_t sequence_length = valid_utf8_sequence_length(value, index);
            if (sequence_length == 0) {
                out += "\\u00";
                out.push_back(hex[(c >> 4) & 0xf]);
                out.push_back(hex[c & 0xf]);
            } else {
                out.append(value, index, sequence_length);
                index += sequence_length;
                continue;
            }
        } else {
            out.push_back(static_cast<char>(c));
        }
        ++index;
    }
    out.push_back('"');
}

inline std::string serialize_node(const char *class_name, const std::string &text, bool text_truncated, int x, int y,
                                  int width, int height) {
    std::string node = "{\"class\":";
    append_json_string(node, class_name != nullptr ? class_name : "other");
    node += ",\"text\":";
    append_json_string(node, text);
    if (text_truncated)
        node += ",\"text_truncated\":true";
    node += ",\"x\":" + std::to_string(x);
    node += ",\"y\":" + std::to_string(y);
    node += ",\"w\":" + std::to_string(width);
    node += ",\"h\":" + std::to_string(height) + "}";
    return node;
}

inline bool append_node(std::string &nodes, std::size_t count, const std::string &node, bool &truncated) {
    if (count >= k_max_nodes || nodes.size() + (count != 0 ? 1 : 0) + node.size() > k_max_nodes_bytes) {
        truncated = true;
        return false;
    }
    if (count != 0)
        nodes.push_back(',');
    nodes += node;
    return true;
}

inline std::string serialize_result(const std::string &nodes, bool truncated) {
    return "{\"nodes\":[" + nodes + "],\"truncated\":" + (truncated ? "true}" : "false}");
}

} // namespace ui_dump
} // namespace cyberdeck_serial
