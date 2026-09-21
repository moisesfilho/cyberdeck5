#include "cyberdeck_ui.h"
#include "event_log.h"
#include "esp_log.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "imu_reader.h"
#include "screen_off.h"
#include "tab5_keyboard.h"
#include "wifi_mgr.h"
#include "screenshot_server.h"

static const char *TAG = "cyberdeck5";

extern "C" void app_main(void)
{
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
    tab5_keyboard_set_callback(cyberdeck_keyboard_input);
    ESP_ERROR_CHECK(tab5_keyboard_init());
    ESP_ERROR_CHECK(bsp_display_brightness_set(20));

    ESP_ERROR_CHECK(screenshot_server_init());
    ESP_ERROR_CHECK(wifi_mgr_add_state_callback(screenshot_server_wifi_state, nullptr));
    ESP_ERROR_CHECK(wifi_mgr_start());
    ESP_LOGI(TAG, "CYBERDECK5 iniciado");
}
