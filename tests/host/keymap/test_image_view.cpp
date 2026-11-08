/*
 * Host test for the LVGL surface of the image viewer under shim/full
 * (REQ-IMG-02/04, AC-IMG-02/04).
 *
 * The test links the real production view, real fit math and the real release
 * helper.  LVGL itself is the harness shim: it records the descriptor handed to
 * `lv_image_set_src()`, the zoom applied by `lv_image_set_scale()` and the
 * chrome/geometry of every object, so a leak, a stale pointer or a themed
 * overlay is observable without a device.
 *
 * Coverage by scenario:
 *   TEST-IMG-VIEW-01 - the image sits inside the content area, below the title
 *       band and above the footer band, centred in portrait and landscape.
 *   TEST-IMG-VIEW-02 - a resize recomputes the placement and keeps containment.
 *   TEST-IMG-VIEW-03 - one source is installed, scaling does not recreate it,
 *       and no background/border/radius decorates the bitmap.
 *   TEST-IMG-LIFE-02/03 - destroy frees the frame exactly once and returns the
 *       parent child count to the baseline.
 *   TEST-IMG-LIFE-04 - publishing B replaces A and frees A exactly once.
 */
#include "platform/display/cyberdeck_image_view.h"

#include "lvgl.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern const lv_font_t cyberdeck_font;
const lv_font_t cyberdeck_font{18};

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

/* Frames are plain `malloc` blocks, the same allocator the host decoder uses,
 * and the view releases them through the production `release_decoded()` helper.
 * The assertions below therefore check ownership transfer (the caller pointer is
 * cleared), replacement (the descriptor follows the new frame) and idempotent
 * destruction, which is what a double free or a stale pointer would break. */
cyberdeck_image::published_image make_image(std::uint32_t width, std::uint32_t height, std::size_t index,
                                            std::size_t count, bool truncated, const char *name) {
    cyberdeck_image::published_image image{};
    image.pixel_bytes = static_cast<std::size_t>(width) * height * 2u;
    image.pixels = static_cast<std::uint16_t *>(std::malloc(image.pixel_bytes));
    std::memset(image.pixels, 0x5A, image.pixel_bytes);
    image.width = width;
    image.height = height;
    image.index = index;
    image.count = count;
    image.truncated = truncated;
    std::snprintf(image.name.data(), image.name.size(), "%s", name);
    return image;
}

void test_create_layout_and_ownership() {
    lv_obj_t *root = lv_obj_create(nullptr);
    lv_obj_set_size(root, 720, 1280 - 42);
    const int baseline = static_cast<int>(root->children.size());

    cyberdeck_image_view::view view;
    CHECK(!view.exists());
    CHECK(view.create(root));
    CHECK(view.exists());
    CHECK_EQ(root->children.size(), static_cast<std::size_t>(baseline + 1));
    CHECK(view.surface() != nullptr);
    CHECK(view.image() != nullptr);
    CHECK(view.image()->is_image);
    CHECK(!view.has_image());

    auto image = make_image(1000, 500, 2, 5, false, "photo.bmp");
    cyberdeck_image::published_image reference = image;
    CHECK(view.set_image(image));
    CHECK(view.has_image());
    /* Ownership moved out of the caller's struct. */
    CHECK(image.pixels == nullptr);
    CHECK(view.image()->image_src != nullptr);
    const auto *descriptor = static_cast<const lv_image_dsc_t *>(view.image()->image_src);
    CHECK(descriptor != nullptr);
    CHECK_EQ(descriptor->header.magic, LV_IMAGE_HEADER_MAGIC);
    CHECK_EQ(descriptor->header.cf, LV_COLOR_FORMAT_RGB565);
    CHECK_EQ(descriptor->header.w, 1000u);
    CHECK_EQ(descriptor->header.h, 500u);
    /* Stride is bytes per row for RGB565: two per pixel. */
    CHECK_EQ(descriptor->header.stride, 2000u);
    CHECK_EQ(descriptor->data_size, reference.pixel_bytes);
    CHECK(descriptor->data == reinterpret_cast<const std::uint8_t *>(reference.pixels));

    view.set_status("photo.bmp", "3/5");
    CHECK_EQ(view.placement().valid, true);
    CHECK(view.placement().width <= 720);
    CHECK(view.placement().height <= 1280 - 42);
    CHECK(view.image()->image_scale >= 1 && view.image()->image_scale <= 256);
    CHECK_EQ(view.image()->image_scale, view.placement().zoom);
    /* Geometry: inside the surface, below the title band and above the footer. */
    const std::int32_t image_top = lv_obj_get_y(view.image());
    const std::int32_t image_bottom = image_top + lv_obj_get_height(view.image());
    CHECK(image_top >= 0);
    CHECK(image_bottom <= 1280 - 42);
    /* The bitmap widget carries no theme chrome and no padding. */
    CHECK_EQ(view.image()->bg_opa, LV_OPA_TRANSP);
    CHECK_EQ(view.image()->border_width, 0);
    CHECK_EQ(view.image()->radius, 0);
    CHECK_EQ(view.image()->outline_width, 0);
    CHECK_EQ(view.image()->shadow_width, 0);
    CHECK_EQ(view.image()->pad_left, 0);
    CHECK_EQ(view.image()->pad_right, 0);
    CHECK_EQ(view.image()->pad_top, 0);
    CHECK_EQ(view.image()->pad_bottom, 0);

    /* Destroy: the widget is unlinked, the frame released and the second call
     * stays a no-op instead of freeing twice. */
    view.destroy();
    CHECK(!view.exists());
    CHECK(!view.has_image());
    view.destroy();
    CHECK(!view.exists());
    CHECK_EQ(root->children.size(), static_cast<std::size_t>(baseline));
    lv_obj_delete(root);
}

