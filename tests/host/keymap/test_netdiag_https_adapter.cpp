#include "apps/network/cyberdeck_netdiag_https_adapter.h"
#include "esp_http_client.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
uint64_t now_ms;
esp_http_client_config_t captured{};
int init_count, perform_count, cleanup_count, active_calls, overlap_count, delay_count;
int status_code;
uint16_t output_status;
esp_err_t perform_result;
bool fail_init, send_body, advance_to_deadline;
int eagain_calls;
uint64_t delay_advance_ms;
bool delay_before_next_perform;
} // namespace

extern "C" int64_t esp_timer_get_time(void) {
    return static_cast<int64_t>(now_ms * 1000);
}
extern "C" esp_err_t esp_crt_bundle_attach(void *) {
    return ESP_OK;
}
extern "C" void vTaskDelay(uint32_t ticks) {
    assert(ticks == 1);
    assert(active_calls == 0);
    assert(perform_count == delay_count + 1);
    ++delay_count;
    delay_before_next_perform = true;
    now_ms += delay_advance_ms;
}
extern "C" esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    ++init_count;
    captured = *config;
    return fail_init ? nullptr : &captured;
}
extern "C" esp_err_t esp_http_client_perform(esp_http_client_handle_t) {
    if (perform_count > 0) {
        assert(delay_before_next_perform);
        delay_before_next_perform = false;
    }
    if (++active_calls != 1)
        ++overlap_count;
    ++perform_count;
    if (send_body) {
        esp_http_client_event_t event{HTTP_EVENT_ON_DATA, captured.user_data, nullptr, 1};
        captured.event_handler(&event);
    }
    if (advance_to_deadline)
        now_ms = 100;
    --active_calls;
    if (eagain_calls > 0) {
        --eagain_calls;
        return ESP_ERR_HTTP_EAGAIN;
    }
    return perform_result;
}
extern "C" int esp_http_client_get_status_code(esp_http_client_handle_t) {
    return status_code;
}
extern "C" esp_err_t esp_http_client_cleanup(esp_http_client_handle_t) {
    assert(active_calls == 0);
    ++cleanup_count;
    return ESP_OK;
}

static void reset() {
    now_ms = 0;
    captured = {};
    init_count = perform_count = cleanup_count = 0;
    active_calls = overlap_count = delay_count = 0;
    status_code = 204;
    output_status = 0;
    perform_result = ESP_OK;
    fail_init = send_body = advance_to_deadline = false;
    eagain_calls = 0;
    delay_advance_ms = 0;
    delay_before_next_perform = false;
}

static void test_head_config_and_sequential_retry() {
    reset();
    // The fake returns EAGAIN twice and then completes; each invocation returns
    // before the next one starts, matching the public async-handle contract.
    eagain_calls = 2;
    status_code = 200;
    assert(cyberdeck_netdiag::https_head(nullptr, "https://example.test/path", "example.test", 90, &output_status));
    assert(output_status == 200);
    assert(init_count == 1 && perform_count == 3 && delay_count == 2 && cleanup_count == 1);
    assert(std::strcmp(captured.url, "https://example.test/path") == 0);
    assert(captured.method == HTTP_METHOD_HEAD && captured.disable_auto_redirect && captured.is_async);
    assert(captured.crt_bundle_attach == esp_crt_bundle_attach);
    assert(captured.event_handler != nullptr && captured.user_data != nullptr);
    assert(overlap_count == 0);
    reset();
    eagain_calls = 1;
    assert(cyberdeck_netdiag::https_head(nullptr, "https://example.test", "example.test", 90, &output_status));
    assert(perform_count == 2 && delay_count == 1 && cleanup_count == 1);
}

static void test_status_redirect_and_no_fallback() {
    reset();
    status_code = 405;
    assert(cyberdeck_netdiag::https_head(nullptr, "https://example.test", "example.test", 90, &output_status));
    assert(output_status == 405 && captured.method == HTTP_METHOD_HEAD && perform_count == 1 && delay_count == 0);
    reset();
    status_code = 302;
    assert(!cyberdeck_netdiag::https_head(nullptr, "https://example.test", "example.test", 90, &output_status));
    assert(output_status == 302 && captured.disable_auto_redirect && perform_count == 1 && delay_count == 0);
    reset();
    perform_result = ESP_FAIL;
    assert(!cyberdeck_netdiag::https_head(nullptr, "https://example.test", "example.test", 90, &output_status));
    assert(perform_count == 1 && delay_count == 0 && cleanup_count == 1 && captured.method == HTTP_METHOD_HEAD);
    reset();
    eagain_calls = 1;
    delay_advance_ms = 100;
    assert(!cyberdeck_netdiag::https_head(nullptr, "https://example.test", "example.test", 100, &output_status));
    assert(perform_count == 1 && delay_count == 1 && cleanup_count == 1 && overlap_count == 0);
}

static void test_validation_body_deadline_and_init_failure() {
    reset();
    assert(!cyberdeck_netdiag::https_head(nullptr, "http://example.test", "example.test", 90, &output_status));
    assert(init_count == 0);
    reset();
    assert(!cyberdeck_netdiag::https_head(nullptr, "https://user@example.test", "example.test", 90, &output_status));
    assert(init_count == 0);
    reset();
    assert(!cyberdeck_netdiag::https_head(nullptr, "https://other.test", "example.test", 90, &output_status));
    assert(init_count == 0);
    reset();
    fail_init = true;
    assert(!cyberdeck_netdiag::https_head(nullptr, "https://example.test", "example.test", 90, &output_status));
    assert(init_count == 1 && cleanup_count == 0);
    reset();
    send_body = true;
    assert(!cyberdeck_netdiag::https_head(nullptr, "https://example.test", "example.test", 90, &output_status));
    assert(cleanup_count == 1);
    reset();
    now_ms = 100;
    assert(!cyberdeck_netdiag::https_head(nullptr, "https://example.test", "example.test", 100, &output_status));
    assert(init_count == 0);
    reset();
    advance_to_deadline = true;
    assert(!cyberdeck_netdiag::https_head(nullptr, "https://example.test", "example.test", 100, &output_status));
    assert(perform_count == 1 && delay_count == 0 && cleanup_count == 1);
}

int main() {
    test_head_config_and_sequential_retry();
    test_status_redirect_and_no_fallback();
    test_validation_body_deadline_and_init_failure();
    std::puts("TEST-ND-04/05 HTTPS adapter behavioral tests passed");
}
