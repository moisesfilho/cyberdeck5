#pragma once

#include "apps/image/cyberdeck_image_formats.h"

#include <cstddef>
#include <cstdint>

/* Bounded BMP envelope and pixel conversion for the isolated image viewer.
 *
 * The module is pure C++: no LVGL, ESP-IDF, VFS, allocation or descriptor.  It
 * therefore runs unchanged on the host harness and on the target, and the
 * device build only feeds it bytes read through the confined storage seam.
 *
 * Approved scope: uncompressed 24-bit BI_RGB only.  Compressed, indexed or
 * alpha BMP variants are rejected with a typed reason instead of guessed. */
namespace cyberdeck_image {

/* BMPFILEHEADER (14) + the 40 common bytes of every supported DIB header
 * (BITMAPINFOHEADER/V4/V5 all start with the same fields), so one fixed
 * prefix is enough to validate the envelope before any row is read. */
inline constexpr std::size_t k_header_prefix_bytes = 54;
inline constexpr std::uint32_t k_min_dib_header_bytes = 40;
inline constexpr std::uint64_t k_bytes_per_pixel = 3;

enum class bmp_status : std::uint8_t {
    ok,
    invalid_signature,
    header_truncated,
    unsupported_dib_header,
    unsupported_planes,
    unsupported_bit_depth,
    unsupported_compression,
    invalid_dimension,
    invalid_layout,
    pixel_data_truncated,
    buffer_too_small,
};

struct bmp_info {
    std::uint32_t width = 0;
    /** Height magnitude; the sign of `biHeight` only selects the row order. */
    std::uint32_t height = 0;
    /** `biHeight < 0`: the first file row is the top row. */
    bool top_down = false;
    std::uint32_t pixel_offset = 0;
    /** Padded BGR row size: ((width * 3) + 3) & ~3. */
    std::uint32_t row_stride = 0;
    std::uint64_t pixel_bytes = 0;
};

/* Validates the envelope.  `prefix` holds the first `prefix_bytes` bytes of
 * the file and `file_bytes` is the complete declared file size, so the bounds
 * check never depends on how much the caller happened to read. */
bmp_status inspect_bmp(const std::uint8_t *prefix, std::size_t prefix_bytes, std::uint64_t file_bytes, bmp_info &out);

/* Exact RGB565 buffer the caller must provide for a validated envelope. */
std::size_t rgb565_buffer_bytes(const bmp_info &info);

/* Converts one padded BGR row into `width` contiguous RGB565 pixels. */
void convert_bgr_row_to_rgb565(const std::uint8_t *row, std::uint32_t width, std::uint16_t *out);

const char *bmp_status_reason(bmp_status status);

} // namespace cyberdeck_image
