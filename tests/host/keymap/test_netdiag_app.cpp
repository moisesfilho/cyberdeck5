#include "apps/network/cyberdeck_netdiag_app.h"
#include "apps/network/cyberdeck_netdiag_dns_adapter.h"
#include "apps/network/cyberdeck_netdiag_https_adapter.h"
#include "apps/network/cyberdeck_netdiag_tcp_adapter.h"
#include "apps/runtime/cyberdeck_app_runtime.h"
#include "apps/wifi/wifi_mgr.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "lwip/dns.h"
#include "lwip/inet.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace cyberdeck_netdiag;
struct fake_sem {
    bool signaled{};
};
namespace {
int64_t clock_us{};
void *scheduled_arg{};
void (*scheduled_entry)(void *){};
bool worker_create_ok{true}, worker_wait_ok{true};
int worker_wait_count{}, worker_create_count{};
bool scheduled_worker_complete{true};
int dns_count{}, tcp_count{}, https_count{}, close_count{}, http_cleanup_count{};
int checks{}, failures{};
uint16_t http_status{405};
bool defer_dns{};
dns_found_callback deferred_dns_callback{};
ip_addr_t deferred_dns_address{};
uint64_t now(void *) {
    return static_cast<uint64_t>(clock_us / 1000);
}
bool schedule(void *, void (*entry)(void *), void *arg, uint32_t stack, void **handle) {
    ++worker_create_count;
    assert(stack >= 4096);
    if (!worker_create_ok)
        return false;
    scheduled_entry = entry;
    scheduled_arg = arg;
    *handle = arg;
    scheduled_worker_complete = false;
    return true;
}
bool wait(void *, void *, uint32_t) {
    ++worker_wait_count;
    return worker_wait_ok && scheduled_worker_complete;
}
void run_worker() {
    assert(scheduled_entry);
    auto fn = scheduled_entry;
    auto arg = scheduled_arg;
    scheduled_entry = nullptr;
    fn(arg);
    scheduled_worker_complete = true;
}
class dependency final : public cyberdeck_apps::application {
  public:
    explicit dependency(const cyberdeck_apps::manifest &m) : manifest_(m) {}
    const cyberdeck_apps::manifest &get_manifest() const override {
        return manifest_;
    }
    bool start() override {
        live = true;
        return true;
    }
    bool stop() override {
        live = false;
        return true;
    }
    bool running() const override {
        return live;
    }
    cyberdeck_apps::result execute(std::string_view command, std::string_view) override {
        return command == manifest_.command
                   ? cyberdeck_apps::result{cyberdeck_apps::result_status::handled, "ssh fixture handled\n"}
                   : cyberdeck_apps::result{};
    }

  private:
    cyberdeck_apps::manifest manifest_;
    bool live{};
};
void check(bool value, const char *text) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL: %s\n", text);
        std::fflush(stdout);
    }
}
cyberdeck_apps::result command(cyberdeck_apps::runtime &r, const char *args) {
    return r.execute_line(std::string("netdiag ") + args);
}
} // namespace

int64_t esp_timer_get_time(void) {
    return clock_us;
}
SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    static fake_sem semaphore{};
    semaphore.signaled = false;
    return &semaphore;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t) {
    auto *s = reinterpret_cast<fake_sem *>(sem);
    if (!s->signaled)
        return pdFALSE;
    s->signaled = false;
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem) {
    reinterpret_cast<fake_sem *>(sem)->signaled = true;
    return pdTRUE;
}
void vTaskDelete(void *) {}
void vTaskDelay(TickType_t ticks) {
    clock_us += static_cast<int64_t>(ticks) * 1000;
}
BaseType_t xTaskCreateWithCaps(void (*)(void *), const char *, uint32_t, void *, unsigned, void **, unsigned) {
    return pdFALSE;
}
int wifi_mgr_get_status(wifi_status_t *) {
    return -1;
}
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *) {
    return nullptr;
}
int esp_netif_get_ip_info(esp_netif_t *, esp_netif_ip_info_t *) {
    return -1;
}
int esp_netif_get_dns_info(esp_netif_t *, int, esp_netif_dns_info_t *) {
    return -1;
}
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *) {
    return -1;
}
char *esp_ip4addr_ntoa(const esp_ip4_addr_t *, char *, int) {
    return nullptr;
}
err_t tcpip_callback(void (*callback)(void *), void *arg) {
    callback(arg);
    return ERR_OK;
}
err_t dns_gethostbyname_addrtype(const char *name, ip_addr_t *out, dns_found_callback cb, void *, unsigned char) {
    ++dns_count;
    assert(std::strcmp(name, "node.test") == 0);
    out->ip4.addr = 0x080200c0;
    if (defer_dns) {
        deferred_dns_callback = cb;
        deferred_dns_address = *out;
        return ERR_INPROGRESS;
    }
    cb(name, out, nullptr);
    return ERR_OK;
}
char *ip4addr_ntoa_r(const ip4_addr_t *, char *out, int) {
    std::strcpy(out, "192.0.2.8");
    return out;
}
extern "C" int __wrap_socket(int, int, int) {
    ++tcp_count;
    return 8;
}
extern "C" int __wrap_close(int) {
    ++close_count;
    return 0;
}
extern "C" int __wrap_fcntl(int, int, ...) {
    return 0;
}
extern "C" int __wrap_connect(int, const sockaddr *, socklen_t) {
    return 0;
}
extern "C" int __wrap_select(int, fd_set *, fd_set *, fd_set *, timeval *) {
    return 1;
}
extern "C" int __wrap_getsockopt(int, int, int, void *value, socklen_t *len) {
    *static_cast<int *>(value) = 0;
    *len = sizeof(int);
    return 0;
}
extern "C" esp_err_t esp_crt_bundle_attach(void *) {
    return ESP_OK;
}
extern "C" esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c) {
    ++https_count;
    assert(c->method == HTTP_METHOD_HEAD && c->disable_auto_redirect && c->is_async);
    assert(std::strcmp(c->url, "https://node.test/health") == 0);
    return const_cast<esp_http_client_config_t *>(c);
}
extern "C" esp_err_t esp_http_client_perform(esp_http_client_handle_t) {
    return ESP_OK;
}
extern "C" int esp_http_client_get_status_code(esp_http_client_handle_t) {
    return http_status;
}
extern "C" esp_err_t esp_http_client_cleanup(esp_http_client_handle_t) {
    ++http_cleanup_count;
    return ESP_OK;
}

