/*
 * Host test for the streaming PNG decoder of the isolated image viewer
 * (REQ-IMGF-03, TEST-IMGF-PNG-01..10, NEG-IMGF-PNG-01..22).
 *
 * The fixtures are committed files generated once by
 * `tools/gen_image_fixtures.py` (manifest with sha256 next to them); the test
 * never re-encodes.  Positive oracles are exact: PNG is lossless, so the
 * expected RGB565 value of a pixel is derived here from the colour the fixture
 * was built with.  Negative cases are single-byte mutations of a valid fixture
 * or a dedicated fixture, never a weakened assertion.
 */
#include "apps/image/cyberdeck_image_png.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(condition)) {                                                                                            \
            ++failures;                                                                                                \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                           \
        }                                                                                                              \
    } while (false)

#define CHECK_EQ(actual, expected)                                                                                     \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if ((actual) != (expected)) {                                                                                  \
            ++failures;                                                                                                \
            std::printf("FAIL %s:%d: %s (actual=%ld, expected=%ld)\n", __FILE__, __LINE__, #actual,                    \
                        static_cast<long>(actual), static_cast<long>(expected));                                       \
        }                                                                                                              \
    } while (false)

namespace image = cyberdeck_image;

std::string fixture_directory() {
    const std::string source = __FILE__;
    const auto slash = source.find_last_of('/');
    const std::string prefix = slash == std::string::npos ? std::string{} : source.substr(0, slash + 1);
    return prefix + "fixtures/image/";
}

bool read_file(const std::string &path, std::vector<std::uint8_t> &out) {
    FILE *handle = std::fopen(path.c_str(), "rb");
    if (handle == nullptr)
        return false;
    std::uint8_t chunk[4096];
    for (;;) {
        const size_t count = std::fread(chunk, 1, sizeof(chunk), handle);
        out.insert(out.end(), chunk, chunk + count);
        if (count < sizeof(chunk))
            break;
    }
    (void)std::fclose(handle);
    return !out.empty();
}

std::vector<std::uint8_t> fixture(const char *name) {
    std::vector<std::uint8_t> bytes;
    if (!read_file(fixture_directory() + name, bytes)) {
        ++failures;
        ++checks;
        std::printf("FAIL missing fixture %s\n", name);
    }
    return bytes;
}

/* The RGB565 packing of the LVGL frame, derived from the specification. */
std::uint16_t rgb565(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(red >> 3) << 11) |
                                      (static_cast<std::uint16_t>(green >> 2) << 5) |
                                      static_cast<std::uint16_t>(blue >> 3));
}

/* ---- allocation/read/cancel witness ---- */
std::size_t g_allocations = 0;
std::size_t g_freed = 0;
std::size_t g_largest = 0;
bool g_refuse = false;
std::size_t g_fail_from = 0;
std::size_t g_cancel_after_reads = 0;
std::size_t g_reads = 0;
bool g_cancel_immediately = false;

bool witness_allocate(void *, std::size_t bytes, void *&memory) {
    if (g_refuse) {
        memory = nullptr;
        return false;
    }
    memory = std::malloc(bytes);
    if (memory != nullptr) {
        ++g_allocations;
        if (bytes > g_largest)
            g_largest = bytes;
    }
    return memory != nullptr;
}

void witness_deallocate(void *, void *memory) {
    if (memory != nullptr)
        ++g_freed;
    std::free(memory);
}

bool witness_read_at(void *context, int descriptor, std::uint8_t *buffer, std::size_t bytes, std::uint64_t offset) {
    ++g_reads;
    if (g_fail_from != 0 && offset >= g_fail_from)
        return false;
    return image::default_decode_ops().read_at(context, descriptor, buffer, bytes, offset);
}

bool witness_cancelled(void *) {
    if (g_cancel_immediately)
        return true;
    return g_cancel_after_reads != 0 && g_reads >= g_cancel_after_reads;
}

image::decode_ops witness_ops() {
    image::decode_ops ops = image::default_decode_ops();
    ops.allocate = witness_allocate;
    ops.deallocate = witness_deallocate;
    ops.read_at = witness_read_at;
    ops.cancelled = witness_cancelled;
    return ops;
}

void reset_witness() {
    g_allocations = 0;
    g_freed = 0;
    g_largest = 0;
    g_refuse = false;
    g_fail_from = 0;
    g_cancel_after_reads = 0;
    g_cancel_immediately = false;
    g_reads = 0;
}

int open_bytes(const std::vector<std::uint8_t> &bytes) {
    char pattern[] = "/tmp/cyberdeck-png-XXXXXX";
    const int descriptor = mkstemp(pattern);
    if (descriptor < 0)
        return -1;
    std::size_t done = 0;
    while (done < bytes.size()) {
        const ssize_t count = ::write(descriptor, bytes.data() + done, bytes.size() - done);
        if (count <= 0) {
            (void)::close(descriptor);
            return -1;
        }
        done += static_cast<std::size_t>(count);
    }
    (void)::unlink(pattern);
    return descriptor;
}

