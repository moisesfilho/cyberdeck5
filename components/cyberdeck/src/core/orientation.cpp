#include "orientation.h"

#include <cmath>

namespace {

constexpr float FLAT_THRESHOLD = 0.45F;
constexpr int DEBOUNCE_SAMPLES = 5;

lv_display_rotation_t s_current = LV_DISPLAY_ROTATION_0;
lv_display_rotation_t s_target = LV_DISPLAY_ROTATION_0;
int s_stable_samples = 0;

lv_display_rotation_t gravity_to_rotation(float gx, float gy)
{
    const float plane = std::sqrt((gx * gx) + (gy * gy));
    if (plane < FLAT_THRESHOLD) {
        return s_current;
    }

    float angle = std::atan2(gx, gy) * 180.0F / static_cast<float>(M_PI);
    if (angle < 0.0F) {
        angle += 360.0F;
    }

    const int quadrant = static_cast<int>((angle + 45.0F) / 90.0F) % 4;
    static constexpr lv_display_rotation_t rotations[] = {
        LV_DISPLAY_ROTATION_0,
        LV_DISPLAY_ROTATION_90,
        LV_DISPLAY_ROTATION_180,
        LV_DISPLAY_ROTATION_270,
    };
    return rotations[quadrant];
}

} // namespace

void orientation_reset(void)
{
    s_current = LV_DISPLAY_ROTATION_0;
    s_target = LV_DISPLAY_ROTATION_0;
    s_stable_samples = 0;
}

void orientation_set_current(lv_display_rotation_t rotation)
{
    s_current = rotation;
    s_target = rotation;
    s_stable_samples = 0;
}

lv_display_rotation_t orientation_update(float ax, float ay, float az)
{
    (void)az;
    const lv_display_rotation_t target = gravity_to_rotation(ax, ay);
    if (target == s_current) {
        s_stable_samples = 0;
        return s_current;
    }

    if (target != s_target) {
        s_target = target;
        s_stable_samples = 0;
        return s_current;
    }

    if (++s_stable_samples >= DEBOUNCE_SAMPLES) {
        s_current = target;
        s_stable_samples = 0;
    }
    return s_current;
}
