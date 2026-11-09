#include "apps/image/cyberdeck_image_png.h"

#include "cyberdeck_image_source.h"
/* Before miniz.h: the prefix keeps the vendored inflater bound on the target
 * instead of the ESP32-P4 ROM copy (see cyberdeck_miniz_names.h). */
#include "third_party/miniz/cyberdeck_miniz_names.h"
#include "third_party/miniz/miniz.h"

#include <cstring>

namespace cyberdeck_image {
using detail::big_endian32;
using detail::descriptor_read;
using detail::descriptor_source;
using detail::prefix_read;
using detail::read_bytes;
using detail::read_status;
using detail::source;

namespace {

static_assert(k_png_window_bytes == TINFL_LZ_DICT_SIZE, "the inflate window must match the tinfl dictionary");
static_assert(sizeof(tinfl_decompressor) <= k_png_inflate_state_bytes,
              "k_png_inflate_state_bytes must cover the vendored inflater state");

constexpr std::uint8_t k_signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
constexpr std::size_t k_chunk_header_bytes = 8;
constexpr std::size_t k_chunk_crc_bytes = 4;
constexpr std::uint32_t k_ihdr_payload_bytes = 13;
/* PNG forbids a chunk length above 2^31-1. */
constexpr std::uint32_t k_max_chunk_bytes = 0x7FFFFFFFu;

constexpr std::size_t align4(std::size_t value) {
    return (value + 3u) & ~static_cast<std::size_t>(3u);
}

constexpr std::size_t align8(std::size_t value) {
    return (value + 7u) & ~static_cast<std::size_t>(7u);
}

std::size_t row_capacity(const png_info &info) {
    return align4(static_cast<std::size_t>(info.row_bytes) + 1u);
}

bool type_is(const std::uint8_t *type, const char *literal) {
    return std::memcmp(type, literal, 4) == 0;
}

bool is_critical(const std::uint8_t *type) {
    return (type[0] & 0x20u) == 0;
}

png_status read_failure_status(read_status status, bool header_seen) {
    switch (status) {
    case read_status::ok:
        return png_status::ok;
    case read_status::out_of_file:
        return header_seen ? png_status::truncated_segment : png_status::header_truncated;
    case read_status::short_prefix:
        return png_status::prefix_incomplete;
    case read_status::io_error:
        break;
    }
    return png_status::read_failed;
}

/* Receives the parts of the stream that only `decode_png` can act on.  Every
 * callback returns `ok` to continue or the status that aborts the walk. */
struct png_consumer {
    void *context = nullptr;
    png_status (*begin)(void *context, const png_info &info) = nullptr;
    png_status (*palette)(void *context, std::size_t offset, const std::uint8_t *bytes, std::size_t length) = nullptr;
    png_status (*data)(void *context, const std::uint8_t *bytes, std::size_t length) = nullptr;
    png_status (*end)(void *context) = nullptr;
};

struct chunk_walk {
    const source &from;
    const png_consumer *consumer = nullptr;
    png_info &out;
    /* `header_only` stops at the first IDAT chunk: a prefix can prove the
     * header (IHDR/PLTE) but never the trailer, so `inspect_png` must not claim
     * to have validated IEND.  `decode_png` walks the whole file. */
    bool header_only = false;
    bool header_seen = false;
    bool palette_seen = false;
    bool image_data_seen = false;
    bool image_data_closed = false;
    std::uint8_t piece[k_png_piece_bytes]{};

    png_status close_image_data() {
        if (!image_data_seen || image_data_closed)
            return png_status::ok;
        image_data_closed = true;
        if (consumer != nullptr && consumer->end != nullptr)
            return consumer->end(consumer->context);
        return png_status::ok;
    }

