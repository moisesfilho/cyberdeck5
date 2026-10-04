#pragma once

#include <cstddef>

using lvgl_port_event_t = int;
inline constexpr lvgl_port_event_t LVGL_PORT_EVENT_USER = 1;

inline std::size_t &lv_shim_wake_count()
{
    static std::size_t count = 0;
    return count;
}
inline void lv_shim_reset_wake_count() { lv_shim_wake_count() = 0; }

inline int lvgl_port_task_wake(lvgl_port_event_t, void *)
{
    ++lv_shim_wake_count();
    return 0;
}
