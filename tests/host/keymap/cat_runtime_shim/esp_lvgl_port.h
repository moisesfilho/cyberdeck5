#pragma once
#define LVGL_PORT_EVENT_USER 1
inline bool lvgl_port_task_wake(int, void *) { return true; }
