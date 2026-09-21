#include "platform/display/screen_off.h"

#include "bsp/esp-bsp.h"
#include "esp_log.h"

namespace {

constexpr uint32_t INACTIVITY_TIMEOUT_MS = 120000;
constexpr uint32_t DOUBLE_TAP_WINDOW_MS = 400;
const char *TAG = "cyberdeck_screen";

lv_display_t *s_display = nullptr;
lv_obj_t *s_off_screen = nullptr;
lv_obj_t *s_previous_screen = nullptr;
lv_timer_t *s_inactivity_timer = nullptr;
uint32_t s_last_tap = 0;
int s_active_brightness = 20;
bool s_active = false;

void hide_screen(void);

void off_screen_clicked(lv_event_t *)
{
    const uint32_t now = lv_tick_get();
    if (s_last_tap != 0 && (now - s_last_tap) <= DOUBLE_TAP_WINDOW_MS) {
        s_last_tap = 0;
        hide_screen();
        return;
    }
    s_last_tap = now;
}

void show_screen(void)
{
    if (s_active || s_display == nullptr) {
        return;
    }

    s_previous_screen = lv_disp_get_scr_act(s_display);
    s_active = true;
    s_last_tap = 0;
    bsp_display_brightness_set(0);
    lv_disp_load_scr(s_off_screen);
    ESP_LOGI(TAG, "tela desligada apos %u segundos de inatividade", INACTIVITY_TIMEOUT_MS / 1000);
}

void hide_screen(void)
{
    if (!s_active || s_display == nullptr) {
        return;
    }

    s_active = false;
    s_last_tap = 0;
    bsp_display_brightness_set(s_active_brightness);
    if (s_previous_screen != nullptr && s_previous_screen != s_off_screen) {
        lv_disp_load_scr(s_previous_screen);
    }
    lv_display_trigger_activity(s_display);
    ESP_LOGI(TAG, "tela religada com duplo toque, brilho=%d%%", s_active_brightness);
}

void inactivity_timer(lv_timer_t *)
{
    if (s_active || s_display == nullptr) {
        return;
    }
    if (lv_display_get_inactive_time(s_display) >= INACTIVITY_TIMEOUT_MS) {
        show_screen();
    }
}

} // namespace

extern "C" esp_err_t screen_off_init(lv_display_t *display, int active_brightness)
{
    if (display == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    s_display = display;
    s_active_brightness = active_brightness;
    s_off_screen = lv_obj_create(nullptr);
    if (s_off_screen == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_size(s_off_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_off_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_off_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_off_screen, 0, 0);
    lv_obj_set_style_pad_all(s_off_screen, 0, 0);
    lv_obj_set_scrollable(s_off_screen, false);
    lv_obj_add_event_cb(s_off_screen, off_screen_clicked, LV_EVENT_CLICKED, nullptr);
    s_inactivity_timer = lv_timer_create(inactivity_timer, 1000, nullptr);
    if (s_inactivity_timer == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "protecao de tela habilitada: timeout=120s, duplo toque=%ums",
             static_cast<unsigned>(DOUBLE_TAP_WINDOW_MS));
    return ESP_OK;
}