    png_status check_crc(std::uint64_t crc_offset, std::uint32_t computed) {
        std::uint8_t stored[k_chunk_crc_bytes]{};
        if (const read_status status = read_bytes(from, stored, sizeof(stored), crc_offset); status != read_status::ok)
            return read_failure_status(status, true);
        return big_endian32(stored) == computed ? png_status::ok : png_status::checksum_mismatch;
    }
    png_status read_header(std::uint64_t data_offset, std::uint32_t length);
    png_status read_palette(std::uint64_t data_offset, std::uint32_t length);
    png_status read_image_data(std::uint64_t data_offset, std::uint32_t length, const std::uint8_t *type);
    png_status walk();
};

png_status chunk_walk::read_header(std::uint64_t data_offset, std::uint32_t length) {
    if (header_seen || length != k_ihdr_payload_bytes)
        return png_status::invalid_header;
    std::uint8_t header[k_ihdr_payload_bytes]{};
    if (const read_status status = read_bytes(from, header, sizeof(header), data_offset); status != read_status::ok)
        return read_failure_status(status, false);

    std::uint32_t crc = mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const std::uint8_t *>("IHDR"), 4);
    crc = mz_crc32(crc, header, sizeof(header));
    if (const png_status status = check_crc(data_offset + length, crc); status != png_status::ok)
        return status;

    const std::uint32_t width = big_endian32(&header[0]);
    const std::uint32_t height = big_endian32(&header[4]);
    const std::uint8_t bit_depth = header[8];
    const std::uint8_t color_type = header[9];
    const std::uint8_t compression = header[10];
    const std::uint8_t filter_method = header[11];
    const std::uint8_t interlace = header[12];

    if (bit_depth != 8)
        return png_status::unsupported_bit_depth;
    if (color_type == 4 || color_type == 6)
        return png_status::unsupported_alpha;
    if (color_type != 0 && color_type != 2 && color_type != 3)
        return png_status::unsupported_color_type;
    if (compression != 0)
        return png_status::unsupported_compression;
    if (filter_method != 0)
        return png_status::unsupported_filter_method;
    if (interlace != 0)
        return png_status::unsupported_interlace;

    std::size_t frame = 0;
    if (!frame_bytes(width, height, frame))
        return png_status::invalid_dimension;

    out.width = width;
    out.height = height;
    out.color_type = color_type;
    out.bytes_per_pixel = color_type == 2 ? 3 : 1;
    out.row_bytes = width * static_cast<std::uint32_t>(out.bytes_per_pixel);
    out.frame_bytes = frame;
    header_seen = true;

    if (consumer != nullptr && consumer->begin != nullptr)
        return consumer->begin(consumer->context, out);
    return png_status::ok;
}

png_status chunk_walk::read_palette(std::uint64_t data_offset, std::uint32_t length) {
    if (palette_seen || image_data_seen)
        return png_status::invalid_palette;
    if (length == 0 || (length % 3u) != 0 || length > k_png_palette_bytes)
        return png_status::invalid_palette;
    /* PLTE is meaningless (and forbidden by the specification) for grayscale. */
    if (out.color_type == 0)
        return png_status::invalid_palette;

    std::uint32_t crc = mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const std::uint8_t *>("PLTE"), 4);
    for (std::uint32_t done = 0; done < length;) {
        const std::uint32_t take = (length - done) < k_png_piece_bytes ? (length - done) : k_png_piece_bytes;
        if (const read_status status = read_bytes(from, piece, take, data_offset + done); status != read_status::ok)
            return read_failure_status(status, true);
        crc = mz_crc32(crc, piece, take);
        if (consumer != nullptr && consumer->palette != nullptr) {
            if (const png_status status = consumer->palette(consumer->context, done, piece, take);
                status != png_status::ok)
                return status;
        }
        done += take;
    }
    if (const png_status status = check_crc(data_offset + length, crc); status != png_status::ok)
        return status;
    out.palette_entries = static_cast<std::uint16_t>(length / 3u);
    palette_seen = true;
    return png_status::ok;
}

