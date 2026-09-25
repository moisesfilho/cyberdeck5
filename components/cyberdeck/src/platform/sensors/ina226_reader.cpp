#include "platform/sensors/ina226_reader.h"

#include "bsp/m5stack_tab5.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace {

constexpr uint8_t INA226_I2C_ADDRESS = 0x41;
constexpr uint32_t INA226_I2C_SPEED_HZ = 100000;
constexpr int INA226_I2C_TIMEOUT_MS = 100;
constexpr uint32_t INA226_TASK_STACK_SIZE = 4096;
constexpr UBaseType_t INA226_TASK_PRIORITY = 4;

constexpr uint8_t INA226_CONFIGURATION_REGISTER = 0x00;
constexpr uint8_t INA226_BUS_VOLTAGE_REGISTER = 0x02;
constexpr uint8_t INA226_CURRENT_REGISTER = 0x04;
constexpr uint8_t INA226_CALIBRATION_REGISTER = 0x05;
constexpr uint8_t INA226_MANUFACTURER_ID_REGISTER = 0xFF;
constexpr uint16_t INA226_CONFIGURATION_VALUE = 0x4527;
constexpr uint16_t INA226_CALIBRATION_VALUE = 0x0D55;
constexpr uint16_t INA226_MANUFACTURER_ID = 0x2260;
constexpr std::int32_t INA226_CURRENT_MA_NUMERATOR = 1525;
constexpr std::int32_t INA226_CURRENT_MA_DENOMINATOR = INA226_CALIBRATION_VALUE;

constexpr char INA226_TASK_NAME[] = "ina226";

i2c_master_bus_handle_t s_bus = nullptr;
i2c_master_dev_handle_t s_device = nullptr;
std::atomic<bool> s_started{false};
std::mutex s_snapshot_mutex;
cyberdeck_battery::snapshot s_snapshot{};
cyberdeck_battery::sample s_raw_sample{};

void publish_sample(const cyberdeck_battery::sample &value)
{
    const cyberdeck_battery::snapshot next = cyberdeck_battery::classify_sample(value);
    std::lock_guard<std::mutex> lock(s_snapshot_mutex);
    s_snapshot = next;
    s_raw_sample = value;
}

