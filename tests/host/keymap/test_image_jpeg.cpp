/*
 * Host test for the baseline JPEG decoder of the isolated image viewer
 * (REQ-IMGF-02, TEST-IMGF-JPG-01..11, NEG-IMGF-JPG-01..15).
 *
 * The fixtures are committed files generated once by
 * `tools/gen_image_fixtures.py` (manifest with sha256 next to them); the test
 * never re-encodes.  Negative cases are single-byte mutations of a valid fixture
 * or a crafted segment, so the oracle stays independent of the decoder: the
 * expected colours are the ones the fixture was built with.
 */
#include "apps/image/cyberdeck_image_jpeg.h"

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

/* Declared tolerance of the lossy oracle at the centre of a flat quadrant: a
 * channel swap, a row inversion or a chroma shear cannot hide inside it. */
constexpr int k_colour_tolerance = 16;

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
    char pattern[] = "/tmp/cyberdeck-jpeg-XXXXXX";
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
        image::decode_descriptor(witness_ops(), descriptor, bytes.size(), image::image_format::jpeg, outcome.image);
    (void)::close(descriptor);
    return outcome;
}

decode_outcome decode_file(const char *name) {
    return decode_bytes(fixture(name));
}

void release(decode_outcome &outcome) {
    image::release_decoded(outcome.image, witness_ops());
}

/* ---- minimal independent marker scanner ---- */

std::size_t find_marker(const std::vector<std::uint8_t> &bytes, std::uint8_t marker) {
    std::size_t offset = 2;
    while (offset + 4 <= bytes.size()) {
        if (bytes[offset] != 0xFF)
            return std::string::npos;
        const std::uint8_t current = bytes[offset + 1];
        if (current == marker)
            return offset;
        const std::size_t length = (static_cast<std::size_t>(bytes[offset + 2]) << 8) | bytes[offset + 3];
        if (length < 2)
            return std::string::npos;
        offset += 2 + length;
    }
    return std::string::npos;
}

void mutate(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint8_t value) {
    CHECK(offset < bytes.size());
    if (offset < bytes.size())
        bytes[offset] = value;
}

/* 4x4 pixels of one quadrant, read from the top-left corner of the quadrant. */
std::uint16_t pixel_at(const image::decoded_image &decoded, std::uint32_t x, std::uint32_t y) {
    return decoded.pixels[static_cast<std::size_t>(y) * decoded.width + x];
}

int red_of(std::uint16_t pixel) {
    return ((pixel >> 11) & 0x1F) * 255 / 31;
}
int green_of(std::uint16_t pixel) {
    return ((pixel >> 5) & 0x3F) * 255 / 63;
}
int blue_of(std::uint16_t pixel) {
    return (pixel & 0x1F) * 255 / 31;
}

bool near_colour(std::uint16_t pixel, int red, int green, int blue) {
    return std::abs(red_of(pixel) - red) <= k_colour_tolerance &&
           std::abs(green_of(pixel) - green) <= k_colour_tolerance &&
           std::abs(blue_of(pixel) - blue) <= k_colour_tolerance;
}

/* ---- scenarios ---- */

void test_inspect_accepted_headers() {
    const auto bytes = fixture("jpeg_444_16x16.jpg");
    image::jpeg_info info{};
    const std::size_t prefix = std::min(bytes.size(), image::k_jpeg_prefix_bytes);
    CHECK_EQ(image::inspect_jpeg(bytes.data(), prefix, bytes.size(), info), image::jpeg_status::ok);
    CHECK_EQ(info.width, 16u);
    CHECK_EQ(info.height, 16u);
    CHECK_EQ(info.components, 3u);
    CHECK_EQ(info.sampling_x, 1u);
    CHECK_EQ(info.sampling_y, 1u);
    CHECK_EQ(info.frame_bytes, 16u * 16u * 2u);

    const auto subsampled = fixture("jpeg_420_16x16.jpg");
    image::jpeg_info sub{};
    CHECK_EQ(image::inspect_jpeg(subsampled.data(), std::min(subsampled.size(), image::k_jpeg_prefix_bytes),
                                 subsampled.size(), sub),
             image::jpeg_status::ok);
    CHECK_EQ(sub.sampling_x, 2u);
    CHECK_EQ(sub.sampling_y, 2u);

    const auto gray = fixture("jpeg_gray_8x8.jpg");
    image::jpeg_info mono{};
    CHECK_EQ(image::inspect_jpeg(gray.data(), std::min(gray.size(), image::k_jpeg_prefix_bytes), gray.size(), mono),
             image::jpeg_status::ok);
    CHECK_EQ(mono.components, 1u);
    CHECK(mono.grayscale);

    /* A big APP1 pushes SOF0 beyond the prefix: the prefix inspection says so,
     * and the streaming decoder still accepts the file. */
    const auto big = fixture("jpeg_app1_big.jpg");
    image::jpeg_info incomplete{};
    CHECK_EQ(image::inspect_jpeg(big.data(), image::k_jpeg_prefix_bytes, big.size(), incomplete),
             image::jpeg_status::prefix_incomplete);

    image::jpeg_info empty{};
    CHECK_EQ(image::inspect_jpeg(nullptr, 0, 0, empty), image::jpeg_status::header_truncated);
    CHECK_EQ(image::inspect_jpeg(bytes.data(), 3, bytes.size(), empty), image::jpeg_status::prefix_incomplete);
}

