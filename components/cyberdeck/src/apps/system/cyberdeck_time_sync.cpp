#include "apps/system/cyberdeck_time_sync.h"

#include "apps/runtime/cyberdeck_app_runtime.h"
#include "apps/wifi/wifi_mgr.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/time.h>

namespace {
constexpr char kTimeApiUrl[] = "https://timeapi.io/api/time/current/zone?timeZone=UTC";
constexpr char kWorldTimeApiUrl[] = "https://worldtimeapi.org/api/timezone/Etc/UTC";
constexpr size_t kBodyLimit = 2048;
constexpr uint32_t kHttpTimeoutMs = 4000;
constexpr uint32_t kLifecycleTimeoutMs = 9000;
constexpr uint32_t kTaskStackBytes = 6144;
constexpr uint64_t kNoConnectionToken = UINT64_MAX;

struct response_buffer {
    char data[kBodyLimit + 1]{};
    size_t length = 0;
    bool overflow = false;
};

struct context {
    TaskHandle_t task = nullptr;
    SemaphoreHandle_t quiesced = nullptr;
    SemaphoreHandle_t wake = nullptr;
    std::atomic<bool> stop{false};
    std::atomic<bool> ready{false};
    std::atomic<bool> boot_completed{false};
    std::atomic<bool> requested{false};
    std::atomic<uint64_t> requested_token{kNoConnectionToken};
    std::atomic<uint64_t> completed_token{kNoConnectionToken};
    std::atomic<bool> started{false};
};

context &state() {
    static context value;
    return value;
}

bool append_body(response_buffer &body, const char *data, size_t length) {
    if (body.overflow || data == nullptr || body.length > kBodyLimit || length > kBodyLimit - body.length) {
        body.overflow = true;
        return false;
    }
    std::memcpy(body.data + body.length, data, length);
    body.length += length;
    body.data[body.length] = '\0';
    return true;
}

bool leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

time_t utc_epoch(const struct tm &value) {
    const int year = value.tm_year + 1900;
    if (year < 1970 || value.tm_mon < 0 || value.tm_mon > 11 || value.tm_mday < 1 || value.tm_hour < 0 ||
        value.tm_hour > 23 || value.tm_min < 0 || value.tm_min > 59 || value.tm_sec < 0 || value.tm_sec > 60)
        return static_cast<time_t>(-1);
    static constexpr int month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const int max_day = month_days[value.tm_mon] + (value.tm_mon == 1 && leap_year(year) ? 1 : 0);
    if (value.tm_mday > max_day)
        return static_cast<time_t>(-1);
    int64_t days = 0;
    for (int current = 1970; current < year; ++current)
        days += leap_year(current) ? 366 : 365;
    for (int month = 0; month < value.tm_mon; ++month)
        days += month_days[month] + (month == 1 && leap_year(year) ? 1 : 0);
    days += value.tm_mday - 1;
    return static_cast<time_t>(days * 86400 + value.tm_hour * 3600 + value.tm_min * 60 + value.tm_sec);
}

esp_err_t http_event(esp_http_client_event_t *event) {
    if (event == nullptr || event->user_data == nullptr)
        return ESP_OK;
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data != nullptr && event->data_len > 0) {
        append_body(*static_cast<response_buffer *>(event->user_data), static_cast<const char *>(event->data),
                    static_cast<size_t>(event->data_len));
    }
    return ESP_OK;
}

bool parse_utc_fields(int year, int month, int day, int hour, int minute, int second, time_t &out) {
    if (year < 1970 || year > 2100)
        return false;
    struct tm value {};
    value.tm_year = year - 1900;
    value.tm_mon = month - 1;
    value.tm_mday = day;
    value.tm_hour = hour;
    value.tm_min = minute;
    value.tm_sec = second;
    value.tm_isdst = 0;
    const time_t candidate = utc_epoch(value);
    struct tm round_trip {};
    if (candidate < 1577836800 || gmtime_r(&candidate, &round_trip) == nullptr || round_trip.tm_year != value.tm_year ||
        round_trip.tm_mon != value.tm_mon || round_trip.tm_mday != value.tm_mday ||
        round_trip.tm_hour != value.tm_hour || round_trip.tm_min != value.tm_min || round_trip.tm_sec != value.tm_sec)
        return false;
    out = candidate;
    return true;
}

bool integer_field(const cJSON *object, const char *name, int &out) {
    const cJSON *field = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(field) || !std::isfinite(field->valuedouble) ||
        std::floor(field->valuedouble) != field->valuedouble)
        return false;
    out = field->valueint;
    return static_cast<double>(out) == field->valuedouble;
}

