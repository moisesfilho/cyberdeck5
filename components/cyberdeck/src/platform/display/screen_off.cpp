#include "platform/display/screen_off.h"

#include "platform/display/cyberdeck_screen_protection.h"

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"

namespace {

using cyberdeck_screen_protection::persisted_timeout;

constexpr uint32_t DOUBLE_TAP_WINDOW_MS = 400;
constexpr UBaseType_t PERSISTENCE_QUEUE_CAPACITY = 4;
constexpr char NVS_NAMESPACE[] = "screen_pro";
constexpr char NVS_EFFECTIVE_KEY[] = "eff_min";
constexpr char NVS_LAST_POSITIVE_KEY[] = "last_min";
const char *TAG = "cyberdeck_screen";

struct persistence_request {
    persisted_timeout value;
};

lv_display_t *s_display = nullptr;
lv_obj_t *s_off_screen = nullptr;
lv_obj_t *s_previous_screen = nullptr;
lv_timer_t *s_inactivity_timer = nullptr;
QueueHandle_t s_persistence_queue = nullptr;
TaskHandle_t s_persistence_task = nullptr;
uint32_t s_last_tap = 0;
bool s_tap_pending = false;
int s_active_brightness = 20;
bool s_active = false;
cyberdeck_screen_protection::state s_state;

void reset_to_default_timeout()
{
    s_state = cyberdeck_screen_protection::state();
    (void)s_state.set_timeout_minutes(
        cyberdeck_screen_protection::default_timeout_minutes);
}

void show_screen(void);
void hide_screen(void);

esp_err_t persist_timeout(const persisted_timeout &value)
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }

    result = nvs_set_u16(handle, NVS_EFFECTIVE_KEY, value.effective_minutes);
    if (result == ESP_OK) {
        result = nvs_set_u16(handle, NVS_LAST_POSITIVE_KEY, value.last_positive_minutes);
    }
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

void load_persisted_timeout()
{
    nvs_handle_t handle;
    const esp_err_t open_result = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (open_result != ESP_OK) {
        if (open_result != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "sem estado de protecao de tela no NVS: %s",
                     esp_err_to_name(open_result));
        }
        reset_to_default_timeout();
        return;
    }

    persisted_timeout value{};
    const esp_err_t effective_result =
        nvs_get_u16(handle, NVS_EFFECTIVE_KEY, &value.effective_minutes);
    const esp_err_t last_positive_result =
        nvs_get_u16(handle, NVS_LAST_POSITIVE_KEY, &value.last_positive_minutes);
    nvs_close(handle);

    if (effective_result != ESP_OK || last_positive_result != ESP_OK ||
        !s_state.restore(value)) {
        if (effective_result != ESP_ERR_NVS_NOT_FOUND &&
            last_positive_result != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "estado de protecao de tela invalido; usando o padrao");
        }
        reset_to_default_timeout();
    }
}

void persistence_task(void *)
{
    for (;;) {
        persistence_request request{};
        if (xQueueReceive(s_persistence_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        const esp_err_t result = persist_timeout(request.value);
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "falha ao persistir timeout de tela: %s",
                     esp_err_to_name(result));
        }
    }
}