struct decode_outcome {
    image::decode_result result{};
    image::decoded_image image{};
};

decode_outcome decode_bytes(const std::vector<std::uint8_t> &bytes) {
    decode_outcome outcome{};
    const int descriptor = open_bytes(bytes);
    if (descriptor < 0) {
        outcome.result = {image::decode_status::read_failed, image::bmp_status::ok, "open failed"};
        return outcome;
    }
    outcome.result =
        image::decode_descriptor(witness_ops(), descriptor, bytes.size(), image::image_format::png, outcome.image);
    (void)::close(descriptor);
    return outcome;
}

decode_outcome decode_file(const char *name) {
    return decode_bytes(fixture(name));
}

void release(decode_outcome &outcome) {
    image::release_decoded(outcome.image, witness_ops());
}

std::uint16_t pixel_at(const image::decoded_image &decoded, std::uint32_t x, std::uint32_t y) {
    return decoded.pixels[static_cast<std::size_t>(y) * decoded.width + x];
}

/* ---- minimal independent chunk locator ---- */
struct chunk_view {
    std::size_t type = 0;
    std::size_t data = 0;
    std::size_t crc = 0;
    std::uint32_t length = 0;
};

bool find_chunk(const std::vector<std::uint8_t> &bytes, const char *type, chunk_view &out) {
    std::size_t offset = 8;
    while (offset + 12 <= bytes.size()) {
        const std::uint32_t length =
            (static_cast<std::uint32_t>(bytes[offset]) << 24) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) | static_cast<std::uint32_t>(bytes[offset + 3]);
        if (std::memcmp(&bytes[offset + 4], type, 4) == 0) {
            out.type = offset + 4;
            out.data = offset + 8;
            out.crc = offset + 8 + length;
            out.length = length;
            return true;
        }
        offset += 12 + length;
    }
    return false;
}

void mutate(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint8_t value) {
    CHECK(offset < bytes.size());
    if (offset < bytes.size())
        bytes[offset] = value;
}

/* ---- scenarios ---- */

void test_truecolour_exact_pixels() {
    reset_witness();
    decode_outcome outcome = decode_file("png_rgb_4x4.png");
    CHECK_EQ(outcome.result.state, image::decode_status::ok);
    CHECK_EQ(outcome.image.width, 4u);
    CHECK_EQ(outcome.image.height, 4u);
    CHECK_EQ(outcome.image.pixel_bytes, 32u);
    CHECK(outcome.image.pixels != nullptr);
    CHECK_EQ(g_allocations, 1u);
    if (outcome.image.pixels != nullptr) {
        /* Exactly the 4x4 colour grid the fixture was generated from. */
        const std::uint8_t grid[4][4][3] = {
            {{0xF8, 0, 0}, {0, 0xFC, 0}, {0, 0, 0xF8}, {0xF8, 0xFC, 0}},
            {{0, 0, 0}, {0x08, 0x04, 0x08}, {0x10, 0x08, 0x10}, {0xF8, 0xF8, 0xF8}},
            {{0x20, 0, 0}, {0, 0x20, 0}, {0, 0, 0x20}, {0x20, 0x20, 0x20}},
            {{0xF0, 0, 0}, {0, 0xF0, 0}, {0, 0, 0xF0}, {0xF0, 0xF0, 0xF0}},
        };
        for (std::uint32_t y = 0; y < 4; ++y) {
            for (std::uint32_t x = 0; x < 4; ++x) {
                const auto &colour = grid[y][x];
                CHECK_EQ(pixel_at(outcome.image, x, y), rgb565(colour[0], colour[1], colour[2]));
            }
        }
    }
    release(outcome);
    CHECK_EQ(g_freed, g_allocations);
}

void test_grayscale_and_palette() {
    reset_witness();
    decode_outcome gray = decode_file("png_gray_4x4.png");
    CHECK_EQ(gray.result.state, image::decode_status::ok);
    if (gray.image.pixels != nullptr) {
        const std::uint8_t values[4][4] = {
            {0x00, 0x40, 0x80, 0xC0}, {0x08, 0x48, 0x88, 0xC8}, {0x10, 0x50, 0x90, 0xD0}, {0x18, 0x58, 0x98, 0xD8}};
        for (std::uint32_t y = 0; y < 4; ++y)
            for (std::uint32_t x = 0; x < 4; ++x)
                CHECK_EQ(pixel_at(gray.image, x, y), rgb565(values[y][x], values[y][x], values[y][x]));
    }
    release(gray);

    reset_witness();
    decode_outcome palette = decode_file("png_palette_4x4.png");
    CHECK_EQ(palette.result.state, image::decode_status::ok);
    image::png_info info{};
    const auto bytes = fixture("png_palette_4x4.png");
    CHECK_EQ(image::inspect_png(bytes.data(), std::min(bytes.size(), image::k_png_prefix_bytes), bytes.size(), info),
             image::png_status::ok);
    CHECK_EQ(info.color_type, 3u);
    CHECK_EQ(info.palette_entries, 4u);
    if (palette.image.pixels != nullptr) {
        const std::uint16_t colours[4] = {rgb565(0xF8, 0, 0), rgb565(0, 0xFC, 0), rgb565(0, 0, 0xF8),
                                          rgb565(0xF8, 0xFC, 0xF8)};
        const std::uint8_t indices[4][4] = {{0, 1, 2, 3}, {3, 2, 1, 0}, {1, 1, 2, 2}, {0, 2, 1, 3}};
        for (std::uint32_t y = 0; y < 4; ++y)
            for (std::uint32_t x = 0; x < 4; ++x)
                CHECK_EQ(pixel_at(palette.image, x, y), colours[indices[y][x]]);
    }
    release(palette);
}

