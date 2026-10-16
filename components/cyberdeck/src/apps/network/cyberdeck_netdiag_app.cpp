#include "apps/network/cyberdeck_netdiag_app.h"

#include "apps/network/cyberdeck_netdiag_core.h"
#include "apps/network/cyberdeck_netdiag_dns.h"
#include "apps/network/cyberdeck_netdiag_https.h"
#include "apps/network/cyberdeck_netdiag_tcp.h"
#include "apps/wifi/wifi_mgr.h"

#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace cyberdeck_netdiag {
namespace {

constexpr std::uint32_t k_worker_stack_bytes = 8192;
constexpr std::uint32_t k_stop_timeout_ms = 25000;
constexpr TickType_t k_poll_interval = pdMS_TO_TICKS(25);
constexpr cyberdeck_apps::manifest k_manifest{"cyberdeck.netdiag",
                                              "Network diagnostics",
                                              "1.0.0",
                                              "Explicit bounded network diagnostics",
                                              "netdiag",
                                              {"cyberdeck.event_log", "cyberdeck.wifi"},
                                              2,
                                              {"network"},
                                              1,
                                              k_stop_timeout_ms,
                                              "1",
                                              cyberdeck_apps::app_type::foreground,
                                              {"network"},
                                              1,
                                              k_worker_stack_bytes,
                                              1,
                                              {"netdiag"},
                                              1,
                                              false};

enum class job_kind : std::uint8_t { none, request, cancel };
struct job {
    job_kind kind = job_kind::none;
    parsed_command command{};
};
struct worker_state {
    TaskHandle_t task = nullptr;
    QueueHandle_t queue = nullptr;
    SemaphoreHandle_t done = nullptr;
    SemaphoreHandle_t mutex = nullptr;
    cyberdeck_apps::grant grant{};
    controller control{};
    outcome published{};
    std::uint64_t published_generation = 0;
    bool initialized = false;
    std::atomic<bool> stopping{false};
    std::atomic<bool> cancel_requested{false};
    std::atomic<bool> active{false};
};

worker_state &worker_context() {
    static worker_state value;
    return value;
}

std::uint64_t now_ms() {
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
}

void publish(worker_state &worker, outcome value, std::uint64_t generation) {
    if (!worker.grant.allows(cyberdeck_apps::resource::network))
        return;
    xSemaphoreTake(worker.mutex, portMAX_DELAY);
    if (worker.grant.allows(cyberdeck_apps::resource::network)) {
        worker.published = value;
        worker.published_generation = generation;
    }
    xSemaphoreGive(worker.mutex);
}

bool resolve_target(worker_state &worker, const request &work, std::uint64_t token, dns::result &address) {
    if (!worker.grant.allows(cyberdeck_apps::resource::network) ||
        dns::start(work.target.data(), token) != dns::start_result::accepted)
        return false;
    while (worker.grant.allows(cyberdeck_apps::resource::network) && !worker.stopping.load() &&
           !worker.cancel_requested.load()) {
        const dns::poll_state polled = dns::poll(token, address);
        if (polled == dns::poll_state::complete)
            return address.code == dns::result_code::resolved;
        if (polled == dns::poll_state::cancelled || polled == dns::poll_state::unknown_token)
            return false;
        vTaskDelay(k_poll_interval);
    }
    (void)dns::cancel(token);
    while (dns::poll(token, address) == dns::poll_state::pending)
        vTaskDelay(k_poll_interval);
    return false;
}

bool address_text(const dns::result &address, char *buffer, std::size_t capacity) {
    if (address.family == dns::address_family::ipv4)
        return inet_ntop(AF_INET, address.address.data(), buffer, capacity) != nullptr;
    if (address.family == dns::address_family::ipv6)
        return inet_ntop(AF_INET6, address.address.data(), buffer, capacity) != nullptr;
    return false;
}

bool submit_owned(void *context, const request &work, std::uint64_t) {
    const auto &worker = *static_cast<const worker_state *>(context);
    if (!worker.grant.allows(cyberdeck_apps::resource::network) || dns::busy())
        return false;
    if (work.kind == operation::tcp)
        return !tcp::busy();
    if (work.kind == operation::https)
        return !https::busy();
    return true;
}

outcome perform_tcp(worker_state &worker, const request &work, std::uint64_t token) {
    dns::result resolved{};
    if (!resolve_target(worker, work, token, resolved))
        return {worker.grant.allows(cyberdeck_apps::resource::network) ? state::failed : state::cancelled,
                error::dns_failed, 0, 0};
    char address[INET6_ADDRSTRLEN]{};
    if (!address_text(resolved, address, sizeof(address)))
        return {state::failed, error::dns_failed, 0, 0};
    if (!worker.grant.allows(cyberdeck_apps::resource::network) || worker.stopping.load() ||
        worker.cancel_requested.load())
        return {state::cancelled, error::grant_revoked, 0, 0};
    if (tcp::start(address, work.port, token) != tcp::start_result::accepted)
        return {state::failed, error::backend_unavailable, 0, 0};
    tcp::result result{};
    for (;;) {
        if (!worker.grant.allows(cyberdeck_apps::resource::network) || worker.stopping.load() ||
            worker.cancel_requested.load()) {
            (void)tcp::cancel(token);
            while (tcp::poll(token, result) == tcp::poll_state::pending)
                vTaskDelay(k_poll_interval);
            return {state::cancelled, error::grant_revoked, 0, 0};
        }
        const tcp::poll_state polled = tcp::poll(token, result);
        if (polled == tcp::poll_state::complete)
            return {state::complete, result.code == tcp::result_code::connected ? error::none : error::tcp_failed,
                    result.duration_ms, 0};
        if (polled == tcp::poll_state::unknown_token || polled == tcp::poll_state::cancelled)
            return {state::failed, error::backend_unavailable, 0, 0};
        vTaskDelay(k_poll_interval);
    }
}

outcome perform_https(worker_state &worker, const request &work, std::uint64_t token) {
    // Resolve only the authority; the HTTPS adapter retains the original URL for SNI and Host.
    const char *authority = work.target.data() + 8;
    const std::size_t length = std::strcspn(authority, "/?");
    char hostname[254]{};
    const char *colon = static_cast<const char *>(std::memchr(authority, ':', length));
    const std::size_t host_length = colon == nullptr ? length : static_cast<std::size_t>(colon - authority);
    if (host_length == 0 || host_length >= sizeof(hostname))
        return {state::failed, error::invalid_target, 0, 0};
    std::memcpy(hostname, authority, host_length);
    request dns_work{};
    dns_work.kind = operation::dns;
    std::memcpy(dns_work.target.data(), hostname, host_length + 1);
    dns::result resolved{};
    if (!resolve_target(worker, dns_work, token, resolved))
        return {state::failed, error::dns_failed, 0, 0};
    char address[INET6_ADDRSTRLEN]{};
    if (!address_text(resolved, address, sizeof(address)) || !worker.grant.allows(cyberdeck_apps::resource::network) ||
        worker.stopping.load() || worker.cancel_requested.load())
        return {state::cancelled, error::grant_revoked, 0, 0};
    if (https::start(work.target.data(), address, token) != https::start_result::accepted)
        return {state::failed, error::backend_unavailable, 0, 0};
    https::result result{};
    for (;;) {
        if (!worker.grant.allows(cyberdeck_apps::resource::network) || worker.stopping.load() ||
            worker.cancel_requested.load())
            (void)https::cancel(token);
        const https::poll_state polled = https::poll(token, result);
        if (polled == https::poll_state::complete)
            return {state::complete,
                    result.code == https::result_code::response    ? error::none
                    : result.code == https::result_code::tls_error ? error::tls_failed
                                                                   : error::http_failed,
                    result.duration_ms, result.http_status};
        if (polled == https::poll_state::cancelled || polled == https::poll_state::unknown_token)
            return {state::cancelled, error::grant_revoked, 0, 0};
        vTaskDelay(k_poll_interval);
    }
}

outcome execute_request(worker_state &worker, const parsed_command &command) {
    error failure = error::none;
    if (!worker.grant.allows(cyberdeck_apps::resource::network))
        return {state::failed, error::grant_revoked, 0, 0};
    // Admission is performed by the owner worker so all controller calls serialize here.
    if (!worker.control.admit(command, now_ms(), worker.grant.allows(cyberdeck_apps::resource::network),
                              {&worker, submit_owned, nullptr}, failure)) {
        return {state::failed, failure, 0, 0};
    }
    const std::uint64_t current = worker.control.generation();
    worker.control.mark_running(current);
    publish(worker, worker.control.snapshot(), current);
    if (command.kind == operation::dns) {
        dns::result result{};
        if (!resolve_target(worker, command.work, current, result))
            return {worker.grant.allows(cyberdeck_apps::resource::network) ? state::failed : state::cancelled,
                    error::dns_failed, 0, 0};
        return {state::complete, error::none, 0, 0};
    }
    if (command.kind == operation::tcp)
        return perform_tcp(worker, command.work, current);
    return perform_https(worker, command.work, current);
}

void worker_task(void *) {
    worker_state &worker = worker_context();
    while (!worker.stopping.load()) {
        job next{};
        if (xQueueReceive(worker.queue, &next, k_poll_interval) != pdTRUE)
            continue;
        if (next.kind == job_kind::cancel) {
            worker.cancel_requested.store(true);
            continue;
        }
        if (next.kind != job_kind::request)
            continue;
        if (!worker.grant.allows(cyberdeck_apps::resource::network))
            continue;
        worker.active.store(true);
        outcome result = execute_request(worker, next.command);
        const std::uint64_t token = worker.control.generation();
        if (worker.cancel_requested.load() && worker.grant.allows(cyberdeck_apps::resource::network))
            (void)worker.control.cancel({});
        if (worker.control.in_flight())
            (void)worker.control.complete(token, result);
        publish(worker, worker.control.snapshot(), token);
        worker.active.store(false);
    }
    if (worker.queue != nullptr)
        xQueueReset(worker.queue);
    worker.task = nullptr;
    xSemaphoreGive(worker.done);
    vTaskDeleteWithCaps(nullptr);
}

const char *error_name(error value) {
    switch (value) {
    case error::none:
        return "none";
    case error::invalid_command:
        return "invalid_command";
    case error::invalid_target:
        return "invalid_target";
    case error::invalid_port:
        return "invalid_port";
    case error::busy:
        return "busy";
    case error::rate_limited:
        return "rate_limited";
    case error::grant_revoked:
        return "grant_revoked";
    case error::backend_unavailable:
        return "backend_unavailable";
    case error::dns_failed:
        return "dns_failed";
    case error::tcp_failed:
        return "tcp_failed";
    case error::tls_failed:
        return "tls_failed";
    case error::http_failed:
        return "http_failed";
    case error::timeout:
        return "timeout";
    case error::cancelled:
        return "cancelled";
    case error::response_too_large:
        return "response_too_large";
    case error::clock_regressed:
        return "clock_regressed";
    }
    return "unknown";
}

std::string result_text(const outcome &result) {
    char buffer[128]{};
    std::snprintf(buffer, sizeof(buffer), "netdiag result: state=%u error=%s duration_ms=%lu http_status=%u\n",
                  static_cast<unsigned>(result.status), error_name(result.reason),
                  static_cast<unsigned long>(result.duration_ms), static_cast<unsigned>(result.http_status));
    return buffer;
}

} // namespace

