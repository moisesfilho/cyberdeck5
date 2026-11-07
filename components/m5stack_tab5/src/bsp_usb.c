/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include "bsp_err_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "usb/usb_host.h"

#include "bsp/m5stack_tab5.h"

#include <stdatomic.h>

static const char *TAG = "M5Stack Tab5";
static TaskHandle_t usb_host_task;  // USB Host Library task
static SemaphoreHandle_t usb_host_stopped;
static atomic_bool usb_host_stop_requested;
static atomic_bool usb_host_task_exited;
static bool usb_host_installed;
static bool usb_power_enabled;

static void usb_lib_task(void *arg)
{
    /* No device is attached at start, so "all devices freed" is the initial
     * state; it flips to false whenever the library reports a live client. */
    bool devices_freed = true;
    while (1) {
        /* Start handling system events. An idle window is reported as
         * ESP_ERR_TIMEOUT and is not an error: the loop must keep running so a
         * device attached later is still enumerated. */
        uint32_t event_flags = 0;
        const esp_err_t result = usb_host_lib_handle_events(pdMS_TO_TICKS(100), &event_flags);
        if (result == ESP_ERR_TIMEOUT) {
            if (atomic_load_explicit(&usb_host_stop_requested, memory_order_acquire) && devices_freed) break;
            continue;
        }
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "USB host event loop failed: %s", esp_err_to_name(result));
            break;
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            const esp_err_t free_result = usb_host_device_free_all();
            if (free_result != ESP_OK) {
                ESP_LOGE(TAG, "USB device cleanup failed: %s", esp_err_to_name(free_result));
                break;
            }
            devices_freed = true;
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGI(TAG, "USB: All devices freed");
            devices_freed = true;
            if (atomic_load_explicit(&usb_host_stop_requested, memory_order_acquire)) break;
        } else if (!(event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)) {
            devices_freed = false;
        }
    }
    atomic_store_explicit(&usb_host_task_exited, true, memory_order_release);
    if (usb_host_stopped != NULL) xSemaphoreGive(usb_host_stopped);
    vTaskDelete(NULL);
}

esp_err_t bsp_usb_host_start(bsp_usb_host_power_mode_t mode, bool limit_500mA)
{
    (void)mode;
    (void)limit_500mA;
    if (usb_host_task != NULL || usb_host_stopped != NULL || usb_host_installed || usb_power_enabled)
        return ESP_ERR_INVALID_STATE;

    usb_host_stopped = xSemaphoreCreateBinary();
    if (usb_host_stopped == NULL) return ESP_ERR_NO_MEM;
    atomic_store_explicit(&usb_host_stop_requested, false, memory_order_release);
    atomic_store_explicit(&usb_host_task_exited, false, memory_order_release);

    /* Enable Feature */
    esp_err_t result = bsp_feature_enable(BSP_FEATURE_USB, true);
    if (result != ESP_OK) goto fail;
    usb_power_enabled = true;

    //Install USB Host driver. Should only be called once in entire application
    ESP_LOGI(TAG, "Installing USB Host");
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LOWMED,
    };
    result = usb_host_install(&host_config);
    if (result != ESP_OK) {
        (void)bsp_feature_enable(BSP_FEATURE_USB, false);
        usb_power_enabled = false;
        goto fail;
    }
    usb_host_installed = true;

    // Create a task that will handle USB library events
    if (xTaskCreate(usb_lib_task, "usb_lib", 4096, NULL, 10, &usb_host_task) != pdTRUE) {
        ESP_LOGE(TAG, "Creating USB host lib task failed");
        usb_host_task = NULL;
        (void)usb_host_uninstall();
        usb_host_installed = false;
        (void)bsp_feature_enable(BSP_FEATURE_USB, false);
        usb_power_enabled = false;
        result = ESP_ERR_NO_MEM;
        goto fail;
    }

    return ESP_OK;

fail:
    vSemaphoreDelete(usb_host_stopped);
    usb_host_stopped = NULL;
    return result;
}

esp_err_t bsp_usb_host_stop(void)
{
    if (usb_host_task != NULL && !atomic_load_explicit(&usb_host_task_exited, memory_order_acquire)) {
        atomic_store_explicit(&usb_host_stop_requested, true, memory_order_release);
        if (xSemaphoreTake(usb_host_stopped, pdMS_TO_TICKS(2000)) != pdTRUE) {
            ESP_LOGE(TAG, "Timed out waiting for USB host library task to drain");
            return ESP_ERR_TIMEOUT;
        }
    }
    if (usb_host_installed) {
        const esp_err_t uninstall_result = usb_host_uninstall();
        if (uninstall_result != ESP_OK) return uninstall_result;
        usb_host_installed = false;
        usb_host_task = NULL;
    }
    if (usb_power_enabled) {
        const esp_err_t power_result = bsp_feature_enable(BSP_FEATURE_USB, false);
        if (power_result != ESP_OK) return power_result;
        usb_power_enabled = false;
    }
    if (usb_host_stopped != NULL) {
        vSemaphoreDelete(usb_host_stopped);
        usb_host_stopped = NULL;
    }
    usb_host_task = NULL;
    return ESP_OK;
}