/* Rows 0..4 use filters 0..4 and rows 5..7 reuse Paeth: the decoded frame must
 * be exactly the unfiltered source, so a wrong predictor is visible. */
/* The 128x128 palette image that failed on the Tab5 with `PNG decode failed`:
 * four 32-row bands, one palette index each.  It also guards the device-only
 * failure class where a ROM `tinfl_decompress` replaced the vendored inflater. */
void test_palette_128_bands() {
    reset_witness();
    decode_outcome outcome = decode_file("png_palette_128.png");
    CHECK_EQ(outcome.result.state, image::decode_status::ok);
    CHECK_EQ(outcome.image.width, 128u);
    CHECK_EQ(outcome.image.height, 128u);
    CHECK_EQ(outcome.image.pixel_bytes, 128u * 128u * 2u);
    CHECK_EQ(g_allocations, 1u);
    if (outcome.image.pixels != nullptr) {
        const std::uint16_t bands[4] = {rgb565(0xF8, 0, 0), rgb565(0, 0xFC, 0), rgb565(0, 0, 0xF8),
                                        rgb565(0xF8, 0xFC, 0xF8)};
        for (std::uint32_t band = 0; band < 4; ++band) {
            const std::uint32_t first = band * 32;
            const std::uint32_t last = first + 31;
            for (std::uint32_t x : {0u, 63u, 127u}) {
                CHECK_EQ(pixel_at(outcome.image, x, first), bands[band]);
                CHECK_EQ(pixel_at(outcome.image, x, last), bands[band]);
            }
        }
        /* Row order is the display order: the palette bands must not be mirrored. */
        CHECK(pixel_at(outcome.image, 0, 0) != pixel_at(outcome.image, 0, 127));
    }
    release(outcome);
    CHECK_EQ(g_freed, g_allocations);
}

void test_all_row_filters() {
    reset_witness();
    decode_outcome outcome = decode_file("png_filters_8x8.png");
    CHECK_EQ(outcome.result.state, image::decode_status::ok);
    CHECK_EQ(outcome.image.width, 8u);
    CHECK_EQ(outcome.image.height, 8u);
    CHECK_EQ(outcome.image.pixel_bytes, 128u);
    if (outcome.image.pixels != nullptr) {
        for (std::uint32_t y = 0; y < 8; ++y) {
            const std::uint8_t red = static_cast<std::uint8_t>((y * 8) & 0xF8);
            const std::uint8_t green = static_cast<std::uint8_t>((y * 4) & 0xFC);
            const std::uint8_t blue = static_cast<std::uint8_t>((0xFF - ((y * 8) % 0x100)) & 0xF8);
            for (std::uint32_t x = 0; x < 8; ++x)
                CHECK_EQ(pixel_at(outcome.image, x, y), rgb565(red, green, blue));
        }
    }
    release(outcome);
}

void test_chunk_permutations_match_single_idat() {
    reset_witness();
    decode_outcome reference = decode_file("png_rgb_4x4.png");
    CHECK_EQ(reference.result.state, image::decode_status::ok);
    const std::vector<std::uint16_t> expected(
        reference.image.pixels, reference.image.pixels + (reference.image.width * reference.image.height));
    release(reference);

    const char *equivalent[] = {"png_multi_idat_4x4.png", "png_ancillary_4x4.png"};
    for (const char *name : equivalent) {
        reset_witness();
        decode_outcome outcome = decode_file(name);
        CHECK_EQ(outcome.result.state, image::decode_status::ok);
        CHECK_EQ(outcome.image.pixel_bytes, expected.size() * 2u);
        if (outcome.image.pixels != nullptr)
            CHECK(std::memcmp(outcome.image.pixels, expected.data(), expected.size() * 2u) == 0);
        release(outcome);
        CHECK_EQ(g_freed, g_allocations);
    }
}

/* TEST-IMGF-PNG-08 vs NEG-IMGF-PNG-18: the specification says the image ends at
 * IEND and a decoder must not process anything after it.  Trailing bytes are
 * therefore ignored, and a well-formed chunk placed after IEND is not read:
 * both cases keep the frame of the file that ends at IEND. */
