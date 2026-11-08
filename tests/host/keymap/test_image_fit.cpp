/*
 * Host test for the aspect-preserving placement of the image viewer
 * (REQ-IMG-02 / AC-IMG-02: the whole image is visible and centred without
 * distortion).
 *
 * Pure integer math only: no LVGL, ESP-IDF, filesystem or hardware.
 *
 * Coverage by scenario:
 *   TEST-IMG-FIT-01 - 1000x500 inside 700x600 fits exactly 700x350.
 *   TEST-IMG-FIT-02 - portrait and landscape always stay inside the area.
 *   TEST-IMG-FIT-03 - zero image or non-positive area fails closed (no
 *       division by zero, so UBSan stays clean).
 *   TEST-IMG-FIT-04 - the single upscale policy: never enlarge.
 *   TEST-IMG-VIEW-01 - the natural-size object is centred on the fitted
 *       rectangle, which is what makes `lv_image_set_scale()` land inside it.
 */
#include "apps/image/cyberdeck_image_fit.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>

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

void test_landscape_fit() {
    const image::fit_result fit = image::fit_to_area(1000, 500, 0, 0, 700, 600);
    CHECK(fit.valid);
    CHECK_EQ(fit.width, 700);
    CHECK_EQ(fit.height, 350);
    /* 700/1000 = 0.7 -> 179.2 in LVGL units; the quantisation error must stay
     * within one pixel of the exact fit. */
    CHECK(fit.zoom >= 178 && fit.zoom <= 180);
    CHECK(fit.width <= 700 && fit.height <= 600);
    /* Aspect error <= 1 px: 700/350 == 2.0 exactly. */
    CHECK(fit.width * 2 == fit.height * 4);
    CHECK_EQ(fit.object_width, 1000u);
    CHECK_EQ(fit.object_height, 500u);
    /* Object centre == fitted rectangle centre. */
    CHECK_EQ(fit.object_x * 2 + static_cast<std::int64_t>(fit.object_width),
             static_cast<std::int64_t>(0 + (700 - fit.width) / 2) * 2 + fit.width);
    CHECK_EQ(fit.object_y * 2 + static_cast<std::int64_t>(fit.object_height),
             static_cast<std::int64_t>(0 + (600 - fit.height) / 2) * 2 + fit.height);
}

void test_portrait_and_landscape_areas() {
    struct case_t {
        std::uint32_t width;
        std::uint32_t height;
        std::int32_t area_width;
        std::int32_t area_height;
    };
    /* Real content areas after the 42 px header on the Tab5 (720x1280 panel)
     * and in landscape, plus the extreme source aspect ratios. */
    const case_t cases[] = {
        {704, 1203, 712, 1230}, {1256, 643, 712, 1230}, {704, 1203, 1272, 678},
        {1256, 643, 1272, 678}, {1, 1280, 712, 1230},   {1280, 1, 712, 1230},
    };
    for (const auto &item : cases) {
        const image::fit_result fit =
            image::fit_to_area(item.width, item.height, 4, 24, item.area_width, item.area_height);
        CHECK(fit.valid);
        CHECK(fit.width >= 1 && fit.width <= item.area_width);
        CHECK(fit.height >= 1 && fit.height <= item.area_height);
        CHECK(fit.zoom >= 1 && fit.zoom <= image::k_zoom_identity);
        /* At least one axis uses the whole area unless the image is smaller. */
        const bool width_bound =
            fit.width == item.area_width || item.width < static_cast<std::uint32_t>(item.area_width);
        const bool height_bound =
            fit.height == item.area_height || item.height < static_cast<std::uint32_t>(item.area_height);
        CHECK(width_bound || height_bound);
        /* Fitted rectangle stays inside the area.  The *object* keeps the
         * natural size and may legitimately start at a negative coordinate
         * (the widget is larger than the downscaled bitmap); what must stay
         * contained is the fitted rectangle, which is where the scaled pixels
         * land. */
        const std::int64_t rect_x =
            static_cast<std::int64_t>(fit.object_x) + (static_cast<std::int64_t>(fit.object_width) - fit.width) / 2;
        const std::int64_t rect_y =
            static_cast<std::int64_t>(fit.object_y) + (static_cast<std::int64_t>(fit.object_height) - fit.height) / 2;
        CHECK(rect_x >= 4);
        CHECK(rect_y >= 24);
        CHECK(rect_x + fit.width <= 4 + item.area_width);
        CHECK(rect_y + fit.height <= 24 + item.area_height);
    }
}

