#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t flushes;
    uint64_t flush_us;
    uint32_t max_flush_us;
    uint64_t pixels_copied;
} lvgl_port_dsi_diag_snapshot_t;

void lvgl_port_dsi_diag_begin(void);
lvgl_port_dsi_diag_snapshot_t lvgl_port_dsi_diag_read(void);
void lvgl_port_dsi_diag_end(void);

#ifdef __cplusplus
}
#endif
