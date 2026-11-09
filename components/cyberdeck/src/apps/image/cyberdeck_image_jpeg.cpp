#include "apps/image/cyberdeck_image_jpeg.h"

#include "cyberdeck_image_source.h"
#include "third_party/tjpgd/tjpgd.h"

#include <algorithm>
#include <cstring>

namespace cyberdeck_image {
using detail::big_endian16;
using detail::descriptor_read;
using detail::descriptor_source;
using detail::prefix_read;
using detail::read_bytes;
using detail::read_status;
using detail::source;

namespace {

/* The pool budget in the header is derived from the vendored configuration: a
 * different JD_FASTDECODE/JD_SZBUF would silently under-allocate, so the build
 * stops instead. */
static_assert(sizeof(jd_yuv_t) == 2, "k_jpeg_pool_bytes assumes JD_FASTDECODE >= 1 (16-bit samples)");
static_assert(JD_FORMAT == 1, "the vendored TJpgDec copy must emit RGB565");
static_assert(JD_USE_SCALE == 0, "descaling must stay disabled");
static_assert(k_jpeg_stream_buffer_bytes == JD_SZBUF, "k_jpeg_stream_buffer_bytes must match JD_SZBUF");
/* The two working blocks are sized from a 16-bit sample; a wider jd_yuv_t would
 * under-allocate the pool silently. */
static_assert(k_jpeg_work_buffer_bytes >= (k_jpeg_max_blocks_per_mcu * 64u * sizeof(jd_yuv_t)) + 64u,
              "IDCT/RGB working buffer must cover the largest MCU");
static_assert(k_jpeg_mcu_buffer_bytes >= (k_jpeg_max_blocks_per_mcu + 2u) * 64u * sizeof(jd_yuv_t),
              "MCU buffer must cover the luma and chroma blocks");
static_assert(k_jpeg_pool_bytes ==
                  (k_jpeg_stream_buffer_bytes + (k_jpeg_max_huffman_tables * k_jpeg_huffman_table_bytes) +
                   k_jpeg_work_buffer_bytes + k_jpeg_mcu_buffer_bytes),
              "k_jpeg_pool_bytes must stay the derived sum of the decoder working blocks");

constexpr std::uint8_t k_marker_byte = 0xFF;
constexpr std::uint8_t k_marker_soi = 0xD8;
constexpr std::uint8_t k_marker_eoi = 0xD9;
constexpr std::uint8_t k_marker_sos = 0xDA;
constexpr std::uint8_t k_marker_sof0 = 0xC0;
constexpr std::uint8_t k_marker_tem = 0x01;

jpeg_status read_failure_status(read_status status) {
    switch (status) {
    case read_status::ok:
        return jpeg_status::ok;
    case read_status::out_of_file:
        return jpeg_status::header_truncated;
    case read_status::short_prefix:
        return jpeg_status::prefix_incomplete;
    case read_status::io_error:
        break;
    }
    return jpeg_status::read_failed;
}

bool standalone_marker(std::uint8_t marker) {
    return marker == k_marker_soi || marker == k_marker_eoi || marker == k_marker_tem ||
           (marker >= 0xD0 && marker <= 0xD7);
}

bool unsupported_sof(std::uint8_t marker) {
    switch (marker) {
    case 0xC1: /* extended sequential */
    case 0xC2: /* progressive */
    case 0xC3: /* lossless */
    case 0xC5:
    case 0xC6:
    case 0xC7:
    case 0xC9: /* arithmetic */
    case 0xCA:
    case 0xCB:
    case 0xCC:
    case 0xCD: /* differential */
    case 0xCE:
    case 0xCF:
        return true;
    default:
        return false;
    }
}

/* Walks the marker segments up to and including SOS.  `scan_offset` receives the
 * first byte of the entropy-coded stream. */
jpeg_status walk_markers(const source &from, jpeg_info &out, std::uint64_t &scan_offset) {
    out = jpeg_info{};
    scan_offset = 0;

    std::uint8_t soi[2]{};
    if (const read_status status = read_bytes(from, soi, sizeof(soi), 0); status != read_status::ok)
        return read_failure_status(status);
    if (soi[0] != k_marker_byte || soi[1] != k_marker_soi)
        return jpeg_status::invalid_soi;

    bool have_frame = false;
    std::uint64_t offset = sizeof(soi);
    for (std::size_t segments = 0; segments < k_jpeg_max_segments; ++segments) {
        std::uint8_t marker_byte = 0;
        if (const read_status status = read_bytes(from, &marker_byte, 1, offset); status != read_status::ok)
            return read_failure_status(status);
        if (marker_byte != k_marker_byte)
            return jpeg_status::invalid_segment;
        /* Any number of 0xFF fill bytes may precede a marker. */
        do {
            ++offset;
            if (const read_status status = read_bytes(from, &marker_byte, 1, offset); status != read_status::ok)
                return read_failure_status(status);
        } while (marker_byte == k_marker_byte);
        ++offset;

        const std::uint8_t marker = marker_byte;
        if (marker == k_marker_eoi)
            return jpeg_status::missing_scan;
        if (standalone_marker(marker))
            continue;

        std::uint8_t length_bytes[2]{};
        if (const read_status status = read_bytes(from, length_bytes, sizeof(length_bytes), offset);
            status != read_status::ok)
            return read_failure_status(status);
        const std::uint32_t length = big_endian16(length_bytes);
        if (length < 2)
            return jpeg_status::invalid_segment;
        const std::uint32_t payload_bytes = length - 2;
        const std::uint64_t payload_offset = offset + 2;
        if (payload_offset > from.file_bytes || payload_bytes > from.file_bytes - payload_offset)
            return jpeg_status::truncated_segment;
        offset = payload_offset + payload_bytes;

        if (unsupported_sof(marker))
            return jpeg_status::unsupported_variant;

        if (marker == k_marker_sof0) {
            if (have_frame)
                return jpeg_status::unsupported_variant;
            std::uint8_t frame[6]{};
            if (payload_bytes < sizeof(frame))
                return jpeg_status::invalid_segment;
            if (const read_status status = read_bytes(from, frame, sizeof(frame), payload_offset);
                status != read_status::ok)
                return read_failure_status(status);
            const std::uint8_t precision = frame[0];
            const std::uint32_t height = big_endian16(&frame[1]);
            const std::uint32_t width = big_endian16(&frame[3]);
            const std::uint8_t components = frame[5];
            if (precision != 8)
                return jpeg_status::unsupported_precision;
            if (components != 1 && components != 3)
                return jpeg_status::unsupported_components;
            if (payload_bytes != 6u + (3u * components))
                return jpeg_status::invalid_segment;
            if (width == 0 || height == 0 || width > k_max_dimension || height > k_max_dimension)
                return jpeg_status::invalid_dimension;

            std::uint8_t sampling[3 * 3]{};
            if (const read_status status = read_bytes(from, sampling, 3u * components, payload_offset + sizeof(frame));
                status != read_status::ok)
                return read_failure_status(status);
            const std::uint8_t luma = sampling[1];
            const std::uint8_t sampling_x = static_cast<std::uint8_t>(luma >> 4);
            const std::uint8_t sampling_y = static_cast<std::uint8_t>(luma & 0x0F);
            const bool supported_luma = (sampling_x == 1 && sampling_y == 1) || /* 4:4:4 */
                                        (sampling_x == 2 && sampling_y == 1) || /* 4:2:2 */
                                        (sampling_x == 2 && sampling_y == 2);   /* 4:2:0 */
            if (!supported_luma)
                return jpeg_status::unsupported_sampling;
            for (std::uint8_t component = 1; component < components; ++component) {
                /* The vendored decoder requires 1x1 chroma; 4:1:1 is therefore
                 * refused with the same typed reason as any other sampling. */
                if (sampling[1u + (3u * component)] != 0x11)
                    return jpeg_status::unsupported_sampling;
            }

            std::size_t frame_bytes_exact = 0;
            if (!frame_bytes(width, height, frame_bytes_exact))
                return jpeg_status::invalid_dimension;
            out.width = width;
            out.height = height;
            out.components = components;
            out.sampling_x = sampling_x;
            out.sampling_y = sampling_y;
            out.grayscale = components == 1;
            out.frame_bytes = frame_bytes_exact;
            have_frame = true;
            continue;
        }

        if (marker == k_marker_sos) {
            if (!have_frame)
                return jpeg_status::missing_scan;
            std::uint8_t scan[1]{};
            if (payload_bytes < sizeof(scan))
                return jpeg_status::invalid_segment;
            if (const read_status status = read_bytes(from, scan, sizeof(scan), payload_offset);
                status != read_status::ok)
                return read_failure_status(status);
            if (scan[0] != out.components || payload_bytes != 4u + (2u * scan[0]))
                return jpeg_status::unsupported_variant;
            scan_offset = offset;
            return jpeg_status::ok;
        }

        /* DQT, DHT, DRI, APPn, COM and unknown segments are skipped here; the
         * entropy decoder validates the tables it actually uses. */
    }
    return jpeg_status::too_many_segments;
}

/* After the entropy stream the next marker must be EOI.  A second SOS is a
 * multi-scan file disguised as baseline, and is refused instead of decoding only
 * the first scan.  The scan starts one stream buffer before the read position
 * (the decoder reads ahead) but never before the first entropy byte, otherwise
 * the header's own SOS would be mistaken for a second scan. */
jpeg_status scan_trailer(const source &from, std::uint64_t scan_offset, std::uint64_t position, std::uint8_t *buffer,
                         std::size_t buffer_bytes) {
    const std::uint64_t lookbehind = position > k_jpeg_stream_buffer_bytes ? position - k_jpeg_stream_buffer_bytes : 0;
    const std::uint64_t start = std::max(scan_offset, lookbehind);
    const std::uint64_t end = std::min<std::uint64_t>(from.file_bytes, position + k_jpeg_trailer_bytes);

    std::uint64_t window_offset = start;
    std::size_t window_bytes = 0;
    const auto ensure = [&](std::uint64_t at, std::size_t need) {
        if (at >= window_offset && need <= window_bytes && at - window_offset <= window_bytes - need)
            return true;
        if (at >= end)
            return false;
        const std::uint64_t take = std::min<std::uint64_t>(end - at, buffer_bytes);
        if (take == 0 || read_bytes(from, buffer, static_cast<std::size_t>(take), at) != read_status::ok)
            return false;
        window_offset = at;
        window_bytes = static_cast<std::size_t>(take);
        return need <= window_bytes;
    };

    std::uint64_t cursor = start;
    while (cursor + 1 < end) {
        if (!ensure(cursor, 2))
            return jpeg_status::missing_eoi;
        const std::uint8_t first = buffer[cursor - window_offset];
        const std::uint8_t second = buffer[cursor - window_offset + 1];
        if (first != k_marker_byte || second == k_marker_byte) {
            ++cursor;
            continue;
        }
        if (second == 0x00 || (second >= 0xD0 && second <= 0xD7)) {
            cursor += 2;
            continue;
        }
        if (second == k_marker_eoi)
            return jpeg_status::ok;
        if (second == k_marker_sos)
            return jpeg_status::multiple_scans;
        if (second == k_marker_soi || second == k_marker_tem) {
            cursor += 2;
            continue;
        }
        if (!ensure(cursor, 4))
            return jpeg_status::missing_eoi;
        const std::uint32_t segment = big_endian16(&buffer[(cursor - window_offset) + 2]);
        if (segment < 2)
            return jpeg_status::decode_failed;
        cursor += 2 + segment;
    }
    return jpeg_status::missing_eoi;
}

struct stream_state {
    const decode_ops *ops = nullptr;
    int descriptor = -1;
    std::uint64_t file_bytes = 0;
    std::uint64_t position = 0;
    std::uint16_t *pixels = nullptr;
    std::uint32_t width = 0;
    bool read_error = false;
    bool cancelled = false;
};

size_t jpeg_input(JDEC *decoder, std::uint8_t *buffer, std::size_t bytes) {
    auto *state = static_cast<stream_state *>(decoder->device);
    if (state == nullptr)
        return 0;
    const std::uint64_t remaining = state->position < state->file_bytes ? state->file_bytes - state->position : 0;
    const std::size_t available = remaining < bytes ? static_cast<std::size_t>(remaining) : bytes;
    if (buffer != nullptr && available != 0 &&
        !state->ops->read_at(state->ops->context, state->descriptor, buffer, available, state->position)) {
        state->read_error = true;
        return 0;
    }
    state->position += available;
    return available;
}

int jpeg_output(JDEC *decoder, void *bitmap, JRECT *rect) {
    auto *state = static_cast<stream_state *>(decoder->device);
    if (state == nullptr || bitmap == nullptr || rect == nullptr)
        return 0;
    /* Cancellation has MCU granularity, which is the finest step the entropy
     * decoder offers. */
    if (state->ops->cancelled != nullptr && state->ops->cancelled(state->ops->context)) {
        state->cancelled = true;
        return 0;
    }
    const auto *source_pixels = static_cast<const std::uint16_t *>(bitmap);
    const std::size_t columns = static_cast<std::size_t>(rect->right - rect->left) + 1;
    for (std::uint32_t row = rect->top; row <= rect->bottom; ++row) {
        std::uint16_t *target = state->pixels + (static_cast<std::size_t>(row) * state->width) + rect->left;
        std::memcpy(target, source_pixels, columns * sizeof(std::uint16_t));
        source_pixels += columns;
    }
    return 1;
}

decode_result cancelled_result() {
    return {decode_status::cancelled, bmp_status::ok, decode_status_reason(decode_status::cancelled)};
}

decode_result read_failed_result() {
    return {decode_status::read_failed, bmp_status::ok, decode_status_reason(decode_status::read_failed)};
}

decode_result allocation_failed_result() {
    return {decode_status::allocation_failed, bmp_status::ok, decode_status_reason(decode_status::allocation_failed)};
}

decode_result format_result(jpeg_status status) {
    const decode_status state = status == jpeg_status::read_failed ? decode_status::read_failed : decode_status::format;
    return {state, bmp_status::ok, jpeg_status_reason(status)};
}

} // namespace

const char *jpeg_status_reason(jpeg_status status) {
    switch (status) {
    case jpeg_status::ok:
        return "ok";
    case jpeg_status::invalid_soi:
        return "invalid SOI marker";
    case jpeg_status::header_truncated:
        return "truncated JPEG header";
    case jpeg_status::prefix_incomplete:
        return "JPEG header beyond the inspection prefix";
    case jpeg_status::unsupported_variant:
        return "unsupported JPEG variant";
    case jpeg_status::unsupported_precision:
        return "unsupported JPEG precision";
    case jpeg_status::unsupported_components:
        return "unsupported JPEG component count";
    case jpeg_status::unsupported_sampling:
        return "unsupported JPEG chroma sampling";
    case jpeg_status::invalid_dimension:
        return "invalid JPEG dimensions";
    case jpeg_status::invalid_segment:
        return "invalid JPEG segment";
    case jpeg_status::truncated_segment:
        return "truncated JPEG segment";
    case jpeg_status::too_many_segments:
        return "too many JPEG segments";
    case jpeg_status::missing_scan:
        return "missing JPEG scan";
    case jpeg_status::multiple_scans:
        return "multiple JPEG scans";
    case jpeg_status::missing_eoi:
        return "missing JPEG end of image";
    case jpeg_status::read_failed:
        return "JPEG read failed";
    case jpeg_status::decode_failed:
        break;
    }
    return "JPEG decode failed";
}

std::size_t jpeg_decode_peak_bytes(const jpeg_info &info) {
    if (info.frame_bytes == 0 || info.frame_bytes > k_max_frame_bytes)
        return 0;
    const std::size_t frame = static_cast<std::size_t>(info.frame_bytes);
    /* One block carries the frame and the entropy pool; keep the 4-byte
     * alignment the pool allocator expects. */
    const std::size_t pool_offset = (frame + 3u) & ~static_cast<std::size_t>(3u);
    if (pool_offset + k_jpeg_pool_bytes < pool_offset)
        return 0;
    return pool_offset + k_jpeg_pool_bytes;
}

jpeg_status inspect_jpeg(const std::uint8_t *prefix, std::size_t prefix_bytes, std::uint64_t file_bytes,
                         jpeg_info &out) {
    out = jpeg_info{};
    if (prefix == nullptr || prefix_bytes == 0 || file_bytes == 0)
        return jpeg_status::header_truncated;
    const source from{prefix, file_bytes, prefix_bytes, true, prefix_read};
    std::uint64_t scan_offset = 0;
    return walk_markers(from, out, scan_offset);
}

decode_result decode_jpeg(const decode_ops &ops, int descriptor, std::size_t file_bytes, decoded_image &out) {
    out = decoded_image{};
    if (descriptor < 0 || ops.read_at == nullptr || ops.allocate == nullptr || ops.deallocate == nullptr)
        return {decode_status::invalid_argument, bmp_status::ok, decode_status_reason(decode_status::invalid_argument)};
    /* NEG-IMGF-CANCEL-01: a request cancelled from the start must not parse,
     * read or allocate - the caller already gave up. */
    if (ops.cancelled != nullptr && ops.cancelled(ops.context))
        return {decode_status::cancelled, bmp_status::ok, decode_status_reason(decode_status::cancelled)};
    if (file_bytes == 0 || file_bytes > k_max_file_bytes)
        return {decode_status::too_large, bmp_status::ok, decode_status_reason(decode_status::too_large)};

    const descriptor_source reader{&ops, descriptor};
    const source from{&reader, file_bytes, file_bytes, false, descriptor_read};
    jpeg_info info{};
    std::uint64_t scan_offset = 0;
    if (const jpeg_status walked = walk_markers(from, info, scan_offset); walked != jpeg_status::ok)
        return format_result(walked);

    std::size_t frame_bytes_exact = 0;
    if (!frame_bytes(info.width, info.height, frame_bytes_exact))
        return format_result(jpeg_status::invalid_dimension);
    const std::size_t pool_offset = (frame_bytes_exact + 3u) & ~static_cast<std::size_t>(3u);
    const std::size_t total = pool_offset + k_jpeg_pool_bytes;
    if (total < pool_offset)
        return {decode_status::too_large, bmp_status::ok, decode_status_reason(decode_status::too_large)};

    void *memory = nullptr;
    if (!ops.allocate(ops.context, total, memory) || memory == nullptr)
        return allocation_failed_result();

    auto *pixels = static_cast<std::uint16_t *>(memory);
    auto *pool = static_cast<std::uint8_t *>(memory) + pool_offset;

    stream_state state{};
    state.ops = &ops;
    state.descriptor = descriptor;
    state.file_bytes = file_bytes;
    state.pixels = pixels;
    state.width = info.width;

    JDEC decoder{};
    JRESULT result = jd_prepare(&decoder, jpeg_input, pool, k_jpeg_pool_bytes, &state);
    if (result == JDR_OK)
        result = jd_decomp(&decoder, jpeg_output, 0);
    if (result != JDR_OK) {
        ops.deallocate(ops.context, memory);
        if (state.cancelled || result == JDR_INTR)
            return cancelled_result();
        if (result == JDR_INP)
            return state.read_error ? read_failed_result() : format_result(jpeg_status::decode_failed);
        if (result == JDR_MEM1 || result == JDR_MEM2)
            return allocation_failed_result();
        if (result == JDR_PAR)
            return {decode_status::invalid_argument, bmp_status::ok,
                    decode_status_reason(decode_status::invalid_argument)};
        return format_result(jpeg_status::decode_failed);
    }

    /* The trailer is validated before publication so a second scan or a missing
     * EOI never produces a partial frame. */
    const jpeg_status trailer = scan_trailer(from, scan_offset, state.position, pool, k_jpeg_pool_bytes);
    if (trailer != jpeg_status::ok) {
        ops.deallocate(ops.context, memory);
        return format_result(trailer);
    }

    out.pixels = pixels;
    out.pixel_bytes = frame_bytes_exact;
    out.width = info.width;
    out.height = info.height;
    return {decode_status::ok, bmp_status::ok, jpeg_status_reason(jpeg_status::ok)};
}

} // namespace cyberdeck_image
