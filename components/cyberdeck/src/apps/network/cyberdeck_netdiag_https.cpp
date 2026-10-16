#include "apps/network/cyberdeck_netdiag_https.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "lwip/inet.h"

namespace cyberdeck_netdiag::https {
namespace {

constexpr std::size_t k_url_capacity = 256;
constexpr std::size_t k_host_capacity = 254;
constexpr std::size_t k_path_capacity = 256;
constexpr int k_io_timeout_ms = 5000;

enum class phase : std::uint8_t { free, running, complete, cancelled };

struct parsed_url {
    char host[k_host_capacity]{};
    char authority[k_host_capacity + 8]{};
    char path[k_path_capacity]{};
    char query[k_path_capacity]{};
    std::uint16_t port = 443;
};

struct slot {
    phase current = phase::free;
    std::uint64_t token = 0;
    bool cancel_requested = false;
    std::int64_t started_us = 0;
    esp_http_client_handle_t client = nullptr;
    bool tls_failure = false;
    result answer{};
};

slot s_slot{};

bool parse_port(const char *text, std::size_t length, std::uint16_t &port) {
    if (length == 0)
        return false;
    unsigned value = 0;
    for (std::size_t i = 0; i < length; ++i) {
        const char c = text[i];
        if (c < '0' || c > '9')
            return false;
        value = value * 10u + static_cast<unsigned>(c - '0');
        if (value > 65535)
            return false;
    }
    if (value == 0)
        return false;
    port = static_cast<std::uint16_t>(value);
    return true;
}

bool valid_host(const char *host, std::size_t length) {
    if (length == 0 || length > 253)
        return false;
    std::size_t label_length = 0;
    bool previous_hyphen = false;
    for (std::size_t i = 0; i < length; ++i) {
        const unsigned char c = static_cast<unsigned char>(host[i]);
        if (c == '.') {
            if (label_length == 0 || previous_hyphen)
                return false;
            label_length = 0;
            previous_hyphen = false;
            continue;
        }
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if ((!alnum && c != '-') || (label_length == 0 && c == '-') || ++label_length > 63)
            return false;
        previous_hyphen = c == '-';
    }
    return label_length != 0 && !previous_hyphen;
}

bool parse_url(const char *url, parsed_url &parsed) {
    if (url == nullptr)
        return false;
    const std::size_t length = strnlen(url, k_url_capacity);
    if (length < 9 || length >= k_url_capacity || std::memcmp(url, "https://", 8) != 0)
        return false;
    for (std::size_t i = 0; i < length; ++i) {
        const unsigned char c = static_cast<unsigned char>(url[i]);
        if (c <= 0x20 || c == 0x7f || c == '#')
            return false;
    }
    const char *authority = url + 8;
    const char *authority_end = authority + strcspn(authority, "/?");
    const std::size_t authority_length = static_cast<std::size_t>(authority_end - authority);
    if (authority_length == 0 || std::memchr(authority, '@', authority_length) != nullptr ||
        std::memchr(authority, '[', authority_length) != nullptr ||
        std::memchr(authority, ']', authority_length) != nullptr)
        return false;
    const char *colon = static_cast<const char *>(std::memchr(authority, ':', authority_length));
    const std::size_t host_length = colon == nullptr ? authority_length : static_cast<std::size_t>(colon - authority);
    if (!valid_host(authority, host_length))
        return false;
    std::memcpy(parsed.host, authority, host_length);
    parsed.host[host_length] = '\0';
    if (colon != nullptr) {
        const std::size_t port_length = static_cast<std::size_t>(authority + authority_length - colon - 1);
        if (!parse_port(colon + 1, port_length, parsed.port))
            return false;
    }
    std::strcpy(parsed.authority, parsed.host);
    if (parsed.port != 443) {
        const std::size_t host_length_for_header = std::strlen(parsed.authority);
        const int written =
            std::snprintf(parsed.authority + host_length_for_header, sizeof(parsed.authority) - host_length_for_header,
                          ":%u", static_cast<unsigned>(parsed.port));
        if (written < 0 || static_cast<std::size_t>(written) >= sizeof(parsed.authority) - host_length_for_header)
            return false;
    }

    const char *query = std::strchr(authority_end, '?');
    const char *path_end = query == nullptr ? url + length : query;
    const char *path_start = authority_end;
    if (*path_start == '?')
        path_start = path_end;
    const std::size_t path_length = static_cast<std::size_t>(path_end - path_start);
    if (path_length >= sizeof(parsed.path))
        return false;
    if (path_length == 0) {
        std::strcpy(parsed.path, "/");
    } else {
        std::memcpy(parsed.path, path_start, path_length);
        parsed.path[path_length] = '\0';
    }
    if (query != nullptr) {
        const std::size_t query_length = static_cast<std::size_t>(url + length - query - 1);
        if (query_length >= sizeof(parsed.query))
            return false;
        std::memcpy(parsed.query, query + 1, query_length);
        parsed.query[query_length] = '\0';
    }
    return true;
}

void cleanup() {
    if (s_slot.client != nullptr) {
        esp_http_client_cleanup(s_slot.client);
        s_slot.client = nullptr;
    }
}

esp_err_t on_http_event(esp_http_client_event_t *event) {
    if (event == nullptr || event->event_id != HTTP_EVENT_ERROR || event->data == nullptr)
        return ESP_OK;
    int tls_error = 0;
    int tls_flags = 0;
    const auto error_handle = static_cast<esp_tls_error_handle_t>(event->data);
    if (esp_tls_get_and_clear_last_error(error_handle, &tls_error, &tls_flags) == ESP_OK &&
        (tls_error != 0 || tls_flags != 0))
        s_slot.tls_failure = true;
    return ESP_OK;
}

} // namespace

start_result start(const char *url, const char *numeric_address, std::uint64_t token) {
    if (s_slot.current != phase::free)
        return start_result::busy;
    parsed_url target{};
    if (!parse_url(url, target) || numeric_address == nullptr)
        return start_result::invalid_request;
    in_addr ipv4{};
    in6_addr ipv6{};
    const bool is_ipv4 = inet_pton(AF_INET, numeric_address, &ipv4) == 1;
    const bool is_ipv6 = !is_ipv4 && inet_pton(AF_INET6, numeric_address, &ipv6) == 1;
    if (!is_ipv4 && !is_ipv6)
        return start_result::invalid_request;

    esp_http_client_config_t config{};
    config.host = numeric_address;
    config.path = target.path;
    config.query = target.query[0] == '\0' ? nullptr : target.query;
    config.port = target.port;
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    config.method = HTTP_METHOD_HEAD;
    config.timeout_ms = k_io_timeout_ms;
    config.disable_auto_redirect = true;
    config.is_async = true;
    config.user_data = &s_slot;
    config.event_handler = on_http_event;
    config.common_name = target.host;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.skip_cert_common_name_check = false;
    config.addr_type = is_ipv4 ? HTTP_ADDR_TYPE_INET : HTTP_ADDR_TYPE_INET6;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr)
        return start_result::client_unavailable;
    if (esp_http_client_set_header(client, "Host", target.authority) != ESP_OK) {
        esp_http_client_cleanup(client);
        return start_result::client_unavailable;
    }
    if (esp_http_client_set_method(client, HTTP_METHOD_HEAD) != ESP_OK) {
        esp_http_client_cleanup(client);
        return start_result::client_unavailable;
    }
    s_slot = {};
    s_slot.current = phase::running;
    s_slot.token = token;
    s_slot.started_us = esp_timer_get_time();
    s_slot.client = client;
    return start_result::accepted;
}