esp_err_t read_register(const uint8_t reg, uint8_t *out, const size_t length)
{
    if (s_device == nullptr || out == nullptr || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(s_device, &reg, sizeof(reg), out, length,
                                        INA226_I2C_TIMEOUT_MS);
}

esp_err_t write_register(const uint8_t reg, const uint16_t value)
{
    if (s_device == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t payload[3] = {
        reg,
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value & 0xFF),
    };
    return i2c_master_transmit(s_device, payload, sizeof(payload),
                               INA226_I2C_TIMEOUT_MS);
}

esp_err_t read_register_u16(const uint8_t reg, uint16_t *out)
{
    uint8_t raw[2] = {};
    const esp_err_t err = read_register(reg, raw, sizeof(raw));
    if (err != ESP_OK) {
        return err;
    }
    *out = (static_cast<uint16_t>(raw[0]) << 8) | raw[1];
    return ESP_OK;
}

bool read_sample(cyberdeck_battery::sample *out)
{
    if (out == nullptr) {
        return false;
    }

    uint16_t bus_voltage_raw = 0;
    uint16_t current_raw_unsigned = 0;
    if (read_register_u16(INA226_BUS_VOLTAGE_REGISTER, &bus_voltage_raw) != ESP_OK ||
        read_register_u16(INA226_CURRENT_REGISTER, &current_raw_unsigned) != ESP_OK ||
        bus_voltage_raw == 0) {
        return false;
    }

    /* INA226 bus-voltage LSB is 1.25 mV (5/4); register 0x02 therefore
     * provides the measured mV value used by the pure battery seam. */
    const std::int32_t bus_voltage_mv = static_cast<std::int32_t>(bus_voltage_raw * 5U / 4U);
    const std::int32_t signed_current_raw =
        current_raw_unsigned >= 0x8000U
            ? static_cast<std::int32_t>(current_raw_unsigned) - 0x10000
            : static_cast<std::int32_t>(current_raw_unsigned);
    const std::int64_t scaled_current =
        static_cast<std::int64_t>(signed_current_raw) * INA226_CURRENT_MA_NUMERATOR;
    std::int32_t current_ma = static_cast<std::int32_t>(
        scaled_current / INA226_CURRENT_MA_DENOMINATOR);
    if (current_ma == 0 && scaled_current != 0) {
        current_ma = scaled_current < 0 ? -1 : 1;
    }

    /* The INA226 bus-voltage register is the source of the percentage input;
     * the pure battery seam owns the approved 6000..8230 mV window. */
    *out = {true, true, bus_voltage_mv, current_ma};
    return true;
}

void ina226_reader_task(void *arg)
{
    (void)arg;
    while (true) {
        cyberdeck_battery::sample value{};
        if (read_sample(&value)) {
            publish_sample(value);
        } else {
            /* The sensor was present during startup, but this sample could not
             * be read.  Keep presence separate from read validity so the UI
             * can report unavailable rather than fabricating a percentage. */
            publish_sample(cyberdeck_battery::sample{true, false, 0, 0});
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelete(nullptr);
}

esp_err_t release_device()
{
    esp_err_t err = ESP_OK;
    if (s_device != nullptr) {
        err = i2c_master_bus_rm_device(s_device);
        s_device = nullptr;
    }
    return err;
}

} // namespace

extern "C" esp_err_t ina226_reader_init(void)
{
    if (s_started.load(std::memory_order_acquire)) {
        return ESP_OK;
    }

    publish_sample({false, false, 0, 0});

    esp_err_t err = bsp_i2c_init();
    if (err != ESP_OK) {
        ESP_LOGW("ina226", "BSP I2C init: %s", esp_err_to_name(err));
        return err;
    }
    s_bus = bsp_i2c_get_handle();
    if (s_bus == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    i2c_device_config_t device_config = {};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = INA226_I2C_ADDRESS;
    device_config.scl_speed_hz = INA226_I2C_SPEED_HZ;
    err = i2c_master_bus_add_device(s_bus, &device_config, &s_device);
    if (err != ESP_OK) {
        ESP_LOGW("ina226", "device setup: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_master_probe(s_bus, INA226_I2C_ADDRESS, INA226_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGW("ina226", "sensor absent: %s", esp_err_to_name(err));
        (void)release_device();
        return err;
    }

    uint16_t manufacturer_id = 0;
    err = read_register_u16(INA226_MANUFACTURER_ID_REGISTER, &manufacturer_id);
    if (err != ESP_OK || manufacturer_id != INA226_MANUFACTURER_ID) {
        ESP_LOGW("ina226", "invalid manufacturer id: 0x%04x", manufacturer_id);
        (void)release_device();
        return err == ESP_OK ? ESP_ERR_NOT_FOUND : err;
    }

    err = write_register(INA226_CONFIGURATION_REGISTER, INA226_CONFIGURATION_VALUE);
    if (err != ESP_OK) {
        (void)release_device();
        return err;
    }
    err = write_register(INA226_CALIBRATION_REGISTER, INA226_CALIBRATION_VALUE);
    if (err != ESP_OK) {
        (void)release_device();
        return err;
    }

    if (xTaskCreate(ina226_reader_task, INA226_TASK_NAME, INA226_TASK_STACK_SIZE,
                    nullptr, INA226_TASK_PRIORITY, nullptr) != pdPASS) {
        (void)release_device();
        return ESP_ERR_NO_MEM;
    }

    s_started.store(true, std::memory_order_release);
    ESP_LOGI("ina226", "reader initialized");
    return ESP_OK;
}

extern "C" esp_err_t ina226_reader_start(void)
{
    return ina226_reader_init();
}

extern "C" bool ina226_reader_started(void)
{
    return s_started.load(std::memory_order_acquire);
}

extern "C" bool ina226_reader_get_snapshot(cyberdeck_battery::snapshot *out_snapshot)
{
    if (out_snapshot == nullptr) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(s_snapshot_mutex);
        *out_snapshot = s_snapshot;
    }
    return ina226_reader_started();
}

extern "C" bool ina226_reader_get_raw_sample(cyberdeck_battery::sample *out_sample)
{
    if (out_sample == nullptr) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(s_snapshot_mutex);
        *out_sample = s_raw_sample;
    }
    return ina226_reader_started();
}
