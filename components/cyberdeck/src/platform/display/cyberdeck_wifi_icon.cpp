#include "platform/display/cyberdeck_wifi_icon.h"
#include "platform/display/cyberdeck_wifi_indicator.h"
#include <cmath>
#if __has_include("lvgl.h")
#include "lvgl.h"
#define CYBERDECK_WIFI_ICON_HAS_LVGL 1
#endif

#if CYBERDECK_WIFI_ICON_HAS_LVGL
namespace {

constexpr uint32_t WIFI_ICON_DEFAULT_COLOR = 0x8A8A8A;
constexpr int32_t WIFI_ICON_DEFAULT_WIDTH = 216;

static void wifi_icon_size_changed_cb(lv_event_t *e)
{
    lv_obj_t *icon = static_cast<lv_obj_t *>(lv_event_get_target(e));
    if (icon != nullptr) {
        int32_t w = lv_obj_get_width(icon);
        if (w > 0) {
            cyberdeck_wifi_icon_update_layout(icon, w);
        }
    }
}

void configure_arc(lv_obj_t *arc, int32_t diameter, lv_color_t color, int32_t x, int32_t y)
{
    lv_obj_set_size(arc, diameter, diameter);
    lv_arc_set_bg_angles(arc, static_cast<int32_t>(CYBERDECK_WIFI_ICON_START_ANGLE),
                         static_cast<int32_t>(CYBERDECK_WIFI_ICON_START_ANGLE + CYBERDECK_WIFI_ICON_SWEEP_ANGLE));
    lv_arc_set_angles(arc, static_cast<int32_t>(CYBERDECK_WIFI_ICON_START_ANGLE),
                      static_cast<int32_t>(CYBERDECK_WIFI_ICON_START_ANGLE + CYBERDECK_WIFI_ICON_SWEEP_ANGLE));
    lv_obj_set_style_arc_width(arc, static_cast<int32_t>(CYBERDECK_WIFI_ICON_DEFAULT_THICKNESS), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_x(arc, x);
    lv_obj_set_y(arc, y);
}

/* Position the object from its real diameter rather than from LVGL's parent
 * alignment calculation.  The layout center is based on the visible extent of
 * the outer arc, so rounding the left edge keeps that extent at the requested
 * right margin even when the container width changes by one pixel. */
int32_t object_x(float center_x, int32_t width)
{
    return static_cast<int32_t>(std::lround(center_x - width / 2.0f));
}

} // namespace
#endif

bool cyberdeck_wifi_icon_validate(const cyberdeck_wifi_icon_layout_t *layout)
{
    if (layout == NULL) {
        return false;
    }
    if (!std::isfinite(layout->center_x) || !std::isfinite(layout->center_y) ||
        !std::isfinite(layout->box_width) || !std::isfinite(layout->box_height) ||
        layout->box_width <= 0.0f || layout->box_height <= 0.0f) {
        return false;
    }
    for (int i = 0; i < CYBERDECK_WIFI_ICON_ARC_COUNT; ++i) {
        if (!std::isfinite(layout->radii[i]) ||
            !std::isfinite(layout->start_angles[i]) ||
            !std::isfinite(layout->sweep_angles[i]) ||
            layout->radii[i] < CYBERDECK_WIFI_ICON_MIN_RADIUS ||
            layout->radii[i] > CYBERDECK_WIFI_ICON_MAX_RADIUS) {
            return false;
        }
        if (layout->start_angles[i] < CYBERDECK_WIFI_ICON_MIN_ANGLE ||
            layout->start_angles[i] > CYBERDECK_WIFI_ICON_MAX_ANGLE) {
            return false;
        }
        if (layout->sweep_angles[i] < CYBERDECK_WIFI_ICON_MIN_ANGLE ||
            layout->sweep_angles[i] > CYBERDECK_WIFI_ICON_MAX_ANGLE) {
            return false;
        }
    }
    if (!std::isfinite(layout->thickness) ||
        layout->thickness < CYBERDECK_WIFI_ICON_MIN_THICKNESS ||
        layout->thickness > CYBERDECK_WIFI_ICON_MAX_THICKNESS) {
        return false;
    }
    if (!std::isfinite(layout->point_radius) ||
        layout->point_radius < CYBERDECK_WIFI_ICON_MIN_POINT_RADIUS ||
        layout->point_radius > CYBERDECK_WIFI_ICON_MAX_POINT_RADIUS) {
        return false;
    }
    return true;
}

bool cyberdeck_wifi_icon_calculate_visual_center_y(
    const cyberdeck_wifi_icon_layout_t *layout, float *out_center_y)
{
    if (layout == NULL || out_center_y == NULL ||
        !cyberdeck_wifi_icon_validate(layout)) {
        return false;
    }

    const float center_y = layout->box_height - 12.5f;
    if (!std::isfinite(center_y)) {
        return false;
    }

    *out_center_y = center_y;
    return true;
}

bool cyberdeck_wifi_icon_calculate_layout(float box_width,
                                          float box_height,
                                          float margin_right,
                                          cyberdeck_wifi_icon_layout_t *out_layout)
{
    if (out_layout == NULL) {
        return false;
    }
    if (!std::isfinite(box_width) || !std::isfinite(box_height) ||
        !std::isfinite(margin_right) || box_width <= 0.0f ||
        box_height <= 0.0f || margin_right < 0.0f) {
        return false;
    }

    cyberdeck_wifi_icon_layout_t layout = {};
    layout.radii[0] = CYBERDECK_WIFI_ICON_RADIUS_0;
    layout.radii[1] = CYBERDECK_WIFI_ICON_RADIUS_1;
    layout.radii[2] = CYBERDECK_WIFI_ICON_RADIUS_2;
    layout.thickness = CYBERDECK_WIFI_ICON_DEFAULT_THICKNESS;
    for (int i = 0; i < CYBERDECK_WIFI_ICON_ARC_COUNT; ++i) {
        layout.start_angles[i] = CYBERDECK_WIFI_ICON_START_ANGLE;
        layout.sweep_angles[i] = CYBERDECK_WIFI_ICON_SWEEP_ANGLE;
    }
    layout.point_radius = CYBERDECK_WIFI_ICON_POINT_RADIUS;
    layout.box_width = box_width;
    layout.box_height = box_height;
    layout.center_y = box_height - 12.5f;

    const float visible_half_width = cyberdeck_wifi_icon_visible_half_width(&layout);
    layout.center_x = box_width - margin_right - visible_half_width;

    if (!cyberdeck_wifi_icon_validate(&layout)) {
        return false;
    }
    *out_layout = layout;
    return true;
}

float cyberdeck_wifi_icon_determinant_outer_radius(const cyberdeck_wifi_icon_layout_t *layout)
{
    if (layout == NULL || !cyberdeck_wifi_icon_validate(layout)) {
        return 0.0f;
    }
    float max_r = layout->radii[0];
    for (int i = 1; i < CYBERDECK_WIFI_ICON_ARC_COUNT; ++i) {
        if (layout->radii[i] > max_r) {
            max_r = layout->radii[i];
        }
    }
    return max_r + layout->thickness;
}

float cyberdeck_wifi_icon_visible_half_width(const cyberdeck_wifi_icon_layout_t *layout)
{
    const float outer_radius = cyberdeck_wifi_icon_determinant_outer_radius(layout);
    if (outer_radius <= 0.0f) {
        return 0.0f;
    }
    constexpr float SIN_45 = 0.7071067811865475f;
    return outer_radius * SIN_45;
}

float cyberdeck_wifi_icon_visual_arc_bottom_y(const cyberdeck_wifi_icon_layout_t *layout)
{
    const float outer_radius = cyberdeck_wifi_icon_determinant_outer_radius(layout);
    if (outer_radius <= 0.0f) {
        return 0.0f;
    }

    constexpr float SIN_45 = 0.7071067811865475f;
    const float visual_arc_bottom =
        layout->center_y - outer_radius * (1.0f - SIN_45);
    return std::isfinite(visual_arc_bottom) ? visual_arc_bottom : 0.0f;
}

size_t cyberdeck_wifi_icon_path_count(const cyberdeck_wifi_icon_layout_t *layout)
{
    if (layout == NULL || !cyberdeck_wifi_icon_validate(layout)) {
        return 0;
    }
    return CYBERDECK_WIFI_ICON_ARC_COUNT + 1; /* 3 arcs + 1 point */
}

bool cyberdeck_wifi_icon_is_lit(bool enabled, bool connected, bool has_ip)
{
    return cyberdeck_wifi_indicator_is_lit(enabled, connected, has_ip);
}

lv_obj_t *cyberdeck_wifi_icon_create(lv_obj_t *parent)
{
#if CYBERDECK_WIFI_ICON_HAS_LVGL
    if (parent == nullptr) return nullptr;

    lv_obj_t *icon = lv_obj_create(parent);
    lv_obj_set_height(icon, static_cast<int32_t>(CYBERDECK_WIFI_ICON_CONTAINER_HEIGHT));
    lv_obj_set_layout(icon, LV_LAYOUT_NONE);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon, 0, 0);
    lv_obj_set_style_pad_all(icon, 0, 0);
    lv_obj_set_style_radius(icon, 0, 0);
    lv_obj_set_scrollable(icon, false);
    /* The children are deliberately kept inside this object's 42 px height,
     * while this also prevents an arc stroke from being clipped at the icon
     * object's own boundary. */
    lv_obj_set_overflow_visible(icon, true);