void test_quadrant_oracle() {
    struct case_t {
        const char *name;
        std::uint32_t width;
        std::uint32_t height;
    };
    const case_t cases[] = {
        {"jpeg_444_16x16.jpg", 16, 16}, {"jpeg_420_16x16.jpg", 16, 16}, {"jpeg_422_32x16.jpg", 32, 16}};
    for (const auto &item : cases) {
        reset_witness();
        decode_outcome outcome = decode_file(item.name);
        CHECK_EQ(outcome.result.state, image::decode_status::ok);
        CHECK_EQ(outcome.image.width, item.width);
        CHECK_EQ(outcome.image.height, item.height);
        CHECK_EQ(outcome.image.pixel_bytes, static_cast<std::size_t>(item.width) * item.height * 2u);
        CHECK(outcome.image.pixels != nullptr);
        CHECK_EQ(g_allocations, 1u);
        CHECK(g_largest >= image::k_jpeg_pool_bytes);
        CHECK(g_largest <= image::jpeg_decode_peak_bytes(image::jpeg_info{item.width, item.height, 3, 1, 1, false,
                                                                          item.width * item.height * 2u}));
        if (outcome.image.pixels != nullptr) {
            const std::uint32_t left = item.width / 4;
            const std::uint32_t right = item.width * 3 / 4;
            const std::uint32_t top = item.height / 4;
            const std::uint32_t bottom = item.height * 3 / 4;
            /* Channel order and top-down row order are both asserted by the
             * four quadrant centres. */
            CHECK(near_colour(pixel_at(outcome.image, left, top), 0xF8, 0, 0));
            CHECK(near_colour(pixel_at(outcome.image, right, top), 0, 0xFC, 0));
            CHECK(near_colour(pixel_at(outcome.image, left, bottom), 0, 0, 0xF8));
            CHECK(near_colour(pixel_at(outcome.image, right, bottom), 0xF8, 0xFC, 0xF8));
        }
        release(outcome);
        CHECK_EQ(g_freed, g_allocations);
        CHECK(outcome.image.pixels == nullptr);
    }
}

void test_geometry_and_small_images() {
    reset_witness();
    decode_outcome tiny = decode_file("jpeg_3x5.jpg");
    CHECK_EQ(tiny.result.state, image::decode_status::ok);
    CHECK_EQ(tiny.image.width, 3u);
    CHECK_EQ(tiny.image.height, 5u);
    /* Three columns in a 16-pixel MCU: the frame must be cropped, not padded. */
    CHECK_EQ(tiny.image.pixel_bytes, 30u);
    release(tiny);

    reset_witness();
    decode_outcome single = decode_file("jpeg_1x1.jpg");
    CHECK_EQ(single.result.state, image::decode_status::ok);
    CHECK_EQ(single.image.pixel_bytes, 2u);
    CHECK_EQ(single.image.width, 1u);
    if (single.image.pixels != nullptr)
        CHECK(near_colour(single.image.pixels[0], 0xF8, 0, 0));
    release(single);

    const auto header = fixture("jpeg_1x1.jpg");
    image::jpeg_info info{};
    CHECK_EQ(
        image::inspect_jpeg(header.data(), std::min(header.size(), image::k_jpeg_prefix_bytes), header.size(), info),
        image::jpeg_status::ok);
    CHECK(info.frame_bytes <= image::jpeg_decode_peak_bytes(info));
}

