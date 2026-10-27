
set(SRC
    src/bsp_display.c
    src/bsp_audio.c
    src/bsp_feature_en.c
    src/bsp_i2c.c
    src/bsp_io_expander.c
    src/bsp_sensors.c
    src/bsp_storage.c
)

set(REQ esp_driver_gpio esp_driver_i2c esp_driver_i2s fatfs vfs esp_driver_sdspi esp_driver_sdmmc esp_codec_dev)
set(PRIV_REQ esp_lcd esp_driver_ledc esp_io_expander_pi4ioe5v6408)

idf_component_register(
    SRCS ${SRC}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES ${REQ}
    PRIV_REQUIRES esp_psram ${PRIV_REQ}
)