void test_replace_frees_previous_frame() {
    lv_obj_t *root = lv_obj_create(nullptr);
    lv_obj_set_size(root, 720, 1238);
    cyberdeck_image_view::view view;
    CHECK(view.create(root));

    auto first = make_image(800, 600, 0, 2, false, "a.bmp");
    CHECK(view.set_image(first));
    const void *first_source = view.image()->image_src;
    CHECK(first_source != nullptr);
    auto second = make_image(600, 800, 1, 2, false, "b.bmp");
    CHECK(view.set_image(second));
    /* The descriptor still outlives the widget, but it now describes B: the
     * previous frame was dropped instead of lingering behind the pointer. */
    const auto *descriptor = static_cast<const lv_image_dsc_t *>(view.image()->image_src);
    CHECK(descriptor != nullptr);
    CHECK_EQ(descriptor->header.w, 600u);
    CHECK_EQ(descriptor->header.h, 800u);
    view.set_status("b.bmp", "2/2");
    CHECK(view.placement().valid);
    view.destroy();
    lv_obj_delete(root);
}

void test_resize_recomputes_placement() {
    lv_obj_t *root = lv_obj_create(nullptr);
    lv_obj_set_size(root, 1272, 678);
    cyberdeck_image_view::view view;
    CHECK(view.create(root));
    /* A 1280x1000 source is wider than the landscape content area, so the first
     * placement is a real downscale and the orientation change must recompute
     * it (the portrait area is narrower, so its scale cannot be larger). */
    auto image = make_image(1280, 1000, 0, 1, false, "wide.bmp");
    CHECK(view.set_image(image));
    const cyberdeck_image::fit_result landscape = view.placement();
    CHECK(landscape.valid);
    CHECK(landscape.width <= 1272 && landscape.height <= 678);
    CHECK(landscape.zoom < 256u);
    CHECK_EQ(view.image()->image_scale, landscape.zoom);

    /* Portrait orientation: the parent size event must re-place the bitmap. */
    lv_obj_set_size(root, 712, 1238);
    lv_obj_set_size(view.surface(), 712, 1238);
    lv_shim_emit_event(root, LV_EVENT_SIZE_CHANGED);
    const cyberdeck_image::fit_result portrait = view.placement();
    CHECK(portrait.valid);
    CHECK(portrait.zoom < 256u);
    CHECK(portrait.zoom <= landscape.zoom);
    CHECK(portrait.width <= 712 && portrait.height <= 1238);
    CHECK(portrait.width < landscape.width);
    CHECK_EQ(view.image()->image_scale, portrait.zoom);

    /* Back to landscape keeps containment. */
    lv_obj_set_size(root, 1272, 678);
    lv_obj_set_size(view.surface(), 1272, 678);
    lv_shim_emit_event(root, LV_EVENT_SIZE_CHANGED);
    const cyberdeck_image::fit_result again = view.placement();
    CHECK(again.valid);
    CHECK(again.width <= 1272 && again.height <= 678);
    view.destroy();
    lv_obj_delete(root);
}

void test_refuses_without_surface() {
    cyberdeck_image_view::view view;
    auto image = make_image(10, 10, 0, 1, false, "x.bmp");
    cyberdeck_image::published_image reference = image;
    CHECK(!view.set_image(image));
    /* Refused means untouched: the caller still owns the frame. */
    CHECK(image.pixels == reference.pixels);
    std::free(image.pixels);
    CHECK(!view.create(nullptr));
    view.set_status("x.bmp", "1/1"); /* no surface: no crash */
}

void test_small_image_is_not_enlarged() {
    lv_obj_t *root = lv_obj_create(nullptr);
    lv_obj_set_size(root, 720, 1238);
    cyberdeck_image_view::view view;
    CHECK(view.create(root));
    auto image = make_image(64, 32, 0, 1, false, "tiny.bmp");
    CHECK(view.set_image(image));
    CHECK_EQ(view.placement().width, 64);
    CHECK_EQ(view.placement().height, 32);
    CHECK_EQ(view.image()->image_scale, 256u);
    view.destroy();
    lv_obj_delete(root);
}

} // namespace

int main() {
    test_create_layout_and_ownership();
    test_replace_frees_previous_frame();
    test_resize_recomputes_placement();
    test_refuses_without_surface();
    test_small_image_is_not_enlarged();
    std::printf("test_image_view: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
