#include "platform/sensors/imu_reader.h"

#include "bsp/esp-bsp.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "iot_sensor_hub.h"
#include "platform/sensors/orientation.h"
#include "sensor_type.h"

#include <atomic>

namespace {

constexpr uint32_t SENSOR_PERIOD_MS = 100;
constexpr uint32_t ROTATION_TIMER_MS = 150;
constexpr TickType_t INITIAL_SAMPLE_TIMEOUT_TICKS = pdMS_TO_TICKS(500);
const char *TAG = "cyberdeck_imu";

lv_display_t *s_display = nullptr;
std::atomic<int> s_target_rotation{LV_DISPLAY_ROTATION_0};
SemaphoreHandle_t s_first_sample_sem = nullptr;
StaticSemaphore_t s_first_sample_storage;
axis3_t s_first_sample_data = {};
std::atomic<bool> s_first_sample_captured{false};
sensor_handle_t s_sensor = nullptr;
sensor_event_handler_instance_t s_sensor_handler = nullptr;
lv_timer_t *s_rotation_timer = nullptr;

void sensor_event_handler(void *, esp_event_base_t, int32_t event_id, void *event_data)
{
    if (event_data == nullptr || event_id != SENSOR_ACCE_DATA_READY) {
        return;
    }

    const auto *data = static_cast<const sensor_data_t *>(event_data);
    const lv_display_rotation_t target = orientation_update(data->acce.x, data->acce.y, data->acce.z);
    s_target_rotation.store(static_cast<int>(target), std::memory_order_release);

    if (s_first_sample_sem != nullptr && !s_first_sample_captured.exchange(true, std::memory_order_acq_rel)) {
        s_first_sample_data = data->acce;
        xSemaphoreGive(s_first_sample_sem);
    }
}

void rotation_timer_callback(lv_timer_t *timer)
{
    auto *display = static_cast<lv_display_t *>(lv_timer_get_user_data(timer));
    const auto target = static_cast<lv_display_rotation_t>(s_target_rotation.load(std::memory_order_acquire));
    if (target != lv_display_get_rotation(display)) {
        lv_display_set_rotation(display, target);
        ESP_LOGI(TAG, "rotacao automatica aplicada: %d", static_cast<int>(target));
    }
}

} // namespace

extern "C" esp_err_t imu_reader_start(lv_display_t *display)
{
    if (display == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_sensor != nullptr || s_rotation_timer != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    s_display = display;
    orientation_reset();
    s_first_sample_captured.store(false, std::memory_order_release);
    s_first_sample_sem = xSemaphoreCreateBinaryStatic(&s_first_sample_storage);
    while (xSemaphoreTake(s_first_sample_sem, 0) == pdTRUE) {}

    const bsp_sensor_config_t config = {
        .type = IMU_ID,
        .mode = MODE_POLLING,
        .period = SENSOR_PERIOD_MS,
    };
    sensor_handle_t sensor = nullptr;
    esp_err_t err = bsp_sensor_init(&config, &sensor);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bsp_sensor_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = iot_sensor_handler_register(sensor, sensor_event_handler, &s_sensor_handler);
    if (err != ESP_OK) {
        iot_sensor_delete(sensor);
        ESP_LOGE(TAG, "sensor handler registration failed: %s", esp_err_to_name(err));
        return err;
    }

    err = iot_sensor_start(sensor);
    if (err != ESP_OK) {
        iot_sensor_handler_unregister(sensor, s_sensor_handler);
        s_sensor_handler = nullptr;
        iot_sensor_delete(sensor);
        ESP_LOGE(TAG, "sensor start failed: %s", esp_err_to_name(err));
        return err;
    }
    s_sensor = sensor;

    // Deteccao e aplicacao imediata da orientacao inicial do display antes da inicializacao da UI
    lv_display_rotation_t initial_rotation = LV_DISPLAY_ROTATION_0;
    bool sample_ok = false;
    if (s_first_sample_sem != nullptr) {
        if (xSemaphoreTake(s_first_sample_sem, INITIAL_SAMPLE_TIMEOUT_TICKS) == pdTRUE) {
            sample_ok = s_first_sample_captured.load(std::memory_order_acquire);
        }
        /* The callback runs on the sensor event task.  This semaphore is
         * static for the lifetime of the reader, so a late callback can never
         * give memory that the boot task has already freed. */
    }

    if (sample_ok) {
        initial_rotation = orientation_from_accel(s_first_sample_data.x, s_first_sample_data.y, s_first_sample_data.z,
                                                  LV_DISPLAY_ROTATION_0);
    } else {
        ESP_LOGW(TAG, "Leitura inicial do acelerometro nao recebida ou excedeu timeout; fallback para 0 graus");
    }

    orientation_set_current(initial_rotation);
    s_target_rotation.store(static_cast<int>(initial_rotation), std::memory_order_release);
    lv_display_set_rotation(display, initial_rotation);
    ESP_LOGI(TAG, "Orientacao inicial do display: %d", static_cast<int>(initial_rotation));

    s_rotation_timer = lv_timer_create(rotation_timer_callback, ROTATION_TIMER_MS, s_display);
    if (s_rotation_timer == nullptr) {
        imu_reader_stop();
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "IMU iniciado, rotacao automatica habilitada");
    return ESP_OK;
}

extern "C" void imu_reader_stop(void)
{
    if (s_rotation_timer != nullptr) {
        lv_timer_del(s_rotation_timer);
        s_rotation_timer = nullptr;
    }
    if (s_sensor != nullptr) {
        (void)iot_sensor_stop(s_sensor);
        if (s_sensor_handler != nullptr) {
            (void)iot_sensor_handler_unregister(s_sensor, s_sensor_handler);
            s_sensor_handler = nullptr;
        }
        (void)iot_sensor_delete(s_sensor);
        s_sensor = nullptr;
    }
    s_display = nullptr;
}
