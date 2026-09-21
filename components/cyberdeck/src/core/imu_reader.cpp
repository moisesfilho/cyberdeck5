#include "imu_reader.h"

#include "bsp/esp-bsp.h"
#include "esp_check.h"
#include "esp_log.h"
#include "iot_sensor_hub.h"
#include "orientation.h"
#include "sensor_type.h"

#include <atomic>

namespace {

constexpr uint32_t SENSOR_PERIOD_MS = 100;
constexpr uint32_t ROTATION_TIMER_MS = 150;
const char *TAG = "cyberdeck_imu";

lv_display_t *s_display = nullptr;
std::atomic<int> s_target_rotation{LV_DISPLAY_ROTATION_0};

void sensor_event_handler(void *, esp_event_base_t, int32_t event_id, void *event_data)
{
    if (event_data == nullptr || event_id != SENSOR_ACCE_DATA_READY) {
        return;
    }

    const auto *data = static_cast<const sensor_data_t *>(event_data);
    const lv_display_rotation_t target = orientation_update(data->acce.x, data->acce.y, data->acce.z);
    s_target_rotation.store(static_cast<int>(target), std::memory_order_release);
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

    s_display = display;
    orientation_reset();
    s_target_rotation.store(static_cast<int>(LV_DISPLAY_ROTATION_0), std::memory_order_release);

    const bsp_sensor_config_t config = {
        .type = IMU_ID,
        .mode = MODE_POLLING,
        .period = SENSOR_PERIOD_MS,
    };
    sensor_handle_t sensor = nullptr;
    ESP_RETURN_ON_ERROR(bsp_sensor_init(&config, &sensor), TAG, "bsp_sensor_init failed");
    ESP_RETURN_ON_ERROR(iot_sensor_handler_register(sensor, sensor_event_handler, nullptr), TAG,
                        "sensor handler registration failed");
    ESP_RETURN_ON_ERROR(iot_sensor_start(sensor), TAG, "sensor start failed");

    lv_timer_create(rotation_timer_callback, ROTATION_TIMER_MS, s_display);
    ESP_LOGI(TAG, "IMU iniciado, rotacao automatica habilitada");
    return ESP_OK;
}
