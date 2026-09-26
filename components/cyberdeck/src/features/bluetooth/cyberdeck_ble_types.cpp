#include "features/bluetooth/cyberdeck_ble_types.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace cyberdeck_ble {

device_kind kind_from_appearance(std::uint16_t appearance)
{
    switch (appearance) {
    case k_appearance_keyboard:
        return device_kind::keyboard;
    case k_appearance_mouse:
        return device_kind::mouse;
    case k_appearance_wearable_headset:
    case k_appearance_headset_mic:
    case k_appearance_headphones:
    case k_appearance_ear_headset:
    case k_appearance_handsfree:
    case k_appearance_handsfree_mic:
        return device_kind::headset;
    default:
        return device_kind::unknown;
    }
}

const char *kind_label(device_kind kind)
{
    switch (kind) {
    case device_kind::keyboard:
        return "Keyboard";
    case device_kind::headset:
        return "Headset";
    case device_kind::mouse:
        return "Mouse";
    default:
        return "Unknown";
    }
}

bool normalize_address(const char *raw, std::size_t len, std::string &out)
{
    if (raw == nullptr || len == 0) {
        return false;
    }

    // Must be exactly 17 chars with colons: "AA:BB:CC:DD:EE:FF"
    // or 12 bare hex digits: "AABBCCDDEEFF"
    // or 17 chars with dashes: "AA-BB-CC-DD-EE-FF"
    // No spaces, no tabs, no leading/trailing separators, no double separators.

    std::string cleaned;
    cleaned.reserve(len);

    char last_sep = 0;
    bool first_byte = true;

    for (std::size_t i = 0; i < len; ++i) {
        char c = raw[i];
        if (c == ':' || c == '-') {
            if (first_byte) {
                return false; // leading separator
            }
            if (last_sep) {
                return false; // double separator
            }
            last_sep = c;
            continue;
        }
        if (c == ' ' || c == '\t') {
            return false; // spaces/tabs not allowed
        }
        cleaned.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        first_byte = false;
        last_sep = 0;
    }

    if (last_sep) {
        return false; // trailing separator
    }

    if (cleaned.size() != 12) {
        return false;
    }

    for (char c : cleaned) {
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }

    std::string formatted;
    formatted.reserve(17);
    for (std::size_t i = 0; i < 12; i += 2) {
        if (i != 0) {
            formatted.push_back(':');
        }
        formatted.push_back(cleaned[i]);
        formatted.push_back(cleaned[i + 1]);
    }

    out = std::move(formatted);
    return true;
}

static bool is_control_char(char32_t cp)
{
    return (cp <= 0x1F) || (cp == 0x7F) || (cp >= 0x80 && cp <= 0x9F) ||
           (cp == 0x2028) || (cp == 0x2029);
}

