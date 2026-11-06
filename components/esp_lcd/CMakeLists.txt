idf_build_get_property(target IDF_TARGET)
if(NOT target STREQUAL "esp32p4")
    message(FATAL_ERROR "The local esp_lcd diagnostic overlay supports ESP32-P4 only")
endif()
if(NOT CONFIG_LCD_DSI_ISR_CACHE_SAFE OR NOT CONFIG_LCD_DSI_ISR_HANDLER_IN_IRAM)
    message(FATAL_ERROR "DSI diagnostic overlay requires the cache-safe IRAM ISR")
endif()

set(upstream "${IDF_PATH}/components/esp_lcd")
file(SHA256 "${upstream}/dsi/esp_lcd_panel_dpi.c" upstream_dpi_sha)
if(NOT upstream_dpi_sha STREQUAL "e963e8bd9cef82e3775bb230b22c4609be9abe54ab1814177c3fb0c1ddab5288")
    message(FATAL_ERROR "ESP-IDF esp_lcd DPI source changed; re-review the local diagnostic overlay")
endif()
file(SHA256 "${upstream}/Kconfig" upstream_kconfig_sha)
if(NOT upstream_kconfig_sha STREQUAL "11b1d2fd1d661cc7a4320f7dd5249f38d01a3aa94376a75b3e59f87b73b22e27")
    message(FATAL_ERROR "ESP-IDF esp_lcd Kconfig changed; re-review the local diagnostic overlay")
endif()
file(SHA256 "${upstream}/CMakeLists.txt" upstream_cmake_sha)
if(NOT upstream_cmake_sha STREQUAL "47f1a53f74e34626cdb2cefc0c1c8ed481001407314114debbb2b63fabd682c6")
    message(FATAL_ERROR "ESP-IDF esp_lcd component wiring changed; re-review the local diagnostic overlay")
endif()

set(srcs
    "${upstream}/src/esp_lcd_common.c"
    "${upstream}/src/esp_lcd_panel_io.c"
    "${upstream}/src/esp_lcd_panel_ssd1306.c"
    "${upstream}/src/esp_lcd_panel_st7789.c"
    "${upstream}/src/esp_lcd_panel_ops.c")

if(CONFIG_SOC_I2C_SUPPORTED)
    list(APPEND srcs "${upstream}/i2c/esp_lcd_panel_io_i2c.c")
endif()
if(CONFIG_SOC_GPSPI_SUPPORTED)
    list(APPEND srcs "${upstream}/spi/esp_lcd_panel_io_spi.c")
endif()
if(CONFIG_SOC_I2S_I80_LCD_SUPPORTED)
    list(APPEND srcs "${upstream}/i80/esp_lcd_panel_io_i2s.c")
endif()
if(CONFIG_SOC_PARLIO_LCD_SUPPORTED)
    list(APPEND srcs "${upstream}/parl/esp_lcd_panel_io_parl.c")
endif()
if(CONFIG_SOC_LCDCAM_I80_LCD_SUPPORTED)
    list(APPEND srcs "${upstream}/i80/esp_lcd_panel_io_i80.c")
endif()
if(CONFIG_SOC_LCDCAM_RGB_LCD_SUPPORTED)
    list(APPEND srcs "${upstream}/rgb/esp_lcd_panel_rgb.c")
endif()
if(CONFIG_SOC_MIPI_DSI_SUPPORTED)
    list(APPEND srcs "${upstream}/dsi/esp_lcd_mipi_dsi_bus.c"
                     "${upstream}/dsi/esp_lcd_panel_io_dbi.c"
                     "dsi/esp_lcd_panel_dpi.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "${upstream}/include" "${upstream}/interface"
                 "${upstream}/rgb/include" "${upstream}/dsi/include" "include"
    PRIV_INCLUDE_DIRS "${upstream}/priv_include" "${upstream}/dsi"
    PRIV_REQUIRES esp_mm esp_psram esp_pm esp_driver_i2s esp_driver_dma
    REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_parlio esp_hal_lcd
    LDFRAGMENTS "${upstream}/linker.lf")
