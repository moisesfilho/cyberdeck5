#include "apps/system/cyberdeck_time_sync.h"
#include "apps/runtime/cyberdeck_app_logger.h"
#include "apps/runtime/cyberdeck_app_runtime.h"
#include "apps/wifi/wifi_mgr.h"
#include "platform/logging/event_log.h"
#include "esp_http_client.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <algorithm>
#include <assert.h>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <string>
#include <sys/time.h>
#include <thread>
#include <vector>

namespace fake {
std::mutex m;
std::condition_variable cv;
wifi_state_cb_t cb{};
void *cb_ctx{};
wifi_status_t status{};
int gets{}, sets{};
int task_creations{}, task_deletions{}, callbacks_added{};
int active_requests{}, max_active_requests{}, cleanups{};
int durable_calls{}, durable_successes{}, durable_failures{}, durable_failures_remaining{};
esp_err_t durable_failure_result = ESP_ERR_NO_MEM;
int primary_gets{}, fallback_gets{};
timeval clock{};
bool pending{}, release{}, split_body{}, stop_callback_removed{};
bool pause_after_primary_failure{}, primary_failure_paused{};
bool suppress_retries = true;
bool route_matrix = false;
int settimeofday_result = 0;
enum mode { good, timeout, dns, tls, http_error, empty, malformed, invalid } behavior = good;
mode primary_behavior = good, fallback_behavior = good;
std::string primary_body, fallback_body;
std::string body;
std::string last_url;
int last_method = -1;
bool tls_bundle = false;
} // namespace fake

namespace cyberdeck_apps {
runtime &global_runtime() {
    static runtime value;
    return value;
}

void runtime::set_logger(logger *value) { logger_ = value; }
} // namespace cyberdeck_apps

class fake_logger final : public cyberdeck_apps::logger {
public:
    int events = 0;
    event last{};

protected:
    void write_event(const event &value) override {
        last = value;
        ++events;
    }

public:
    std::size_t latest(std::size_t, line_callback, void *) override { return 0; }
};

static fake_logger logger;

extern "C" void event_log_write(char, const char *, const char *) {}
extern "C" esp_err_t event_log_write_durable(char level, const char *tag, const char *message) {
    ++fake::durable_calls;
    if (fake::durable_failures_remaining > 0) {
        --fake::durable_failures_remaining;
        ++fake::durable_failures;
        return fake::durable_failure_result;
    }
    /* The fake acknowledges only after the append/fsync seam succeeds. */
    ++fake::durable_successes;
    ++logger.events;
    logger.last.severity = level == 'I' ? cyberdeck_apps::logger::level::info : cyberdeck_apps::logger::level::error;
    std::strncpy(logger.last.tag, tag, sizeof(logger.last.tag) - 1);
    logger.last.tag_bytes = std::strlen(tag);
    std::strncpy(logger.last.payload, message, sizeof(logger.last.payload) - 1);
    logger.last.payload_bytes = std::strlen(message);
    return ESP_OK;
}

extern "C" esp_err_t wifi_mgr_add_state_callback(wifi_state_cb_t cb, void *ctx) {
    fake::cb = cb;
    fake::cb_ctx = ctx;
    ++fake::callbacks_added;
    return ESP_OK;
}
extern "C" esp_err_t wifi_mgr_remove_state_callback(wifi_state_cb_t cb, void *ctx) {
    {
        std::lock_guard<std::mutex> l(fake::m);
        fake::stop_callback_removed = true;
    }
    fake::cv.notify_all();
    if (fake::cb == cb && fake::cb_ctx == ctx) {
        fake::cb = nullptr;
        fake::cb_ctx = nullptr;
    }
    return ESP_OK;
}
extern "C" esp_err_t wifi_mgr_get_status(wifi_status_t *s) {
    *s = fake::status;
    if (fake::suppress_retries)
        s->has_ip = false;
    return ESP_OK;
}
extern "C" int settimeofday(const timeval *tv, const struct timezone *) {
    std::lock_guard<std::mutex> l(fake::m);
    if (fake::settimeofday_result != 0)
        return fake::settimeofday_result;
    fake::clock = *tv;
    ++fake::sets;
    return 0;
}

