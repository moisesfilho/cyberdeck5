/*
 * Host test for the bounded BMP envelope of the isolated image viewer
 * (REQ-IMG-01 / AC-IMG-01: 24-bit BI_RGB, no compression, no transparency,
 * 1..1280 per axis, 5 MiB cap, fail-closed).
 *
 * The test links the real production parser/converter.  It touches no
 * ESP-IDF, FreeRTOS, LVGL, VFS, simulation bridge or hardware: every case is a
 * pure function over a byte buffer built here, so the oracle is independent of
 * the decoder under test.
 *
 * Coverage by scenario:
 *   TEST-IMG-BMP-02 - padded row stride for odd widths.
 *   TEST-IMG-BMP-04 - compression, bit depth, plane count and DIB size refused.
 *   TEST-IMG-BMP-05 - truncated/inconsistent envelope and pixel data.
 *   TEST-IMG-BMP-06 - missing `BM` signature.
 *   TEST-IMG-LIMIT-02 - 1280 accepted, 1281 refused on either axis.
 *   TEST-IMG-LIMIT-03 - the 1280x1280 payload plus header fits the 5 MiB cap,
 *       derived from the production constants instead of a literal.
 */
#include "apps/image/cyberdeck_image_bmp.h"

#include <cstdio>
#include <cstring>
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

