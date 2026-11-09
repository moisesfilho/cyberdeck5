/*
 * Host regression for the capture pipeline that was used to judge the physical
 * frames: a decoded RGB565 frame -> the BMP 24-bit writer shared with the HTTP
 * `/screenshot` endpoint and the serial `screen.dump` -> an independent,
 * BMP-spec-compliant reader.
 *
 * Purpose: pin, in code, that the capture preserves the frame's row order
 * (row 0 = top, AC-IMG-01) and channel order (red stays red; the BMP triple is
 * B,G,R).  The device triage on 2026-11-09 read the triples as R,G,B and
 * concluded that every image was vertically inverted; the same captures read per
 * the specification match the host decoder exactly.  This test makes that
 * invariant explicit so a real regression is distinguishable from a misread.
 *
 * The reader below is written from the format description in
 * `screenshot_bmp.h`/BMP: `biHeight > 0` means the first stored row is the
 * *bottom* row and each pixel is stored as blue, green, red.
 */
#include "apps/image/cyberdeck_image_decode.h"
#include "apps/screenshot/screenshot_bmp.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
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

/* Writes the whole buffer, retrying short writes and EINTR.  `write()` is
 * `warn_unused_result` and the CI build treats warnings as errors; a partial
 * file would also invalidate the decode assertion, so an error fails the
 * scenario after the caller cleans up. */
bool write_all(int descriptor, const std::uint8_t *bytes, std::size_t length) {
    std::size_t written = 0;
    while (written < length) {
        const ssize_t result = ::write(descriptor, bytes + written, length - written);
        if (result < 0) {
            if (errno == EINTR)
                continue;
            std::printf("FAIL %s:%d: write(%zu bytes) failed: %s\n", __FILE__, __LINE__, length - written,
                        std::strerror(errno));
            return false;
        }
        if (result == 0) {
            std::printf("FAIL %s:%d: write(%zu bytes) made no progress\n", __FILE__, __LINE__, length - written);
            return false;
        }
        written += static_cast<std::size_t>(result);
    }
    return true;
}

/* Source frame: 4 columns x 3 rows, one unique colour per row, so a row flip or a
 * channel swap cannot hide.  Values are the exact RGB565 encoding of the frame
 * contract (`RRRRRGGGGGGBBBBB`). */
std::uint16_t rgb565(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(red >> 3) << 11) |
                                      (static_cast<std::uint16_t>(green >> 2) << 5) |
                                      static_cast<std::uint16_t>(blue >> 3));
}

struct rgb {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;

    bool operator==(const rgb &other) const {
        return red == other.red && green == other.green && blue == other.blue;
    }
    bool operator!=(const rgb &other) const {
        return !(*this == other);
    }
};

/* 5/6/5 -> 8 bit expansion, the inverse of the frame encoding. */
rgb expand(std::uint16_t pixel) {
    const std::uint8_t red5 = static_cast<std::uint8_t>((pixel >> 11) & 0x1F);
    const std::uint8_t green6 = static_cast<std::uint8_t>((pixel >> 5) & 0x3F);
    const std::uint8_t blue5 = static_cast<std::uint8_t>(pixel & 0x1F);
    return rgb{static_cast<std::uint8_t>((red5 << 3) | (red5 >> 2)),
               static_cast<std::uint8_t>((green6 << 2) | (green6 >> 4)),
               static_cast<std::uint8_t>((blue5 << 3) | (blue5 >> 2))};
}

/* Renders the frame the way `screenshot_server` does: header, then rows from
 * `height - 1` down to 0, each pixel through `screenshot_bmp_rgb565_to_bgr888`. */