void test_trailing_bytes_after_iend_are_ignored() {
    const auto base = fixture("png_rgb_4x4.png");
    reset_witness();
    decode_outcome reference = decode_bytes(base);
    CHECK_EQ(reference.result.state, image::decode_status::ok);
    const std::vector<std::uint16_t> expected(
        reference.image.pixels, reference.image.pixels + (reference.image.width * reference.image.height));
    release(reference);

    auto garbage = base;
    garbage.insert(garbage.end(), 1024, 0x5A);
    reset_witness();
    decode_outcome trailing = decode_bytes(garbage);
    CHECK_EQ(trailing.result.state, image::decode_status::ok);
    CHECK_EQ(trailing.image.pixel_bytes, expected.size() * 2u);
    if (trailing.image.pixels != nullptr)
        CHECK(std::memcmp(trailing.image.pixels, expected.data(), expected.size() * 2u) == 0);
    release(trailing);

    /* A second IDAT after IEND is someone else's trailing data: never decoded. */
    auto after_iend = base;
    const std::uint8_t extra_idat[] = {0x00, 0x00, 0x00, 0x02, 'I', 'D', 'A', 'T', 0x78, 0x9C, 0x00, 0x00, 0x00, 0x00};
    after_iend.insert(after_iend.end(), extra_idat, extra_idat + sizeof(extra_idat));
    reset_witness();
    decode_outcome ignored = decode_bytes(after_iend);
    CHECK_EQ(ignored.result.state, image::decode_status::ok);
    if (ignored.image.pixels != nullptr)
        CHECK(std::memcmp(ignored.image.pixels, expected.data(), expected.size() * 2u) == 0);
    release(ignored);
}

void test_small_and_maximum_geometry() {
    reset_witness();
    decode_outcome single = decode_file("png_1x1.png");
    CHECK_EQ(single.result.state, image::decode_status::ok);
    CHECK_EQ(single.image.pixel_bytes, 2u);
    if (single.image.pixels != nullptr)
        CHECK_EQ(single.image.pixels[0], rgb565(0xF8, 0, 0));
    release(single);

    const auto bytes = fixture("png_1280.png");
    image::png_info info{};
    CHECK_EQ(image::inspect_png(bytes.data(), std::min(bytes.size(), image::k_png_prefix_bytes), bytes.size(), info),
             image::png_status::ok);
    CHECK_EQ(info.width, image::k_max_dimension);
    CHECK_EQ(info.height, image::k_max_dimension);
    CHECK_EQ(info.row_bytes, image::k_max_dimension * 3u);
    const std::size_t scratch = image::png_scratch_bytes(info);
    CHECK(scratch >= image::k_png_window_bytes);
    CHECK(scratch < 64u * 1024u);
    const std::size_t peak = image::png_decode_peak_bytes(info);
    CHECK(peak > image::k_max_frame_bytes);
    CHECK(peak <= image::k_max_frame_bytes + 64u * 1024u + 8u);

    reset_witness();
    decode_outcome outcome = decode_bytes(bytes);
    CHECK_EQ(outcome.result.state, image::decode_status::ok);
    CHECK_EQ(outcome.image.pixel_bytes, image::k_max_frame_bytes);
    CHECK(g_largest <= peak);
    if (outcome.image.pixels != nullptr) {
        /* Vertical gradient: row 0 is black-ish, the last row is not. */
        CHECK(pixel_at(outcome.image, 0, 0) != pixel_at(outcome.image, 0, image::k_max_dimension - 1));
    }
    release(outcome);
    CHECK_EQ(g_freed, g_allocations);
}

/* Independent CRC-32 (bitwise, straight from the PNG specification) so a
 * negative can corrupt one header field while keeping the chunk valid. */
std::uint32_t crc32(const std::uint8_t *bytes, std::size_t length) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t index = 0; index < length; ++index) {
        crc ^= bytes[index];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1u) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return crc ^ 0xFFFFFFFFu;
}

void write_be32(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 24);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 16);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 8);
    bytes[offset + 3] = static_cast<std::uint8_t>(value);
}

/* IHDR of every fixture starts at 8: length(4) type(4) data(13) crc(4). */
constexpr std::size_t k_ihdr_data = 16;
constexpr std::size_t k_ihdr_crc = 29;

void repair_ihdr_crc(std::vector<std::uint8_t> &bytes) {
    write_be32(bytes, k_ihdr_crc, crc32(&bytes[k_ihdr_data - 4], 4 + 13));
}

/* Builds a well-formed chunk so a structural negative never depends on a
 * pre-recorded fixture that could drift from the assertion. */
std::vector<std::uint8_t> make_chunk(const char *type, const std::uint8_t *data, std::uint32_t length) {
    std::vector<std::uint8_t> chunk(12u + length, 0);
    write_be32(chunk, 0, length);
    std::memcpy(&chunk[4], type, 4);
    if (length != 0)
        std::memcpy(&chunk[8], data, length);
    std::vector<std::uint8_t> crc_input(4u + length, 0);
    std::memcpy(crc_input.data(), type, 4);
    if (length != 0)
        std::memcpy(crc_input.data() + 4, data, length);
    write_be32(chunk, 8u + length, crc32(crc_input.data(), crc_input.size()));
    return chunk;
}