struct Client {
    esp_http_client_config_t c;
    int status = 200;
};
extern "C" esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c) {
    auto *x = new Client{*c};
    ++fake::gets;
    if (std::strstr(c->url, "timeapi.io") != nullptr) ++fake::primary_gets;
    else ++fake::fallback_gets;
    {
        std::lock_guard<std::mutex> l(fake::m);
        fake::last_url = c->url;
        fake::last_method = c->method;
        fake::tls_bundle = c->crt_bundle_attach != nullptr;
    }
    return x;
}
extern "C" esp_err_t esp_http_client_perform(esp_http_client_handle_t h) {
    auto *x = static_cast<Client *>(h);
    std::unique_lock<std::mutex> l(fake::m);
    fake::pending = true;
    fake::cv.notify_all();
    ++fake::active_requests;
    fake::max_active_requests = std::max(fake::max_active_requests, fake::active_requests);
    const bool primary = std::strstr(x->c.url, "timeapi.io") != nullptr;
    const auto selected_behavior = fake::route_matrix ? (primary ? fake::primary_behavior : fake::fallback_behavior) : fake::behavior;
    if (selected_behavior == fake::timeout) {
        l.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        l.lock();
        --fake::active_requests;
        fake::pending = false;
        return ESP_ERR_TIMEOUT;
    }
    if (selected_behavior == fake::dns || selected_behavior == fake::tls) {
        --fake::active_requests;
        fake::pending = false;
        if (primary && selected_behavior == fake::dns && fake::pause_after_primary_failure) {
            fake::primary_failure_paused = true;
            fake::cv.notify_all();
            fake::cv.wait(l, [] { return !fake::pause_after_primary_failure; });
        }
        return ESP_FAIL;
    }
    if (selected_behavior == fake::http_error)
        x->status = 503;
    std::string b = fake::route_matrix ? (primary ? fake::primary_body : fake::fallback_body) : fake::body;
    if (selected_behavior == fake::empty)
        b.clear();
    if (selected_behavior == fake::malformed)
        b = "{";
    if (selected_behavior == fake::invalid)
        b = R"({"timezone":"Etc/UTC","utc_datetime":"2024-02-30T00:00:00+00:00"})";
    bool wait = fake::release == false;
    if (wait)
        fake::cv.wait(l, [&] { return fake::release; });
    if (!b.empty()) {
        const size_t first = fake::split_body ? std::min<size_t>(2048, b.size()) : b.size();
        esp_http_client_event_t e{HTTP_EVENT_ON_DATA, x->c.user_data, (void *)b.data(), static_cast<int>(first)};
        x->c.event_handler(&e);
        if (first < b.size()) {
            e.data = b.data() + first;
            e.data_len = static_cast<int>(b.size() - first);
            x->c.event_handler(&e);
        }
    }
    --fake::active_requests;
    fake::pending = false;
    return ESP_OK;
}
extern "C" int esp_http_client_get_status_code(esp_http_client_handle_t h) {
    return static_cast<Client *>(h)->status;
}
extern "C" esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h) {
    ++fake::cleanups;
    delete static_cast<Client *>(h);
    return ESP_OK;
}

struct Sem {
    std::mutex m;
    std::condition_variable cv;
    bool value = false;
};
extern "C" SemaphoreHandle_t xSemaphoreCreateBinary() {
    return new Sem;
}
extern "C" void vSemaphoreDelete(SemaphoreHandle_t s) {
    delete static_cast<Sem *>(s);
}
extern "C" BaseType_t xSemaphoreGive(SemaphoreHandle_t s) {
    auto *x = static_cast<Sem *>(s);
    {
        std::lock_guard<std::mutex> l(x->m);
        x->value = true;
    }
    x->cv.notify_one();
    return pdTRUE;
}
extern "C" BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ms) {
    auto *x = static_cast<Sem *>(s);
    std::unique_lock<std::mutex> l(x->m);
    if (!x->cv.wait_for(l, std::chrono::milliseconds(ms), [&] { return x->value; }))
        return pdFALSE;
    x->value = false;
    return pdTRUE;
}
extern "C" BaseType_t xTaskCreateWithCaps(TaskFunction_t fn, const char *, uint32_t, void *arg, unsigned,
                                          TaskHandle_t *out, uint32_t) {
    ++fake::task_creations;
    std::thread([=] { fn(arg); }).detach();
    *out = reinterpret_cast<TaskHandle_t>(1);
    return pdPASS;
}
extern "C" void vTaskDelete(TaskHandle_t) {
    pthread_exit(nullptr);
}
extern "C" void vTaskDeleteWithCaps(TaskHandle_t handle) {
    assert(handle == nullptr);
    ++fake::task_deletions;
    pthread_exit(nullptr);
}
extern "C" void vTaskDelay(TickType_t) {}