png_status chunk_walk::read_image_data(std::uint64_t data_offset, std::uint32_t length, const std::uint8_t *type) {
    if (image_data_closed)
        return png_status::invalid_header;
    if (out.color_type == 3 && !palette_seen)
        return png_status::invalid_palette;
    image_data_seen = true;
    if (header_only)
        return png_status::ok;
    if (consumer == nullptr || consumer->data == nullptr) {
        /* Inspection skips the payload: the CRC is only provable once the bytes
         * are streamed, which `decode_png` does. */
        return png_status::ok;
    }

    std::uint32_t crc = mz_crc32(MZ_CRC32_INIT, type, 4);
    for (std::uint32_t done = 0; done < length;) {
        const std::uint32_t take = (length - done) < k_png_piece_bytes ? (length - done) : k_png_piece_bytes;
        if (const read_status status = read_bytes(from, piece, take, data_offset + done); status != read_status::ok)
            return read_failure_status(status, true);
        crc = mz_crc32(crc, piece, take);
        if (const png_status status = consumer->data(consumer->context, piece, take); status != png_status::ok)
            return status;
        done += take;
    }
    return check_crc(data_offset + length, crc);
}

png_status chunk_walk::walk() {
    std::uint8_t signature[sizeof(k_signature)]{};
    if (const read_status status = read_bytes(from, signature, sizeof(signature), 0); status != read_status::ok)
        return read_failure_status(status, false);
    if (std::memcmp(signature, k_signature, sizeof(signature)) != 0)
        return png_status::invalid_signature;

    std::uint8_t header[k_chunk_header_bytes]{};
    std::uint64_t offset = sizeof(k_signature);
    for (std::size_t chunks = 0; chunks < k_png_max_chunks; ++chunks) {
        if (const read_status status = read_bytes(from, header, sizeof(header), offset); status != read_status::ok)
            return read_failure_status(status, header_seen);

        const std::uint32_t length = big_endian32(header);
        const std::uint8_t *type = header + 4;
        if (length > k_max_chunk_bytes)
            return png_status::invalid_header;
        const std::uint64_t data_offset = offset + k_chunk_header_bytes;
        if (data_offset > from.file_bytes || length > from.file_bytes - data_offset)
            return png_status::truncated_segment;
        const std::uint64_t crc_offset = data_offset + length;
        if (crc_offset > from.file_bytes || k_chunk_crc_bytes > from.file_bytes - crc_offset)
            return png_status::truncated_segment;
        offset = crc_offset + k_chunk_crc_bytes;

        if (!header_seen && !type_is(type, "IHDR"))
            return png_status::invalid_header;
        if (type_is(type, "IHDR")) {
            if (const png_status status = read_header(data_offset, length); status != png_status::ok)
                return status;
            continue;
        }
        if (type_is(type, "PLTE")) {
            if (const png_status status = read_palette(data_offset, length); status != png_status::ok)
                return status;
            continue;
        }
        if (type_is(type, "tRNS"))
            return png_status::unsupported_alpha;
        if (type_is(type, "IDAT")) {
            if (const png_status status = read_image_data(data_offset, length, type); status != png_status::ok)
                return status;
            if (header_only)
                return png_status::ok;
            continue;
        }
        if (type_is(type, "IEND")) {
            if (length != 0)
                return png_status::invalid_header;
            std::uint32_t crc = mz_crc32(MZ_CRC32_INIT, type, 4);
            if (const png_status status = check_crc(crc_offset, crc); status != png_status::ok)
                return status;
            if (!image_data_seen)
                return png_status::missing_image_data;
            if (const png_status status = close_image_data(); status != png_status::ok)
                return status;
            return png_status::ok;
        }
        if (is_critical(type))
            return png_status::unsupported_critical_chunk;
        if (const png_status status = close_image_data(); status != png_status::ok)
            return status;
    }
    return png_status::too_many_chunks;
}

/* ---- decode ---- */

struct png_decode_state {
    const decode_ops *ops = nullptr;
    int descriptor = -1;
    png_info info{};
    std::uint8_t *block = nullptr;
    std::uint8_t *window = nullptr;
    std::uint8_t *previous = nullptr;
    std::uint8_t *current = nullptr;
    std::uint8_t *palette = nullptr;
    std::uint8_t *input = nullptr;
    std::size_t input_length = 0;
    std::uint16_t *pixels = nullptr;
    /* 8.4 KiB: far too large for the worker stack, so it lives in the block. */
    tinfl_decompressor *inflator = nullptr;
    std::size_t window_offset = 0;
    std::uint64_t produced = 0;
    std::uint64_t expected = 0;
    std::size_t filled = 0;
    std::uint32_t row = 0;
    bool finished = false;
};