void put_u16(std::vector<std::uint8_t> &buffer, std::size_t offset, std::uint16_t value) {
    buffer[offset] = static_cast<std::uint8_t>(value & 0xFF);
    buffer[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

void put_u32(std::vector<std::uint8_t> &buffer, std::size_t offset, std::uint32_t value) {
    for (int index = 0; index < 4; ++index)
        buffer[offset + static_cast<std::size_t>(index)] = static_cast<std::uint8_t>((value >> (8 * index)) & 0xFF);
}

std::size_t padded_stride(std::uint32_t width) {
    return (static_cast<std::size_t>(width) * 3u + 3u) & ~static_cast<std::size_t>(3u);
}

/* Builds a minimal 24-bit BI_RGB file: 14-byte file header, 40-byte DIB header,
 * `height` padded rows of a single colour. */
std::vector<std::uint8_t> make_bmp(std::uint32_t width, std::int32_t height, std::uint8_t blue = 0x10,
                                   std::uint8_t green = 0x20, std::uint8_t red = 0x30) {
    const std::uint32_t magnitude =
        height < 0 ? static_cast<std::uint32_t>(-height) : static_cast<std::uint32_t>(height);
    const std::size_t stride = padded_stride(width);
    const std::size_t pixel_bytes = stride * magnitude;
    const std::size_t total = 54 + pixel_bytes;
    std::vector<std::uint8_t> buffer(total, 0);
    buffer[0] = 'B';
    buffer[1] = 'M';
    put_u32(buffer, 2, static_cast<std::uint32_t>(total));
    put_u32(buffer, 10, 54);
    put_u32(buffer, 14, 40);
    put_u32(buffer, 18, width);
    put_u32(buffer, 22, static_cast<std::uint32_t>(height));
    put_u16(buffer, 26, 1);
    put_u16(buffer, 28, 24);
    put_u32(buffer, 30, 0);
    put_u32(buffer, 34, static_cast<std::uint32_t>(pixel_bytes));
    for (std::size_t row = 0; row < magnitude; ++row) {
        for (std::uint32_t column = 0; column < width; ++column) {
            const std::size_t offset = 54 + row * stride + static_cast<std::size_t>(column) * 3;
            buffer[offset] = blue;
            buffer[offset + 1] = green;
            buffer[offset + 2] = red;
        }
    }
    return buffer;
}

image::bmp_info inspect(const std::vector<std::uint8_t> &buffer, std::size_t declared = 0) {
    image::bmp_info info{};
    const std::size_t file_bytes = declared != 0 ? declared : buffer.size();
    const std::size_t prefix_bytes =
        buffer.size() < image::k_header_prefix_bytes ? buffer.size() : image::k_header_prefix_bytes;
    const image::bmp_status status = image::inspect_bmp(buffer.data(), prefix_bytes, file_bytes, info);
    CHECK_EQ(status, image::bmp_status::ok);
    return info;
}

void test_envelope_accepts_bottom_up_and_top_down() {
    for (const std::int32_t height : {2, -2}) {
        const auto buffer = make_bmp(2, height);
        const image::bmp_info info = inspect(buffer);
        CHECK_EQ(info.width, 2);
        CHECK_EQ(info.height, 2);
        CHECK_EQ(info.top_down, height < 0);
        CHECK_EQ(info.pixel_offset, 54);
        CHECK_EQ(info.row_stride, 8u); /* 2*3 = 6 rounded to 8 */
        CHECK_EQ(info.pixel_bytes, 16u);
    }
}

void test_stride_padding() {
    const auto even = make_bmp(100, 1);
    CHECK_EQ(inspect(even).row_stride, 300u);
    const auto odd = make_bmp(101, 1);
    /* 101*3 = 303 -> one padding byte; a decoder that used `width*3` would read
     * the next row one byte early and shear the image. */
    CHECK_EQ(inspect(odd).row_stride, 304u);
}

void test_rejects_unsupported_variants() {
    struct case_t {
        const char *label;
        std::size_t offset;
        std::uint32_t value;
        image::bmp_status expected;
    };
    const case_t cases[] = {
        {"compression", 30, 1, image::bmp_status::unsupported_compression},
        {"RLE8 compression", 30, 2, image::bmp_status::unsupported_compression},
        {"planes", 26, 2, image::bmp_status::unsupported_planes},
        {"bit depth 8", 28, 8, image::bmp_status::unsupported_bit_depth},
        {"bit depth 16", 28, 16, image::bmp_status::unsupported_bit_depth},
        {"bit depth 32", 28, 32, image::bmp_status::unsupported_bit_depth},
        {"DIB header", 14, 12, image::bmp_status::unsupported_dib_header},
    };
    for (const auto &item : cases) {
        auto buffer = make_bmp(4, 4);
        if (item.offset == 26 || item.offset == 28)
            put_u16(buffer, item.offset, static_cast<std::uint16_t>(item.value));
        else
            put_u32(buffer, item.offset, item.value);
        image::bmp_info info{};
        CHECK_EQ(image::inspect_bmp(buffer.data(), image::k_header_prefix_bytes, buffer.size(), info), item.expected);
        CHECK_EQ(info.width, 0u);
    }
}

void test_rejects_bad_signature_and_truncation() {
    auto buffer = make_bmp(4, 4);
    buffer[0] = 'X';
    image::bmp_info info{};
    CHECK_EQ(image::inspect_bmp(buffer.data(), image::k_header_prefix_bytes, buffer.size(), info),
             image::bmp_status::invalid_signature);
    buffer = make_bmp(4, 4);
    CHECK_EQ(image::inspect_bmp(buffer.data(), 20, buffer.size(), info), image::bmp_status::header_truncated);
    CHECK_EQ(image::inspect_bmp(nullptr, 0, 0, info), image::bmp_status::header_truncated);
    /* Whole file shorter than the supported header: never inspected as valid. */
    const auto tiny = make_bmp(4, 4);
    CHECK_EQ(image::inspect_bmp(tiny.data(), image::k_header_prefix_bytes, 40, info),
             image::bmp_status::header_truncated);
}

void test_rejects_inconsistent_layout() {
    /* bfOffBits inside the header area. */
    auto buffer = make_bmp(4, 4);
    put_u32(buffer, 10, 20);
    image::bmp_info info{};
    CHECK_EQ(image::inspect_bmp(buffer.data(), image::k_header_prefix_bytes, buffer.size(), info),
             image::bmp_status::invalid_layout);

    /* biSizeImage smaller than the padded payload. */
    buffer = make_bmp(4, 4);
    put_u32(buffer, 34, 8);
    CHECK_EQ(image::inspect_bmp(buffer.data(), image::k_header_prefix_bytes, buffer.size(), info),
             image::bmp_status::invalid_layout);

    /* bfSize smaller than what the geometry requires. */
    buffer = make_bmp(4, 4);
    put_u32(buffer, 2, 60);
    CHECK_EQ(image::inspect_bmp(buffer.data(), image::k_header_prefix_bytes, buffer.size(), info),
             image::bmp_status::invalid_layout);

    /* Pixel data not fully present in the file. */
    buffer = make_bmp(4, 4);
    CHECK_EQ(image::inspect_bmp(buffer.data(), image::k_header_prefix_bytes, buffer.size() - 1, info),
             image::bmp_status::pixel_data_truncated);

    /* biSizeImage == 0 is legal for BI_RGB and must stay accepted. */
    buffer = make_bmp(4, 4);
    put_u32(buffer, 34, 0);
    CHECK_EQ(image::inspect_bmp(buffer.data(), image::k_header_prefix_bytes, buffer.size(), info),
             image::bmp_status::ok);
}

void test_dimension_limits() {
    const auto accepted = make_bmp(image::k_max_dimension, static_cast<std::int32_t>(image::k_max_dimension));
    const image::bmp_info info = inspect(accepted);
    CHECK_EQ(info.width, image::k_max_dimension);
    CHECK_EQ(info.height, image::k_max_dimension);

    image::bmp_info rejected{};
    auto wide = make_bmp(4, 4);
    put_u32(wide, 18, image::k_max_dimension + 1);
    CHECK_EQ(image::inspect_bmp(wide.data(), image::k_header_prefix_bytes, wide.size(), rejected),
             image::bmp_status::invalid_dimension);

    auto tall = make_bmp(4, 4);
    put_u32(tall, 22, image::k_max_dimension + 1);
    CHECK_EQ(image::inspect_bmp(tall.data(), image::k_header_prefix_bytes, tall.size(), rejected),
             image::bmp_status::invalid_dimension);

    auto negative_tall = make_bmp(4, 4);
    put_u32(negative_tall, 22, static_cast<std::uint32_t>(-static_cast<std::int32_t>(image::k_max_dimension) - 1));
    CHECK_EQ(image::inspect_bmp(negative_tall.data(), image::k_header_prefix_bytes, negative_tall.size(), rejected),
             image::bmp_status::invalid_dimension);

    for (const std::uint32_t zero_axis : {0u}) {
        auto zero = make_bmp(4, 4);
        put_u32(zero, 18, zero_axis);
        CHECK_EQ(image::inspect_bmp(zero.data(), image::k_header_prefix_bytes, zero.size(), rejected),
                 image::bmp_status::invalid_dimension);
        zero = make_bmp(4, 4);
        put_u32(zero, 22, zero_axis);
        CHECK_EQ(image::inspect_bmp(zero.data(), image::k_header_prefix_bytes, zero.size(), rejected),
                 image::bmp_status::invalid_dimension);
    }
}

/* TEST-IMG-LIMIT-03: the maximum supported frame plus its header must fit the
 * approved file cap, and the file cap must still refuse one byte more. */
void test_cap_matches_maximum_geometry() {
    const std::size_t pixel_bytes =
        padded_stride(image::k_max_dimension) * static_cast<std::size_t>(image::k_max_dimension);
    const std::size_t required = image::k_header_prefix_bytes + pixel_bytes;
    CHECK(required <= image::k_max_file_bytes);
    CHECK(image::k_max_file_bytes - required < 2u * 1024u * 1024u);

    const auto buffer = make_bmp(image::k_max_dimension, static_cast<std::int32_t>(image::k_max_dimension));
    const image::bmp_info info = inspect(buffer);
    CHECK_EQ(info.pixel_bytes, static_cast<std::uint64_t>(pixel_bytes));
    CHECK(image::rgb565_buffer_bytes(info) <=
          static_cast<std::size_t>(image::k_max_dimension) * image::k_max_dimension * 2u);

    /* TEST-IMG-LIMIT-01: the cap itself is inclusive.  A file whose declared
     * size is exactly 5 MiB is still a valid envelope; only one byte above it is
     * refused.  Without this case a `>` -> `>=` mutation of the production gate
     * would survive. */
    const image::bmp_info at_cap = inspect(buffer, image::k_max_file_bytes);
    CHECK_EQ(at_cap.pixel_bytes, static_cast<std::uint64_t>(pixel_bytes));
    CHECK_EQ(at_cap.width, image::k_max_dimension);

    /* File declared above the cap is refused before any pixel work. */
    image::bmp_info refused{};
    CHECK_EQ(image::inspect_bmp(buffer.data(), image::k_header_prefix_bytes, image::k_max_file_bytes + 1, refused),
             image::bmp_status::invalid_layout);
}

void test_rgb565_conversion() {
    CHECK_EQ(image::rgb565_buffer_bytes(image::bmp_info{}), 0u);
    image::bmp_info info{};
    info.width = 2;
    info.height = 3;
    CHECK_EQ(image::rgb565_buffer_bytes(info), 12u);
    info.width = image::k_max_dimension + 1;
    CHECK_EQ(image::rgb565_buffer_bytes(info), 0u);

    /* Exact bit layout LVGL expects for RGB565: red high, blue low. */
    CHECK_EQ(image::rgb565_buffer_bytes(info), 0u);
    const std::uint8_t red_row[] = {0x00, 0x00, 0xFF};
    std::uint16_t pixel = 0;
    image::convert_bgr_row_to_rgb565(red_row, 1, &pixel);
    CHECK_EQ(pixel, 0xF800u);
    const std::uint8_t green_row[] = {0x00, 0xFF, 0x00};
    image::convert_bgr_row_to_rgb565(green_row, 1, &pixel);
    CHECK_EQ(pixel, 0x07E0u);
    const std::uint8_t blue_row[] = {0xFF, 0x00, 0x00};
    image::convert_bgr_row_to_rgb565(blue_row, 1, &pixel);
    CHECK_EQ(pixel, 0x001Fu);
    const std::uint8_t black_row[] = {0x00, 0x00, 0x00};
    image::convert_bgr_row_to_rgb565(black_row, 1, &pixel);
    CHECK_EQ(pixel, 0x0000u);
    const std::uint8_t white_row[] = {0xFF, 0xFF, 0xFF};
    image::convert_bgr_row_to_rgb565(white_row, 1, &pixel);
    CHECK_EQ(pixel, 0xFFFFu);
    /* BGR order: the first byte is blue even though the value is red. */
    const std::uint8_t bgr_row[] = {0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00};
    std::uint16_t two_pixels[2]{};
    image::convert_bgr_row_to_rgb565(bgr_row, 2, two_pixels);
    CHECK_EQ(two_pixels[0], 0xF800u);
    CHECK_EQ(two_pixels[1], 0x001Fu);
}

void test_reason_strings_are_stable() {
    CHECK(std::strcmp(image::bmp_status_reason(image::bmp_status::ok), "ok") == 0);
    CHECK(std::strlen(image::bmp_status_reason(image::bmp_status::unsupported_bit_depth)) > 0);
    for (const image::bmp_status status :
         {image::bmp_status::ok, image::bmp_status::invalid_signature, image::bmp_status::header_truncated,
          image::bmp_status::unsupported_dib_header, image::bmp_status::unsupported_planes,
          image::bmp_status::unsupported_bit_depth, image::bmp_status::unsupported_compression,
          image::bmp_status::invalid_dimension, image::bmp_status::invalid_layout,
          image::bmp_status::pixel_data_truncated, image::bmp_status::buffer_too_small}) {
        CHECK(image::bmp_status_reason(status) != nullptr);
    }
}

} // namespace

int main() {
    test_envelope_accepts_bottom_up_and_top_down();
    test_stride_padding();
    test_rejects_unsupported_variants();
    test_rejects_bad_signature_and_truncation();
    test_rejects_inconsistent_layout();
    test_dimension_limits();
    test_cap_matches_maximum_geometry();
    test_rgb565_conversion();
    test_reason_strings_are_stable();
    std::printf("test_image_bmp: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