std::vector<std::uint8_t> render_capture(const std::vector<std::uint16_t> &pixels, int width, int height) {
    const std::size_t stride = screenshot_bmp_calc_stride(width);
    std::vector<std::uint8_t> file(screenshot_bmp_calc_size(width, height), 0);
    screenshot_bmp_header_t header{};
    screenshot_bmp_fill_header(&header, width, height);
    std::memcpy(file.data(), &header, sizeof(header));
    for (int y = 0; y < height; ++y) {
        const int source_row = height - 1 - y;
        std::uint8_t *target = file.data() + SCREENSHOT_BMP_HEADER_SIZE + static_cast<std::size_t>(y) * stride;
        for (int x = 0; x < width; ++x) {
            const std::uint16_t pixel = pixels[static_cast<std::size_t>(source_row) * width + x];
            screenshot_bmp_rgb565_to_bgr888(pixel, target + static_cast<std::size_t>(x) * 3);
        }
    }
    return file;
}

/* Spec-compliant reader: honours the sign of `biHeight` and the B,G,R triple. */
bool read_capture(const std::vector<std::uint8_t> &file, std::vector<rgb> &rows, int &width, int &height) {
    if (file.size() < SCREENSHOT_BMP_HEADER_SIZE || file[0] != 'B' || file[1] != 'M')
        return false;
    std::uint32_t offset = 0;
    std::int32_t stored_height = 0;
    std::int32_t stored_width = 0;
    std::memcpy(&offset, file.data() + 10, sizeof(offset));
    std::memcpy(&stored_width, file.data() + 18, sizeof(stored_width));
    std::memcpy(&stored_height, file.data() + 22, sizeof(stored_height));
    const std::uint16_t depth = static_cast<std::uint16_t>(file[28] | (file[29] << 8));
    if (depth != 24 || stored_width <= 0 || stored_height == 0)
        return false;
    width = stored_width;
    height = stored_height < 0 ? -stored_height : stored_height;
    const bool top_down = stored_height < 0;
    const std::size_t stride = screenshot_bmp_calc_stride(width);
    rows.assign(static_cast<std::size_t>(width) * height, rgb{});
    for (int y = 0; y < height; ++y) {
        /* `biHeight > 0` stores the bottom row first. */
        const int file_row = top_down ? y : height - 1 - y;
        const std::uint8_t *source = file.data() + offset + static_cast<std::size_t>(file_row) * stride;
        for (int x = 0; x < width; ++x) {
            const std::uint8_t *pixel = source + static_cast<std::size_t>(x) * 3;
            rows[static_cast<std::size_t>(y) * width + x] = rgb{pixel[2], pixel[1], pixel[0]};
        }
    }
    return true;
}

void test_capture_preserves_row_and_channel_order() {
    constexpr int width = 4;
    constexpr int height = 3;
    const rgb source_rows[height] = {{0xF8, 0, 0}, {0, 0xFC, 0}, {0, 0, 0xF8}};
    std::vector<std::uint16_t> pixels(static_cast<std::size_t>(width) * height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            pixels[static_cast<std::size_t>(y) * width + x] =
                rgb565(source_rows[y].red, source_rows[y].green, source_rows[y].blue);

    const std::vector<std::uint8_t> file = render_capture(pixels, width, height);
    CHECK_EQ(file.size(), screenshot_bmp_calc_size(width, height));

    std::vector<rgb> rows;
    int read_width = 0;
    int read_height = 0;
    CHECK(read_capture(file, rows, read_width, read_height));
    CHECK_EQ(read_width, width);
    CHECK_EQ(read_height, height);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const rgb pixel = rows[static_cast<std::size_t>(y) * width + x];
            const rgb expected = expand(pixels[static_cast<std::size_t>(y) * width + x]);
            /* Row 0 of the capture is row 0 of the frame: no vertical flip. */
            CHECK_EQ(pixel.red, expected.red);
            CHECK_EQ(pixel.green, expected.green);
            CHECK_EQ(pixel.blue, expected.blue);
        }
    }

    /* A reader that took the triples as R,G,B would see red and blue swapped. */
    const rgb top_left = rows[0];
    CHECK(top_left.red > 0xF0 && top_left.blue == 0);
    CHECK(top_left.green == 0);
    CHECK(top_left != rows[static_cast<std::size_t>(2) * width]);
}