int main() {
    application ungranted({nullptr, now, cyberdeck_netdiag::resolve_ipv4, cyberdeck_netdiag::tcp_connect,
                           cyberdeck_netdiag::https_head, schedule, wait});
    check(ungranted.start(), "app can initialize without runtime grant");
    check(ungranted.execute("netdiag", "dns node.test").status == cyberdeck_apps::result_status::rejected &&
              dns_count == 0 && scheduled_entry == nullptr,
          "network request denied without grant");
    check(ungranted.stop(), "ungranted app stops cleanly");
    constexpr cyberdeck_apps::manifest event_manifest{"cyberdeck.event_log", "events", "1", "", "", {}, 0, {}, 0};
    constexpr cyberdeck_apps::manifest wifi_manifest{"cyberdeck.wifi", "wifi", "1", "", "", {}, 0, {}, 0};
    constexpr cyberdeck_apps::manifest ssh_manifest{"cyberdeck.ssh", "SSH", "1", "", "ssh", {}, 0, {}, 0};
    dependency events(event_manifest), wifi(wifi_manifest), ssh(ssh_manifest);
    application app({nullptr, now, cyberdeck_netdiag::resolve_ipv4, cyberdeck_netdiag::tcp_connect,
                     cyberdeck_netdiag::https_head, schedule, wait});
    cyberdeck_apps::runtime runtime;
    check(runtime.register_application(events) && runtime.register_application(wifi) &&
              runtime.register_application(ssh),
          "required dependencies and SSH command register");
    check(runtime.start_application("cyberdeck.ssh"), "independent SSH app starts");
    check(runtime.register_application(app), "network diagnostic app registers");
    check(runtime.find("cyberdeck.netdiag") == &app, "runtime resolves registered app");
    check(runtime.execute_line("ssh host.test").status == cyberdeck_apps::result_status::handled,
          "netdiag registration preserves independent SSH command routing");
    check(runtime.start_application("cyberdeck.netdiag"), "runtime starts app and dependencies");
    check(app.running() && app.app_grant().allows(cyberdeck_apps::resource::network),
          "network grant is live after start");
    check(command(runtime, "status").status == cyberdeck_apps::result_status::handled, "status accepted");
    check(command(runtime, "bad target").status == cyberdeck_apps::result_status::rejected &&
              dns_count + tcp_count + https_count == 0,
          "invalid command has no network calls");

    auto r = command(runtime, "dns node.test");
    check(r.status == cyberdeck_apps::result_status::handled && dns_count == 0, "DNS request admitted asynchronously");
    check(command(runtime, "tcp node.test 443").status == cyberdeck_apps::result_status::rejected,
          "parallel request rejected while worker active");
    run_worker();
    r = command(runtime, "result");
    check(r.output.find("state=complete") != std::string::npos && r.output.find("ipv4=192.0.2.8") != std::string::npos,
          "DNS result published through runtime");
    check(dns_count == 1 && tcp_count == 0 && https_count == 0, "DNS operation calls DNS only");

    clock_us += 29999000;
    check(command(runtime, "tcp node.test 443").status == cyberdeck_apps::result_status::rejected,
          "request rejected one millisecond before rate limit boundary");
    clock_us += 1000;
    r = command(runtime, "tcp node.test 443");
    check(r.status == cyberdeck_apps::result_status::handled, "TCP request admitted at exact rate limit boundary");
    run_worker();
    r = command(runtime, "result");
    check(r.output.find("state=complete") != std::string::npos && dns_count == 2 && tcp_count == 1 && https_count == 0,
          "TCP performs DNS-for-connect then TCP without HTTPS");
    clock_us += 30001000;
    r = command(runtime, "https https://node.test/health");
    check(r.status == cyberdeck_apps::result_status::handled, "HTTPS request admitted");
    run_worker();
    r = command(runtime, "result");
    check(r.output.find("http_status=405") != std::string::npos && https_count == 1 && dns_count == 2 && tcp_count == 1,
          "HTTPS uses only HTTPS port and preserves final 405");

    clock_us += 30001000;
    r = command(runtime, "dns node.test");
    check(r.status == cyberdeck_apps::result_status::handled, "cancellable request admitted");
    check(command(runtime, "cancel").status == cyberdeck_apps::result_status::handled, "cancel accepted");
    run_worker();
    check(command(runtime, "result").output.find("state=cancelled") != std::string::npos,
          "cancelled result remains visible");

    clock_us += 30001000;
    worker_create_ok = false;
    check(command(runtime, "dns node.test").status == cyberdeck_apps::result_status::rejected,
          "worker creation failure rejected");
    worker_create_ok = true;
    clock_us += 30001000;
    check(command(runtime, "dns node.test").status == cyberdeck_apps::result_status::handled,
          "request admitted before stop-timeout scenario");
    const auto retained_grant = app.app_grant();
    worker_wait_ok = false;
    check(!runtime.stop_application("cyberdeck.netdiag"), "stop timeout is surfaced");
    check(retained_grant.valid() && retained_grant.allows(cyberdeck_apps::resource::network),
          "stop timeout preserves copied network grant while worker remains owned");
    check(app.running(), "timed-out stop preserves app state and worker ownership");
    check(!runtime.restart_application("cyberdeck.netdiag"), "restart rejected while prior worker is quarantined");
    const int creates_before_quarantine = worker_create_count;
    check(!app.start(), "direct app start cannot bypass quarantined worker");
    check(app.execute("netdiag", "dns node.test").status == cyberdeck_apps::result_status::rejected &&
              worker_create_count == creates_before_quarantine,
          "direct execute refuses new work while quarantined worker remains owned");
    const int dns_before_revoked_worker = dns_count;
    run_worker();
    check(dns_count == dns_before_revoked_worker, "revoked grant prevents active worker from entering DNS adapter");
    check(!app.start(), "worker completion alone does not clear handle before wait-worker reap");
    check(worker_wait_count >= 4, "completion is observed only through wait-worker");
    worker_wait_ok = true;
    const int waits_before_reap = worker_wait_count;
    check(runtime.restart_application("cyberdeck.netdiag"),
          "restart reaps completed worker and finalizes pending stop");
    check(!retained_grant.valid(), "successful teardown revokes copied network grant");
    check(worker_wait_count == waits_before_reap + 1, "completed worker handle is reaped exactly once");
    check(app.running() && app.app_grant().allows(cyberdeck_apps::resource::network),
          "restart restores app with a fresh network grant");
    check(runtime.stop_application("cyberdeck.netdiag"), "restarted app stops");

    clock_us += 30001000;
    check(runtime.start_application("cyberdeck.netdiag"), "app can start without a live network grant");
    defer_dns = true;
    check(command(runtime, "dns node.test").status == cyberdeck_apps::result_status::handled,
          "DNS request with grant schedules worker");
    run_worker();
    check(deferred_dns_callback != nullptr, "DNS adapter retains callback while lookup is pending");
    const int dns_before_stale_callback = dns_count;
    deferred_dns_callback("node.test", &deferred_dns_address, nullptr);
    deferred_dns_callback = nullptr;
    check(command(runtime, "result").output.find("state=failed") != std::string::npos &&
              dns_count == dns_before_stale_callback,
          "late DNS callback after timeout cannot publish stale success or start another lookup");
    defer_dns = false;
    clock_us += 30001000;
    check(command(runtime, "dns node.test").status == cyberdeck_apps::result_status::handled,
          "DNS slot accepts fresh request after abandoned callback is drained");
    run_worker();
    check(command(runtime, "result").output.find("state=complete") != std::string::npos,
          "fresh request completes after stale callback");
    check(runtime.execute_line("ssh host.test").status == cyberdeck_apps::result_status::handled,
          "SSH command remains routable alongside network diagnostics");
    check(runtime.stop_application("cyberdeck.netdiag"), "final app stop succeeds");
    if (failures != 0) {
        std::printf("FAIL netdiag app runtime integration: %d/%d checks failed\n", failures, checks);
        return 1;
    }
    std::printf("PASS netdiag app runtime integration: %d checks\n", checks);
}