bool parse_timeapi_response(const char *payload, time_t &out) {
    cJSON *root = cJSON_ParseWithLength(payload, std::strlen(payload));
    if (root == nullptr || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    const cJSON *zone = cJSON_GetObjectItemCaseSensitive(root, "timeZone");
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    const bool valid = cJSON_IsString(zone) && std::strcmp(zone->valuestring, "UTC") == 0 &&
                       integer_field(root, "year", year) && integer_field(root, "month", month) &&
                       integer_field(root, "day", day) && integer_field(root, "hour", hour) &&
                       integer_field(root, "minute", minute) && integer_field(root, "seconds", second) &&
                       parse_utc_fields(year, month, day, hour, minute, second, out);
    cJSON_Delete(root);
    return valid;
}

bool parse_worldtime_response(const char *payload, time_t &out) {
    cJSON *root = cJSON_ParseWithLength(payload, std::strlen(payload));
    if (root == nullptr || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    const cJSON *zone = cJSON_GetObjectItemCaseSensitive(root, "timezone");
    const cJSON *stamp = cJSON_GetObjectItemCaseSensitive(root, "utc_datetime");
    bool valid = cJSON_IsString(zone) && std::strcmp(zone->valuestring, "Etc/UTC") == 0 && cJSON_IsString(stamp);
    struct tm value {};
    if (valid) {
        const char *text = stamp->valuestring;
        const size_t length = std::strlen(text);
        valid = length >= 25 && length <= 34 && text[4] == '-' && text[7] == '-' && text[10] == 'T' &&
                text[13] == ':' && text[16] == ':' &&
                ((text[19] == '+' && length == 25) || (text[19] == '.' && length >= 26)) && text[length - 6] == '+' &&
                text[length - 5] == '0' && text[length - 4] == '0' && text[length - 3] == ':' &&
                text[length - 2] == '0' && text[length - 1] == '0';
        if (valid) {
            for (size_t index = 0; index < 19; ++index)
                if (index != 4 && index != 7 && index != 10 && index != 13 && index != 16 &&
                    (text[index] < '0' || text[index] > '9'))
                    valid = false;
            for (size_t index = 20; index < length - 6; ++index)
                if (text[index] < '0' || text[index] > '9')
                    valid = false;
        }
        if (valid) {
            const int year = (text[0] - '0') * 1000 + (text[1] - '0') * 100 + (text[2] - '0') * 10 + text[3] - '0';
            const int month = (text[5] - '0') * 10 + text[6] - '0';
            const int day = (text[8] - '0') * 10 + text[9] - '0';
            const int hour = (text[11] - '0') * 10 + text[12] - '0';
            const int minute = (text[14] - '0') * 10 + text[15] - '0';
            const int second = (text[17] - '0') * 10 + text[18] - '0';
            valid = year >= 1970 && year <= 2100;
            value.tm_year = year - 1900;
            value.tm_mon = month - 1;
            value.tm_mday = day;
            value.tm_hour = hour;
            value.tm_min = minute;
            value.tm_sec = second;
            value.tm_isdst = 0;
        }
    }
    if (valid) {
        const time_t candidate = utc_epoch(value);
        struct tm round_trip {};
        valid = candidate >= 1577836800 && gmtime_r(&candidate, &round_trip) != nullptr &&
                round_trip.tm_year == value.tm_year && round_trip.tm_mon == value.tm_mon &&
                round_trip.tm_mday == value.tm_mday && round_trip.tm_hour == value.tm_hour &&
                round_trip.tm_min == value.tm_min && round_trip.tm_sec == value.tm_sec;
        if (valid)
            out = candidate;
    }
    cJSON_Delete(root);
    return valid;
}

bool fetch_bounded(const char *url, response_buffer &body) {
    esp_http_client_config_t config{};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = kHttpTimeoutMs;
    config.buffer_size = 1024;
    config.buffer_size_tx = 512;
    config.event_handler = http_event;
    config.user_data = &body;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr)
        return false;
    const esp_err_t result = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (result != ESP_OK || status < 200 || status >= 300 || body.overflow || body.length == 0)
        return false;
    return true;
}

bool fetch_and_parse(const char *url, bool (*parser)(const char *, time_t &), time_t &out) {
    response_buffer body;
    if (!fetch_bounded(url, body))
        return false;
    return parser(body.data, out);
}

bool set_clock(time_t timestamp) {
    struct timeval tv {
        timestamp, 0
    };
    if (settimeofday(&tv, nullptr) != 0)
        return false;
    if (cyberdeck_apps::logger *logger = cyberdeck_apps::global_runtime().app_logger(); logger != nullptr)
        (void)logger->write(cyberdeck_apps::logger::level::info, "time_sync", "success");
    return true;
}

bool fetch_and_set_clock(const context &ctx) {
    time_t timestamp = 0;
    if (fetch_and_parse(kTimeApiUrl, parse_timeapi_response, timestamp)) {
        if (ctx.stop.load(std::memory_order_acquire))
            return false;
        return set_clock(timestamp);
    }
    // The fallback is a second bounded network operation.  Do not start it
    // after teardown has been requested; this keeps stop quiescent without
    // adding an unbounded wait to the boot path.
    if (ctx.stop.load(std::memory_order_acquire))
        return false;
    if (!fetch_and_parse(kWorldTimeApiUrl, parse_worldtime_response, timestamp))
        return false;
    if (ctx.stop.load(std::memory_order_acquire))
        return false;
    return set_clock(timestamp);
}

void request_for_status(const wifi_status_t &status) {
    context &ctx = state();
    if (!ctx.ready.load(std::memory_order_acquire) || !status.connected || !status.has_ip)
        return;
    const uint64_t token = status.connection_token;
    uint64_t expected = ctx.completed_token.load(std::memory_order_acquire);
    if (expected == token || ctx.requested_token.load(std::memory_order_acquire) == token)
        return;
    ctx.requested_token.store(token, std::memory_order_release);
    ctx.requested.store(true, std::memory_order_release);
    if (ctx.wake != nullptr)
        (void)xSemaphoreGive(ctx.wake);
}

void wifi_callback(const wifi_status_t *status, bool, void *) {
    if (status != nullptr)
        request_for_status(*status);
}

void sync_task(void *) {
    context &ctx = state();
    for (;;) {
        (void)xSemaphoreTake(ctx.wake, pdMS_TO_TICKS(250));
        if (ctx.stop.load(std::memory_order_acquire))
            break;
        if (!ctx.requested.exchange(false, std::memory_order_acq_rel))
            continue;
        const uint64_t token = ctx.requested_token.load(std::memory_order_acquire);
        if (fetch_and_set_clock(ctx))
            ctx.completed_token.store(token, std::memory_order_release);
    }
    (void)xSemaphoreGive(ctx.quiesced);
    vTaskDeleteWithCaps(nullptr);
}
} // namespace

