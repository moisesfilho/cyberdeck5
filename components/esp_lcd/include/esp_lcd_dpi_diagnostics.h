#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t bridge_underruns;
    uint32_t dma_frames;
} esp_lcd_dpi_diagnostics_t;

esp_lcd_dpi_diagnostics_t esp_lcd_dpi_diagnostics_read(void);

#ifdef __cplusplus
}
#endif
