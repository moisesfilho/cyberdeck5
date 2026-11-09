#pragma once

#include "apps/image/cyberdeck_image_decode.h"

#include <cstddef>
#include <cstdint>

/* Bounded static PNG inspection and streaming decode.
 *
 * Pure C++ over the injectable `decode_ops` seams: no LVGL, ESP-IDF, VFS or
 * platform allocation.  Inflation uses the vendored miniz `tinfl` decoder with
 * a 32 KiB sliding window, so a scanline is reconstructed as soon as its bytes
 * arrive and the whole image is never staged in memory.  See
 * `components/cyberdeck/THIRD_PARTY.md` for provenance and license.
 *
 * Approved variant set: 8-bit non-interlaced, compression 0, filter method 0,
 * color type 0 (grayscale, replicated to R=G=B), 2 (truecolor) and 3 (palette
 * with PLTE, at most 256 entries).  Alpha is refused with a typed reason:
 * color types 4/6 and any tRNS chunk.  Filters 0..4 are supported per row. */
namespace cyberdeck_image {

/* Enough of the header to reach the first IDAT of a typical file. */
inline constexpr std::size_t k_png_prefix_bytes = 4096;
/* Sliding window of the inflater (TINFL_LZ_DICT_SIZE) and the staging piece of
 * chunk data the walker reads at a time; both are bounded, so scratch never
 * grows with the image height. */
inline constexpr std::size_t k_png_window_bytes = 32768;
inline constexpr std::size_t k_png_piece_bytes = 512;
/* Compressed bytes staged for the inflater: tinfl can ask for more input while
 * leaving a few bytes unconsumed, so the remainder must be kept. */
inline constexpr std::size_t k_png_staging_bytes = 4096;
/* Declared upper bound of one tinfl decompressor state (8.376 B measured on the
 * host).  It is 8 KiB and therefore lives in the heap scratch block, never on
 * the worker stack; the translation unit asserts the vendored decoder fits. */
inline constexpr std::size_t k_png_inflate_state_bytes = 8448;
/* Declared chunk budget of one file. */
inline constexpr std::size_t k_png_max_chunks = 4096;
inline constexpr std::size_t k_png_max_palette_entries = 256;
inline constexpr std::size_t k_png_palette_bytes = k_png_max_palette_entries * 3u;

enum class png_status : std::uint8_t {
    ok,
    invalid_signature,
    header_truncated,
    /* The supplied prefix ends inside the header; the file itself may be fine. */
    prefix_incomplete,
    invalid_header,
    unsupported_bit_depth,
    unsupported_color_type,
    unsupported_alpha,
    unsupported_interlace,
    unsupported_compression,
    unsupported_filter_method,
    unsupported_critical_chunk,
    invalid_dimension,
    invalid_palette,
    invalid_palette_entry,
    checksum_mismatch,
    truncated_segment,
    missing_image_data,
    invalid_filter,
    truncated_image,
    excessive_data,
    too_many_chunks,
    /* Decode-only outcomes, kept here so the chunk walker can stop with the
     * status the caller must report. */
    read_failed,
    allocation_failed,
    cancelled,
    decode_failed,
};

struct png_info {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    /* 0 grayscale, 2 truecolor, 3 palette. */
    std::uint8_t color_type = 0;
    std::uint16_t palette_entries = 0;
    /* Scanline bytes without the leading filter byte. */
    std::uint32_t row_bytes = 0;
    /* 1 for grayscale/palette, 3 for truecolor (filter distance). */
    std::uint8_t bytes_per_pixel = 0;
    std::uint64_t frame_bytes = 0;
};

/* Validates signature, IHDR, PLTE (length and CRC) and chunk order without
 * reading past `prefix_bytes`.  IDAT payload is skipped, so its CRC and its
 * zlib stream are validated by `decode_png` (the only consumer that reads it). */
png_status inspect_png(const std::uint8_t *prefix, std::size_t prefix_bytes, std::uint64_t file_bytes, png_info &out);

/* Single source of the `image: <reason>` text for every png_status. */
const char *png_status_reason(png_status status);

/* Declared scratch of one decode: sliding window, inflater state, compressed
 * input staging, the two scanline buffers and the palette.  Independent of the
 * image height. */
std::size_t png_scratch_bytes(const png_info &info);
/* Declared peak: the published frame plus `png_scratch_bytes`. */
std::size_t png_decode_peak_bytes(const png_info &info);

/* Decodes straight into one RGB565 block.  On failure `out` stays empty and
 * every allocation is released; cancellation is polled once per scanline. */
decode_result decode_png(const decode_ops &ops, int descriptor, std::size_t file_bytes, decoded_image &out);

} // namespace cyberdeck_image
