#include "apps/network/cyberdeck_netdiag_https.h"
#include "esp_http_client.h"
#include "esp_tls.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace {
int checks = 0;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(x)) {                                                                                                    \
            std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #x);                                           \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (0)
struct Fake {
    int mode = 0, performs = 0, cleanups = 0, init_count = 0, header_count = 0, method_count = 0, status = 200;
    bool fail_init = false, fail_header = false, fail_method = false;
    std::string host, path, query, common, header_name, header_value;
    int port = 0, method = -1, transport = -1, timeout = 0;
    bool async = false, redirect = false, skip_cn = true, has_ca = false, handler = false;
    esp_http_client_event_handler_t event_handler{};
    std::uint64_t cancel_token = 0;
} f;
} // namespace
int netdiag_test_tls_error = 42, netdiag_test_tls_flags = 0;
std::int64_t now = 0;
std::int64_t esp_timer_get_time() {
    return now += 1000;
}
esp_err_t esp_crt_bundle_attach(void *) {
    return ESP_OK;
}
esp_err_t esp_tls_get_and_clear_last_error(esp_tls_error_handle_t, int *e, int *flags) {
    *e = netdiag_test_tls_error;
    *flags = netdiag_test_tls_flags;
    return ESP_OK;
}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c) {
    ++f.init_count;
    if (f.fail_init)
        return nullptr;
    f.host = c->host;
    f.path = c->path;
    f.query = c->query ? c->query : "";
    f.common = c->common_name;
    f.port = c->port;
    f.method = c->method;
    f.transport = c->transport_type;
    f.timeout = c->timeout_ms;
    f.async = c->is_async;
    f.redirect = c->disable_auto_redirect;
    f.skip_cn = c->skip_cert_common_name_check;
    f.has_ca = c->crt_bundle_attach != nullptr;
    f.handler = c->event_handler != nullptr;
    f.event_handler = c->event_handler;
    return &f;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char *n, const char *v) {
    ++f.header_count;
    f.header_name = n;
    f.header_value = v;
    return f.fail_header ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_http_client_set_method(esp_http_client_handle_t, esp_http_client_method_t m) {
    ++f.method_count;
    f.method = m;
    return f.fail_method ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_http_client_perform(esp_http_client_handle_t) {
    ++f.performs;
    if (f.cancel_token) {
        CHECK(cyberdeck_netdiag::https::cancel(f.cancel_token));
        f.cancel_token = 0;
    }
    if (f.mode == 1)
        return ESP_ERR_HTTP_EAGAIN;
    if (f.mode == 2) {
        esp_http_client_event_t e{HTTP_EVENT_ERROR, &f};
        f.event_handler(&e);
        return ESP_FAIL;
    }
    if (f.mode == 3)
        return ESP_FAIL;
    return ESP_OK;
}
int esp_http_client_get_status_code(esp_http_client_handle_t) {
    return f.status;
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t) {
    ++f.cleanups;
    return ESP_OK;
}

int main() {
    using namespace cyberdeck_netdiag::https;
    result r{};
    for (const char *bad :
         {"http://example.com", "https://u@example.com/", "https://example.com/#frag", "https://example.com:0/",
          "https://example.com:65536/", "https://example.com:4294967297/", "https://[::1]/", "https://bad..host/"}) {
        const int before = f.init_count;
        CHECK(start(bad, "192.0.2.1", 1) == start_result::invalid_request);
        CHECK(f.init_count == before);
    }
    CHECK(start("https://example.com/a?x=1", "192.0.2.8", 10) == start_result::accepted);
    CHECK(f.host == "192.0.2.8" && f.common == "example.com" && f.path == "/a" && f.query == "x=1" && f.port == 443);
    CHECK(f.transport == HTTP_TRANSPORT_OVER_SSL && f.async && f.has_ca && !f.skip_cn && f.redirect &&
          f.method == HTTP_METHOD_HEAD);
    CHECK(f.header_name == "Host" && f.header_value == "example.com" && f.method_count == 1);
    CHECK(poll(10, r) == poll_state::complete && r.code == result_code::response && r.http_status == 200 &&
          f.performs == 1 && f.cleanups == 1);
    f = {};
    f.mode = 1;
    CHECK(start("https://example.com:8443/p?q=", "2001:db8::1", 11) == start_result::accepted);
    CHECK(f.header_value == "example.com:8443" && f.path == "/p" && f.query == "q=" && f.port == 8443);
    CHECK(poll(11, r) == poll_state::pending && f.cleanups == 0 && f.performs == 1 && f.init_count == 1);
    f.mode = 0;
    f.status = 405;
    CHECK(poll(11, r) == poll_state::complete && r.http_status == 405 && r.code == result_code::response &&
          f.performs == 2 && f.cleanups == 1 && f.init_count == 1 && f.method == HTTP_METHOD_HEAD &&
          f.method_count == 1);
    CHECK(poll(11, r) == poll_state::unknown_token && f.performs == 2 && f.cleanups == 1);
    f = {};
    f.status = 301;
    CHECK(start("https://example.com/redirect", "192.0.2.1", 15) == start_result::accepted);
    CHECK(poll(15, r) == poll_state::complete && r.code == result_code::response && r.http_status == 301 &&
          f.performs == 1 && f.init_count == 1 && f.cleanups == 1);
    CHECK(poll(15, r) == poll_state::unknown_token && f.performs == 1 && f.init_count == 1);
    f = {};
    f.mode = 2;
    netdiag_test_tls_error = 0;
    netdiag_test_tls_flags = 1;
    CHECK(start("https://example.com", "192.0.2.1", 12) == start_result::accepted);
    CHECK(poll(12, r) == poll_state::complete && r.code == result_code::tls_error && f.cleanups == 1);
    netdiag_test_tls_error = 42;
    netdiag_test_tls_flags = 0;
    f = {};
    f.mode = 3;
    CHECK(start("https://example.com", "192.0.2.1", 13) == start_result::accepted);
    CHECK(poll(13, r) == poll_state::complete && r.code == result_code::transport_error && f.cleanups == 1);
    f = {};
    f.mode = 1;
    CHECK(start("https://example.com", "192.0.2.1", 14) == start_result::accepted);
    f.cancel_token = 14;
    CHECK(poll(14, r) == poll_state::pending && f.cleanups == 0);
    CHECK(poll(14, r) == poll_state::cancelled && r.code == result_code::cancelled && f.cleanups == 1);
    CHECK(poll(14, r) == poll_state::unknown_token);
    f = {};
    CHECK(start("https://example.com", "192.0.2.1", 15) == start_result::accepted);
    CHECK(start("https://example.com", "192.0.2.1", 16) == start_result::busy);
    CHECK(cancel(15));
    CHECK(poll(15, r) == poll_state::cancelled && f.performs == 0 && f.cleanups == 1);
    f = {};
    f.fail_header = true;
    CHECK(start("https://example.com", "192.0.2.1", 17) == start_result::client_unavailable && f.cleanups == 1);
    f = {};
    f.fail_method = true;
    CHECK(start("https://example.com", "192.0.2.1", 18) == start_result::client_unavailable && f.cleanups == 1);
    f = {};
    f.fail_init = true;
    CHECK(start("https://example.com", "192.0.2.1", 19) == start_result::client_unavailable && f.cleanups == 0);
    std::printf("HTTPS adapter: %d checks\n", checks);
}