std::string sanitize_name(const char *raw, std::size_t len)
{
    if (raw == nullptr || len == 0) {
        return k_unnamed_placeholder;
    }

    std::string out;
    out.reserve(std::min(len, k_max_name_bytes) + 1);

    std::size_t i = 0;
    bool seen_non_space = false;  // Track if we've seen a non-space character (original non-space or replacement)

    while (i < len) {
        unsigned char first = static_cast<unsigned char>(raw[i]);
        char32_t cp = 0;
        std::size_t seq_len = 0;
        bool is_ascii_space = false;

        if (first < 0x80) {
            cp = first;
            seq_len = 1;
            is_ascii_space = (cp == 0x20);
        } else if ((first & 0xE0) == 0xC0) {
            if (i + 1 >= len) {
                if (!seen_non_space) {
                    // Leading incomplete sequence -> skip
                } else {
                    out.push_back(' ');
                }
                break;
            }
            unsigned char second = static_cast<unsigned char>(raw[i + 1]);
            if ((second & 0xC0) != 0x80) {
                if (!seen_non_space) {
                    // Leading invalid sequence -> skip
                } else {
                    out.push_back(' ');
                }
                i += 1;
                continue;
            }
            cp = ((first & 0x1F) << 6) | (second & 0x3F);
            if (cp < 0x80) {
                if (!seen_non_space) {
                    // Overlong encoding -> skip
                } else {
                    out.push_back(' ');
                }
                i += 2;
                continue;
            }
            seq_len = 2;
        } else if ((first & 0xF0) == 0xE0) {
            if (i + 2 >= len) {
                if (!seen_non_space) {
                    // Leading incomplete sequence -> skip
                } else {
                    out.push_back(' ');
                }
                break;
            }
            unsigned char second = static_cast<unsigned char>(raw[i + 1]);
            unsigned char third = static_cast<unsigned char>(raw[i + 2]);
            if ((second & 0xC0) != 0x80 || (third & 0xC0) != 0x80) {
                if (!seen_non_space) {
                    // Leading invalid sequence -> skip
                } else {
                    out.push_back(' ');
                }
                i += 1;
                continue;
            }
            cp = ((first & 0x0F) << 12) | ((second & 0x3F) << 6) | (third & 0x3F);
            if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) {
                if (!seen_non_space) {
                    // Overlong or surrogate -> skip
                } else {
                    out.push_back(' ');
                }
                i += 3;
                continue;
            }
            seq_len = 3;
        } else if ((first & 0xF8) == 0xF0) {
            if (i + 3 >= len) {
                if (!seen_non_space) {
                    // Leading incomplete sequence -> skip
                } else {
                    out.push_back(' ');
                }
                break;
            }
            unsigned char second = static_cast<unsigned char>(raw[i + 1]);
            unsigned char third = static_cast<unsigned char>(raw[i + 2]);
            unsigned char fourth = static_cast<unsigned char>(raw[i + 3]);
            if ((second & 0xC0) != 0x80 || (third & 0xC0) != 0x80 || (fourth & 0xC0) != 0x80) {
                if (!seen_non_space) {
                    // Leading invalid sequence -> skip
                } else {
                    out.push_back(' ');
                }
                i += 1;
                continue;
            }
            cp = ((first & 0x07) << 18) | ((second & 0x3F) << 12) | ((third & 0x3F) << 6) | (fourth & 0x3F);
            if (cp < 0x10000 || cp > 0x10FFFF) {
                if (!seen_non_space) {
                    // Out of range -> skip
                } else {
                    out.push_back(' ');
                }
                i += 4;
                continue;
            }
            seq_len = 4;
        } else {
            if (!seen_non_space) {
                // Leading invalid byte -> skip
            } else {
                out.push_back(' ');
            }
            ++i;
            continue;
        }

        // Skip leading ASCII spaces (0x20) from the input
        if (!seen_non_space && is_ascii_space) {
            i += seq_len;
            continue;
        }

        if (is_control_char(cp)) {
            if (out.size() < k_max_name_bytes) {
                out.push_back(' ');
                seen_non_space = true;
            }
        } else {
            if (out.size() + seq_len > k_max_name_bytes) {
                break;
            }
            out.append(raw + i, seq_len);
            seen_non_space = true;
        }
        i += seq_len;
    }

    // Trim trailing ASCII spaces
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }

    if (out.empty()) {
        return k_unnamed_placeholder;
    }
    return out;
}

std::string display_name(const device &item)
{
    if (item.name.empty()) {
        return k_unnamed_placeholder;
    }
    return item.name;
}

int clamp_rssi(int raw)
{
    if (raw < k_min_rssi) return k_min_rssi;
    if (raw > k_max_rssi) return k_max_rssi;
    return raw;
}

bool parse_passkey(const char *text, std::size_t len, std::uint32_t &out)
{
    if (text == nullptr || len == 0) {
        return false;
    }

    const char *start = text;
    const char *end = text + len;

    while (start < end && (*start == ' ' || *start == '\t')) {
        ++start;
    }
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
        --end;
    }

    std::size_t trimmed_len = static_cast<std::size_t>(end - start);
    if (trimmed_len != k_passkey_digits) {
        return false;
    }

    std::uint32_t value = 0;
    for (std::size_t i = 0; i < trimmed_len; ++i) {
        char c = start[i];
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + static_cast<std::uint32_t>(c - '0');
    }

    if (value >= k_passkey_modulus) {
        return false;
    }

    out = value;
    return true;
}

std::string format_passkey(std::uint32_t passkey)
{
    if (passkey >= k_passkey_modulus) {
        return "";
    }
    char buf[7];
    std::snprintf(buf, sizeof(buf), "%06u", static_cast<unsigned>(passkey));
    return std::string(buf);
}

std::string mask_passkey(std::uint32_t /*passkey*/)
{
    return "******";
}

namespace {
struct device_entry {
    device item;
    std::size_t insert_order = 0;
};
}

class device_list::Impl {
public:
    std::vector<device_entry> items;
    std::size_t selection = 0;
    std::size_t next_insert_order = 0;