esp_err_t start_persistence_task()
{
    if (s_persistence_queue != nullptr) {
        return ESP_OK;
    }

    s_persistence_queue = xQueueCreate(PERSISTENCE_QUEUE_CAPACITY,
                                       sizeof(persistence_request));
    if (s_persistence_queue == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    const BaseType_t created = xTaskCreate(
        persistence_task, "screen_nvs", 4096, nullptr, 4, &s_persistence_task);
    if (created != pdPASS) {
        vQueueDelete(s_persistence_queue);
        s_persistence_queue = nullptr;
        s_persistence_task = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t queue_timeout_persistence(const persisted_timeout &value)
{
    if (s_persistence_queue == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    const persistence_request request{value};
    if (xQueueSend(s_persistence_queue, &request, 0) != pdTRUE) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void off_screen_clicked(lv_event_t *)
{
    const uint32_t now = lv_tick_get();
    if (s_tap_pending &&
        static_cast<uint32_t>(now - s_last_tap) <= DOUBLE_TAP_WINDOW_MS) {
        s_tap_pending = false;
        show_screen();
        return;
    }

    s_tap_pending = true;
    s_last_tap = now;
}

void show_screen(void)
{
    if (!s_active || s_display == nullptr) {
        return;
    }

    s_active = false;
    s_tap_pending = false;
    bsp_display_brightness_set(s_active_brightness);
    if (s_previous_screen != nullptr && s_previous_screen != s_off_screen) {
        lv_disp_load_scr(s_previous_screen);
    }
    lv_display_trigger_activity(s_display);
    s_state.turn_on();
    ESP_LOGI(TAG, "tela religada com duplo toque, brilho=%d%%", s_active_brightness);
}

void hide_screen(void)
{
    if (s_active || s_display == nullptr) {
        return;
    }

    s_previous_screen = lv_disp_get_scr_act(s_display);
    s_active = true;
    s_tap_pending = false;
    s_state.turn_off();
    bsp_display_brightness_set(0);
    if (s_previous_screen != s_off_screen) {
        lv_disp_load_scr(s_off_screen);
    }
    ESP_LOGI(TAG, "tela desligada por inatividade ou comando");
}

void inactivity_timer(lv_timer_t *)
{
    if (s_display == nullptr) {
        return;
    }

    if (s_state.evaluate_inactivity(lv_display_get_inactive_time(s_display))) {
        hide_screen();
    }
}

} // namespace

extern "C" esp_err_t screen_off_init(lv_display_t *display, int active_brightness)
{
    if (display == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_inactivity_timer != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    s_display = display;
    s_active_brightness = active_brightness;
    s_previous_screen = nullptr;
    s_last_tap = 0;
    s_tap_pending = false;
    s_active = false;
    reset_to_default_timeout();

    s_off_screen = lv_obj_create(nullptr);
    if (s_off_screen == nullptr) {
        s_display = nullptr;
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_size(s_off_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_off_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_off_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_off_screen, 0, 0);
    lv_obj_set_style_pad_all(s_off_screen, 0, 0);
    lv_obj_set_scrollable(s_off_screen, false);
    lv_obj_add_event_cb(s_off_screen, off_screen_clicked, LV_EVENT_CLICKED, nullptr);

    /* Restore before creating the timer so the first tick uses the saved policy. */
    load_persisted_timeout();

    s_inactivity_timer = lv_timer_create(inactivity_timer, 1000, nullptr);
    if (s_inactivity_timer == nullptr) {
        lv_obj_delete(s_off_screen);
        s_off_screen = nullptr;
        s_display = nullptr;
        return ESP_ERR_NO_MEM;
    }
    if (!s_state.timeout_enabled()) {
        lv_timer_pause(s_inactivity_timer);
    }

    const esp_err_t persistence_result = start_persistence_task();
    if (persistence_result != ESP_OK) {
        lv_timer_delete(s_inactivity_timer);
        s_inactivity_timer = nullptr;
        lv_obj_delete(s_off_screen);
        s_off_screen = nullptr;
        s_display = nullptr;
        return persistence_result;
    }

    ESP_LOGI(TAG, "protecao de tela habilitada: timeout=%u min, duplo toque=%ums",
             static_cast<unsigned>(s_state.effective_timeout_minutes()),
             static_cast<unsigned>(DOUBLE_TAP_WINDOW_MS));
    return ESP_OK;
}

extern "C" void screen_off_turn_on(void)
{
    if (s_display == nullptr) {
        return;
    }
    show_screen();
    lv_display_trigger_activity(s_display);
}

extern "C" void screen_off_turn_off(void)
{
    hide_screen();
}

extern "C" esp_err_t screen_off_set_timeout_minutes(uint16_t minutes)
{
    if (s_display == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_state.set_timeout_minutes(minutes)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_inactivity_timer != nullptr) {
        if (s_state.timeout_enabled()) {
            lv_timer_resume(s_inactivity_timer);
        } else {
            lv_timer_pause(s_inactivity_timer);
        }
    }

    return queue_timeout_persistence(s_state.persistence_view());
}