void test_never_enlarges() {
    /* Small bitmap, big area: native size and 1:1 zoom. */
    const image::fit_result small = image::fit_to_area(100, 50, 10, 10, 700, 600);
    CHECK(small.valid);
    CHECK_EQ(small.width, 100);
    CHECK_EQ(small.height, 50);
    CHECK_EQ(small.zoom, image::k_zoom_identity);
    CHECK_EQ(small.object_x, 10 + (700 - 100) / 2);
    CHECK_EQ(small.object_y, 10 + (600 - 50) / 2);

    /* Exactly the area: still 1:1. */
    const image::fit_result exact = image::fit_to_area(700, 600, 0, 0, 700, 600);
    CHECK(exact.valid);
    CHECK_EQ(exact.zoom, image::k_zoom_identity);
    CHECK_EQ(exact.width, 700);
    CHECK_EQ(exact.height, 600);
}

void test_invalid_geometry_fails_closed() {
    CHECK(!image::fit_to_area(0, 500, 0, 0, 700, 600).valid);
    CHECK(!image::fit_to_area(500, 0, 0, 0, 700, 600).valid);
    CHECK(!image::fit_to_area(0, 0, 0, 0, 700, 600).valid);
    CHECK(!image::fit_to_area(500, 500, 0, 0, 0, 600).valid);
    CHECK(!image::fit_to_area(500, 500, 0, 0, 700, 0).valid);
    CHECK(!image::fit_to_area(500, 500, 0, 0, -1, 600).valid);
    CHECK(!image::fit_to_area(500, 500, 0, 0, 700, -600).valid);
    for (const auto &bad : {image::fit_to_area(0, 0, 0, 0, 0, 0), image::fit_to_area(10, 10, 0, 0, 0, 5)}) {
        CHECK_EQ(bad.width, 0);
        CHECK_EQ(bad.height, 0);
        CHECK_EQ(bad.zoom, 0u);
    }
}

void test_extreme_downscale_keeps_a_pixel() {
    /* A 1280x1 strip inside a 700x600 area: width-limited, height rounds to 1. */
    const image::fit_result strip = image::fit_to_area(1280, 1, 0, 0, 700, 600);
    CHECK(strip.valid);
    CHECK_EQ(strip.width, 700);
    CHECK_EQ(strip.height, 1);
    /* A 1x1280 strip: height-limited, width rounds to 1 and never to 0. */
    const image::fit_result column = image::fit_to_area(1, 1280, 0, 0, 700, 600);
    CHECK(column.valid);
    CHECK_EQ(column.height, 600);
    CHECK_EQ(column.width, 1);
    CHECK(column.zoom >= 1);
}

void test_monotonic_zoom() {
    std::uint32_t previous = 0;
    for (std::int32_t area = 40; area <= 1280; area += 40) {
        const image::fit_result fit = image::fit_to_area(1000, 800, 0, 0, area, area);
        CHECK(fit.valid);
        CHECK(fit.zoom >= previous);
        CHECK(fit.zoom <= image::k_zoom_identity);
        previous = fit.zoom;
        CHECK(fit.width <= area && fit.height <= area);
    }
}

} // namespace

int main() {
    test_landscape_fit();
    test_portrait_and_landscape_areas();
    test_never_enlarges();
    test_invalid_geometry_fails_closed();
    test_extreme_downscale_keeps_a_pixel();
    test_monotonic_zoom();
    std::printf("test_image_fit: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