std::uint16_t rgb565(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(red & 0xF8u) << 8) |
                                      (static_cast<std::uint16_t>(green & 0xFCu) << 3) |
                                      static_cast<std::uint16_t>(blue >> 3));
}

std::uint8_t paeth(std::uint8_t left, std::uint8_t up, std::uint8_t upper_left) {
    const int base = static_cast<int>(left) + static_cast<int>(up) - static_cast<int>(upper_left);
    const int to_left = base > left ? base - left : left - base;
    const int to_up = base > up ? base - up : up - base;
    const int to_corner = base > upper_left ? base - upper_left : upper_left - base;
    if (to_left <= to_up && to_left <= to_corner)
        return left;
    if (to_up <= to_corner)
        return up;
    return upper_left;
}

bool unfilter_row(std::uint8_t filter, std::uint8_t *row, const std::uint8_t *prior, std::size_t length,
                  std::size_t bytes_per_pixel) {
    switch (filter) {
    case 0:
        return true;
    case 1:
        for (std::size_t index = bytes_per_pixel; index < length; ++index)
            row[index] = static_cast<std::uint8_t>(row[index] + row[index - bytes_per_pixel]);
        return true;
    case 2:
        for (std::size_t index = 0; index < length; ++index)
            row[index] = static_cast<std::uint8_t>(row[index] + prior[index]);
        return true;
    case 3:
        for (std::size_t index = 0; index < length; ++index) {
            const std::uint32_t left = index >= bytes_per_pixel ? row[index - bytes_per_pixel] : 0;
            row[index] = static_cast<std::uint8_t>(row[index] + ((left + prior[index]) >> 1));
        }
        return true;
    case 4:
        for (std::size_t index = 0; index < length; ++index) {
            const std::uint8_t left = index >= bytes_per_pixel ? row[index - bytes_per_pixel] : 0;
            const std::uint8_t corner = index >= bytes_per_pixel ? prior[index - bytes_per_pixel] : 0;
            row[index] = static_cast<std::uint8_t>(row[index] + paeth(left, prior[index], corner));
        }
        return true;
    default:
        return false;
    }
}

bool convert_row(const png_decode_state &state, const std::uint8_t *row, std::uint16_t *target) {
    const std::uint32_t width = state.info.width;
    if (state.info.color_type == 0) {
        for (std::uint32_t column = 0; column < width; ++column) {
            const std::uint8_t value = row[column];
            target[column] = rgb565(value, value, value);
        }
        return true;
    }
    if (state.info.color_type == 2) {
        for (std::uint32_t column = 0; column < width; ++column) {
            const std::uint8_t *pixel = row + (static_cast<std::size_t>(column) * 3u);
            target[column] = rgb565(pixel[0], pixel[1], pixel[2]);
        }
        return true;
    }
    if (state.info.color_type == 3) {
        for (std::uint32_t column = 0; column < width; ++column) {
            const std::uint8_t index = row[column];
            if (index >= state.info.palette_entries)
                return false;
            const std::uint8_t *entry = state.palette + (static_cast<std::size_t>(index) * 3u);
            target[column] = rgb565(entry[0], entry[1], entry[2]);
        }
        return true;
    }
    return false;
}

png_status finish_row(png_decode_state &state) {
    if (state.ops->cancelled != nullptr && state.ops->cancelled(state.ops->context))
        return png_status::cancelled;
    const std::uint8_t filter = state.current[0];
    std::uint8_t *row = state.current + 1;
    if (!unfilter_row(filter, row, state.previous + 1, state.info.row_bytes, state.info.bytes_per_pixel))
        return png_status::invalid_filter;
    std::uint16_t *target = state.pixels + (static_cast<std::size_t>(state.row) * state.info.width);
    if (!convert_row(state, row, target))
        return png_status::invalid_palette_entry;
    /* The next row filters against this one; `previous[0]` is never read. */
    std::memcpy(state.previous + 1, row, state.info.row_bytes);
    ++state.row;
    return png_status::ok;
}

