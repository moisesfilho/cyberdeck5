#pragma once

#include "apps/image/cyberdeck_image_bmp.h"
#include "apps/image/cyberdeck_image_formats.h"

#include <cstddef>
#include <cstdint>

/* Streaming image -> RGB565 decode behind an injectable seam.
 *
 * The real backend reads the envelope of the requested format through `pread`
 * and then streams the payload (one padded BMP row, one JPEG MCU or one PNG
 * scanline at a time), so the file never needs a 5 MiB staging buffer: the only
 * large allocation is the RGB565 frame (w*h*2, at most k_max_frame_bytes) plus
 * the bounded working pool of the format.  Allocation, read and cancellation
 * are function pointers so the host harness can prove the failure paths
 * (allocation refused, short read, cancel) without a device.
 *
 * Phase 2 integration (deliberately not done here): `application` must resolve
 * the format from the entry name with `format_from_name` and pass it to
 * `decode_descriptor`; the listing must accept the whole `image_format` set, the
 * view/UI must keep installing exactly one published frame, and `help`/docs must
 * describe the supported subset.  `POST /control/upload` stays BMP-only through
 * its own `validate_upload_name`, never through this module. */
namespace cyberdeck_image {

enum class decode_status : std::uint8_t {
    ok,
    invalid_argument,
    too_large,
    format,
    read_failed,
    allocation_failed,
    cancelled,
    /* The requested format has no decoder wired into this build. */
    unsupported_format,
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
    /* BMP envelope reason; meaningless for the other formats. */
    bmp_status format = bmp_status::ok;
    /* Single-line reason of `state`, taken from the reason table of the format
     * that produced it (never duplicated at the call site).  Never null after
     * `decode_descriptor`/`decode_*` returns. */
    const char *reason = nullptr;
};

/* Reason for the format-independent statuses. */
const char *decode_status_reason(decode_status status);

/* `file_bytes` is the size reported by the confined descriptor, not how much
 * the caller read.  On failure `out` is left empty and any allocation made on
 * the way is released. */
decode_result decode_descriptor(const decode_ops &ops, int descriptor, std::size_t file_bytes, image_format format,
                                decoded_image &out);

/* BMP-only compatibility entry point (the current viewer caller). */
decode_result decode_descriptor(const decode_ops &ops, int descriptor, std::size_t file_bytes, decoded_image &out);

/* Idempotent: frees the frame and clears the descriptor. */
void release_decoded(decoded_image &image, const decode_ops &ops = default_decode_ops());

} // namespace cyberdeck_image
