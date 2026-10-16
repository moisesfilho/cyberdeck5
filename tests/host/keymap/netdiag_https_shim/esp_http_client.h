#pragma once
#include "esp_err.h"
#include <stddef.h>
#ifndef ESP_ERR_HTTP_EAGAIN
#define ESP_ERR_HTTP_EAGAIN 0x7001
#endif

typedef void *esp_http_client_handle_t;
typedef enum { HTTP_METHOD_GET = 0, HTTP_METHOD_HEAD = 1 } esp_http_client_method_t;
typedef enum { HTTP_EVENT_ON_DATA = 0 } esp_http_client_event_id_t;
typedef struct {
    esp_http_client_event_id_t event_id;
    void *user_data;
    void *data;
    int data_len;
} esp_http_client_event_t;
typedef esp_err_t (*esp_http_client_event_cb_t)(esp_http_client_event_t *);
typedef esp_err_t (*esp_crt_bundle_attach_fn)(void *);
typedef struct {
    const char *url;
    esp_http_client_method_t method;
    int timeout_ms;
    int buffer_size;
    int buffer_size_tx;
    bool disable_auto_redirect;
    bool is_async;
    esp_crt_bundle_attach_fn crt_bundle_attach;
    esp_http_client_event_cb_t event_handler;
    void *user_data;
} esp_http_client_config_t;

extern "C" {
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_perform(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
esp_err_t esp_crt_bundle_attach(void *);
}