    cyberdeck_wifi_icon_layout_t layout = {};
    int32_t init_w = lv_obj_get_width(parent);
    if (init_w <= 0) init_w = WIFI_ICON_DEFAULT_WIDTH;
    cyberdeck_wifi_icon_calculate_layout(static_cast<float>(init_w),
                                         CYBERDECK_WIFI_ICON_CONTAINER_HEIGHT,
                                         CYBERDECK_WIFI_ICON_MARGIN_RIGHT,
                                         &layout);

    const lv_color_t color = lv_color_hex(WIFI_ICON_DEFAULT_COLOR);
    for (int i = 0; i < CYBERDECK_WIFI_ICON_ARC_COUNT; ++i) {
        const int32_t diameter = static_cast<int32_t>(layout.radii[i] * 2.0f) +
                                 static_cast<int32_t>(layout.thickness * 2.0f);
        lv_obj_t *arc = lv_arc_create(icon);
        configure_arc(arc, diameter, color,
                      object_x(layout.center_x, diameter),
                      static_cast<int32_t>(std::lround(layout.center_y - diameter / 2.0f)));
    }

    lv_obj_t *point = lv_obj_create(icon);
    const int32_t point_diameter = static_cast<int32_t>(layout.point_radius * 2.0f);
    lv_obj_set_size(point, point_diameter, point_diameter);
    lv_obj_set_style_bg_color(point, color, 0);
    lv_obj_set_style_bg_opa(point, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(point, 0, 0);
    lv_obj_set_style_radius(point, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_x(point, object_x(layout.center_x, point_diameter));
    const float point_center_y = cyberdeck_wifi_icon_calculate_point_center_y(
        &layout, CYBERDECK_WIFI_ICON_POINT_GAP);
    if (point_center_y <= 0.0f) {
        lv_obj_delete(icon);
        return nullptr;
    }
    lv_obj_set_y(point, static_cast<int32_t>(std::lround(point_center_y - point_diameter / 2.0f)));
    lv_obj_set_scrollable(point, false);

    lv_obj_add_event_cb(icon, wifi_icon_size_changed_cb, LV_EVENT_SIZE_CHANGED, nullptr);

    return icon;
#else
    (void)parent;
    return nullptr;
#endif
}

void cyberdeck_wifi_icon_update_layout(lv_obj_t *icon, int32_t container_width)
{
#if CYBERDECK_WIFI_ICON_HAS_LVGL
    if (icon == nullptr || container_width <= 0) return;

    cyberdeck_wifi_icon_layout_t layout = {};
    if (!cyberdeck_wifi_icon_calculate_layout(static_cast<float>(container_width),
                                              CYBERDECK_WIFI_ICON_CONTAINER_HEIGHT,
                                              CYBERDECK_WIFI_ICON_MARGIN_RIGHT,
                                              &layout)) {
        return;
    }

    for (int32_t i = 0; i < CYBERDECK_WIFI_ICON_ARC_COUNT; ++i) {
        lv_obj_t *arc = lv_obj_get_child(icon, i);
        if (arc != nullptr) {
            const int32_t diameter = lv_obj_get_width(arc);
            lv_obj_set_x(arc, object_x(layout.center_x, diameter));
            lv_obj_set_y(arc, static_cast<int32_t>(std::lround(layout.center_y - diameter / 2.0f)));
        }
    }

    lv_obj_t *point = lv_obj_get_child(icon, CYBERDECK_WIFI_ICON_ARC_COUNT);
    if (point != nullptr) {
        const int32_t diameter = lv_obj_get_width(point);
        lv_obj_set_x(point, object_x(layout.center_x, diameter));
        const float point_center_y = cyberdeck_wifi_icon_calculate_point_center_y(
            &layout, CYBERDECK_WIFI_ICON_POINT_GAP);
        if (point_center_y > 0.0f) {
            lv_obj_set_y(point, static_cast<int32_t>(std::lround(point_center_y - diameter / 2.0f)));
        }
    }
#else
    (void)icon;
    (void)container_width;
#endif
}

void cyberdeck_wifi_icon_set_color(lv_obj_t *icon, uint32_t color_hex)
{
#if CYBERDECK_WIFI_ICON_HAS_LVGL
    if (icon == nullptr) return;
    const lv_color_t color = lv_color_hex(color_hex);
    for (int32_t i = 0; i < CYBERDECK_WIFI_ICON_ARC_COUNT; ++i) {
        lv_obj_t *arc = lv_obj_get_child(icon, i);
        if (arc != nullptr) lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    }
    lv_obj_t *point = lv_obj_get_child(icon, CYBERDECK_WIFI_ICON_ARC_COUNT);
    if (point != nullptr) lv_obj_set_style_bg_color(point, color, 0);
#else
    (void)icon;
    (void)color_hex;
#endif
}

void cyberdeck_wifi_icon_destroy(lv_obj_t *icon)
{
#if CYBERDECK_WIFI_ICON_HAS_LVGL
    if (icon != nullptr) lv_obj_delete(icon);
#else
    (void)icon;
#endif
}