png_status consume_output(png_decode_state &state, const std::uint8_t *bytes, std::size_t length) {
    const std::size_t capacity = static_cast<std::size_t>(state.info.row_bytes) + 1u;
    for (std::size_t index = 0; index < length; ++index) {
        if (state.produced >= state.expected)
            return png_status::excessive_data;
        state.current[state.filled++] = bytes[index];
        ++state.produced;
        if (state.filled == capacity) {
            if (const png_status status = finish_row(state); status != png_status::ok)
                return status;
            state.filled = 0;
        }
    }
    return png_status::ok;
}

/* tinfl may return NEEDS_MORE_INPUT without consuming every byte it was given
 * (its bit-buffer refill wants a handful more), so the compressed bytes are
 * staged and the remainder is kept for the next round instead of being dropped.
 * The staging area is part of the heap scratch, never a large stack frame. */
png_status drain(png_decode_state &state) {
    constexpr mz_uint32 flags = TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_HAS_MORE_INPUT;
    std::size_t consumed = 0;
    while (consumed < state.input_length) {
        if (state.finished)
            return png_status::ok; /* trailing compressed bytes carry no pixels */
        /* tinfl reports the *consumed* count in `in_available` on return, not the
         * remainder; treating it as a remainder silently drops compressed bytes. */
        std::size_t in_available = state.input_length - consumed;
        std::size_t out_available = k_png_window_bytes - state.window_offset;
        const tinfl_status status =
            tinfl_decompress(state.inflator, state.input + consumed, &in_available, state.window,
                             state.window + state.window_offset, &out_available, flags);
        const std::size_t consumed_now = in_available;
        consumed += consumed_now;

        if (out_available != 0) {
            if (const png_status guarded = consume_output(state, state.window + state.window_offset, out_available);
                guarded != png_status::ok)
                return guarded;
            state.window_offset = (state.window_offset + out_available) & (k_png_window_bytes - 1u);
        }
        if (status == TINFL_STATUS_DONE) {
            state.finished = true;
            return png_status::ok;
        }
        if (status == TINFL_STATUS_NEEDS_MORE_INPUT)
            break;
        if (status == TINFL_STATUS_HAS_MORE_OUTPUT) {
            /* Neither input nor the sliding window moved: refuse to spin. */
            if (consumed_now == 0 && out_available == 0)
                return png_status::decode_failed;
            continue;
        }
        if (status == TINFL_STATUS_ADLER32_MISMATCH)
            return png_status::checksum_mismatch;
        return png_status::decode_failed;
    }

    if (consumed != 0) {
        const std::size_t remaining = state.input_length - consumed;
        if (remaining != 0)
            std::memmove(state.input, state.input + consumed, remaining);
        state.input_length = remaining;
    }
    return png_status::ok;
}

png_status stage(png_decode_state &state, const std::uint8_t *bytes, std::size_t length) {
    std::size_t offset = 0;
    while (offset < length) {
        if (state.input_length == k_png_staging_bytes) {
            if (const png_status status = drain(state); status != png_status::ok)
                return status;
            if (state.input_length == k_png_staging_bytes)
                return png_status::decode_failed; /* no progress with a full stage */
        }
        const std::size_t room = k_png_staging_bytes - state.input_length;
        const std::size_t take = room < (length - offset) ? room : (length - offset);
        std::memcpy(state.input + state.input_length, bytes + offset, take);
        state.input_length += take;
        offset += take;
        if (const png_status status = drain(state); status != png_status::ok)
            return status;
    }
    return png_status::ok;
}

