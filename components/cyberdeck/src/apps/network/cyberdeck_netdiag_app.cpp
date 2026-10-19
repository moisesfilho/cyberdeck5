#include "apps/network/cyberdeck_netdiag_app.h"

#include "apps/network/cyberdeck_netdiag_dns_adapter.h"
#include "apps/network/cyberdeck_netdiag_https_adapter.h"
#include "apps/network/cyberdeck_netdiag_tcp_adapter.h"
#include "apps/wifi/wifi_mgr.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace cyberdeck_netdiag {
namespace {

constexpr uint32_t k_task_stack_bytes = 12288;
constexpr uint32_t k_stop_timeout_ms = 6000;

constexpr cyberdeck_apps::manifest k_manifest{
    "cyberdeck.netdiag",
    "Network diagnostics",
    "1.0.0",
    "Explicit-target connectivity diagnostics",
    "",
    {"cyberdeck.event_log", "cyberdeck.wifi"},
    2,
    {"network"},
    1,
    6000,
    "1",
    cyberdeck_apps::app_type::foreground,
    {"network"},
    1,
    k_task_stack_bytes,
    1,
    {"netdiag"},
    1,
    false,
    {{{"netdiag [status|dns <host>|tcp <host> <port>|https <https-url>|result|cancel]",
       "status: show Wi-Fi interface status\ndns <host>: resolve an explicit host\ntcp <host> <port>: test a TCP "
       "connection\nhttps <https-url>: issue an HTTPS HEAD request\nresult: report the current operation; cancel: "
       "request cancellation\nhost: 1..253 ASCII letters, digits, dots, or hyphens; port: decimal 1..65535\nURL: "
       "https, at most 512 bytes; no userinfo, fragment, whitespace, or explicit port in authority\n"}}}};

uint64_t monotonic_ms(void *) {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

bool dns(void *, const char *host, uint64_t deadline, char output[16]) {
    return resolve_ipv4(nullptr, host, deadline, output);
}

bool tcp(void *, const char *address, uint16_t port, uint64_t deadline) {
    return tcp_connect(nullptr, address, port, deadline);
}

bool https(void *, const char *url, const char *host, uint64_t deadline, uint16_t *status) {
    return https_head(nullptr, url, host, deadline, status);
}

bool create_freertos_worker(void *, void (*entry)(void *), void *argument, uint32_t stack_bytes, void **handle) {
    if (entry == nullptr || handle == nullptr)
        return false;
    TaskHandle_t task = nullptr;
    if (xTaskCreateWithCaps(entry, "netdiag", stack_bytes, argument, 5, &task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) !=
        pdPASS)
        return false;
    *handle = task;
    return true;
}

bool valid_host(std::string_view host) {
    if (host.empty() || host.size() > k_max_host_length)
        return false;
    for (const unsigned char c : host)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-'))
            return false;
    return true;
}

bool parse_port(std::string_view text, uint16_t &port) {
    if (text.empty() || text.size() > 5)
        return false;
    unsigned value = 0;
    for (char c : text) {
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + static_cast<unsigned>(c - '0');
    }
    if (value == 0 || value > 65535)
        return false;
    port = static_cast<uint16_t>(value);
    return true;
}

std::string status_report(const cyberdeck_apps::grant &grant) {
    if (!grant.allows(cyberdeck_apps::resource::network))
        return "netdiag: network grant unavailable\n";
    wifi_status_t wifi{};
    if (wifi_mgr_get_status(&wifi) != ESP_OK)
        return "netdiag: interface=unavailable ip=<missing> gateway=<missing> dns=<missing> rssi=<missing>\n";
    std::string out = "netdiag: interface=WIFI_STA_DEF ip=";
    out += wifi.has_ip ? wifi.ip : "<missing>";
    out += " gateway=";
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info{};
    if (netif != nullptr && wifi.has_ip && esp_netif_get_ip_info(netif, &info) == ESP_OK && info.gw.addr != 0) {
        char address[16]{};
        (void)esp_ip4addr_ntoa(&info.gw, address, sizeof(address));
        out += address;
    } else
        out += "<missing>";
    out += " dns=";
    esp_netif_dns_info_t dns_info{};
    if (netif != nullptr && esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK &&
        dns_info.ip.u_addr.ip4.addr != 0) {
        char address[16]{};
        (void)esp_ip4addr_ntoa(&dns_info.ip.u_addr.ip4, address, sizeof(address));
        out += address;
    } else
        out += "<missing>";
    wifi_ap_record_t ap{};
    out += " rssi=";
    if (wifi.connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
        out += std::to_string(ap.rssi) + "dBm";
    else
        out += "<missing>";
    out += '\n';
    return out;
}

const char *stage_name(stage value) {
    switch (value) {
    case stage::idle:
        return "idle";
    case stage::queued:
        return "queued";
    case stage::dns:
        return "dns";
    case stage::tcp:
        return "tcp";
    case stage::https:
        return "https";
    case stage::complete:
        return "complete";
    case stage::failed:
        return "failed";
    case stage::cancelled:
        return "cancelled";
    }
    return "failed";
}

const char *error_name(error value) {
    switch (value) {
    case error::none:
        return "none";
    case error::invalid_request:
        return "invalid_request";
    case error::busy:
        return "busy";
    case error::rate_limited:
        return "rate_limited";
    case error::grant_revoked:
        return "grant_revoked";
    case error::dns_failed:
        return "dns_failed";
    case error::tcp_failed:
        return "tcp_failed";
    case error::https_failed:
        return "https_failed";
    case error::timeout:
        return "timeout";
    case error::cancelled:
        return "cancelled";
    case error::stale_token:
        return "stale_token";
    case error::invalid_ports:
        return "invalid_ports";
    }
    return "failed";
}

const char *operation_name(operation value) {
    switch (value) {
    case operation::dns:
        return "dns";
    case operation::tcp:
        return "tcp";
    case operation::https:
        return "https";
    }
    return "unknown";
}

} // namespace

application::application()
    : application({nullptr, monotonic_ms, dns, tcp, https, create_freertos_worker, production_wait}) {}

application::application(const application_platform &platform) : platform_(platform) {
    if (platform_.wait_worker == production_wait)
        platform_.context = this;
}

const cyberdeck_apps::manifest &application::get_manifest() const {
    return k_manifest;
}

bool application::start() {
    if (stop_pending_) {
        if (!reap_worker(0))
            return false;
        finish_stop();
    } else if (worker_handle_ != nullptr) {
        if (!reap_worker(0))
            return false;
    }
    if (initialized_)
        return !stop_pending_;
    if (platform_.now_ms == nullptr || platform_.resolve_ipv4 == nullptr || platform_.tcp_connect == nullptr ||
        platform_.https_head == nullptr || platform_.create_worker == nullptr || platform_.wait_worker == nullptr)
        return false;
    if (synchronization_ == nullptr) {
        synchronization_ = xSemaphoreCreateBinary();
        if (synchronization_ == nullptr)
            return false;
    }
    initialized_ = true;
    worker_handle_ = nullptr;
    worker_token_ = 0;
    stop_pending_ = false;
    return true;
}

bool application::stop() {
    if (!initialized_)
        return true;
    stop_pending_ = true;
    if (worker_handle_ != nullptr) {
        (void)controller_.cancel(worker_token_, platform_.now_ms(platform_.context));
        if (!reap_worker(k_stop_timeout_ms))
            return false;
    }
    finish_stop();
    return true;
}

bool application::reap_worker(uint32_t timeout_ms) {
    if (worker_handle_ == nullptr)
        return true;
    if (platform_.wait_worker == nullptr || !platform_.wait_worker(platform_.context, worker_handle_, timeout_ms))
        return false;
    worker_handle_ = nullptr;
    request_grant_ = {};
    return true;
}

void application::finish_stop() {
    request_grant_ = {};
    initialized_ = false;
    stop_pending_ = false;
}

bool application::running() const {
    return initialized_;
}

uint64_t application::platform_now(void *context) {
    auto *self = static_cast<application *>(context);
    return self->platform_.now_ms(self->platform_.context);
}

bool application::platform_grant_valid(void *context) {
    auto *self = static_cast<application *>(context);
    return self->request_grant_.allows(cyberdeck_apps::resource::network);
}

bool application::platform_resolve(void *context, const char *host, uint64_t deadline, char output[16]) {
    auto *self = static_cast<application *>(context);
    return self->platform_.resolve_ipv4(self->platform_.context, host, deadline, output);
}

bool application::platform_tcp(void *context, const char *address, uint16_t port, uint64_t deadline) {
    auto *self = static_cast<application *>(context);
    return self->platform_.tcp_connect(self->platform_.context, address, port, deadline);
}

bool application::platform_https(void *context, const char *url, const char *host, uint64_t deadline,
                                 uint16_t *status) {
    auto *self = static_cast<application *>(context);
    return self->platform_.https_head(self->platform_.context, url, host, deadline, status);
}

void application::worker_entry(void *context) {
    auto *self = static_cast<application *>(context);
    const bool self_delete = self->platform_.wait_worker == production_wait;
    const auto synchronization = static_cast<SemaphoreHandle_t>(self->synchronization_);
    const ports io{self, platform_now, platform_grant_valid, platform_resolve, platform_tcp, platform_https};
    self->controller_.run(self->worker_token_, io);
    xSemaphoreGive(synchronization);
    if (self_delete)
        vTaskDelete(nullptr);
}

bool application::production_wait(void *context, void *, uint32_t timeout_ms) {
    auto *self = static_cast<application *>(context);
    return xSemaphoreTake(static_cast<SemaphoreHandle_t>(self->synchronization_), pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

cyberdeck_apps::result application::execute(std::string_view command, std::string_view args) {
    if (command != "netdiag" || !initialized_)
        return {cyberdeck_apps::result_status::rejected, "netdiag: unavailable\n"};
    if (stop_pending_ && worker_handle_ == nullptr)
        return {cyberdeck_apps::result_status::rejected, "netdiag: unavailable\n"};

    const bool is_result = args == "result" || args == " result";
    const bool is_cancel = args == "cancel" || args == " cancel";
    if (is_result || is_cancel) {
        const uint64_t token = worker_token_;
        if (is_cancel) {
            const error cancelled = token == 0 ? error::stale_token : controller_.cancel(token, platform_now(this));
            return {cancelled == error::none ? cyberdeck_apps::result_status::handled
                                             : cyberdeck_apps::result_status::rejected,
                    cancelled == error::none ? "netdiag: cancellation requested\n" : "netdiag: no active operation\n"};
        }

        snapshot current{};
        if (token == 0 || controller_.snapshot_for(token, &current) != error::none)
            return {cyberdeck_apps::result_status::handled, "netdiag: state=idle\n"};
        std::string output = "netdiag: state=";
        output += stage_name(current.current_stage);
        output += " operation=";
        output += operation_name(current_operation_);
        output += " duration_ms=" + std::to_string(current.duration_ms);
        if (current.http_status != 0)
            output += " http_status=" + std::to_string(current.http_status);
        if (current.failure != error::none)
            output += " error=" + std::string(error_name(current.failure));
        if (current.resolved_ipv4[0] != '\0')
            output += " ipv4=" + std::string(current.resolved_ipv4);
        output += '\n';
        return {cyberdeck_apps::result_status::handled, std::move(output)};
    }

    if (worker_handle_ != nullptr) {
        if (!reap_worker(0))
            return {cyberdeck_apps::result_status::rejected, "netdiag: worker still active\n"};
        if (stop_pending_)
            return {cyberdeck_apps::result_status::rejected, "netdiag: unavailable\n"};
    }
    if (args == "status" || args == " status")
        return {cyberdeck_apps::result_status::handled, status_report(app_grant())};

    uint64_t token = worker_token_;
    request submitted{};
    std::string_view target;
    bool parsed = false;
    if (args.rfind("dns ", 0) == 0 || args.rfind(" dns ", 0) == 0) {
        target = args.substr(args[0] == ' ' ? 5 : 4);
        submitted.requested_operation = operation::dns;
        parsed = valid_host(target);
        if (parsed)
            std::memcpy(submitted.hostname, target.data(), target.size());
    } else if (args.rfind("tcp ", 0) == 0 || args.rfind(" tcp ", 0) == 0) {
        const size_t offset = args[0] == ' ' ? 5 : 4;
        const std::string_view parameters = args.substr(offset);
        const size_t split = parameters.rfind(' ');
        uint16_t port = 0;
        if (split != parameters.npos && valid_host(parameters.substr(0, split)) &&
            parse_port(parameters.substr(split + 1), port)) {
            target = parameters.substr(0, split);
            submitted.requested_operation = operation::tcp;
            submitted.tcp_port = port;
            std::memcpy(submitted.hostname, target.data(), target.size());
            parsed = true;
        }
    } else if (args.rfind("https ", 0) == 0 || args.rfind(" https ", 0) == 0) {
        target = args.substr(args[0] == ' ' ? 7 : 6);
        const size_t authority_start = 8;
        if (target.rfind("https://", 0) != 0 || target.size() > k_max_url_length || target.find('@') != target.npos ||
            target.find('#') != target.npos || target.find_first_of(" \r\n\t") != target.npos) {
            parsed = false;
        } else {
            const size_t authority_end = target.find_first_of("/?", authority_start);
            const std::string_view host = target.substr(
                authority_start, (authority_end == target.npos ? target.size() : authority_end) - authority_start);
            if (valid_host(host)) {
                submitted.requested_operation = operation::https;
                std::memcpy(submitted.hostname, host.data(), host.size());
                std::memcpy(submitted.https_url, target.data(), target.size());
                parsed = true;
            }
        }
    }
    if (!parsed)
        return {cyberdeck_apps::result_status::rejected,
                "usage: netdiag [status|dns <host>|tcp <host> <port>|https <https-url>|result|cancel]\n"};

    if (worker_handle_ != nullptr)
        return {cyberdeck_apps::result_status::rejected, "netdiag: worker still active\n"};
    request_grant_ = app_grant();
    const error accepted = controller_.submit(submitted, request_grant_.allows(cyberdeck_apps::resource::network),
                                              platform_now(this), &token);
    if (accepted != error::none) {
        request_grant_ = {};
        return {cyberdeck_apps::result_status::rejected, "netdiag: request rejected\n"};
    }
    current_operation_ = submitted.requested_operation;
    worker_token_ = token;
    (void)xSemaphoreTake(static_cast<SemaphoreHandle_t>(synchronization_), 0);
    if (platform_.create_worker == nullptr ||
        !platform_.create_worker(platform_.context, worker_entry, this, k_task_stack_bytes, &worker_handle_)) {
        (void)controller_.cancel(token, platform_now(this));
        worker_handle_ = nullptr;
        request_grant_ = {};
        return {cyberdeck_apps::result_status::rejected, "netdiag: worker unavailable\n"};
    }
    return {cyberdeck_apps::result_status::handled, "netdiag: operation started; use `netdiag result`\n"};
}

application &global_application() {
    static application instance;
    return instance;
}

} // namespace cyberdeck_netdiag
