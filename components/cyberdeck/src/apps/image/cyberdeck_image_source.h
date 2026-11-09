#pragma once

#include "apps/image/cyberdeck_image_decode.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

/* Shared random-access source of the format walkers (JPEG markers, PNG chunks).
 *
 * Private to the decoders: it is not part of the public image API and carries no
 * platform dependency.  `source::limit` is what `read` can actually deliver —
 * the whole file for the descriptor path, or the inspected prefix — so a short
 * read can be reported as `prefix_incomplete` instead of an I/O failure. */
namespace cyberdeck_image {
namespace detail {

enum class read_status : std::uint8_t { ok, out_of_file, short_prefix, io_error };

struct source {
    const void *context = nullptr;
    std::uint64_t file_bytes = 0;
    std::uint64_t limit = 0;
    bool limited_prefix = false;
    bool (*read)(const void *context, std::uint8_t *buffer, std::size_t bytes, std::uint64_t offset) = nullptr;
};

inline read_status read_bytes(const source &from, std::uint8_t *buffer, std::size_t bytes, std::uint64_t offset) {
    if (from.read == nullptr)
        return read_status::io_error;
    if (offset > from.file_bytes || bytes > from.file_bytes - offset)
        return read_status::out_of_file;
    if (offset > from.limit || bytes > from.limit - offset)
        return from.limited_prefix ? read_status::short_prefix : read_status::io_error;
    if (!from.read(from.context, buffer, bytes, offset))
        return read_status::io_error;
    return read_status::ok;
}

inline bool prefix_read(const void *context, std::uint8_t *buffer, std::size_t bytes, std::uint64_t offset) {
    std::memcpy(buffer, static_cast<const std::uint8_t *>(context) + offset, bytes);
    return true;
}

struct descriptor_source {
    const decode_ops *ops = nullptr;
    int descriptor = -1;
};

inline bool descriptor_read(const void *context, std::uint8_t *buffer, std::size_t bytes, std::uint64_t offset) {
    const auto *from = static_cast<const descriptor_source *>(context);
    return from->ops->read_at(from->ops->context, from->descriptor, buffer, bytes, offset);
}

inline std::uint32_t big_endian16(const std::uint8_t *bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 8) | static_cast<std::uint32_t>(bytes[1]);
}

inline std::uint32_t big_endian32(const std::uint8_t *bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24) | (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) | static_cast<std::uint32_t>(bytes[3]);
}

} // namespace detail
} // namespace cyberdeck_image