/* Copy of one complete chunk (header, data, CRC) found by `find_chunk`. */
std::vector<std::uint8_t> chunk_copy(const std::vector<std::uint8_t> &bytes, const chunk_view &view) {
    const std::size_t start = view.type - 4;
    return std::vector<std::uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(start),
                                     bytes.begin() + static_cast<std::ptrdiff_t>(view.crc + 4));
}

/* NEG-IMGF-PNG-04/06/18/19/20: the chunk structure the specification cares
 * about, built from a valid fixture instead of weakening an existing case. */
void test_chunk_structure_and_iend() {
    const auto base = fixture("png_rgb_4x4.png");
    reset_witness();
    decode_outcome reference = decode_bytes(base);
    CHECK_EQ(reference.result.state, image::decode_status::ok);
    const std::vector<std::uint16_t> expected(
        reference.image.pixels, reference.image.pixels + (reference.image.width * reference.image.height));
    release(reference);

    /* PNG-04: an IDAT of length zero is legal and changes nothing. */
    {
        chunk_view idat{};
        CHECK(find_chunk(base, "IDAT", idat));
        if (idat.crc != 0) {
            auto bytes = base;
            const auto empty = make_chunk("IDAT", nullptr, 0);
            bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(idat.type - 4), empty.begin(), empty.end());
            reset_witness();
            decode_outcome outcome = decode_bytes(bytes);
            CHECK_EQ(outcome.result.state, image::decode_status::ok);
            if (outcome.image.pixels != nullptr)
                CHECK(std::memcmp(outcome.image.pixels, expected.data(), expected.size() * 2u) == 0);
            release(outcome);
            CHECK_EQ(g_freed, g_allocations);
        }
    }

    /* PNG-06 (tRNS part is D3 and rejected elsewhere): a PLTE is allowed on a
     * truecolor image as a suggested palette and must not change the pixels. */
    {
        const std::uint8_t palette[9] = {0xF8, 0, 0, 0, 0xFC, 0, 0, 0, 0xF8};
        const auto plte = make_chunk("PLTE", palette, sizeof(palette));
        auto bytes = base;
        bytes.insert(bytes.begin() + 33, plte.begin(), plte.end());
        reset_witness();
        decode_outcome outcome = decode_bytes(bytes);
        CHECK_EQ(outcome.result.state, image::decode_status::ok);
        if (outcome.image.pixels != nullptr)
            CHECK(std::memcmp(outcome.image.pixels, expected.data(), expected.size() * 2u) == 0);
        release(outcome);
        CHECK_EQ(g_freed, g_allocations);
    }

    /* NEG-IMGF-PNG-19: a length above 2^31-1 is refused as an invalid header;
     * a huge but legal length that runs past the file is truncated, never read. */
    {
        chunk_view idat{};
        CHECK(find_chunk(base, "IDAT", idat));
        if (idat.crc != 0) {
            auto too_big = base;
            write_be32(too_big, idat.type - 4, 0x80000000u);
            reset_witness();
            decode_outcome outcome = decode_bytes(too_big);
            CHECK_EQ(outcome.result.state, image::decode_status::format);
            CHECK_EQ(std::strcmp(outcome.result.reason, image::png_status_reason(image::png_status::invalid_header)),
                     0);
            CHECK(outcome.image.pixels == nullptr);
            release(outcome);

            auto past_end = base;
            write_be32(past_end, idat.type - 4, 0x7FFFFFFFu);
            reset_witness();
            decode_outcome truncated = decode_bytes(past_end);
            CHECK_EQ(truncated.result.state, image::decode_status::format);
            CHECK_EQ(
                std::strcmp(truncated.result.reason, image::png_status_reason(image::png_status::truncated_segment)),
                0);
            CHECK(truncated.image.pixels == nullptr);
            CHECK_EQ(g_freed, g_allocations);
            release(truncated);
        }
    }

    /* NEG-IMGF-PNG-18 first half: IEND is mandatory.  Removing it must fail
     * closed instead of publishing the rows that were already decoded. */
    {
        chunk_view iend{};
        CHECK(find_chunk(base, "IEND", iend));
        if (iend.crc != 0) {
            auto bytes = base;
            bytes.resize(iend.type - 4);
            reset_witness();
            decode_outcome outcome = decode_bytes(bytes);
            CHECK_EQ(outcome.result.state, image::decode_status::format);
            CHECK_EQ(std::strcmp(outcome.result.reason, image::png_status_reason(image::png_status::truncated_segment)),
                     0);
            CHECK(outcome.image.pixels == nullptr);
            release(outcome);
            CHECK_EQ(g_freed, g_allocations);
        }
    }

    /* IEND without any IDAT is a missing image, not an empty frame. */
    {
        chunk_view idat{};
        chunk_view iend{};
        CHECK(find_chunk(base, "IDAT", idat));
        CHECK(find_chunk(base, "IEND", iend));
        if (idat.crc != 0 && iend.crc != 0) {
            std::vector<std::uint8_t> bytes(base.begin(), base.begin() + static_cast<std::ptrdiff_t>(idat.type - 4));
            const auto end_chunk = chunk_copy(base, iend);
            bytes.insert(bytes.end(), end_chunk.begin(), end_chunk.end());
            reset_witness();
            decode_outcome outcome = decode_bytes(bytes);
            CHECK_EQ(outcome.result.state, image::decode_status::format);
            CHECK_EQ(
                std::strcmp(outcome.result.reason, image::png_status_reason(image::png_status::missing_image_data)), 0);
            CHECK(outcome.image.pixels == nullptr);
            release(outcome);
            CHECK_EQ(g_freed, g_allocations);
        }
    }

    /* NEG-IMGF-PNG-20: a chunk budget stops a file that would otherwise force an
     * unbounded walk; the declared cap comes from the production constant. */
    {
        const auto filler = make_chunk("tEXt", nullptr, 0);
        auto bytes = base;
        const std::size_t after_ihdr = 33;
        std::vector<std::uint8_t> bomb;
        bomb.reserve(filler.size() * image::k_png_max_chunks);
        for (std::size_t index = 0; index < image::k_png_max_chunks; ++index)
            bomb.insert(bomb.end(), filler.begin(), filler.end());
        bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(after_ihdr), bomb.begin(), bomb.end());
        reset_witness();
        decode_outcome outcome = decode_bytes(bytes);
        CHECK_EQ(outcome.result.state, image::decode_status::format);
        CHECK_EQ(std::strcmp(outcome.result.reason, image::png_status_reason(image::png_status::too_many_chunks)), 0);
        CHECK(outcome.image.pixels == nullptr);
        release(outcome);
        CHECK_EQ(g_freed, g_allocations);
    }
}

