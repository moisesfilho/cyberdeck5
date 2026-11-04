#include "platform/sensors/cyberdeck_expander_device.h"

#include "bsp/m5stack_tab5.h"
#include "driver/i2c_master.h"
// Address macro only: this file must never create a second driver instance.
#include "esp_io_expander_pi4ioe5v6408.h"
#include "platform/sensors/cyberdeck_expander_broker.h"

#include <chrono>

namespace {
esp_io_expander_handle_t s_handle = nullptr;
i2c_master_dev_handle_t s_raw_handle = nullptr;

esp_err_t add_raw_handle(i2c_master_bus_handle_t bus, uint32_t address)
{
    i2c_device_config_t config{};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address;
    config.scl_speed_hz = 100000;
    return i2c_master_bus_add_device(bus, &config, &s_raw_handle);
}
}

extern "C" esp_err_t cyberdeck_expander_device_init(void)
{
    cyberdeck_expander_broker::lease guard(std::chrono::milliseconds(250));
    if (!guard) return ESP_ERR_TIMEOUT;
    if (s_handle != nullptr && s_raw_handle != nullptr) return ESP_OK;
    if (bsp_i2c_init() != ESP_OK) return ESP_FAIL;

    if (s_handle == nullptr) {
        // Expander B must have exactly one driver instance for the whole boot.
        // esp_io_expander_new_i2c_pi4ioe5v6408() performs a physical chip reset
        // (0xFF to PI4IO_REG_CHIP_RESET), which clears direction/latch/high-Z
        // and returns every pin to its power-on default: CHG_STAT would become
        // an output and CHG_QC_EN/CHG_EN/WIFI_EN high-impedance. The BSP
        // singleton is the only owner, so the reset happens at most once even
        // when bsp_feature_enable(BSP_FEATURE_WIFI/USB) runs later.
        s_handle = bsp_io_expander1_init();
        if (s_handle == nullptr) return ESP_FAIL;
    }
    if (s_raw_handle == nullptr) {
        auto bus = bsp_i2c_get_handle();
        if (bus == nullptr) return ESP_ERR_INVALID_STATE;
        // Read-only register access. i2c_master_bus_add_device() does not touch
        // the chip, so diagnostics never reset or rewrite expander state.
        return add_raw_handle(bus, BSP_IO_EXPANDER_ADDRESS_1);
    }
    return ESP_OK;
}

extern "C" i2c_master_dev_handle_t cyberdeck_expander_raw_handle(void)
{
    return s_raw_handle;
}

extern "C" esp_io_expander_handle_t cyberdeck_expander_device_handle(void)
{
    return s_handle;
}
