#include "apps/network/cyberdeck_netdiag_https_adapter.h"

#include <cstring>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace cyberdeck_netdiag {
namespace {

constexpr uint32_t k_perform_timeout_ms = 250;

uint64_t monotonic_ms() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

bool matches_hostname(const char *url, const char *hostname) {
    if (url == nullptr || hostname == nullptr || hostname[0] == '\0' || std::strncmp(url, "https://", 8) != 0)
        return false;
    const char *authority = url + 8;
    const char *end = authority;
    while (*end != '\0' && *end != '/' && *end != '?' && *end != '#')
        ++end;
    const size_t length = static_cast<size_t>(end - authority);
    return std::strchr(authority, '@') == nullptr && std::strlen(hostname) == length &&
           std::memcmp(authority, hostname, length) == 0;
}

esp_err_t on_http_event(esp_http_client_event_t *event) {
    if (event != nullptr && event->user_data != nullptr && event->event_id == HTTP_EVENT_ON_DATA &&
        event->data_len > 0) {
        *static_cast<bool *>(event->user_data) = true;
        return ESP_FAIL;
    }
    return ESP_OK;
}

} // namespace

bool https_head(void *, const char *url, const char *hostname, uint64_t deadline_ms, uint16_t *http_status) {
    if (http_status == nullptr)
        return false;
    *http_status = 0;
    if (!matches_hostname(url, hostname) || monotonic_ms() >= deadline_ms)
        return false;

    bool received_body = false;
    esp_http_client_config_t config{};
    config.url = url;
    config.method = HTTP_METHOD_HEAD;
    config.timeout_ms = static_cast<int>(k_perform_timeout_ms);
    config.buffer_size = 512;
    config.buffer_size_tx = 512;
    config.disable_auto_redirect = true;
    config.is_async = true;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.event_handler = on_http_event;
    config.user_data = &received_body;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr)
        return false;

    esp_err_t result = ESP_FAIL;
    for (;;) {
        if (monotonic_ms() >= deadline_ms)
            break;
        result = esp_http_client_perform(client);
        if (result != ESP_ERR_HTTP_EAGAIN)
            break;
        vTaskDelay(1);
    }

    const int status = esp_http_client_get_status_code(client);
    if (status >= 0 && status <= UINT16_MAX)
        *http_status = static_cast<uint16_t>(status);
    const bool before_deadline = monotonic_ms() < deadline_ms;
    esp_http_client_cleanup(client);

    return before_deadline && result == ESP_OK && !received_body && status >= 200 && !(status >= 300 && status < 400);
}

} // namespace cyberdeck_netdiag