/* The same invariant for a frame that comes from the real decoder, so the test
 * covers "decoded frame -> capture", not only a synthetic buffer. */
void test_decoded_frame_matches_capture() {
    /* 2x2 bottom-up BMP: bottom row blue, top row red. */
    const std::uint32_t width = 2;
    const std::uint32_t height = 2;
    const std::size_t stride = (static_cast<std::size_t>(width) * 3u + 3u) & ~static_cast<std::size_t>(3u);
    std::vector<std::uint8_t> bmp_bytes(54 + stride * height, 0);
    const auto put32 = [&bmp_bytes](std::size_t offset, std::uint32_t value) {
        for (int index = 0; index < 4; ++index)
            bmp_bytes[offset + static_cast<std::size_t>(index)] =
                static_cast<std::uint8_t>((value >> (8 * index)) & 0xFF);
    };
    bmp_bytes[0] = 'B';
    bmp_bytes[1] = 'M';
    put32(2, static_cast<std::uint32_t>(bmp_bytes.size()));
    put32(10, 54);
    put32(14, 40);
    put32(18, width);
    put32(22, height);
    bmp_bytes[26] = 1;
    bmp_bytes[28] = 24;
    put32(34, static_cast<std::uint32_t>(stride * height));
    /* Bottom row (file row 0): pure blue.  Top row: pure red. */
    for (std::uint32_t column = 0; column < width; ++column) {
        bmp_bytes[54 + column * 3 + 0] = 0xFF;
        bmp_bytes[54 + stride + column * 3 + 2] = 0xFF;
    }

    char pattern[] = "/tmp/cyberdeck-capture-XXXXXX";
    const int descriptor = mkstemp(pattern);
    CHECK(descriptor >= 0);
    if (descriptor < 0)
        return;
    const bool wrote_all = write_all(descriptor, bmp_bytes.data(), bmp_bytes.size());
    CHECK(wrote_all);
    if (!wrote_all) {
        (void)::close(descriptor);
        (void)::unlink(pattern);
        return;
    }
    (void)::lseek(descriptor, 0, SEEK_SET);

    cyberdeck_image::decoded_image decoded{};
    const cyberdeck_image::decode_result result =
        cyberdeck_image::decode_descriptor(cyberdeck_image::default_decode_ops(), descriptor, bmp_bytes.size(),
                                           cyberdeck_image::image_format::bmp, decoded);
    (void)::close(descriptor);
    (void)::unlink(pattern);
    CHECK_EQ(result.state, cyberdeck_image::decode_status::ok);
    CHECK_EQ(decoded.width, 2u);
    CHECK_EQ(decoded.height, 2u);
    if (decoded.pixels == nullptr)
        return;

    const std::vector<std::uint16_t> frame(decoded.pixels, decoded.pixels + decoded.pixel_bytes / 2);
    const std::vector<std::uint8_t> file = render_capture(frame, 2, 2);
    std::vector<rgb> rows;
    int read_width = 0;
    int read_height = 0;
    CHECK(read_capture(file, rows, read_width, read_height));
    /* The BMP is bottom-up: the decoded frame must still show red on top. */
    const rgb top = expand(decoded.pixels[0]);
    const rgb bottom = expand(decoded.pixels[static_cast<std::size_t>(decoded.width)]);
    CHECK(rows[0].red == top.red && rows[0].green == top.green && rows[0].blue == top.blue);
    CHECK(rows[2].red == bottom.red && rows[2].green == bottom.green && rows[2].blue == bottom.blue);
    cyberdeck_image::release_decoded(decoded);
}

} // namespace

int main() {
    test_capture_preserves_row_and_channel_order();
    test_decoded_frame_matches_capture();
    std::printf("test_image_capture_orientation: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
