#include "apps/image/cyberdeck_image_fit.h"

namespace cyberdeck_image {
namespace {

/* Rounded nearest, saturating at 1 so a valid image never collapses to zero. */
std::int32_t rounded_dimension(std::uint64_t numerator, std::uint32_t denominator) {
    if (denominator == 0)
        return 0;
    const std::uint64_t value = (numerator + denominator / 2) / denominator;
    if (value == 0)
        return 1;
    return static_cast<std::int32_t>(value);
}

} // namespace

fit_result fit_to_area(std::uint32_t image_width, std::uint32_t image_height, std::int32_t area_x, std::int32_t area_y,
                       std::int32_t area_width, std::int32_t area_height) {
    fit_result out{};
    if (image_width == 0 || image_height == 0 || area_width <= 0 || area_height <= 0)
        return out;

    const std::uint64_t width = image_width;
    const std::uint64_t height = image_height;
    const std::uint64_t area_w = static_cast<std::uint64_t>(area_width);
    const std::uint64_t area_h = static_cast<std::uint64_t>(area_height);

    std::int32_t fitted_width = 0;
    std::int32_t fitted_height = 0;
    std::uint32_t zoom = k_zoom_identity;
    if (area_w >= width && area_h >= height) {
        /* No upscale: the approved policy keeps small images at native size. */
        fitted_width = static_cast<std::int32_t>(width);
        fitted_height = static_cast<std::int32_t>(height);
    } else if (area_w * height <= area_h * width) {
        /* Width-limited: the horizontal dimension uses the whole area. */
        fitted_width = area_width;
        fitted_height = rounded_dimension(height * area_w, image_width);
        zoom = static_cast<std::uint32_t>((area_w * k_zoom_identity) / width);
    } else {
        fitted_height = area_height;
        fitted_width = rounded_dimension(width * area_h, image_height);
        zoom = static_cast<std::uint32_t>((area_h * k_zoom_identity) / height);
    }
    if (zoom == 0)
        zoom = 1;
    if (zoom > k_zoom_identity)
        zoom = k_zoom_identity;
    if (fitted_width <= 0 || fitted_height <= 0)
        return out;

    out.width = fitted_width;
    out.height = fitted_height;
    out.zoom = zoom;
    out.object_width = image_width;
    out.object_height = image_height;
    /* The object keeps the natural size because LVGL scales around its centre;
     * placing that centre on the fitted rectangle centre is what makes the
     * scaled pixels land exactly inside the area. */
    const std::int64_t rect_x = static_cast<std::int64_t>(area_x) + (area_width - fitted_width) / 2;
    const std::int64_t rect_y = static_cast<std::int64_t>(area_y) + (area_height - fitted_height) / 2;
    out.object_x = static_cast<std::int32_t>(rect_x - (static_cast<std::int64_t>(image_width) - fitted_width) / 2);
    out.object_y = static_cast<std::int32_t>(rect_y - (static_cast<std::int64_t>(image_height) - fitted_height) / 2);
    out.valid = true;
    return out;
}

} // namespace cyberdeck_image