png_status decode_begin(void *context, const png_info &info) {
    auto *state = static_cast<png_decode_state *>(context);
    state->info = info;
    const std::size_t frame = static_cast<std::size_t>(info.frame_bytes);
    const std::size_t frame_offset = align8(frame);
    const std::size_t scratch = png_scratch_bytes(info);
    if (scratch == 0)
        return png_status::allocation_failed;
    const std::size_t total = frame_offset + scratch;
    if (total < frame_offset)
        return png_status::allocation_failed;
    void *memory = nullptr;
    if (!state->ops->allocate(state->ops->context, total, memory) || memory == nullptr)
        return png_status::allocation_failed;

    state->block = static_cast<std::uint8_t *>(memory);
    state->pixels = static_cast<std::uint16_t *>(memory);
    std::uint8_t *cursor = state->block + frame_offset;
    state->window = cursor;
    cursor += k_png_window_bytes;
    state->inflator = reinterpret_cast<tinfl_decompressor *>(cursor);
    cursor += k_png_inflate_state_bytes;
    state->previous = cursor;
    cursor += row_capacity(info);
    state->current = cursor;
    cursor += row_capacity(info);
    state->palette = cursor;
    cursor += k_png_palette_bytes;
    state->input = cursor;

    /* miniz reads the dictionary as a ring and expects it zeroed before the
     * first byte is written (its own helper memsets the dict). */
    std::memset(state->window, 0, k_png_window_bytes);
    std::memset(state->previous, 0, row_capacity(info));
    std::memset(state->current, 0, row_capacity(info));
    std::memset(state->palette, 0, k_png_palette_bytes);
    tinfl_init(state->inflator);
    state->expected = static_cast<std::uint64_t>(info.height) * (static_cast<std::uint64_t>(info.row_bytes) + 1u);
    return png_status::ok;
}

png_status decode_palette(void *context, std::size_t offset, const std::uint8_t *bytes, std::size_t length) {
    auto *state = static_cast<png_decode_state *>(context);
    if (state->palette == nullptr || offset > k_png_palette_bytes || length > k_png_palette_bytes - offset)
        return png_status::invalid_palette;
    std::memcpy(state->palette + offset, bytes, length);
    /* The palette arrives after IHDR, so the entry count is only known here; the
     * rows are converted later, during IDAT. */
    const std::size_t entries = (offset + length) / 3u;
    if (entries > k_png_max_palette_entries)
        return png_status::invalid_palette;
    state->info.palette_entries = static_cast<std::uint16_t>(entries);
    return png_status::ok;
}

png_status decode_data(void *context, const std::uint8_t *bytes, std::size_t length) {
    return stage(*static_cast<png_decode_state *>(context), bytes, length);
}

png_status decode_end(void *context) {
    auto *state = static_cast<png_decode_state *>(context);
    if (!state->finished || state->produced != state->expected)
        return png_status::truncated_image;
    if (state->filled != 0 || state->row != state->info.height)
        return png_status::truncated_image;
    return png_status::ok;
}

decode_result png_result(png_status status) {
    decode_status state = decode_status::format;
    switch (status) {
    case png_status::read_failed:
        state = decode_status::read_failed;
        break;
    case png_status::allocation_failed:
        state = decode_status::allocation_failed;
        break;
    case png_status::cancelled:
        state = decode_status::cancelled;
        break;
    default:
        break;
    }
    return {state, bmp_status::ok, png_status_reason(status)};
}

} // namespace

const char *png_status_reason(png_status status) {
    switch (status) {
    case png_status::ok:
        return "ok";
    case png_status::invalid_signature:
        return "invalid PNG signature";
    case png_status::header_truncated:
        return "truncated PNG header";
    case png_status::prefix_incomplete:
        return "PNG header beyond the inspection prefix";
    case png_status::invalid_header:
        return "invalid PNG header";
    case png_status::unsupported_bit_depth:
        return "unsupported PNG bit depth";
    case png_status::unsupported_color_type:
        return "unsupported PNG color type";
    case png_status::unsupported_alpha:
        return "unsupported PNG transparency";
    case png_status::unsupported_interlace:
        return "unsupported PNG interlace";
    case png_status::unsupported_compression:
        return "unsupported PNG compression method";
    case png_status::unsupported_filter_method:
        return "unsupported PNG filter method";
    case png_status::unsupported_critical_chunk:
        return "unsupported PNG critical chunk";
    case png_status::invalid_dimension:
        return "invalid PNG dimensions";
    case png_status::invalid_palette:
        return "invalid PNG palette";
    case png_status::invalid_palette_entry:
        return "PNG palette index out of range";
    case png_status::checksum_mismatch:
        return "PNG checksum mismatch";
    case png_status::truncated_segment:
        return "truncated PNG chunk";
    case png_status::missing_image_data:
        return "missing PNG image data";
    case png_status::invalid_filter:
        return "invalid PNG row filter";
    case png_status::truncated_image:
        return "truncated PNG image";
    case png_status::excessive_data:
        return "PNG image data exceeds its geometry";
    case png_status::too_many_chunks:
        return "too many PNG chunks";
    case png_status::read_failed:
        return "PNG read failed";
    case png_status::allocation_failed:
        return "PNG allocation failed";
    case png_status::cancelled:
        return "PNG decode cancelled";
    case png_status::decode_failed:
        break;
    }
    return "PNG decode failed";
}