/* NEG-IMGF-PNG-07: a PLTE whose length is not a multiple of three is invalid
 * before any palette byte is interpreted. */
void test_invalid_palette_length() {
    const auto base = fixture("png_palette_4x4.png");
    chunk_view plte{};
    CHECK(find_chunk(base, "PLTE", plte));
    if (plte.crc == 0)
        return;
    auto bytes = base;
    write_be32(bytes, plte.type - 4, 13);
    reset_witness();
    decode_outcome outcome = decode_bytes(bytes);
    CHECK_EQ(outcome.result.state, image::decode_status::format);
    CHECK_EQ(std::strcmp(outcome.result.reason, image::png_status_reason(image::png_status::invalid_palette)), 0);
    CHECK(outcome.image.pixels == nullptr);
    release(outcome);
    CHECK_EQ(g_freed, g_allocations);
}

void test_refused_variants() {
    struct mutation_case {
        const char *label;
        std::size_t offset;
        std::uint8_t value;
        image::png_status expected;
        bool repair_crc;
    };
    const auto base = fixture("png_rgb_4x4.png");
    const mutation_case mutations[] = {
        {"depth 1", k_ihdr_data + 8, 1, image::png_status::unsupported_bit_depth, true},
        {"depth 2", k_ihdr_data + 8, 2, image::png_status::unsupported_bit_depth, true},
        {"depth 4", k_ihdr_data + 8, 4, image::png_status::unsupported_bit_depth, true},
        {"depth 16", k_ihdr_data + 8, 16, image::png_status::unsupported_bit_depth, true},
        {"color 1", k_ihdr_data + 9, 1, image::png_status::unsupported_color_type, true},
        {"color 5", k_ihdr_data + 9, 5, image::png_status::unsupported_color_type, true},
        {"color 7", k_ihdr_data + 9, 7, image::png_status::unsupported_color_type, true},
        {"color 4 (alpha)", k_ihdr_data + 9, 4, image::png_status::unsupported_alpha, true},
        {"color 6 (rgba)", k_ihdr_data + 9, 6, image::png_status::unsupported_alpha, true},
        {"compression", k_ihdr_data + 10, 1, image::png_status::unsupported_compression, true},
        {"filter method", k_ihdr_data + 11, 1, image::png_status::unsupported_filter_method, true},
        {"interlace", k_ihdr_data + 12, 1, image::png_status::unsupported_interlace, true},
        {"width 0", k_ihdr_data + 3, 0, image::png_status::invalid_dimension, true},
        {"CRC IHDR", k_ihdr_data + 16, 0x5A, image::png_status::checksum_mismatch, false},
    };
    for (const auto &item : mutations) {
        auto bytes = base;
        mutate(bytes, item.offset, item.value);
        if (item.repair_crc)
            repair_ihdr_crc(bytes);
        reset_witness();
        decode_outcome outcome = decode_bytes(bytes);
        CHECK_EQ(outcome.result.state, image::decode_status::format);
        if (outcome.result.reason != nullptr)
            CHECK_EQ(std::strcmp(outcome.result.reason, image::png_status_reason(item.expected)), 0);
        CHECK(outcome.image.pixels == nullptr);
        release(outcome);
    }

    const char *fixtures[] = {"png_rgba_4x4.png",          "png_16bit_4x4.png",      "png_interlaced_4x4.png",
                              "png_no_plte_4x4.png",       "png_bad_filter_4x4.png", "png_short_data_4x4.png",
                              "png_bomb_4x4.png",          "png_truncated_4x4.png",  "png_unknown_critical_4x4.png",
                              "png_palette_index_4x4.png", "png_trns_4x4.png",       "png_zlib_bad_4x4.png"};
    const image::png_status expected[] = {image::png_status::unsupported_alpha,
                                          image::png_status::unsupported_bit_depth,
                                          image::png_status::unsupported_interlace,
                                          image::png_status::invalid_palette,
                                          image::png_status::invalid_filter,
                                          image::png_status::truncated_image,
                                          image::png_status::excessive_data,
                                          image::png_status::truncated_segment,
                                          image::png_status::unsupported_critical_chunk,
                                          image::png_status::invalid_palette_entry,
                                          image::png_status::unsupported_alpha,
                                          image::png_status::decode_failed};
    for (std::size_t index = 0; index < sizeof(fixtures) / sizeof(fixtures[0]); ++index) {
        reset_witness();
        decode_outcome outcome = decode_file(fixtures[index]);
        CHECK_EQ(outcome.result.state, image::decode_status::format);
        if (outcome.result.reason != nullptr)
            CHECK_EQ(std::strcmp(outcome.result.reason, image::png_status_reason(expected[index])), 0);
        CHECK(outcome.image.pixels == nullptr);
        CHECK_EQ(g_freed, g_allocations);
        release(outcome);
    }

    /* IDAT CRC: the deflate stream stays valid, only the checksum is wrong. */
    {
        auto bytes = base;
        chunk_view idat{};
        CHECK(find_chunk(bytes, "IDAT", idat));
        if (idat.crc != 0)
            mutate(bytes, idat.crc + 3, static_cast<std::uint8_t>(bytes[idat.crc + 3] ^ 0xFF));
        reset_witness();
        decode_outcome outcome = decode_bytes(bytes);
        CHECK_EQ(outcome.result.state, image::decode_status::format);
        if (outcome.result.reason != nullptr)
            CHECK_EQ(std::strcmp(outcome.result.reason, image::png_status_reason(image::png_status::checksum_mismatch)),
                     0);
        CHECK(outcome.image.pixels == nullptr);
        release(outcome);
    }

    /* Two IHDR chunks. */
    {
        auto bytes = base;
        chunk_view ihdr{};
        CHECK(find_chunk(bytes, "IHDR", ihdr));
        const std::vector<std::uint8_t> duplicate(bytes.begin() + 8, bytes.begin() + 33);
        bytes.insert(bytes.begin() + 8, duplicate.begin(), duplicate.end());
        decode_outcome outcome = decode_bytes(bytes);
        CHECK_EQ(outcome.result.state, image::decode_status::format);
        if (outcome.result.reason != nullptr)
            CHECK_EQ(std::strcmp(outcome.result.reason, image::png_status_reason(image::png_status::invalid_header)),
                     0);
        release(outcome);
    }
}

