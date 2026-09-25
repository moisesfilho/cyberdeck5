#include "platform/display/cyberdeck_ui.h"
#include "platform/logging/event_log.h"
#include "esp_log.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "platform/sensors/imu_reader.h"
#include "platform/sensors/battery_protection.h"
#include "platform/display/screen_off.h"
#include "platform/input/tab5_keyboard.h"
#include "features/wifi/wifi_mgr.h"
#include "features/screenshot/screenshot_server.h"
#include "features/serial/cyberdeck_serial_bridge.h"
#include "bsp/m5stack_tab5.h"

static const char *TAG = "cyberdeck5";

extern "C" void app_main(void)
{
    /* The terminal exposes its virtual root / only after the physical /sdcard
     * mount has completed and its BSP handle has been verified. */
    ESP_ERROR_CHECK(bsp_sdcard_mount());
    if (bsp_sdcard_get_handle() == nullptr) return;

    ESP_ERROR_CHECK(event_log_init());

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    lv_display_t *display = bsp_display_start();
    if (display == nullptr) {
        ESP_LOGE(TAG, "Falha ao iniciar o display");
        return;
    }

    bsp_display_lock(0);
    ESP_ERROR_CHECK(imu_reader_start(display));
    ESP_ERROR_CHECK(cyberdeck_ui_init());
    ESP_ERROR_CHECK(screen_off_init(display, 20));
    bsp_display_unlock();

    const esp_err_t battery_err = battery_protection_start();
    if (battery_err != ESP_OK) {
        ESP_LOGW(TAG, "Battery protection unavailable: %s", esp_err_to_name(battery_err));
    }

    tab5_keyboard_set_callback(cyberdeck_keyboard_input);
    ESP_ERROR_CHECK(tab5_keyboard_init());
    ESP_ERROR_CHECK(bsp_display_brightness_set(20));

    ESP_ERROR_CHECK(screenshot_server_init());
    ESP_ERROR_CHECK(wifi_mgr_add_state_callback(screenshot_server_wifi_state, nullptr));
    ESP_ERROR_CHECK(wifi_mgr_start());

    /* Ponte manual USB Serial-JTAG NDJSON (REQ-002..009): task propria,
     * fora da stack do LVGL; falha aqui nao derruba o boot. */
    if (!cyberdeck_serial::bridge_start()) {
        ESP_LOGW(TAG, "Ponte USB Serial-JTAG nao iniciada");
    }
    ESP_LOGI(TAG, "CYBERDECK5 iniciado");
}
