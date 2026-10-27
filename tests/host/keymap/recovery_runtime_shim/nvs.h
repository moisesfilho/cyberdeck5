#pragma once

#include "esp_err.h"

#include <stddef.h>

typedef int nvs_handle_t;
typedef int nvs_open_mode_t;

#define NVS_READWRITE 1

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *out_handle);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *size);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t size);
esp_err_t nvs_commit(nvs_handle_t handle);
void nvs_close(nvs_handle_t handle);

#ifdef __cplusplus
}
#endif