void test_grayscale_is_replicated() {
    reset_witness();
    decode_outcome mono = decode_file("jpeg_gray_8x8.jpg");
    CHECK_EQ(mono.result.state, image::decode_status::ok);
    if (mono.image.pixels != nullptr) {
        for (std::uint32_t y = 1; y < mono.image.height; y += 3) {
            for (std::uint32_t x = 1; x < mono.image.width; x += 3) {
                const std::uint16_t pixel = pixel_at(mono.image, x, y);
                /* Red and blue share the 5-bit width, so they must match exactly;
                 * the 6-bit green channel is compared with the same tolerance as
                 * the colour oracle. */
                CHECK_EQ((pixel >> 11) & 0x1F, pixel & 0x1F);
                CHECK(std::abs(green_of(pixel) - red_of(pixel)) <= k_colour_tolerance);
            }
        }
        /* Quadrant values of the fixture: 0x20 / 0x60 / 0xA0 / 0xC0. */
        CHECK(near_colour(pixel_at(mono.image, 1, 1), 0x20, 0x20, 0x20));
        CHECK(near_colour(pixel_at(mono.image, 6, 1), 0x60, 0x60, 0x60));
        CHECK(near_colour(pixel_at(mono.image, 1, 6), 0xA0, 0xA0, 0xA0));
        CHECK(near_colour(pixel_at(mono.image, 6, 6), 0xC0, 0xC0, 0xC0));
    }
    release(mono);
}

/* The four accepted companions of the same 16x16 4:4:4 source must reproduce the
 * plain frame byte for byte: restart markers, EXIF orientation, a large APP1 and
 * trailing bytes after EOI may not change a single pixel. */
void test_companions_match_plain_frame() {
    reset_witness();
    decode_outcome plain = decode_file("jpeg_444_16x16.jpg");
    CHECK_EQ(plain.result.state, image::decode_status::ok);
    const std::vector<std::uint16_t> reference(
        plain.image.pixels, plain.image.pixels + (static_cast<std::size_t>(plain.image.width) * plain.image.height));

    const char *companions[] = {"jpeg_exif_16x16.jpg", "jpeg_app1_big.jpg", "jpeg_trailing_16x16.jpg"};
    for (const char *name : companions) {
        reset_witness();
        decode_outcome outcome = decode_file(name);
        CHECK_EQ(outcome.result.state, image::decode_status::ok);
        CHECK_EQ(outcome.image.pixel_bytes, reference.size() * 2u);
        if (outcome.image.pixels != nullptr)
            CHECK(std::memcmp(outcome.image.pixels, reference.data(), reference.size() * 2u) == 0);
        release(outcome);
        CHECK_EQ(g_freed, g_allocations);
    }
    release(plain);

    /* Restart markers (DRI/RSTn) only change the entropy layout, so a 4:2:0
     * image with `-restart 1` must match the plain 4:2:0 frame exactly. */
    reset_witness();
    decode_outcome subsampled = decode_file("jpeg_420_16x16.jpg");
    CHECK_EQ(subsampled.result.state, image::decode_status::ok);
    reset_witness();
    decode_outcome restart = decode_file("jpeg_restart_16x16.jpg");
    CHECK_EQ(restart.result.state, image::decode_status::ok);
    CHECK_EQ(restart.image.pixel_bytes, subsampled.image.pixel_bytes);
    if (restart.image.pixels != nullptr && subsampled.image.pixels != nullptr)
        CHECK(std::memcmp(restart.image.pixels, subsampled.image.pixels, subsampled.image.pixel_bytes) == 0);
    release(restart);
    release(subsampled);
}

void test_maximum_geometry() {
    const auto bytes = fixture("jpeg_1280.jpg");
    image::jpeg_info info{};
    CHECK_EQ(image::inspect_jpeg(bytes.data(), std::min(bytes.size(), image::k_jpeg_prefix_bytes), bytes.size(), info),
             image::jpeg_status::ok);
    CHECK_EQ(info.width, image::k_max_dimension);
    CHECK_EQ(info.frame_bytes, image::k_max_frame_bytes);
    const std::size_t peak = image::jpeg_decode_peak_bytes(info);
    CHECK(peak > info.frame_bytes);
    CHECK(peak < info.frame_bytes + 16384u);

    reset_witness();
    decode_outcome outcome = decode_bytes(bytes);
    CHECK_EQ(outcome.result.state, image::decode_status::ok);
    CHECK_EQ(outcome.image.pixel_bytes, image::k_max_frame_bytes);
    CHECK(g_largest <= peak);
    release(outcome);
    CHECK_EQ(g_freed, g_allocations);
}