const cyberdeck_apps::manifest &application::get_manifest() const {
    return k_manifest;
}

bool application::start() {
    worker_state &worker = worker_context();
    if (running_)
        return worker.task != nullptr && worker.grant.allows(cyberdeck_apps::resource::network);
    if (worker.initialized)
        return false;
    worker.queue = xQueueCreate(1, sizeof(job));
    worker.done = xSemaphoreCreateBinary();
    worker.mutex = xSemaphoreCreateMutex();
    if (worker.queue == nullptr || worker.done == nullptr || worker.mutex == nullptr) {
        if (worker.queue != nullptr)
            vQueueDelete(worker.queue);
        if (worker.done != nullptr)
            vSemaphoreDelete(worker.done);
        if (worker.mutex != nullptr)
            vSemaphoreDelete(worker.mutex);
        worker.queue = nullptr;
        worker.done = nullptr;
        worker.mutex = nullptr;
        return false;
    }
    worker.grant = app_grant();
    worker.control = {};
    worker.published = {};
    worker.stopping.store(false);
    worker.cancel_requested.store(false);
    worker.active.store(false);
    worker.initialized = true;
    if (xTaskCreateWithCaps(worker_task, "netdiag", k_worker_stack_bytes, nullptr, 4, &worker.task,
                            MALLOC_CAP_SPIRAM) != pdPASS) {
        worker.initialized = false;
        vQueueDelete(worker.queue);
        vSemaphoreDelete(worker.done);
        vSemaphoreDelete(worker.mutex);
        worker.queue = nullptr;
        worker.done = nullptr;
        worker.mutex = nullptr;
        worker.task = nullptr;
        return false;
    }
    running_ = true;
    return true;
}

