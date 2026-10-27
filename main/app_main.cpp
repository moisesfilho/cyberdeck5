#include "platform/display/cyberdeck_ui.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "platform/sensors/imu_reader.h"
#include "platform/sensors/battery_protection.h"
#include "platform/display/screen_off.h"
#include "platform/input/tab5_keyboard.h"
#include "apps/system/cyberdeck_system_apps.h"
#include "apps/system/cyberdeck_recovery.h"
#include "apps/shell/cyberdeck_local_shell.h"
#include "apps/screenshot/screenshot_server.h"
#include "apps/wifi/wifi_mgr.h"
#include "platform/display/cyberdeck_display_port.h"
#include "bsp/m5stack_tab5.h"
#include "platform/storage/cyberdeck_sd_io_coordinator.h"
#include "apps/runtime/cyberdeck_app_runtime.h"
#include "platform/power/cyberdeck_poweroff.h"

static const char *TAG = "cyberdeck5";

extern "C" void app_main(void)
{
    cyberdeck_apps::runtime &app_runtime = cyberdeck_apps::global_runtime();
    app_runtime.set_command_dispatch_ready(false);

    static cyberdeck_sd_io::coordinator sd_io_coordinator;
    cyberdeck_sd_io::bind_global(sd_io_coordinator);
    cyberdeck_poweroff_init(sd_io_coordinator);

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS unavailable: %s (not erasing)", esp_err_to_name(err));
    }
    /* Recovery is NVS-backed, so establish safe mode before deciding whether
     * normal boot should power and initialize the C6 before the SD card. */
    const esp_err_t recovery_err = cyberdeck_recovery::init();
    if (recovery_err != ESP_OK) {
        ESP_LOGE(TAG, "recovery state unavailable: %s", esp_err_to_name(recovery_err));
    }

    bool hosted_transport_ready = false;
    if (!cyberdeck_recovery::safe_mode()) {
        ESP_LOGI(TAG, "HOSTED_DIAG boot stage=hosted_prepare begin t_us=%lld",
                 static_cast<long long>(esp_timer_get_time()));
        const esp_err_t hosted_err = wifi_mgr_hosted_transport_prepare();
        ESP_LOGI(TAG, "HOSTED_DIAG boot stage=hosted_prepare end result=%s t_us=%lld", esp_err_to_name(hosted_err),
                 static_cast<long long>(esp_timer_get_time()));
        if (hosted_err == ESP_OK) {
            hosted_transport_ready = true;
        } else {
            ESP_LOGW(TAG, "ESP-Hosted pre-init failed: %s; trying normal SD mount",
                     esp_err_to_name(hosted_err));
        }
    }

    /* The terminal exposes its virtual root / only after the physical /sdcard
     * mount has completed and its BSP handle has been verified. On IDF 6,
     * ESP-Hosted and SD share the SDMMC controller (C6 slot 1, SD slot 0), so
     * Hosted must claim it first and the SD VFS must not re-init/deinit it. */
    const int64_t sd_mount_started_us = esp_timer_get_time();
    ESP_LOGI(TAG, "HOSTED_DIAG boot stage=sd_mount begin hosted_ready=%d t_us=%lld", hosted_transport_ready,
             static_cast<long long>(sd_mount_started_us));
    const esp_err_t sd_mount_err = hosted_transport_ready ? bsp_sdcard_mount_hosted() : bsp_sdcard_mount();
    ESP_LOGI(TAG, "HOSTED_DIAG boot stage=sd_mount end result=%s duration_ms=%lld t_us=%lld",
             esp_err_to_name(sd_mount_err), static_cast<long long>((esp_timer_get_time() - sd_mount_started_us) / 1000),
             static_cast<long long>(esp_timer_get_time()));
    ESP_ERROR_CHECK(sd_mount_err);
    if (bsp_sdcard_get_handle() == nullptr) return;
    {
        auto recovery_lease = sd_io_coordinator.acquire(cyberdeck_sd_io::access::write);
        if (!recovery_lease) {
            ESP_LOGW(TAG, "storage recovery skipped: SD storage unavailable");
        } else {
            const cyberdeck_recovery_report storage_recovery = recover_save_sidecars("/sdcard");
            for (const auto &entry : storage_recovery.entries) {
                ESP_LOGW(TAG, "storage recovery sidecar=%s action=%u stage=%u errno=%d",
                         entry.sidecar.c_str(), static_cast<unsigned>(entry.action),
                         static_cast<unsigned>(entry.stage), entry.error);
            }
        }
    }

    /* Recovery state is persisted before registering or starting services.
     * Corrupt/unavailable NVS is reported, never erased automatically. */
    ESP_ERROR_CHECK(cyberdeck_system_apps_register());
    cyberdeck_apps::global_runtime().set_sd_io(&sd_io_coordinator);
    cyberdeck_ui_bind_sd_io(sd_io_coordinator);
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

    /* A zero-timeout probe is not an ownership guarantee.  Do not let IMU
     * startup mutate LVGL concurrently with lvgl_port_task. */
    if (!bsp_display_lock(pdMS_TO_TICKS(1000))) {
        ESP_LOGE(TAG, "Falha ao adquirir o lock do display durante o boot");
        return;
    }
    err = imu_reader_start(display);
    if (err != ESP_OK) {
        bsp_display_unlock();
        ESP_ERROR_CHECK(err);
    }
    err = cyberdeck_ui_init();
    if (err != ESP_OK) {
        imu_reader_stop();
        bsp_display_unlock();
        ESP_ERROR_CHECK(err);
    }
    ESP_ERROR_CHECK(screenshot_server_set_display_port(cyberdeck_display_capture,
                                                       cyberdeck_display_release, nullptr));
    ESP_ERROR_CHECK(screen_off_init(display, 20));
    bsp_display_unlock();

    ESP_LOGI(TAG, "HOSTED_DIAG boot stage=battery_protection begin t_us=%lld",
             static_cast<long long>(esp_timer_get_time()));
    const esp_err_t battery_err = battery_protection_start();
    ESP_LOGI(TAG, "HOSTED_DIAG boot stage=battery_protection end result=%s t_us=%lld", esp_err_to_name(battery_err),
             static_cast<long long>(esp_timer_get_time()));
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
    app_runtime.set_command_dispatch_ready(true);
    ESP_LOGI(TAG, "CYBERDECK5 iniciado");
}