void test_rejected_variants() {
    const std::vector<std::uint8_t> base = fixture("jpeg_444_16x16.jpg");
    const std::size_t sof = find_marker(base, 0xC0);
    CHECK(sof != std::string::npos);

    struct mutation_case {
        const char *label;
        std::size_t offset;
        std::uint8_t value;
        image::jpeg_status expected;
    };
    const mutation_case mutations[] = {
        {"SOF1", sof + 1, 0xC1, image::jpeg_status::unsupported_variant},
        {"SOF2", sof + 1, 0xC2, image::jpeg_status::unsupported_variant},
        {"SOF3", sof + 1, 0xC3, image::jpeg_status::unsupported_variant},
        {"SOF9", sof + 1, 0xC9, image::jpeg_status::unsupported_variant},
        {"precision 12", sof + 4, 12, image::jpeg_status::unsupported_precision},
        {"four components", sof + 9, 4, image::jpeg_status::unsupported_components},
        {"two components", sof + 9, 2, image::jpeg_status::unsupported_components},
        {"4:1:1", sof + 11, 0x41, image::jpeg_status::unsupported_sampling},
        {"chroma sampling", sof + 14, 0x21, image::jpeg_status::unsupported_sampling},
    };
    for (const auto &item : mutations) {
        auto bytes = base;
        mutate(bytes, item.offset, item.value);
        reset_witness();
        decode_outcome outcome = decode_bytes(bytes);
        CHECK_EQ(outcome.result.state, image::decode_status::format);
        if (outcome.result.reason != nullptr)
            CHECK_EQ(std::strcmp(outcome.result.reason, image::jpeg_status_reason(item.expected)), 0);
        CHECK(outcome.image.pixels == nullptr);
        CHECK_EQ(g_allocations, 0u);
        release(outcome);
    }

    /* width 1281 needs both width bytes */
    {
        auto bytes = base;
        mutate(bytes, sof + 7, 0x05);
        mutate(bytes, sof + 8, 0x01);
        decode_outcome outcome = decode_bytes(bytes);
        CHECK_EQ(outcome.result.state, image::decode_status::format);
        CHECK_EQ(std::strcmp(outcome.result.reason, image::jpeg_status_reason(image::jpeg_status::invalid_dimension)),
                 0);
        CHECK(outcome.image.pixels == nullptr);
        release(outcome);
    }

    /* Progressive fixture (real SOF2 file, not a mutation). */
    {
        reset_witness();
        decode_outcome outcome = decode_file("jpeg_progressive_16x16.jpg");
        CHECK_EQ(outcome.result.state, image::decode_status::format);
        CHECK_EQ(std::strcmp(outcome.result.reason, image::jpeg_status_reason(image::jpeg_status::unsupported_variant)),
                 0);
        CHECK_EQ(g_allocations, 0u);
        release(outcome);
    }

    /* Missing SOI. */
    {
        auto bytes = base;
        mutate(bytes, 0, 'X');
        decode_outcome outcome = decode_bytes(bytes);
        CHECK_EQ(std::strcmp(outcome.result.reason, image::jpeg_status_reason(image::jpeg_status::invalid_soi)), 0);
        release(outcome);
    }

    /* Segment length that overruns the file. */
    {
        auto bytes = base;
        const std::size_t app = find_marker(bytes, 0xE0);
        if (app != std::string::npos) {
            mutate(bytes, app + 2, 0xFF);
            mutate(bytes, app + 3, 0xF0);
            decode_outcome outcome = decode_bytes(bytes);
            CHECK_EQ(outcome.result.state, image::decode_status::format);
            CHECK_EQ(
                std::strcmp(outcome.result.reason, image::jpeg_status_reason(image::jpeg_status::truncated_segment)),
                0);
            release(outcome);
        }
    }

    /* A second SOS before EOI is a multi-scan file; the trailer check refuses it
     * after the first scan instead of publishing a partial image. */
    {
        auto bytes = base;
        const auto eoi = static_cast<std::size_t>(bytes.size()) - 2;
        CHECK(bytes[eoi] == 0xFF);
        CHECK(bytes[eoi + 1] == 0xD9);
        const std::uint8_t second_scan[] = {0xFF, 0xDA, 0x00, 0x0C, 0x03, 0x01, 0x00,
                                            0x02, 0x11, 0x03, 0x11, 0x00, 0x3F, 0x00};
        bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(eoi), second_scan, second_scan + sizeof(second_scan));
        reset_witness();
        decode_outcome outcome = decode_bytes(bytes);
        CHECK_EQ(outcome.result.state, image::decode_status::format);
        CHECK_EQ(std::strcmp(outcome.result.reason, image::jpeg_status_reason(image::jpeg_status::multiple_scans)), 0);
        CHECK(outcome.image.pixels == nullptr);
        CHECK_EQ(g_freed, g_allocations);
        release(outcome);
    }

    /* Missing EOI with intact entropy: the two marker bytes are overwritten, so
     * the failure can only come from the trailer check. */
    {
        auto bytes = base;
        bytes[bytes.size() - 2] = 0x00;
        bytes[bytes.size() - 1] = 0x00;
        decode_outcome outcome = decode_bytes(bytes);
        CHECK_EQ(outcome.result.state, image::decode_status::format);
        CHECK_EQ(std::strcmp(outcome.result.reason, image::jpeg_status_reason(image::jpeg_status::missing_eoi)), 0);
        CHECK(outcome.image.pixels == nullptr);
        release(outcome);
    }

    /* Truncated entropy: the decoder itself reports the failure. */
    {
        auto bytes = base;
        bytes.resize(bytes.size() - 8);
        decode_outcome outcome = decode_bytes(bytes);
        CHECK_EQ(outcome.result.state, image::decode_status::format);
        CHECK_EQ(std::strcmp(outcome.result.reason, image::jpeg_status_reason(image::jpeg_status::decode_failed)), 0);
        CHECK(outcome.image.pixels == nullptr);
        release(outcome);
    }

    /* Invalid DHT table class. */
    {
        auto bytes = base;
        const std::size_t dht = find_marker(bytes, 0xC4);
        CHECK(dht != std::string::npos);
        if (dht != std::string::npos) {
            mutate(bytes, dht + 4, 0x22);
            decode_outcome outcome = decode_bytes(bytes);
            CHECK_EQ(outcome.result.state, image::decode_status::format);
            CHECK(outcome.image.pixels == nullptr);
            release(outcome);
        }
    }
}

