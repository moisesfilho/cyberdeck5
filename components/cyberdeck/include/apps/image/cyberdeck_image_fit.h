#pragma once

#include <cstdint>

/* Aspect-preserving placement of one image inside a rectangular content area.
 *
 * Approved policy: the image is scaled by `min(area_w / w, area_h / h)` and
 * never enlarged, so a small bitmap keeps its native size and a big one is
 * contained with both dimensions inside the area.  The module is pure integer
 * math: no LVGL, no floating point and no division by a user-supplied zero. */
namespace cyberdeck_image {

/** LVGL scale unit: 256 == 1:1. */
inline constexpr std::uint32_t k_zoom_identity = 256;

struct fit_result {
    /** Image rectangle actually covered inside the area, in pixels. */
    std::int32_t width = 0;
    std::int32_t height = 0;
    /** LVGL zoom for `lv_image_set_scale()` (0..256). */
    std::uint32_t zoom = 0;
    /** Position of the natural-size object whose centre is the rect centre. */
    std::int32_t object_x = 0;
    std::int32_t object_y = 0;
    std::uint32_t object_width = 0;
    std::uint32_t object_height = 0;
    bool valid = false;
};

/* `area_x`/`area_y` are absolute coordinates (the object is positioned with
 * `lv_obj_set_pos`).  Returns `valid = false` for a zero image dimension, a
 * non-positive area or any geometry that cannot be represented. */
fit_result fit_to_area(std::uint32_t image_width, std::uint32_t image_height, std::int32_t area_x, std::int32_t area_y,
                       std::int32_t area_width, std::int32_t area_height);

} // namespace cyberdeck_image