void test_limits_seams_and_cancellation() {
    const auto bytes = fixture("png_rgb_4x4.png");
    const int descriptor = open_bytes(bytes);
    CHECK(descriptor >= 0);

    reset_witness();
    image::decoded_image empty{};
    CHECK_EQ(image::decode_descriptor(witness_ops(), descriptor, 0, image::image_format::png, empty).state,
             image::decode_status::too_large);
    CHECK_EQ(image::decode_descriptor(witness_ops(), descriptor, image::k_max_file_bytes + 1, image::image_format::png,
                                      empty)
                 .state,
             image::decode_status::too_large);
    CHECK_EQ(g_allocations, 0u);
    CHECK_EQ(image::decode_descriptor(witness_ops(), -1, bytes.size(), image::image_format::png, empty).state,
             image::decode_status::invalid_argument);
    /* Content of another format is refused instead of sniffed. */
    CHECK_EQ(image::decode_descriptor(witness_ops(), descriptor, bytes.size(), image::image_format::bmp, empty).state,
             image::decode_status::format);

    reset_witness();
    g_refuse = true;
    image::decoded_image refused{};
    CHECK_EQ(image::decode_descriptor(witness_ops(), descriptor, bytes.size(), image::image_format::png, refused).state,
             image::decode_status::allocation_failed);
    CHECK(refused.pixels == nullptr);
    CHECK_EQ(g_allocations, 0u);

    /* NEG-IMGF-CANCEL-01: no chunk walk, no read and no window/frame allocation. */
    reset_witness();
    g_cancel_immediately = true;
    image::decoded_image cancelled{};
    CHECK_EQ(
        image::decode_descriptor(witness_ops(), descriptor, bytes.size(), image::image_format::png, cancelled).state,
        image::decode_status::cancelled);
    CHECK(cancelled.pixels == nullptr);
    CHECK_EQ(cancelled.pixel_bytes, 0u);
    CHECK_EQ(g_reads, 0u);
    CHECK_EQ(g_allocations, 0u);
    CHECK_EQ(g_freed, 0u);

    reset_witness();
    g_fail_from = bytes.size() / 2;
    image::decoded_image broken{};
    CHECK_EQ(image::decode_descriptor(witness_ops(), descriptor, bytes.size(), image::image_format::png, broken).state,
             image::decode_status::read_failed);
    CHECK(broken.pixels == nullptr);
    CHECK_EQ(g_freed, g_allocations);

    (void)::close(descriptor);
}