extern "C" esp_err_t cyberdeck_time_sync_start(void) {
    context &ctx = state();
    bool expected = false;
    if (!ctx.started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return ESP_OK;
    ctx.wake = xSemaphoreCreateBinary();
    ctx.quiesced = xSemaphoreCreateBinary();
    if (ctx.wake == nullptr || ctx.quiesced == nullptr) {
        if (ctx.wake != nullptr)
            vSemaphoreDelete(ctx.wake);
        if (ctx.quiesced != nullptr)
            vSemaphoreDelete(ctx.quiesced);
        ctx.wake = nullptr;
        ctx.quiesced = nullptr;
        ctx.started.store(false, std::memory_order_release);
        return ESP_ERR_NO_MEM;
    }
    ctx.stop.store(false, std::memory_order_release);
    ctx.ready.store(false, std::memory_order_release);
    ctx.requested.store(false, std::memory_order_release);
    ctx.requested_token.store(kNoConnectionToken, std::memory_order_release);
    ctx.completed_token.store(kNoConnectionToken, std::memory_order_release);
    BaseType_t result =
        xTaskCreateWithCaps(sync_task, "time_sync", kTaskStackBytes, nullptr, 4, &ctx.task, MALLOC_CAP_SPIRAM);
    if (result != pdPASS) {
        vSemaphoreDelete(ctx.wake);
        vSemaphoreDelete(ctx.quiesced);
        ctx.wake = nullptr;
        ctx.quiesced = nullptr;
        ctx.started.store(false, std::memory_order_release);
        return ESP_ERR_NO_MEM;
    }
    if (wifi_mgr_add_state_callback(wifi_callback, nullptr) != ESP_OK) {
        (void)cyberdeck_time_sync_stop(kLifecycleTimeoutMs);
        return ESP_ERR_NO_MEM;
    }
    ctx.ready.store(ctx.boot_completed.load(std::memory_order_acquire), std::memory_order_release);
    if (ctx.ready.load(std::memory_order_acquire)) {
        wifi_status_t status{};
        if (wifi_mgr_get_status(&status) == ESP_OK)
            request_for_status(status);
    }
    return ESP_OK;
}

extern "C" esp_err_t cyberdeck_time_sync_stop(uint32_t timeout_ms) {
    context &ctx = state();
    if (!ctx.started.load(std::memory_order_acquire))
        return ESP_OK;
    (void)wifi_mgr_remove_state_callback(wifi_callback, nullptr);
    ctx.ready.store(false, std::memory_order_release);
    ctx.stop.store(true, std::memory_order_release);
    if (ctx.wake != nullptr)
        (void)xSemaphoreGive(ctx.wake);
    if (ctx.quiesced == nullptr || xSemaphoreTake(ctx.quiesced, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    ctx.task = nullptr;
    vSemaphoreDelete(ctx.wake);
    vSemaphoreDelete(ctx.quiesced);
    ctx.wake = nullptr;
    ctx.quiesced = nullptr;
    ctx.requested.store(false, std::memory_order_release);
    ctx.requested_token.store(kNoConnectionToken, std::memory_order_release);
    ctx.completed_token.store(kNoConnectionToken, std::memory_order_release);
    ctx.started.store(false, std::memory_order_release);
    return ESP_OK;
}

extern "C" void cyberdeck_time_sync_boot_ready(void) {
    context &ctx = state();
    ctx.boot_completed.store(true, std::memory_order_release);
    if (!ctx.started.load(std::memory_order_acquire))
        return;
    ctx.ready.store(true, std::memory_order_release);
    wifi_status_t status{};
    if (wifi_mgr_get_status(&status) == ESP_OK)
        request_for_status(status);
}
