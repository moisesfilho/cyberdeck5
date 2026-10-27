#include "apps/music/cyberdeck_music_model.h"

#include <algorithm>
#include <cstring>

namespace cyberdeck_music {
namespace {
bool safe_path(std::string_view path) {
    if (path.size() < 5 || path.size() > k_max_path_bytes || path.front() != '/' || path.find("//") != path.npos)
        return false;
    std::size_t begin = 1;
    while (begin <= path.size()) {
        const std::size_t end = path.find('/', begin);
        const auto part = path.substr(begin, end == path.npos ? path.size() - begin : end - begin);
        if (part == "." || part == ".." || part == "sdcard")
            return false;
        if (end == path.npos)
            break;
        begin = end + 1;
    }
    const std::string_view ext = path.substr(path.size() - 4);
    return ext[0] == '.' && (ext[1] == 'm' || ext[1] == 'M') && (ext[2] == 'p' || ext[2] == 'P') && ext[3] == '3';
}

std::uint32_t frame_size(const unsigned char *p, std::size_t count, std::uint8_t version) {
    if (version == 4) {
        if (count < 4 || (p[0] | p[1] | p[2] | p[3]) & 0x80)
            return UINT32_MAX;
        return (std::uint32_t(p[0]) << 21) | (std::uint32_t(p[1]) << 14) | (std::uint32_t(p[2]) << 7) | p[3];
    }
    if (count < 4)
        return UINT32_MAX;
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

void copy_text(std::array<char, k_max_title_bytes + 1> &dst, std::array<char, k_max_artist_bytes + 1> *artist,
               const unsigned char *data, std::size_t length) {
    if (length < 1)
        return;
    const unsigned encoding = data[0];
    std::size_t out = 0;
    const std::size_t max = artist == nullptr ? k_max_title_bytes : k_max_artist_bytes;
    auto *target = artist == nullptr ? dst.data() : artist->data();
    if (encoding == 0) {
        for (std::size_t i = 1; i < length && out < max; ++i) {
            const unsigned char c = data[i];
            if (c == 0)
                break;
            target[out++] = c < 0x20 ? ' ' : c < 0x7f ? static_cast<char>(c) : '?';
        }
    } else if (encoding == 3) {
        for (std::size_t i = 1; i < length && out < max;) {
            const unsigned char lead = data[i];
            if (lead == 0)
                break;
            if (lead < 0x80) {
                target[out++] = lead < 0x20 ? ' ' : static_cast<char>(lead);
                ++i;
                continue;
            }
            const std::size_t bytes = lead >= 0xc2 && lead <= 0xdf   ? 2
                                      : lead >= 0xe0 && lead <= 0xef ? 3
                                      : lead >= 0xf0 && lead <= 0xf4 ? 4
                                                                     : 0;
            if (bytes == 0 || i + bytes > length || out + bytes > max)
                break;
            bool valid = true;
            for (std::size_t j = 1; j < bytes; ++j)
                valid = valid && (data[i + j] & 0xc0) == 0x80;
            if (bytes == 3 && lead == 0xe0)
                valid = valid && data[i + 1] >= 0xa0;
            if (bytes == 3 && lead == 0xed)
                valid = valid && data[i + 1] < 0xa0;
            if (bytes == 4 && lead == 0xf0)
                valid = valid && data[i + 1] >= 0x90;
            if (bytes == 4 && lead == 0xf4)
                valid = valid && data[i + 1] < 0x90;
            if (!valid)
                break;
            for (std::size_t j = 0; j < bytes; ++j)
                target[out++] = static_cast<char>(data[i++]);
        }
    } else if ((encoding == 1 || encoding == 2) && length >= 3) {
        bool little = encoding == 2 || (data[1] == 0xff && data[2] == 0xfe);
        std::size_t i = encoding == 1 ? 3 : 1;
        for (; i + 1 < length && out < max; i += 2) {
            const unsigned char c = little ? data[i] : data[i + 1];
            if (c == 0 && (little ? data[i + 1] : data[i]) == 0)
                break;
            target[out++] = c >= 0x20 && c < 0x7f ? static_cast<char>(c) : '?';
        }
    }
    target[out] = '\0';
}
} // namespace

bool is_mp3_path(std::string_view path) {
    return safe_path(path);
}

bool parse_id3v2(std::string_view bytes, track &out) {
    if (bytes.size() < 10 || bytes.substr(0, 3) != "ID3")
        return true;
    const auto *p = reinterpret_cast<const unsigned char *>(bytes.data());
    const std::uint8_t version = p[3];
    if (version < 2 || version > 4 || p[4] == 0xff || (p[6] | p[7] | p[8] | p[9]) & 0x80)
        return false;
    const std::size_t tag_bytes =
        (std::size_t(p[6]) << 21) | (std::size_t(p[7]) << 14) | (std::size_t(p[8]) << 7) | p[9];
    if (tag_bytes > 64 * 1024 || tag_bytes + 10 > bytes.size())
        return false;
    std::size_t offset = 10;
    const std::size_t end = 10 + tag_bytes;
    const std::size_t header_bytes = version == 2 ? 6 : 10;
    while (offset + header_bytes <= end) {
        if (p[offset] == 0)
            break;
        const std::uint32_t size =
            version == 2 ? (std::uint32_t(p[offset + 3]) << 16) | (std::uint32_t(p[offset + 4]) << 8) | p[offset + 5]
                         : frame_size(p + offset + 4, 4, version);
        if (size == UINT32_MAX || size == 0 || offset + header_bytes + size > end)
            return false;
        const char *id = reinterpret_cast<const char *>(p + offset);
        const bool title =
            (version == 2 && std::memcmp(id, "TT2", 3) == 0) || (version != 2 && std::memcmp(id, "TIT2", 4) == 0);
        const bool artist =
            (version == 2 && std::memcmp(id, "TP1", 3) == 0) || (version != 2 && std::memcmp(id, "TPE1", 4) == 0);
        if (title)
            copy_text(out.title, nullptr, p + offset + header_bytes, size);
        if (artist)
            copy_text(out.title, &out.artist, p + offset + header_bytes, size);
        offset += header_bytes + size;
    }
    return true;
}

bool model::replace_queue(const track *items, std::size_t count) {
    if (count > k_max_tracks || (items == nullptr && count != 0))
        return false;
    for (std::size_t i = 0; i < count; ++i) {
        if (items[i].path.back() != '\0' || !safe_path(items[i].path.data()) || items[i].title.back() != '\0' ||
            items[i].artist.back() != '\0' || items[i].size_bytes == 0 || items[i].size_bytes > k_max_track_bytes)
            return false;
    }
    tracks_ = {};
    if (count != 0)
        std::copy(items, items + count, tracks_.begin());
    count_ = count;
    selected_ = 0;
    position_ms_ = 0;
    state_ = playback_state::stopped;
    return true;
}
bool model::select(std::size_t index) {
    if (index >= count_)
        return false;
    selected_ = index;
    position_ms_ = 0;
    return true;
}
bool model::next() {
    if (!count_)
        return false;
    if (repeat_ == repeat_mode::one)
        return true;
    if (shuffle_ && count_ > 1)
        selected_ = (selected_ + 1 + (position_ms_ % (count_ - 1))) % count_;
    else if (selected_ + 1 < count_)
        ++selected_;
    else if (repeat_ == repeat_mode::all)
        selected_ = 0;
    else
        return false;
    position_ms_ = 0;
    return true;
}
bool model::previous() {
    if (!count_)
        return false;
    if (position_ms_ > 3000) {
        position_ms_ = 0;
        return true;
    }
    if (selected_ > 0)
        --selected_;
    else if (repeat_ == repeat_mode::all)
        selected_ = count_ - 1;
    else
        return false;
    position_ms_ = 0;
    return true;
}
bool model::seek(std::uint32_t value) {
    const track *item = current();
    if (item == nullptr || item->duration_ms == 0 || value > item->duration_ms)
        return false;
    position_ms_ = value;
    return true;
}
void model::set_volume(std::uint8_t value) {
    volume_ = std::min<std::uint8_t>(value, 50);
}
void model::cycle_repeat() {
    repeat_ = repeat_ == repeat_mode::off   ? repeat_mode::all
              : repeat_ == repeat_mode::all ? repeat_mode::one
                                            : repeat_mode::off;
}
void model::clear() {
    tracks_ = {};
    count_ = selected_ = position_ms_ = 0;
    state_ = playback_state::stopped;
}
const track *model::current() const {
    return at(selected_);
}
const track *model::at(std::size_t index) const {
    return index < count_ ? &tracks_[index] : nullptr;
}
snapshot model::status() const {
    snapshot out{};
    out.state = state_;
    out.selected = selected_;
    out.track_count = count_;
    out.position_ms = position_ms_;
    out.volume = volume_;
    out.repeat = repeat_;
    out.shuffle = shuffle_;
    if (const track *item = current())
        out.current = *item;
    return out;
}
} // namespace cyberdeck_music