bool cancel(std::uint64_t token) {
    if (s_slot.current != phase::running || s_slot.token != token)
        return false;
    s_slot.cancel_requested = true;
    return true;
}

poll_state poll(std::uint64_t token, result &completed) {
    if (s_slot.current == phase::free || s_slot.token != token)
        return poll_state::unknown_token;
    if (s_slot.current == phase::running && s_slot.cancel_requested) {
        cleanup();
        s_slot.answer = {result_code::cancelled, 0,
                         static_cast<std::uint32_t>((esp_timer_get_time() - s_slot.started_us) / 1000)};
        s_slot.current = phase::cancelled;
    } else if (s_slot.current == phase::running) {
        const esp_err_t status = esp_http_client_perform(s_slot.client);
        if (status == ESP_ERR_HTTP_EAGAIN)
            return poll_state::pending;
        const std::uint32_t duration = static_cast<std::uint32_t>((esp_timer_get_time() - s_slot.started_us) / 1000);
        if (status == ESP_OK) {
            s_slot.answer = {result_code::response,
                             static_cast<std::uint16_t>(esp_http_client_get_status_code(s_slot.client)), duration};
        } else {
            s_slot.answer = {s_slot.tls_failure ? result_code::tls_error : result_code::transport_error, 0, duration};
        }
        cleanup();
        s_slot.current = phase::complete;
    }
    completed = s_slot.answer;
    const poll_state result_state = s_slot.current == phase::cancelled ? poll_state::cancelled : poll_state::complete;
    s_slot = {};
    return result_state;
}

bool busy() {
    return s_slot.current != phase::free;
}

} // namespace cyberdeck_netdiag::https
