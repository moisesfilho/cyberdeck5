#include "apps/music/cyberdeck_music_metadata.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>
#include <sys/types.h>
#include <unistd.h>

namespace cyberdeck_music {
namespace {
constexpr std::size_t k_id3_metadata_limit = 64 * 1024;
} // namespace

void read_metadata(int fd, std::size_t size, metadata &out) {
    unsigned char header[10]{};
    if (pread(fd, header, sizeof(header), 0) != static_cast<ssize_t>(sizeof(header)) ||
        std::memcmp(header, "ID3", 3) != 0)
        return;
    const std::size_t tag_size =
        (std::size_t(header[6]) << 21) | (std::size_t(header[7]) << 14) | (std::size_t(header[8]) << 7) | header[9];
    const std::size_t read_size =
        std::min<std::size_t>(size, std::min<std::size_t>(tag_size + 10, k_id3_metadata_limit + 10));
    std::unique_ptr<char[]> bytes(new (std::nothrow) char[read_size]);
    if (!bytes || read_size < 10)
        return;
    const ssize_t count = pread(fd, bytes.get(), read_size, 0);
    if (count < 10)
        return;
    metadata decoded{};
    if (parse_id3v2(std::string_view(bytes.get(), static_cast<std::size_t>(count)), decoded)) {
        if (decoded.title[0] != '\0')
            out.title = decoded.title;
        if (decoded.artist[0] != '\0')
            out.artist = decoded.artist;
    }
}
} // namespace cyberdeck_music