    void clamp_selection()
    {
        if (items.empty()) {
            selection = 0;
        } else if (selection >= items.size()) {
            selection = items.size() - 1;
        }
    }
};

device_list::device_list() : pimpl_(new Impl()) {}
device_list::~device_list() { delete pimpl_; }

void device_list::clear()
{
    pimpl_->items.clear();
    pimpl_->selection = 0;
    pimpl_->next_insert_order = 0;
}

bool device_list::add(const device &item)
{
    if (item.address.empty()) {
        return false;
    }
    if (item.address.size() > k_max_address_bytes) {
        return false;
    }

    // Validate address format using normalize_address
    std::string normalized;
    if (!normalize_address(item.address.c_str(), item.address.size(), normalized)) {
        return false;
    }

    for (auto &entry : pimpl_->items) {
        if (entry.item.address == item.address) {
            entry.item.rssi = std::max(entry.item.rssi, item.rssi);
            if (entry.item.name.empty() && !item.name.empty()) {
                entry.item.name = item.name;
            }
            if (entry.item.kind == device_kind::unknown && item.kind != device_kind::unknown) {
                entry.item.kind = item.kind;
            }
            if (item.paired) {
                entry.item.paired = true;
            }
            if (!item.connectable) {
                entry.item.connectable = false;
            }
            return true;
        }
    }

    if (pimpl_->items.size() >= k_max_devices) {
        return false;
    }

    device_entry new_entry;
    new_entry.item = item;
    new_entry.insert_order = pimpl_->next_insert_order++;
    pimpl_->items.push_back(std::move(new_entry));
    pimpl_->clamp_selection();
    return true;
}

std::size_t device_list::size() const
{
    return pimpl_->items.size();
}

std::size_t device_list::capacity() const
{
    return k_max_devices;
}

const device *device_list::at(std::size_t index) const
{
    if (index >= pimpl_->items.size()) {
        return nullptr;
    }
    return &pimpl_->items[index].item;
}

const device *device_list::find(const std::string &address) const
{
    for (const auto &entry : pimpl_->items) {
        if (entry.item.address == address) {
            return &entry.item;
        }
    }
    return nullptr;
}

bool device_list::remove(const std::string &address)
{
    for (auto it = pimpl_->items.begin(); it != pimpl_->items.end(); ++it) {
        if (it->item.address == address) {
            pimpl_->items.erase(it);
            pimpl_->clamp_selection();
            return true;
        }
    }
    return false;
}

std::size_t device_list::selected_index() const
{
    return pimpl_->selection;
}

void device_list::select(std::size_t index)
{
    if (index < pimpl_->items.size()) {
        pimpl_->selection = index;
    }
}

void device_list::move_up()
{
    if (pimpl_->selection > 0) {
        --pimpl_->selection;
    }
}

void device_list::move_down()
{
    if (!pimpl_->items.empty() && pimpl_->selection + 1 < pimpl_->items.size()) {
        ++pimpl_->selection;
    }
}

const device *device_list::selected() const
{
    if (pimpl_->items.empty()) {
        return nullptr;
    }
    if (pimpl_->selection >= pimpl_->items.size()) {
        return nullptr;
    }
    return &pimpl_->items[pimpl_->selection].item;
}

std::vector<device> device_list::snapshot() const
{
    std::vector<device> out;
    out.reserve(pimpl_->items.size());
    for (const auto &entry : pimpl_->items) {
        out.push_back(entry.item);
    }
    return out;
}

std::string device_list::render() const
{
    if (pimpl_->items.empty()) {
        return "No Bluetooth devices found.\n";
    }

    std::string out;
    out.reserve(pimpl_->items.size() * 64);
    out += "Found ";
    out += std::to_string(pimpl_->items.size());
    out += " Bluetooth devices (UP/DOWN navigate, ENTER pair, ESC cancel):\n";

    for (std::size_t i = 0; i < pimpl_->items.size(); ++i) {
        const device &d = pimpl_->items[i].item;
        out += (i == pimpl_->selection) ? "> [" : "  [";
        out += std::to_string(i + 1);
        out += "] ";
        out += display_name(d);
        out += " (";
        out += kind_label(d.kind);
        out += ", ";
        out += std::to_string(clamp_rssi(d.rssi));
        out += " dBm)\n";
    }
    return out;
}

std::size_t device_list::max_devices()
{
    return k_max_devices;
}

} // namespace cyberdeck_ble