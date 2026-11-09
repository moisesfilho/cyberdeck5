#pragma once

#include "apps/image/cyberdeck_image_decode.h"

#include <cstddef>
#include <cstdint>

/* Bounded baseline JPEG (SOF0, 8-bit) inspection and streaming decode.
 *
 * Pure C++ over the injectable `decode_ops` seams: no LVGL, ESP-IDF, VFS or
 * platform allocation, so the same translation unit is linked by the host
 * harness and by the firmware.  The entropy decoder is the vendored TJpgDec
 * (see `components/cyberdeck/THIRD_PARTY.md`).
 *
 * Approved variant set: SOF0 only, 8 bits, 1 component (grayscale, replicated
 * to R=G=B) or 3 components YCbCr with 4:4:4, 4:2:2 or 4:2:0 sampling.
 * Everything else is refused with a typed reason: progressive/extended/
 * arithmetic/lossless SOF, 12-bit precision, 2 or 4 components, and 4:1:1
 * (the vendored R0.03 decoder implements only the three samplings above).
 * APPn/COM are ignored, so EXIF orientation and ICC profiles are never
 * applied. */
namespace cyberdeck_image {

/* Enough of the header to reach SOF0 and SOS for a typical file.  A prefix that
 * ends earlier reports `prefix_incomplete`; the decoder never depends on this
 * size because it walks the same segments through `read_at`. */
inline constexpr std::size_t k_jpeg_prefix_bytes = 4096;
/* TJpgDec stream input buffer (JD_SZBUF) and the largest post-scan window the
 * trailer check may read to find EOI. */
inline constexpr std::size_t k_jpeg_stream_buffer_bytes = 1024;
inline constexpr std::size_t k_jpeg_trailer_bytes = 8192;
/* Declared marker budget; a file with more segments is refused instead of
 * walking a 5 MiB marker soup. */
inline constexpr std::size_t k_jpeg_max_segments = 1024;

/* Working pool of the entropy decoder, derived from the vendored decoder
 * (JD_FASTDECODE >= 1, so one sample is 2 bytes):
 *   stream buffer 1024
 * + at most 4 Huffman tables (2 DC + 2 AC) of 16 + 256*2 + 256 bytes
 * + IDCT/RGB working buffer of the largest MCU (4 blocks * 64 * 2 + 64)
 * + MCU buffer of 4 luma blocks plus 2 chroma blocks ((4+2) * 64 * 2). */
inline constexpr std::size_t k_jpeg_huffman_table_bytes = 16u + (256u * 2u) + 256u;
inline constexpr std::size_t k_jpeg_max_huffman_tables = 4;
inline constexpr std::size_t k_jpeg_max_blocks_per_mcu = 4;
inline constexpr std::size_t k_jpeg_work_buffer_bytes = (k_jpeg_max_blocks_per_mcu * 64u * 2u) + 64u;
inline constexpr std::size_t k_jpeg_mcu_buffer_bytes = (k_jpeg_max_blocks_per_mcu + 2u) * 64u * 2u;
inline constexpr std::size_t k_jpeg_pool_bytes = k_jpeg_stream_buffer_bytes +
                                                 (k_jpeg_max_huffman_tables * k_jpeg_huffman_table_bytes) +
                                                 k_jpeg_work_buffer_bytes + k_jpeg_mcu_buffer_bytes;

enum class jpeg_status : std::uint8_t {
    ok,
    invalid_soi,
    header_truncated,
    /* The supplied prefix ends inside the header; the file itself may be fine. */
    prefix_incomplete,
    unsupported_variant,
    unsupported_precision,
    unsupported_components,
    unsupported_sampling,
    invalid_dimension,
    invalid_segment,
    truncated_segment,
    too_many_segments,
    missing_scan,
    multiple_scans,
    missing_eoi,
    read_failed,
    decode_failed,
};

struct jpeg_info {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t components = 0;
    /* Luma sampling factors: (1,1) 4:4:4, (2,1) 4:2:2, (2,2) 4:2:0. */
    std::uint8_t sampling_x = 0;
    std::uint8_t sampling_y = 0;
    bool grayscale = false;
    std::uint64_t frame_bytes = 0;
};

/* Validates marker structure up to (and including) SOS without reading past
 * `prefix_bytes`.  `file_bytes` is the size the confined descriptor reports, so
 * a segment length that overruns the file is refused with `truncated_segment`. */
jpeg_status inspect_jpeg(const std::uint8_t *prefix, std::size_t prefix_bytes, std::uint64_t file_bytes,
                         jpeg_info &out);

/* Single source of the `image: <reason>` text for every jpeg_status. */
const char *jpeg_status_reason(jpeg_status status);

/* Declared peak of one decode: the published frame plus the entropy pool. */
std::size_t jpeg_decode_peak_bytes(const jpeg_info &info);

/* Decodes straight into one RGB565 block.  On failure `out` stays empty and the
 * pool/frame allocation is released; cancellation is polled once per MCU. */
decode_result decode_jpeg(const decode_ops &ops, int descriptor, std::size_t file_bytes, decoded_image &out);

} // namespace cyberdeck_image