void test_limits_and_seams() {
    const auto bytes = fixture("jpeg_444_16x16.jpg");
    const int descriptor = open_bytes(bytes);
    CHECK(descriptor >= 0);

    reset_witness();
    image::decoded_image empty{};
    CHECK_EQ(image::decode_descriptor(witness_ops(), descriptor, 0, image::image_format::jpeg, empty).state,
             image::decode_status::too_large);
    CHECK_EQ(image::decode_descriptor(witness_ops(), descriptor, image::k_max_file_bytes + 1, image::image_format::jpeg,
                                      empty)
                 .state,
             image::decode_status::too_large);
    CHECK_EQ(g_allocations, 0u);
    CHECK_EQ(image::decode_descriptor(witness_ops(), -1, bytes.size(), image::image_format::jpeg, empty).state,
             image::decode_status::invalid_argument);
    /* Wrong format for the bytes: the extension is authoritative, never sniffed. */
    CHECK_EQ(image::decode_descriptor(witness_ops(), descriptor, bytes.size(), image::image_format::png, empty).state,
             image::decode_status::format);
    CHECK_EQ(image::decode_descriptor(witness_ops(), descriptor, bytes.size(), image::image_format::none, empty).state,
             image::decode_status::unsupported_format);

    /* NEG-IMGF-EXT-05: JPEG bytes declared as PNG are refused with the PNG
     * signature reason, and a PNG declared as JPEG reaches the JPEG SOI check. */
    {
        const auto png = fixture("png_rgb_4x4.png");
        const int png_descriptor = open_bytes(png);
        CHECK(png_descriptor >= 0);
        if (png_descriptor >= 0) {
            image::decoded_image disguised{};
            const auto result = image::decode_descriptor(witness_ops(), png_descriptor, png.size(),
                                                         image::image_format::jpeg, disguised);
            CHECK_EQ(result.state, image::decode_status::format);
            CHECK(result.reason != nullptr);
            if (result.reason != nullptr)
                CHECK_EQ(std::strcmp(result.reason, image::jpeg_status_reason(image::jpeg_status::invalid_soi)), 0);
            CHECK(disguised.pixels == nullptr);
            (void)::close(png_descriptor);
        }
    }

    /* Allocation refused: nothing published, nothing leaked. */
    reset_witness();
    g_refuse = true;
    image::decoded_image refused{};
    CHECK_EQ(
        image::decode_descriptor(witness_ops(), descriptor, bytes.size(), image::image_format::jpeg, refused).state,
        image::decode_status::allocation_failed);
    CHECK(refused.pixels == nullptr);
    CHECK_EQ(g_allocations, 0u);

    /* NEG-IMGF-CANCEL-01: cancelled from the start means no parse, no read and no
     * allocation - the frame/scratch budget is never spent on a dead request. */
    reset_witness();
    g_cancel_immediately = true;
    image::decoded_image cancelled{};
    CHECK_EQ(
        image::decode_descriptor(witness_ops(), descriptor, bytes.size(), image::image_format::jpeg, cancelled).state,
        image::decode_status::cancelled);
    CHECK(cancelled.pixels == nullptr);
    CHECK_EQ(cancelled.pixel_bytes, 0u);
    CHECK_EQ(g_reads, 0u);
    CHECK_EQ(g_allocations, 0u);
    CHECK_EQ(g_freed, 0u);

    /* Cancellation after the header was parsed but during the entropy pass. */
    reset_witness();
    g_cancel_after_reads = g_reads + 2;
    image::decoded_image middle{};
    CHECK_EQ(image::decode_descriptor(witness_ops(), descriptor, bytes.size(), image::image_format::jpeg, middle).state,
             image::decode_status::cancelled);
    CHECK(middle.pixels == nullptr);
    CHECK_EQ(g_freed, g_allocations);

    /* Read failure mid-stream. */
    reset_witness();
    g_fail_from = bytes.size() / 2;
    image::decoded_image broken{};
    CHECK_EQ(image::decode_descriptor(witness_ops(), descriptor, bytes.size(), image::image_format::jpeg, broken).state,
             image::decode_status::read_failed);
    CHECK(broken.pixels == nullptr);
    CHECK_EQ(g_freed, g_allocations);

    (void)::close(descriptor);
}

