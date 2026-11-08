#pragma once

#include "apps/image/cyberdeck_image_bmp.h"

#include <cstddef>
#include <cstdint>

/* Streaming BMP -> RGB565 decode behind an injectable seam.
 *
 * The real backend reads the 54-byte envelope and then one padded BGR row at
 * a time with `pread`, so the file never needs a 5 MiB staging buffer: the
 * only large allocation is the RGB565 frame (w*h*2, at most 3,276,800 B) plus
 * a single row scratch packed into the same block.  Allocation, read and
 * cancellation are function pointers so the host harness can prove the failure
 * paths (allocation refused, short read, cancel) without a device. */
namespace cyberdeck_image {

enum class decode_status : std::uint8_t {
    ok,
    invalid_argument,
    too_large,
    format,
    read_failed,
    allocation_failed,
    cancelled,
};

struct decode_ops {
    void *context = nullptr;
    bool (*read_at)(void *context, int descriptor, std::uint8_t *buffer, std::size_t bytes,
                    std::uint64_t offset) = nullptr;
    bool (*allocate)(void *context, std::size_t bytes, void *&memory) = nullptr;
    void (*deallocate)(void *context, void *memory) = nullptr;
    bool (*cancelled)(void *context) = nullptr;
};

const decode_ops &default_decode_ops();

struct decoded_image {
    std::uint16_t *pixels = nullptr;
    std::size_t pixel_bytes = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct decode_result {
    decode_status state = decode_status::format;
    bmp_status format = bmp_status::ok;
};

/* `file_bytes` is the size reported by the confined descriptor, not how much
 * the caller read.  On failure `out` is left empty and any allocation made on
 * the way is released. */
decode_result decode_descriptor(const decode_ops &ops, int descriptor, std::size_t file_bytes, decoded_image &out);

/* Idempotent: frees the frame and clears the descriptor. */
void release_decoded(decoded_image &image, const decode_ops &ops = default_decode_ops());

} // namespace cyberdeck_image
