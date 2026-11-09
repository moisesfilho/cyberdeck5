/*
 * Host test for the single extension table and shared limits of the image
 * viewer (REQ-IMGF-01 / AC-IMGF-01, TEST-IMGF-FMT-01/02 and
 * NEG-IMGF-LIMIT-05/06/07).
 *
 * The test links the real production table.  Every expectation is derived from
 * the production constants instead of a literal copy, so raising a limit does
 * not silently keep the old numbers alive here.
 */
#include "apps/image/cyberdeck_image_formats.h"

#include <cstdio>
#include <cstring>
#include <string>

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

void test_known_extensions() {
    CHECK_EQ(image::format_from_name("a.bmp"), image::image_format::bmp);
    CHECK_EQ(image::format_from_name("A.BMP"), image::image_format::bmp);
    CHECK_EQ(image::format_from_name("a.BmP"), image::image_format::bmp);
    CHECK_EQ(image::format_from_name("fotos/praia.jpg"), image::image_format::jpeg);
    CHECK_EQ(image::format_from_name("FOTO.JPG"), image::image_format::jpeg);
    CHECK_EQ(image::format_from_name("retrato.jpeg"), image::image_format::jpeg);
    CHECK_EQ(image::format_from_name("RETRATO.JPEG"), image::image_format::jpeg);
    CHECK_EQ(image::format_from_name("mapa.png"), image::image_format::png);
    CHECK_EQ(image::format_from_name("MAPA.PNG"), image::image_format::png);
}

void test_refused_names() {
    /* Only the extension decides, and the file name is bounded. */
    CHECK_EQ(image::format_from_name("a.gif"), image::image_format::none);
    CHECK_EQ(image::format_from_name("a.webp"), image::image_format::none);
    CHECK_EQ(image::format_from_name("a.qoi"), image::image_format::none);
    CHECK_EQ(image::format_from_name("a.tif"), image::image_format::none);
    CHECK_EQ(image::format_from_name("a.heic"), image::image_format::none);
    CHECK_EQ(image::format_from_name("a.txt"), image::image_format::none);
    CHECK_EQ(image::format_from_name("semextensao"), image::image_format::none);
    CHECK_EQ(image::format_from_name("a.bmp.txt"), image::image_format::none);
    CHECK_EQ(image::format_from_name("a.pn"), image::image_format::none);
    CHECK_EQ(image::format_from_name("a.jpe"), image::image_format::none);
    /* Tester decision (2026-11-09): TEST-IMGF-FMT-02 listed `a.jpg.png` among the
     * refused names, but the extension is the last one, exactly as `a.jpg.bmp`
     * resolved to BMP before this delivery.  Rejecting multi-dot names here would
     * change the accepted BMP set (regression) or duplicate the rule per format,
     * so the entry is superseded: the last extension selects the format and the
     * content check (NEG-IMGF-EXT-02/05/06) still refuses a disguised file. */
    CHECK_EQ(image::format_from_name("a.jpg.png"), image::image_format::png);
    CHECK_EQ(image::format_from_name("a.png.jpg"), image::image_format::jpeg);
    CHECK_EQ(image::format_from_name("a.jpg.bmp"), image::image_format::bmp);
    CHECK_EQ(image::format_from_name("a.png.jpeg"), image::image_format::jpeg);
    CHECK_EQ(image::format_from_name(".bmp"), image::image_format::none);
    CHECK_EQ(image::format_from_name(".png"), image::image_format::none);
    CHECK_EQ(image::format_from_name(""), image::image_format::none);

    const std::string too_long(image::k_max_name_bytes + 1, 'a');
    CHECK_EQ(image::format_from_name(too_long), image::image_format::none);
    std::string shortest = "a.bmp";
    CHECK_EQ(image::format_from_name(shortest), image::image_format::bmp);

    /* The bound is inclusive: a name of exactly k_max_name_bytes is accepted. */
    std::string at_limit = "a";
    at_limit.append(image::k_max_name_bytes - 5, 'b');
    at_limit += ".bmp";
    CHECK_EQ(at_limit.size(), image::k_max_name_bytes);
    CHECK_EQ(image::format_from_name(at_limit), image::image_format::bmp);
}

void test_format_names() {
    CHECK(std::strcmp(image::format_name(image::image_format::bmp), "bmp") == 0);
    CHECK(std::strcmp(image::format_name(image::image_format::jpeg), "jpeg") == 0);
    CHECK(std::strcmp(image::format_name(image::image_format::png), "png") == 0);
    CHECK(std::strcmp(image::format_name(image::image_format::none), "none") == 0);
}

void test_frame_geometry_limits() {
    std::size_t bytes = 12345;
    CHECK(!image::frame_bytes(0, 4, bytes));
    CHECK_EQ(bytes, 0u);
    bytes = 1;
    CHECK(!image::frame_bytes(4, 0, bytes));
    CHECK_EQ(bytes, 0u);

    CHECK(image::frame_bytes(image::k_max_dimension, image::k_max_dimension, bytes));
    CHECK_EQ(bytes, static_cast<std::size_t>(image::k_max_dimension) * image::k_max_dimension * 2u);
    CHECK_EQ(bytes, image::k_max_frame_bytes);

    /* NEG-IMGF-LIMIT-05: one pixel past either axis is refused before any
     * multiplication happens. */
    CHECK(!image::frame_bytes(image::k_max_dimension + 1, 1, bytes));
    CHECK(!image::frame_bytes(1, image::k_max_dimension + 1, bytes));
    CHECK_EQ(bytes, 0u);

    /* NEG-IMGF-LIMIT-07: a declared 0xFFFFFFFF geometry must not wrap into a
     * small allocation. */
    CHECK(!image::frame_bytes(0xFFFFFFFFu, 0xFFFFFFFFu, bytes));
    CHECK_EQ(bytes, 0u);
    CHECK(!image::frame_bytes(0xFFFFFFFFu, 1, bytes));

    /* Odd geometries stay exact: the frame is width*height*2 with no padding. */
    CHECK(image::frame_bytes(3, 5, bytes));
    CHECK_EQ(bytes, 30u);
    CHECK(image::frame_bytes(1, 1, bytes));
    CHECK_EQ(bytes, 2u);
}

} // namespace

int main() {
    test_known_extensions();
    test_refused_names();
    test_format_names();
    test_frame_geometry_limits();
    std::printf("test_image_formats: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
