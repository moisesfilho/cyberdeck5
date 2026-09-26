#include "features/bluetooth/cyberdeck_ble_store.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

namespace cyberdeck_ble {

static bool normalize_record_address(const std::string &raw, std::string &out)
{
    return normalize_address(raw.c_str(), raw.size(), out);
}

struct bond_store::Impl {
    std::vector<bond_record> records;
};

bond_store::bond_store() : pimpl_(new Impl()) {}
bond_store::~bond_store() { delete pimpl_; }

store_result bond_store::add(const bond_record &record)
{
    bond_record normalized = record;
    if (!normalize_record_address(record.address, normalized.address)) {
        return store_result::invalid;
    }
    for (const auto &r : pimpl_->records) {
        if (r.address == normalized.address) {
            return store_result::duplicate;
        }
    }
    if (pimpl_->records.size() >= k_max_bonds) {
        return store_result::full;
    }
    pimpl_->records.push_back(std::move(normalized));
    return store_result::ok;
}

store_result bond_store::update(const bond_record &record)
{
    bond_record normalized = record;
    if (!normalize_record_address(record.address, normalized.address)) {
        return store_result::invalid;
    }
    for (auto &r : pimpl_->records) {
        if (r.address == normalized.address) {
            r = std::move(normalized);
            return store_result::ok;
        }
    }
    return store_result::not_found;
}

bool bond_store::remove(const std::string &address)
{
    std::string normalized;
    if (!normalize_record_address(address, normalized)) {
        return false;
    }
    for (auto it = pimpl_->records.begin(); it != pimpl_->records.end(); ++it) {
        if (it->address == normalized) {
            pimpl_->records.erase(it);
            return true;
        }
    }
    return false;
}

const bond_record *bond_store::find(const std::string &address) const
{
    std::string normalized;
    if (!normalize_record_address(address, normalized)) {
        return nullptr;
    }
    for (const auto &r : pimpl_->records) {
        if (r.address == normalized) {
            return &r;
        }
    }
    return nullptr;
}

std::size_t bond_store::size() const
{
    return pimpl_->records.size();
}

std::size_t bond_store::capacity() const
{
    return k_max_bonds;
}

std::vector<bond_record> bond_store::snapshot() const
{
    return pimpl_->records;
}

std::string bond_store::serialize() const
{
    std::string out;
    out.reserve(pimpl_->records.size() * 80);
    for (const auto &r : pimpl_->records) {
        out += encode_bond(r);
    }
    if (out.size() > k_max_store_bytes) {
        out.resize(k_max_store_bytes);
    }
    return out;
}

bool bond_store::deserialize(const char *text, std::size_t len)
{
    if (text == nullptr || len == 0) {
        pimpl_->records.clear();
        return true;
    }
    if (len > k_max_store_bytes) {
        return false;
    }

    std::vector<bond_record> parsed;
    parsed.reserve(k_max_bonds);
    std::size_t pos = 0;

    while (pos < len && parsed.size() < k_max_bonds) {
        const char *line_start = text + pos;
        const char *line_end = static_cast<const char *>(memchr(line_start, '\n', len - pos));
        std::size_t line_len = line_end ? static_cast<std::size_t>(line_end - line_start) : len - pos;

        if (line_len == 0) {
            pos += line_end ? 1 : 0;
            continue;
        }

        bond_record record;
        if (!decode_bond(line_start, line_len, record)) {
            return false;
        }
        for (const auto &existing : parsed) {
            if (existing.address == record.address) {
                return false;
            }
        }
        parsed.push_back(record);

        if (line_end) {
            pos = static_cast<std::size_t>(line_end - text) + 1;
        } else {
            pos = len;
        }
    }

    if (pos < len) {
        const char *remaining = text + pos;
        std::size_t remaining_len = len - pos;
        for (std::size_t i = 0; i < remaining_len; ++i) {
            if (remaining[i] != '\n' && remaining[i] != '\r') {
                return false;
            }
        }
    }

    pimpl_->records = std::move(parsed);
    return true;
}

void bond_store::clear()
{
    pimpl_->records.clear();
}

static std::string sanitize_for_encode(const char *raw, std::size_t len)
{
    if (raw == nullptr || len == 0) {
        return "";
    }
    std::string out;
    out.reserve(std::min(len, k_max_name_bytes));
    for (std::size_t i = 0; i < len; ++i) {
        unsigned char c = static_cast<unsigned char>(raw[i]);
        if (c <= 0x1F || c == 0x7F || (c >= 0x80 && c <= 0x9F) || c == ';') {
            if (!out.empty() && out.back() != ' ') {
                out.push_back(' ');
            }
        } else {
            if (out.size() < k_max_name_bytes) {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    while (!out.empty() && out.front() == ' ') {
        out.erase(0, 1);
    }
    return out;
}

static std::string kind_to_token(device_kind kind)
{
    switch (kind) {
    case device_kind::keyboard: return "keyboard";
    case device_kind::headset: return "headset";
    case device_kind::mouse: return "mouse";
    default: return "unknown";
    }
}

std::string encode_bond(const bond_record &record)
{
    std::string address;
    if (!normalize_record_address(record.address, address)) {
        return "";
    }

    std::string name = sanitize_for_encode(record.name.c_str(), record.name.size());
    std::string kind = kind_to_token(record.kind);

    char buf[128];
    int written = std::snprintf(buf, sizeof(buf), "CDB1;addr=%s;name=%s;kind=%s;last=%d\n",
                                address.c_str(), name.c_str(), kind.c_str(),
                                record.last_connected ? 1 : 0);
    if (written <= 0 || static_cast<std::size_t>(written) > k_max_record_bytes) {
        return "";
    }
    return std::string(buf, written);
}

static bool parse_field(const char *start, const char *end, const char *prefix,
                        std::string &out)
{
    std::size_t prefix_len = std::strlen(prefix);
    if (static_cast<std::size_t>(end - start) < prefix_len) {
        return false;
    }
    if (std::strncmp(start, prefix, prefix_len) != 0) {
        return false;
    }
    out.assign(start + prefix_len, end - (start + prefix_len));
    return true;
}

bool decode_bond(const char *text, std::size_t len, bond_record &out)
{
    if (text == nullptr || len == 0) {
        return false;
    }

    if (len < 5 || std::strncmp(text, k_bond_magic, 4) != 0 || text[4] != ';') {
        return false;
    }

    const char *pos = text + 5;
    const char *end = text + len;
    if (end > pos && end[-1] == '\n') {
        --end;
    }
    std::string addr, name, kind_str, last_str;
    const char *prefixes[] = {"addr=", "name=", "kind=", "last="};
    std::string *values[] = {&addr, &name, &kind_str, &last_str};
    std::size_t field = 0;

    while (pos < end) {
        const char *field_end = static_cast<const char *>(memchr(pos, ';', end - pos));
        if (!field_end) {
            field_end = end;
        }

        if (field >= 4 || !parse_field(pos, field_end, prefixes[field], *values[field])) {
            return false;
        }
        ++field;

        if (field_end == end) {
            break;
        }
        pos = field_end + 1;
    }

    if (field != 4) {
        return false;
    }

    std::string normalized_address;
    if (!normalize_record_address(addr, normalized_address)) {
        return false;
    }

    if (name.size() > k_max_name_bytes) {
        return false;
    }
    if (sanitize_for_encode(name.c_str(), name.size()) != name) {
        return false;
    }

    device_kind kind = device_kind::unknown;
    if (kind_str == "keyboard") kind = device_kind::keyboard;
    else if (kind_str == "headset") kind = device_kind::headset;
    else if (kind_str == "mouse") kind = device_kind::mouse;
    else if (kind_str != "unknown") {
        return false;
    }

    bool last = false;
    if (last_str == "1") last = true;
    else if (last_str != "0") return false;

    bond_record decoded;
    decoded.address = std::move(normalized_address);
    decoded.name = std::move(name);
    decoded.kind = kind;
    decoded.last_connected = last;
    out = std::move(decoded);
    return true;
}

} // namespace cyberdeck_ble
