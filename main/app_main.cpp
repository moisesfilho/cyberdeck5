#include "platform/display/cyberdeck_ui.h"
#include "esp_log.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "platform/sensors/imu_reader.h"
#include "platform/sensors/battery_protection.h"
#include "platform/display/screen_off.h"
#include "platform/input/tab5_keyboard.h"
#include "apps/system/cyberdeck_system_apps.h"
#include "apps/system/cyberdeck_recovery.h"
#include "apps/screenshot/screenshot_server.h"
#include "platform/display/cyberdeck_display_port.h"
#include "bsp/m5stack_tab5.h"

static const char *TAG = "cyberdeck5";

extern "C" void app_main(void)
{
    /* The terminal exposes its virtual root / only after the physical /sdcard
     * mount has completed and its BSP handle has been verified. */
    ESP_ERROR_CHECK(bsp_sdcard_mount());
    if (bsp_sdcard_get_handle() == nullptr) return;

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS unavailable: %s (not erasing)", esp_err_to_name(err));
    }
    /* Recovery state is persisted before registering or starting services.
     * Corrupt/unavailable NVS is reported, never erased automatically. */
    const esp_err_t recovery_err = cyberdeck_recovery::init();
    if (recovery_err != ESP_OK) {
        ESP_LOGE(TAG, "recovery state unavailable: %s", esp_err_to_name(recovery_err));
    }
    ESP_ERROR_CHECK(cyberdeck_system_apps_register());
    /* Event-log startup is diagnostic infrastructure, not a boot prerequisite.
     * In particular, do not turn an unavailable NVS/log backend into a reboot
     * loop before the recovery latch can select safe mode. */
    const esp_err_t logging_err = cyberdeck_system_apps_start_logging();
    if (logging_err != ESP_OK) {
        ESP_LOGE(TAG, "event log unavailable during boot: %s (continuing)",
                 esp_err_to_name(logging_err));
    }

    lv_display_t *display = bsp_display_start();
    if (display == nullptr) {
        ESP_LOGE(TAG, "Falha ao iniciar o display");
        return;
    }

    bsp_display_lock(0);
    ESP_ERROR_CHECK(imu_reader_start(display));
    ESP_ERROR_CHECK(cyberdeck_ui_init());
    ESP_ERROR_CHECK(screenshot_server_set_display_port(cyberdeck_display_capture,
                                                       cyberdeck_display_release, nullptr));
    ESP_ERROR_CHECK(screen_off_init(display, 20));
    bsp_display_unlock();

    const esp_err_t battery_err = battery_protection_start();
    if (battery_err != ESP_OK) {
        ESP_LOGW(TAG, "Battery protection unavailable: %s", esp_err_to_name(battery_err));
    }

    tab5_keyboard_set_callback(cyberdeck_keyboard_input);
    ESP_ERROR_CHECK(tab5_keyboard_init());
    ESP_ERROR_CHECK(bsp_display_brightness_set(20));

    /* The checkpoint is deliberately after display, input and services are
     * ready; an interrupted boot remains pending across the next reset. */
    bool boot_ready = true;
    if (cyberdeck_recovery::safe_mode()) {
        ESP_LOGW(TAG, "safe mode latched: starting recovery surface only");
        const esp_err_t safe_mode_err = cyberdeck_system_apps_start_safe_mode();
        if (safe_mode_err != ESP_OK) {
            boot_ready = false;
            ESP_LOGE(TAG, "safe mode surface incomplete: %s (latch preserved)",
                     esp_err_to_name(safe_mode_err));
        }
    } else {
        const esp_err_t services_err = cyberdeck_system_apps_start();
        if (services_err != ESP_OK) {
            boot_ready = false;
            ESP_LOGE(TAG, "normal startup incomplete: %s (latch preserved)",
                     esp_err_to_name(services_err));
        }
    }
    if (boot_ready) {
        const esp_err_t checkpoint_err = cyberdeck_system_apps_commit_ready();
        if (checkpoint_err != ESP_OK) {
            ESP_LOGW(TAG, "recovery checkpoint unavailable: %s", esp_err_to_name(checkpoint_err));
        }
    } else {
        ESP_LOGW(TAG, "recovery checkpoint deferred after startup failure");
    }
    ESP_LOGI(TAG, "CYBERDECK5 iniciado");
}
