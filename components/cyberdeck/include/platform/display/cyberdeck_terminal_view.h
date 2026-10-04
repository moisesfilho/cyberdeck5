#pragma once

#include "lvgl.h"

#include <cstddef>
#include <string>

namespace cyberdeck_terminal_view {

struct callbacks {
    lv_event_cb_t focused = nullptr;
    lv_event_cb_t inserted = nullptr;
    lv_event_cb_t changed = nullptr;
    lv_event_cb_t key = nullptr;
    lv_event_cb_t virtual_keyboard_changed = nullptr;
    lv_event_cb_t geometry_changed = nullptr;
};

class view {
public:
    bool create(lv_obj_t *screen, lv_obj_t *parent, std::size_t max_length,
                const callbacks &callbacks);
    lv_obj_t *textarea() const { return s_terminal; }
    lv_obj_t *scrollback() const { return s_surface; }
    lv_obj_t *keyboard() const { return s_keyboard; }
    std::size_t viewport_capacity() const;
    void render(const std::string &text);
    lv_event_cb_t geometry_callback() const { return s_geometry_changed; }

private:
    static constexpr std::size_t k_max_lines = 64;
    static constexpr std::size_t k_viewport_bytes = 4096;
    lv_obj_t *s_terminal = nullptr;
    lv_obj_t *s_surface = nullptr;
    lv_obj_t *s_parent = nullptr;
    lv_obj_t *s_keyboard = nullptr;
    lv_obj_t *s_lines[k_max_lines]{};
    std::size_t s_line_count = 0;
    lv_event_cb_t s_geometry_changed = nullptr;
};

} // namespace cyberdeck_terminal_view
