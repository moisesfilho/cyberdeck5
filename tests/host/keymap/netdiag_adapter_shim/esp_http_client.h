#pragma once
#include <cstdint>
#include "esp_err.h"
enum esp_http_client_transport_t { HTTP_TRANSPORT_UNKNOWN, HTTP_TRANSPORT_OVER_SSL };
enum esp_http_client_method_t { HTTP_METHOD_GET, HTTP_METHOD_HEAD };
enum esp_http_client_event_id_t { HTTP_EVENT_ERROR, HTTP_EVENT_ON_HEADER };
enum esp_http_client_addr_type_t { HTTP_ADDR_TYPE_INET, HTTP_ADDR_TYPE_INET6 };
struct esp_http_client_event_t { esp_http_client_event_id_t event_id; void *data; };
using esp_http_client_handle_t = void *;
using esp_http_client_event_handler_t = esp_err_t (*)(esp_http_client_event_t *);
using esp_http_client_crt_bundle_attach_t = esp_err_t (*)(void *);
struct esp_http_client_config_t {
 const char *host{}; const char *path{}; const char *query{}; int port{};
 esp_http_client_transport_t transport_type{}; esp_http_client_method_t method{};
 int timeout_ms{}; bool disable_auto_redirect{}; int max_redirection_count{};
 bool is_async{}; void *user_data{}; esp_http_client_event_handler_t event_handler{};
 const char *common_name{}; esp_http_client_crt_bundle_attach_t crt_bundle_attach{};
 bool skip_cert_common_name_check{}; esp_http_client_addr_type_t addr_type{};
};
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char *, const char *);
esp_err_t esp_http_client_set_method(esp_http_client_handle_t, esp_http_client_method_t);
esp_err_t esp_http_client_perform(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
