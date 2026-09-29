#pragma once

#include "lvgl.h"

#include <cstddef>

namespace cyberdeck_terminal_view {

struct callbacks {
    lv_event_cb_t focused = nullptr;
    lv_event_cb_t inserted = nullptr;
    lv_event_cb_t changed = nullptr;
    lv_event_cb_t key = nullptr;
    lv_event_cb_t virtual_keyboard_changed = nullptr;
};

class view {
public:
    bool create(lv_obj_t *screen, lv_obj_t *parent, std::size_t max_length,
                const callbacks &callbacks);
    lv_obj_t *textarea() const { return s_terminal; }
    lv_obj_t *keyboard() const { return s_keyboard; }

private:
    lv_obj_t *s_terminal = nullptr;
    lv_obj_t *s_keyboard = nullptr;
};

} // namespace cyberdeck_terminal_view