bool application::stop() {
    worker_state &worker = worker_context();
    if (!running_)
        return !worker.initialized;
    worker.stopping.store(true);
    if (xSemaphoreTake(worker.done, pdMS_TO_TICKS(k_stop_timeout_ms)) != pdTRUE)
        return false;
    worker.grant = {};
    vQueueDelete(worker.queue);
    vSemaphoreDelete(worker.done);
    vSemaphoreDelete(worker.mutex);
    worker.queue = nullptr;
    worker.done = nullptr;
    worker.mutex = nullptr;
    worker.initialized = false;
    running_ = false;
    return true;
}

bool application::running() const {
    return running_;
}

cyberdeck_apps::result application::execute(std::string_view command, std::string_view args) {
    worker_state &worker = worker_context();
    if (!running_ || command != "netdiag" || !worker.grant.allows(cyberdeck_apps::resource::network))
        return {cyberdeck_apps::result_status::rejected, "netdiag: unavailable\n"};
    std::string line{"netdiag"};
    line.append(args.data(), args.size());
    const parsed_command parsed = parse_command(line);
    if (parsed.failure != error::none)
        return {cyberdeck_apps::result_status::rejected, std::string("netdiag: ") + error_name(parsed.failure) + "\n"};
    if (parsed.kind == operation::status) {
        wifi_status_t wifi{};
        if (wifi_mgr_get_status(&wifi) != ESP_OK || !worker.grant.allows(cyberdeck_apps::resource::network))
            return {cyberdeck_apps::result_status::rejected, "netdiag status: unavailable\n"};
        char gateway[INET6_ADDRSTRLEN] = "<missing>";
        char dns_server[INET6_ADDRSTRLEN] = "<missing>";
        char rssi[16] = "<missing>";
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip_info{};
        if (netif != nullptr && wifi.has_ip && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.gw.addr != 0)
            (void)inet_ntop(AF_INET, &ip_info.gw, gateway, sizeof(gateway));
        esp_netif_dns_info_t dns_info{};
        if (netif != nullptr && esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK &&
            !ESP_IP_IS_ANY(dns_info.ip)) {
            const int family = dns_info.ip.type == ESP_IPADDR_TYPE_V4 ? AF_INET : AF_INET6;
            const void *address = family == AF_INET ? static_cast<const void *>(&dns_info.ip.u_addr.ip4)
                                                    : static_cast<const void *>(&dns_info.ip.u_addr.ip6);
            (void)inet_ntop(family, address, dns_server, sizeof(dns_server));
        }
        wifi_ap_record_t access_point{};
        if (wifi.connected && esp_wifi_sta_get_ap_info(&access_point) == ESP_OK)
            std::snprintf(rssi, sizeof(rssi), "%d", static_cast<int>(access_point.rssi));
        char output[256]{};
        std::snprintf(output, sizeof(output),
                      "netdiag status: interface=sta connected=%s ip=%s gateway=%s dns=%s rssi=%s\n",
                      wifi.connected && wifi.has_ip ? "yes" : "no", wifi.has_ip ? wifi.ip : "<missing>", gateway,
                      dns_server, rssi);
        return {cyberdeck_apps::result_status::handled, output};
    }
    if (parsed.kind == operation::result) {
        outcome snapshot{};
        xSemaphoreTake(worker.mutex, portMAX_DELAY);
        snapshot = worker.published;
        xSemaphoreGive(worker.mutex);
        return {cyberdeck_apps::result_status::handled, result_text(snapshot)};
    }
    if (parsed.kind == operation::cancel) {
        if (!worker.active.load())
            return {cyberdeck_apps::result_status::handled, "netdiag: no operation\n"};
        worker.cancel_requested.store(true);
        job cancel{};
        cancel.kind = job_kind::cancel;
        (void)xQueueSend(worker.queue, &cancel, 0);
        return {cyberdeck_apps::result_status::handled, "netdiag: cancel requested\n"};
    }
    if (worker.active.load())
        return {cyberdeck_apps::result_status::rejected, "netdiag: busy\n"};
    job next{};
    next.kind = job_kind::request;
    next.command = parsed;
    if (xQueueSend(worker.queue, &next, 0) != pdTRUE)
        return {cyberdeck_apps::result_status::rejected, "netdiag: busy\n"};
    return {cyberdeck_apps::result_status::handled, "netdiag: queued\n"};
}

application &global_application() {
    static application instance;
    return instance;
}

} // namespace cyberdeck_netdiag