std::size_t png_scratch_bytes(const png_info &info) {
    if (info.row_bytes == 0 || info.height == 0 || info.frame_bytes == 0)
        return 0;
    const std::size_t inflator_offset = align8(k_png_window_bytes);
    const std::size_t rows = 2u * row_capacity(info);
    if (rows < row_capacity(info))
        return 0;
    return inflator_offset + k_png_inflate_state_bytes + rows + k_png_palette_bytes + k_png_staging_bytes;
}

std::size_t png_decode_peak_bytes(const png_info &info) {
    const std::size_t frame = static_cast<std::size_t>(info.frame_bytes);
    const std::size_t scratch = png_scratch_bytes(info);
    if (frame == 0 || scratch == 0)
        return 0;
    const std::size_t frame_offset = align8(frame);
    if (frame_offset + scratch < frame_offset)
        return 0;
    return frame_offset + scratch;
}

png_status inspect_png(const std::uint8_t *prefix, std::size_t prefix_bytes, std::uint64_t file_bytes, png_info &out) {
    out = png_info{};
    if (prefix == nullptr || prefix_bytes == 0 || file_bytes == 0)
        return png_status::header_truncated;
    const source from{prefix, file_bytes, prefix_bytes, true, prefix_read};
    chunk_walk walk{from, nullptr, out};
    walk.header_only = true;
    return walk.walk();
}

decode_result decode_png(const decode_ops &ops, int descriptor, std::size_t file_bytes, decoded_image &out) {
    out = decoded_image{};
    if (descriptor < 0 || ops.read_at == nullptr || ops.allocate == nullptr || ops.deallocate == nullptr)
        return {decode_status::invalid_argument, bmp_status::ok, decode_status_reason(decode_status::invalid_argument)};
    /* NEG-IMGF-CANCEL-01: a request cancelled from the start must not parse,
     * read or allocate - the caller already gave up. */
    if (ops.cancelled != nullptr && ops.cancelled(ops.context))
        return {decode_status::cancelled, bmp_status::ok, decode_status_reason(decode_status::cancelled)};
    if (file_bytes == 0 || file_bytes > k_max_file_bytes)
        return {decode_status::too_large, bmp_status::ok, decode_status_reason(decode_status::too_large)};

    png_decode_state state{};
    state.ops = &ops;
    state.descriptor = descriptor;
    const png_consumer consumer{&state, decode_begin, decode_palette, decode_data, decode_end};
    const descriptor_source reader{&ops, descriptor};
    const source from{&reader, file_bytes, file_bytes, false, descriptor_read};

    png_info info{};
    chunk_walk walk{from, &consumer, info};
    const png_status walked = walk.walk();
    if (walked != png_status::ok) {
        if (state.block != nullptr)
            ops.deallocate(ops.context, state.block);
        return png_result(walked);
    }

    out.pixels = state.pixels;
    out.pixel_bytes = static_cast<std::size_t>(info.frame_bytes);
    out.width = info.width;
    out.height = info.height;
    return {decode_status::ok, bmp_status::ok, png_status_reason(png_status::ok)};
}

} // namespace cyberdeck_image