/* Deterministic fuzzing: single-byte mutations must never crash, leak or publish
 * a frame that disagrees with its own geometry. */
void test_single_byte_mutations_are_safe() {
    const auto base = fixture("jpeg_444_16x16.jpg");
    std::uint32_t state = 0x12345678u;
    const auto next = [&state]() {
        state = (state * 1664525u) + 1013904223u;
        return state;
    };
    for (int iteration = 0; iteration < 64; ++iteration) {
        auto bytes = base;
        const std::size_t offset = next() % bytes.size();
        bytes[offset] = static_cast<std::uint8_t>(next() & 0xFF);
        reset_witness();
        decode_outcome outcome = decode_bytes(bytes);
        if (outcome.result.state == image::decode_status::ok) {
            CHECK(outcome.image.pixels != nullptr);
            CHECK_EQ(outcome.image.pixel_bytes,
                     static_cast<std::size_t>(outcome.image.width) * outcome.image.height * 2u);
            CHECK(outcome.image.width <= image::k_max_dimension);
        } else {
            CHECK(outcome.image.pixels == nullptr);
        }
        release(outcome);
        CHECK_EQ(g_freed, g_allocations);
    }
}

void test_peak_budget_is_derived() {
    image::jpeg_info info{};
    info.width = 3;
    info.height = 5;
    info.frame_bytes = 30;
    const std::size_t peak = image::jpeg_decode_peak_bytes(info);
    CHECK(peak >= 30u + image::k_jpeg_pool_bytes);
    CHECK(peak <= 30u + 4u + image::k_jpeg_pool_bytes);
    image::jpeg_info invalid{};
    CHECK_EQ(image::jpeg_decode_peak_bytes(invalid), 0u);
    image::jpeg_info oversized{};
    oversized.frame_bytes = image::k_max_frame_bytes + 2u;
    CHECK_EQ(image::jpeg_decode_peak_bytes(oversized), 0u);
}

} // namespace

int main() {
    test_inspect_accepted_headers();
    test_quadrant_oracle();
    test_geometry_and_small_images();
    test_grayscale_is_replicated();
    test_companions_match_plain_frame();
    test_maximum_geometry();
    test_rejected_variants();
    test_limits_and_seams();
    test_single_byte_mutations_are_safe();
    test_peak_budget_is_derived();
    std::printf("test_image_jpeg: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
