#pragma once

struct esp_app_desc_t {
    char version[32];
};

inline esp_app_desc_t &lv_shim_app_description() {
    static esp_app_desc_t description{"1.0.0"};
    return description;
}

inline const esp_app_desc_t *esp_app_get_description() {
    return &lv_shim_app_description();
}
