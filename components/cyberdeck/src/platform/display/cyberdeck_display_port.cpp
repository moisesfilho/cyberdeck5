#include "platform/display/cyberdeck_display_port.h"

#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "platform/display/cyberdeck_lvgl_dispatch.h"

#include <cstdlib>
#include <cstring>
#include <mutex>

namespace {

constexpr size_t k_snapshot_max_bytes = static_cast<size_t>(BSP_LCD_H_RES) * BSP_LCD_V_RES * sizeof(uint16_t);
constexpr size_t k_snapshot_alignment_slack = 64;
lv_draw_buf_t s_snapshot_draw_buf{};
void *s_snapshot_storage = nullptr;
bool s_snapshot_initialized = false;
std::once_flag s_storage_once;
lv_result_t s_snapshot_result = LV_RESULT_INVALID;
esp_err_t s_snapshot_error = ESP_OK;

void take_snapshot_on_lvgl(void *)
{
    if (!s_snapshot_initialized) {
        if (lv_draw_buf_init(&s_snapshot_draw_buf, BSP_LCD_H_RES, BSP_LCD_V_RES,
                             LV_COLOR_FORMAT_RGB565, 0, s_snapshot_storage,
                             k_snapshot_max_bytes + k_snapshot_alignment_slack) != LV_RESULT_OK) {
            s_snapshot_error = ESP_ERR_NO_MEM;
            return;
        }
        s_snapshot_initialized = true;
    }
    s_snapshot_result = lv_snapshot_take_to_draw_buf(lv_screen_active(), LV_COLOR_FORMAT_RGB565,
                                                      &s_snapshot_draw_buf);
}

}

extern "C" esp_err_t cyberdeck_display_capture(void *, screenshot_frame_t *out)
{
    if (out == nullptr) return ESP_ERR_INVALID_ARG;
    *out = {};
    // Same benign stale-write reasoning documented by the result reset below.
    s_snapshot_error = ESP_OK;
    std::call_once(s_storage_once, [] {
        s_snapshot_storage = heap_caps_malloc(k_snapshot_max_bytes + k_snapshot_alignment_slack,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    });
    if (s_snapshot_storage == nullptr) return ESP_ERR_NO_MEM;
    // Reset before run() so a capture that never executes cannot leak the value
    // of a previous frame. This may race with a callback detached by an earlier
    // call, but the race is benign: admission is only re-armed after that
    // callback finished (it releases the slot on finish), so its late write can
    // only land before the new callback runs, and the new callback is the last
    // writer of s_snapshot_result/s_snapshot_error for this call.
    s_snapshot_result = LV_RESULT_INVALID;
    if (!cyberdeck_lvgl_dispatch::run(take_snapshot_on_lvgl, &s_snapshot_draw_buf, 2000)) return ESP_ERR_TIMEOUT;
    if (s_snapshot_result != LV_RESULT_OK) return s_snapshot_error == ESP_OK ? ESP_ERR_NO_MEM : s_snapshot_error;

    const size_t size = static_cast<size_t>(s_snapshot_draw_buf.header.stride) * s_snapshot_draw_buf.header.h;
    auto *data = static_cast<uint8_t *>(std::malloc(size));
    if (data == nullptr) return ESP_ERR_NO_MEM;
    std::memcpy(data, s_snapshot_draw_buf.data, size);
    out->width = s_snapshot_draw_buf.header.w;
    out->height = s_snapshot_draw_buf.header.h;
    out->stride = s_snapshot_draw_buf.header.stride;
    out->data = data;
    return ESP_OK;
}

extern "C" void cyberdeck_display_release(void *, screenshot_frame_t *frame)
{
    if (frame == nullptr) return;
    std::free(frame->data);
    *frame = {};
}