void test_inspect_prefix_semantics() {
    const auto bytes = fixture("png_rgb_4x4.png");
    image::png_info info{};
    CHECK_EQ(image::inspect_png(bytes.data(), std::min(bytes.size(), image::k_png_prefix_bytes), bytes.size(), info),
             image::png_status::ok);
    CHECK_EQ(info.width, 4u);
    CHECK_EQ(info.height, 4u);
    CHECK_EQ(info.color_type, 2u);
    CHECK_EQ(info.bytes_per_pixel, 3u);
    CHECK_EQ(info.row_bytes, 12u);
    CHECK_EQ(info.frame_bytes, 32u);

    image::png_info empty{};
    CHECK_EQ(image::inspect_png(nullptr, 0, 0, empty), image::png_status::header_truncated);
    CHECK_EQ(image::inspect_png(bytes.data(), 4, bytes.size(), empty), image::png_status::prefix_incomplete);
    /* A prefix that cannot even hold IHDR is a truncated header, not a valid one. */
    CHECK_EQ(image::inspect_png(bytes.data(), 12, 12, empty), image::png_status::header_truncated);

    auto wrong_signature = bytes;
    wrong_signature[1] = 'X';
    CHECK_EQ(image::inspect_png(wrong_signature.data(), wrong_signature.size(), wrong_signature.size(), empty),
             image::png_status::invalid_signature);

    /* A prefix that ends before the palette cannot validate a palette image. */
    const auto palette = fixture("png_palette_4x4.png");
    CHECK_EQ(image::inspect_png(palette.data(), 20, palette.size(), empty), image::png_status::prefix_incomplete);
}

void test_single_byte_mutations_are_safe() {
    const auto base = fixture("png_rgb_4x4.png");
    std::uint32_t state = 0xC0FFEE11u;
    const auto next = [&state]() {
        state = (state * 1664525u) + 1013904223u;
        return state;
    };
    for (int iteration = 0; iteration < 64; ++iteration) {
        auto bytes = base;
        bytes[next() % bytes.size()] = static_cast<std::uint8_t>(next() & 0xFF);
        reset_witness();
        decode_outcome outcome = decode_bytes(bytes);
        if (outcome.result.state == image::decode_status::ok) {
            CHECK(outcome.image.pixels != nullptr);
            CHECK_EQ(outcome.image.pixel_bytes,
                     static_cast<std::size_t>(outcome.image.width) * outcome.image.height * 2u);
        } else {
            CHECK(outcome.image.pixels == nullptr);
        }
        release(outcome);
        CHECK_EQ(g_freed, g_allocations);
    }
}

void test_scratch_is_independent_of_height() {
    image::png_info tall{};
    tall.width = image::k_max_dimension;
    tall.height = image::k_max_dimension;
    tall.color_type = 2;
    tall.bytes_per_pixel = 3;
    tall.row_bytes = image::k_max_dimension * 3u;
    tall.frame_bytes = image::k_max_frame_bytes;
    image::png_info flat = tall;
    flat.height = 1;
    flat.frame_bytes = static_cast<std::uint64_t>(tall.width) * 2u;
    /* The scratch depends on the row width only; the frame is what grows. */
    CHECK_EQ(image::png_scratch_bytes(tall), image::png_scratch_bytes(flat));
    CHECK(image::png_scratch_bytes(tall) <= 64u * 1024u);
    image::png_info invalid{};
    CHECK_EQ(image::png_scratch_bytes(invalid), 0u);
    CHECK_EQ(image::png_decode_peak_bytes(invalid), 0u);
}

} // namespace

int main() {
    test_truecolour_exact_pixels();
    test_grayscale_and_palette();
    test_palette_128_bands();
    test_all_row_filters();
    test_chunk_permutations_match_single_idat();
    test_trailing_bytes_after_iend_are_ignored();
    test_small_and_maximum_geometry();
    test_refused_variants();
    test_chunk_structure_and_iend();
    test_invalid_palette_length();
    test_limits_seams_and_cancellation();
    test_inspect_prefix_semantics();
    test_single_byte_mutations_are_safe();
    test_scratch_is_independent_of_height();
    std::printf("test_image_png: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
