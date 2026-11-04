/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_err.h"
#include "esp_check.h"
#include "driver/gpio.h"

#include "bsp/m5stack_tab5.h"
#include "platform/sensors/cyberdeck_expander_broker.h"

#include <chrono>

/* Expander B is mutated by the BSP (WIFI_EN/USB_EN) and by app-level owners
 * (battery protection CHG_STAT/CHG_QC_EN/CHG_EN and poweroff P4). They share a
 * single driver cache, so every transaction holds the same exclusive lease. */
static constexpr std::chrono::milliseconds k_expander_lease_timeout{250};

/* Feature enable */
esp_err_t bsp_feature_enable(bsp_feature_t feature, bool enable)
{
    esp_err_t ret = ESP_OK;

    switch (feature) {
    case BSP_FEATURE_LCD: {
        esp_io_expander_handle_t io_expander = bsp_io_expander_init();
        if (enable) {
            ret |= esp_io_expander_set_pullupdown(io_expander, BSP_LCD_EN, IO_EXPANDER_PULL_UP);
            ret |= esp_io_expander_set_dir(io_expander, BSP_LCD_EN, IO_EXPANDER_INPUT);
            ret |= esp_io_expander_set_output_mode(io_expander, BSP_LCD_EN, IO_EXPANDER_OUTPUT_MODE_OPEN_DRAIN);
        } else {
            ret |= esp_io_expander_set_dir(io_expander, BSP_LCD_EN, IO_EXPANDER_OUTPUT);
            ret |= esp_io_expander_set_level(io_expander, BSP_LCD_EN, 0);
            ret |= esp_io_expander_set_output_mode(io_expander, BSP_LCD_EN, IO_EXPANDER_OUTPUT_MODE_PUSH_PULL);
        }
        break;
    }
    case BSP_FEATURE_TOUCH: {
        esp_io_expander_handle_t io_expander = bsp_io_expander_init();
        ret |= esp_io_expander_set_dir(io_expander, BSP_TOUCH_EN, IO_EXPANDER_OUTPUT);
        ret |= esp_io_expander_set_level(io_expander, BSP_TOUCH_EN, enable);
        ret |= esp_io_expander_set_output_mode(io_expander, BSP_TOUCH_EN, IO_EXPANDER_OUTPUT_MODE_PUSH_PULL);
        break;
    }
    case BSP_FEATURE_SPEAKER: {
        esp_io_expander_handle_t io_expander = bsp_io_expander_init();
        ret |= esp_io_expander_set_dir(io_expander, BSP_SPEAKER_EN, IO_EXPANDER_OUTPUT);
        ret |= esp_io_expander_set_level(io_expander, BSP_SPEAKER_EN, enable);
        ret |= esp_io_expander_set_output_mode(io_expander, BSP_SPEAKER_EN, IO_EXPANDER_OUTPUT_MODE_PUSH_PULL);
        break;
    }
    case BSP_FEATURE_CAMERA: {
        esp_io_expander_handle_t io_expander = bsp_io_expander_init();
        ret |= esp_io_expander_set_dir(io_expander, BSP_CAMERA_EN, IO_EXPANDER_OUTPUT);
        ret |= esp_io_expander_set_level(io_expander, BSP_CAMERA_EN, enable);
        ret |= esp_io_expander_set_output_mode(io_expander, BSP_CAMERA_EN, IO_EXPANDER_OUTPUT_MODE_PUSH_PULL);
        break;
    }
    case BSP_FEATURE_USB: {
        cyberdeck_expander_broker::lease lease(k_expander_lease_timeout);
        if (!lease) {
            ret = ESP_ERR_TIMEOUT;
            break;
        }
        esp_io_expander_handle_t io_expander = bsp_io_expander1_init();
        ret |= esp_io_expander_set_dir(io_expander, BSP_USB_EN, IO_EXPANDER_OUTPUT);
        ret |= esp_io_expander_set_level(io_expander, BSP_USB_EN, enable);
        ret |= esp_io_expander_set_output_mode(io_expander, BSP_USB_EN, IO_EXPANDER_OUTPUT_MODE_PUSH_PULL);
        break;
    }
    case BSP_FEATURE_WIFI: {
        cyberdeck_expander_broker::lease lease(k_expander_lease_timeout);
        if (!lease) {
            ret = ESP_ERR_TIMEOUT;
            break;
        }
        esp_io_expander_handle_t io_expander = bsp_io_expander1_init();
        ret |= esp_io_expander_set_dir(io_expander, BSP_WIFI_EN, IO_EXPANDER_OUTPUT);
        ret |= esp_io_expander_set_level(io_expander, BSP_WIFI_EN, enable);
        ret |= esp_io_expander_set_output_mode(io_expander, BSP_WIFI_EN, IO_EXPANDER_OUTPUT_MODE_PUSH_PULL);
        break;
    }
    }

    return ret;
}
