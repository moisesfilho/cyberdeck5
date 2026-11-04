#pragma once

#include "esp_err.h"
#include "esp_io_expander.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cyberdeck_expander_device_init(void);
esp_io_expander_handle_t cyberdeck_expander_device_handle(void);
i2c_master_dev_handle_t cyberdeck_expander_raw_handle(void);

#ifdef __cplusplus
}
#endif
