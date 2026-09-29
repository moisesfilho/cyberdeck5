#pragma once

#include "lvgl.h"
#include "platform/display/cyberdeck_battery_view.h"

#include <cstdint>

namespace cyberdeck_header_view {

class view {
public:
    bool create(lv_obj_t *parent);
    void update_clock(const char *text);
    void update_ble(bool connected);
    void update_wifi(bool lit);
    void update_battery(const cyberdeck_battery_view::presentation &presentation);

private:
    lv_obj_t *s_clock_status = nullptr;
    lv_obj_t *s_ble_cell = nullptr;
    lv_obj_t *s_ble_status = nullptr;
    lv_obj_t *s_wifi_status = nullptr;
    lv_obj_t *s_battery_status = nullptr;
    lv_obj_t *s_battery_symbol = nullptr;
    lv_obj_t *s_battery_percentage = nullptr;
};

} // namespace cyberdeck_header_view
