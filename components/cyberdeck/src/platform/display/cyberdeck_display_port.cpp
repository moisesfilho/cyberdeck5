#include "platform/display/cyberdeck_display_port.h"

#include "bsp/esp-bsp.h"
#include "lvgl.h"

#include <cstdlib>
#include <cstring>

extern "C" esp_err_t cyberdeck_display_capture(void *, screenshot_frame_t *out)
{
    if (out == nullptr || !bsp_display_lock(pdMS_TO_TICKS(1000))) return ESP_ERR_TIMEOUT;
    lv_draw_buf_t *snapshot = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    if (snapshot == nullptr) {
        bsp_display_unlock();
        return ESP_ERR_NO_MEM;
    }
    const size_t size = static_cast<size_t>(snapshot->header.stride) * snapshot->header.h;
    auto *data = static_cast<uint8_t *>(std::malloc(size));
    if (data != nullptr) std::memcpy(data, snapshot->data, size);
    out->width = snapshot->header.w;
    out->height = snapshot->header.h;
    out->stride = snapshot->header.stride;
    out->data = data;
    lv_draw_buf_destroy(snapshot);
    bsp_display_unlock();
    return data == nullptr ? ESP_ERR_NO_MEM : ESP_OK;
}

extern "C" void cyberdeck_display_release(void *, screenshot_frame_t *frame)
{
    if (frame == nullptr) return;
    std::free(frame->data);
    *frame = {};
}