static void reset() {
    fake::release = true;
    fake::cv.notify_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    fake::gets = fake::sets = 0;
    fake::task_creations = fake::task_deletions = fake::callbacks_added = 0;
    fake::active_requests = fake::max_active_requests = fake::cleanups = 0;
    fake::durable_calls = fake::durable_successes = fake::durable_failures = 0;
    fake::durable_failures_remaining = 0;
    fake::durable_failure_result = ESP_ERR_NO_MEM;
    fake::primary_gets = fake::fallback_gets = 0;
    fake::split_body = false;
    fake::stop_callback_removed = false;
    fake::pause_after_primary_failure = false;
    fake::primary_failure_paused = false;
    fake::suppress_retries = true;
    fake::status = {};
    fake::clock = {111, 0};
    fake::behavior = fake::good;
    fake::settimeofday_result = 0;
    fake::body = R"({"timezone":"Etc/UTC","utc_datetime":"2024-02-29T23:59:59.123456+00:00"})";
    fake::route_matrix = false;
    fake::primary_behavior = fake::fallback_behavior = fake::good;
    fake::primary_body = R"({"timeZone":"UTC","year":2024,"month":2,"day":29,"hour":23,"minute":59,"seconds":58})";
    fake::fallback_body = fake::body;
    logger = fake_logger{};
    cyberdeck_apps::global_runtime().set_logger(&logger);
}
static void ip(uint64_t token) {
    std::lock_guard<std::mutex> l(fake::m);
    fake::release = false;
    fake::status = {true, true, "test", "192.0.2.1", token};
    if (fake::cb)
        fake::cb(&fake::status, true, fake::cb_ctx);
}
static void no_ip() {
    fake::status.connected = true;
    fake::status.has_ip = false;
    if (fake::cb)
        fake::cb(&fake::status, true, fake::cb_ctx);
}
static void finish() {
    fake::release = true;
    fake::cv.notify_all();
}
static void wait_pending() {
    std::unique_lock<std::mutex> l(fake::m);
    fake::cv.wait_for(l, std::chrono::milliseconds(100), [] { return fake::pending; });
}
static void wait_primary_failure() {
    std::unique_lock<std::mutex> l(fake::m);
    fake::cv.wait_for(l, std::chrono::milliseconds(100), [] { return fake::primary_failure_paused; });
}
static void release_primary_failure() {
    {
        std::lock_guard<std::mutex> l(fake::m);
        fake::pause_after_primary_failure = false;
        fake::release = true;
    }
    fake::cv.notify_all();
}
static void wait_stop_callback_removed() {
    std::unique_lock<std::mutex> l(fake::m);
    fake::cv.wait_for(l, std::chrono::milliseconds(100), [] { return fake::stop_callback_removed; });
}
static void wait_sets(int n) {
    for (int i = 0; i < 100 && fake::sets < n; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
static void wait_idle() {
    for (int i = 0; i < 100; ++i) {
        {
            std::lock_guard<std::mutex> l(fake::m);
            if (fake::active_requests == 0 && !fake::pending)
                return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

int main() {
    /* TEST-TIME-STACK-01 is a structural contract: the runtime manifest must
       use the production 16384-byte stack and must not regress to 6144 bytes.
       TEST-TIME-01..09 are intentionally all exercised through the production
       task and its HTTP/TLS/clock/Wi-Fi seams; no socket or public DNS exists. */
    reset();
    assert(cyberdeck_time_sync_start() == ESP_OK);
    assert(cyberdeck_time_sync_start() == ESP_OK);
    cyberdeck_time_sync_boot_ready();
    no_ip();
    assert(fake::gets == 0);
    ip(1);
    wait_pending();
    finish();
    wait_sets(1);
    assert(fake::gets == 2 && fake::primary_gets == 1 && fake::fallback_gets == 1 && fake::sets == 1);
    /* TEST-TIME-01: worldtimeapi returns fractional seconds; the sync accepts
       the real utc_datetime shape and applies its whole-second epoch. */
    assert(fake::clock.tv_sec == 1709251199 && fake::clock.tv_usec == 0);
    assert(logger.events == 1); // TEST-TIME-11: one fixed bounded success event.
    assert(logger.last.severity == cyberdeck_apps::logger::level::info);
    assert(logger.last.tag_bytes == 9 && std::strcmp(logger.last.tag, "time_sync") == 0);
    assert(logger.last.payload_bytes == 7 && std::strcmp(logger.last.payload, "success") == 0);
    assert(fake::durable_calls == 1 && fake::durable_successes == 1 && fake::durable_failures == 0);
    /* TEST-TIME-13: the primary TimeAPI schema is accepted and wins without
       touching the fallback. */
    fake::route_matrix = true;
    fake::primary_behavior = fake::good;
    fake::fallback_behavior = fake::good;
    fake::primary_body = R"({"timeZone":"UTC","year":2024,"month":2,"day":29,"hour":23,"minute":59,"seconds":58})";
    fake::fallback_body = R"({"timezone":"Etc/UTC","utc_datetime":"2024-02-29T23:59:59+00:00"})";
    const int fallback_before_primary = fake::fallback_gets;
    ip(20); wait_pending(); finish(); wait_sets(2); wait_idle();
    assert(fake::sets == 2 && fake::primary_gets == 2 && fake::fallback_gets == fallback_before_primary);
    assert(logger.events == 2);
    /* TEST-TIME-14: transport failure on TimeAPI falls back to valid
       WorldTimeAPI, which performs exactly one update/event. */
    fake::primary_behavior = fake::dns;
    fake::fallback_behavior = fake::good;
    const int sets_before_fallback = fake::sets;
    const int events_before_fallback = logger.events;
    ip(21); wait_pending(); finish(); wait_sets(3); wait_idle();
    assert(fake::sets == sets_before_fallback + 1 && logger.events == events_before_fallback + 1);
    assert(fake::fallback_gets == fallback_before_primary + 1);
    /* TEST-TIME-15/16: both endpoints failing, malformed schemas, invalid
       timezone/date, and oversized bodies never partially mutate the clock or
       emit success. */
    const std::string valid_primary = fake::primary_body;
    const std::string valid_fallback = fake::fallback_body;
    const int stable_sets = fake::sets;
    const int stable_events = logger.events;
    const std::vector<std::pair<std::string, std::string>> invalid_bodies = {
        {"{}", "{}"},
        {R"({"timeZone":"Europe/Paris","year":2024,"month":2,"day":29,"hour":23,"minute":59,"seconds":58})", R"({"timezone":"UTC","utc_datetime":"2024-02-29T23:59:59+00:00"})"},
        {R"({"timeZone":"UTC","year":2024,"month":2,"day":30,"hour":23,"minute":59,"seconds":58})", R"({"timezone":"Etc/UTC","utc_datetime":"2024-02-30T23:59:59+00:00"})"},
        {std::string(R"({"timeZone":"UTC","year":2024,"month":2,"day":29,"hour":23,"minute":59,"seconds":58,"padding":")") + std::string(2050, 'x') + "\"}",
         std::string(R"({"timezone":"Etc/UTC","utc_datetime":"2024-02-29T23:59:59+00:00","padding":")") + std::string(2050, 'x') + "\"}"}};
    for (const auto &invalid : invalid_bodies) {
        fake::primary_behavior = fake::good; fake::fallback_behavior = fake::good;
        fake::primary_body = invalid.first; fake::fallback_body = invalid.second;
        ip(30 + static_cast<uint64_t>(&invalid - &invalid_bodies[0])); wait_pending(); finish(); wait_idle();
        assert(fake::sets == stable_sets && logger.events == stable_events);
    }
    fake::primary_body = valid_primary; fake::fallback_body = valid_fallback;
    fake::primary_behavior = fake::timeout; fake::fallback_behavior = fake::tls;
    ip(40); wait_pending(); finish(); wait_idle();
    assert(fake::sets == stable_sets && logger.events == stable_events);
    /* End the first lifecycle before resetting the host seams.  This observes
       the worker's capability-aware self-delete and prevents a stale worker
       from making the next start a logical no-op. */
    assert(cyberdeck_time_sync_stop(9000) == ESP_OK);
    assert(fake::task_deletions == 1);
    assert(cyberdeck_time_sync_stop(9000) == ESP_OK);
    assert(fake::task_deletions == 1);

    /* TEST-TIME-08/17: stop between a failed primary and fallback quiesces the
       worker before fallback starts; the next lifecycle can start normally. */
    reset();
    assert(cyberdeck_time_sync_start() == ESP_OK);
    cyberdeck_time_sync_boot_ready();
    fake::route_matrix = true;
    fake::primary_behavior = fake::dns;
    fake::fallback_behavior = fake::good;
    fake::pause_after_primary_failure = true;
    ip(300);
    wait_primary_failure();
    const int fallback_before_stop = fake::fallback_gets;
    std::thread fallback_stopper([] { assert(cyberdeck_time_sync_stop(9000) == ESP_OK); });
    wait_stop_callback_removed();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    release_primary_failure();
    fallback_stopper.join();
    assert(fake::fallback_gets == fallback_before_stop);
    assert(fake::sets == 0);
    assert(fake::task_deletions == 1);

    assert(cyberdeck_time_sync_start() == ESP_OK);
    cyberdeck_time_sync_boot_ready();
    fake::primary_behavior = fake::good;
    ip(301);
    wait_pending();
    finish();
    wait_sets(1);
    assert(fake::sets == 1 && fake::primary_gets == 2 && fake::fallback_gets == fallback_before_stop);
    assert(cyberdeck_time_sync_stop(9000) == ESP_OK);
    assert(fake::task_deletions == 2);

    /* TEST-TIME-17: a valid primary response does not trigger fallback, and
       duplicate concurrent IP notifications remain one update. */
    reset();
    assert(cyberdeck_time_sync_start() == ESP_OK);
    cyberdeck_time_sync_boot_ready();
    fake::route_matrix = true;
    fake::primary_behavior = fake::good; fake::fallback_behavior = fake::good;
    const int primary_before_idempotence = fake::primary_gets;
    const int fallback_before_idempotence = fake::fallback_gets;
    const int sets_before_idempotence = fake::sets;
    std::vector<std::thread> duplicate_events;
    for (int i = 0; i < 8; ++i) duplicate_events.emplace_back([] { ip(41); });
    for (auto &event : duplicate_events) event.join();
    wait_pending(); finish(); wait_sets(sets_before_idempotence + 1); wait_idle();
    assert(fake::sets == sets_before_idempotence + 1);
    assert(fake::primary_gets == primary_before_idempotence + 1 && fake::fallback_gets == fallback_before_idempotence);
    fake::route_matrix = false;
    int old = fake::sets;
    const int events_before_failures = logger.events;
    for (auto b :
         {fake::timeout, fake::dns, fake::tls, fake::http_error, fake::empty, fake::malformed, fake::invalid}) {
        fake::behavior = b;
        ip(100 + (int)b);
        wait_pending();
        finish();
        wait_idle();
        assert(fake::sets == old);
        assert(logger.events == events_before_failures); // TEST-TIME-12
    }
    /* TEST-TIME-04: fractional timestamps remain valid while missing/wrong
       fields, divergent timezone, and >2048 bytes remain rejected. */
    fake::body = R"({"timezone":"Etc/UTC","utc_datetime":"2024-02-29T23:59:59.999999+00:00"})";
    fake::behavior = fake::good;
    ip(140);
    wait_pending();
    finish();
    wait_sets(old + 1);
    assert(fake::sets >= old + 1);
    assert(logger.events >= events_before_failures + 1);
    old = fake::sets;
    const int events_before_settimeofday_failure = logger.events;
    fake::settimeofday_result = -1;
    fake::behavior = fake::good;
    ip(145);
    wait_pending();
    finish();
    wait_idle();
    assert(fake::sets == old);
    assert(logger.events == events_before_settimeofday_failure); // TEST-TIME-12
    fake::settimeofday_result = 0;
    const int events_before_unavailable_logger = logger.events;
    cyberdeck_apps::global_runtime().set_logger(nullptr);
    ip(146);
    wait_pending();
    finish();
    wait_sets(old + 1);
    assert(fake::sets >= old + 1);
    assert(logger.events == events_before_unavailable_logger + 1); // TEST-TIMELOG-01: event log is the real adapter.
    old = fake::sets;
    cyberdeck_apps::global_runtime().set_logger(&logger);
    for (const std::string &body :
         {std::string(R"({"utc_datetime":"2024-02-29T23:59:59+00:00"})"),
          std::string(R"({"timezone":7,"utc_datetime":"2024-02-29T23:59:59+00:00"})"),
          std::string(R"({"timezone":"America/Sao_Paulo","utc_datetime":"2024-02-29T23:59:59+00:00"})")}) {
        fake::body = body;
        fake::behavior = fake::good;
        ip(150 + fake::gets);
        wait_pending();
        finish();
        wait_idle();
        assert(fake::sets == old);
    }
    fake::body = R"({"timezone":"Etc/UTC","utc_datetime":"2024-02-29T23:59:59+00:00","padding":")" +
                 std::string(2050, 'x') + "\"}";
    fake::split_body = true;
    ip(180);
    wait_pending();
    finish();
    wait_idle();
    assert(fake::sets == old);
    std::string last_url;
    int last_method;
    bool tls_bundle;
    {
        std::lock_guard<std::mutex> l(fake::m);
        last_url = fake::last_url;
        last_method = fake::last_method;
        tls_bundle = fake::tls_bundle;
    }
    assert(last_url == "https://worldtimeapi.org/api/timezone/Etc/UTC" && last_method == HTTP_METHOD_GET && tls_bundle);
    fake::behavior = fake::good;
    const int before = fake::gets;
    std::vector<std::thread> events;
    for (int i = 0; i < 8; ++i)
        events.emplace_back([] { ip(200); });
    for (auto &event : events)
        event.join();
    wait_pending();
    finish();
    wait_sets(1);
    assert(fake::gets == before + 1);
    assert(fake::max_active_requests == 1); // TEST-TIME-06
    assert(fake::callbacks_added == 1 && fake::task_creations == 1);
    assert(cyberdeck_time_sync_stop(1000) == ESP_OK);
    assert(cyberdeck_time_sync_stop(1000) == ESP_OK);
    assert(fake::task_deletions == 1); // TEST-TIME-07: no double teardown.
    assert(fake::cleanups == fake::gets);
    /* TEST-TIME-07: a new lifecycle gets one new worker and one new token. */
    assert(cyberdeck_time_sync_start() == ESP_OK);
    cyberdeck_time_sync_boot_ready();
    ip(201);
    wait_pending();
    finish();
    wait_sets(1);
    assert(fake::task_creations == 2 && fake::callbacks_added == 2);
    assert(cyberdeck_time_sync_stop(1000) == ESP_OK);
    assert(fake::task_deletions == 2); // TEST-TIME-07: one delete per lifecycle.
    /* TEST-TIME-08: stop while HTTP is pending, then release a late completion. */
    reset();
    assert(cyberdeck_time_sync_start() == ESP_OK);
    cyberdeck_time_sync_boot_ready();
    ip(9);
    wait_pending();
    std::thread stopper([] { assert(cyberdeck_time_sync_stop(1000) == ESP_OK); });
    finish();
    stopper.join();
    int writes = fake::sets;
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    assert(fake::sets == writes);
    assert(fake::task_deletions == 1); // TEST-TIME-08: late completion cannot teardown twice.
    assert(cyberdeck_time_sync_stop(1000) == ESP_OK);
    assert(fake::task_deletions == 1);

    /* TEST-TIMELOG-04: bounded durable backpressure is retried by the worker,
       not by the Wi-Fi callback; the critical success event is not lost. */
    reset();
     assert(cyberdeck_time_sync_start() == ESP_OK);
     cyberdeck_time_sync_boot_ready();
     fake::durable_failure_result = ESP_ERR_NO_MEM;
     fake::durable_failures_remaining = 2;
    fake::suppress_retries = false;
    ip(500);
    wait_pending();
    finish();
    for (int i = 0; i < 100 && logger.events == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
     assert(fake::durable_calls == 3 && fake::durable_failures == 2 && fake::durable_successes == 1);
     assert(logger.events == 1 && std::strcmp(logger.last.tag, "time_sync") == 0);
     /* TEST-TIMELOG-07: once a token completed durably, a repeated Wi-Fi
        notification for that same token cannot emit a second event. */
     const int durable_calls_after_completion = fake::durable_calls;
     const int events_after_completion = logger.events;
     ip(500);
     std::this_thread::sleep_for(std::chrono::milliseconds(5));
     assert(fake::durable_calls == durable_calls_after_completion);
     assert(logger.events == events_after_completion);
     assert(cyberdeck_time_sync_stop(1000) == ESP_OK);

     /* TEST-TIMELOG-08: an ACK timeout means the durable request may already
        be queued, so it is not retried and cannot duplicate the event. */
     reset();
     assert(cyberdeck_time_sync_start() == ESP_OK);
     cyberdeck_time_sync_boot_ready();
     fake::durable_failure_result = ESP_ERR_TIMEOUT;
     fake::durable_failures_remaining = 1;
     fake::suppress_retries = false;
     ip(550);
     wait_pending();
     finish();
     wait_idle();
     std::this_thread::sleep_for(std::chrono::milliseconds(5));
     assert(fake::durable_calls == 1 && fake::durable_failures == 1 && fake::durable_successes == 0);
     assert(fake::sets == 1 && logger.events == 0);
     ip(550);
     std::this_thread::sleep_for(std::chrono::milliseconds(5));
     assert(fake::durable_calls == 1 && logger.events == 0);
     assert(cyberdeck_time_sync_stop(1000) == ESP_OK);

     /* TEST-TIMELOG-06: one token is bounded to three worker attempts, even
       while duplicate/concurrent notifications are presented to the callback;
       restart starts a fresh token budget rather than reusing the old one. */
    reset();
    assert(cyberdeck_time_sync_start() == ESP_OK);
    cyberdeck_time_sync_boot_ready();
     fake::durable_failures_remaining = 10;
     fake::durable_failure_result = ESP_ERR_NO_MEM;
    fake::suppress_retries = false;
    std::vector<std::thread> retry_events;
    for (int i = 0; i < 8; ++i) retry_events.emplace_back([] { ip(600); });
    for (auto &event : retry_events) event.join();
    wait_pending();
    finish();
    for (int i = 0; i < 1000 && fake::durable_calls < 3; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(fake::durable_calls == 3 && fake::durable_successes == 0);
    assert(logger.events == 0);
    assert(cyberdeck_time_sync_stop(1000) == ESP_OK);
    assert(cyberdeck_time_sync_start() == ESP_OK);
    cyberdeck_time_sync_boot_ready();
    fake::durable_failures_remaining = 0;
    fake::suppress_retries = false;
    const int durable_calls_before_restart = fake::durable_calls;
    ip(601);
    wait_pending();
    finish();
    for (int i = 0; i < 100 && fake::durable_successes < 1; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(fake::durable_calls == durable_calls_before_restart + 1 && fake::durable_successes == 1);
    assert(cyberdeck_time_sync_stop(1000) == ESP_OK);
    return 0;
}
