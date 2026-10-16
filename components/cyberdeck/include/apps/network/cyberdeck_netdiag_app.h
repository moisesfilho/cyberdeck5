#pragma once

#include "apps/network/cyberdeck_netdiag_controller.h"
#include "apps/runtime/cyberdeck_app_runtime.h"

#include <cstdint>

namespace cyberdeck_netdiag {

struct application_platform {
    void *context{};
    uint64_t (*now_ms)(void *){};
    bool (*resolve_ipv4)(void *, const char *, uint64_t, char[16]){};
    bool (*tcp_connect)(void *, const char *, uint16_t, uint64_t){};
    bool (*https_head)(void *, const char *, const char *, uint64_t, uint16_t *){};
    bool (*create_worker)(void *, void (*)(void *), void *, uint32_t stack_bytes, void **handle){};
    bool (*wait_worker)(void *, void *handle, uint32_t timeout_ms){};
};

class application final : public cyberdeck_apps::application {
  public:
    application();
    explicit application(const application_platform &platform);
    const cyberdeck_apps::manifest &get_manifest() const override;
    bool start() override;
    bool stop() override;
    bool running() const override;
    cyberdeck_apps::result execute(std::string_view command, std::string_view args) override;

  private:
    static void worker_entry(void *context);
    static uint64_t platform_now(void *context);
    static bool platform_grant_valid(void *context);
    static bool platform_resolve(void *context, const char *host, uint64_t deadline, char output[16]);
    static bool platform_tcp(void *context, const char *address, uint16_t port, uint64_t deadline);
    static bool platform_https(void *context, const char *url, const char *host, uint64_t deadline, uint16_t *status);
    static bool production_wait(void *context, void *handle, uint32_t timeout_ms);
    bool reap_worker(uint32_t timeout_ms);
    void finish_stop();

    application_platform platform_{};
    controller controller_{};
    cyberdeck_apps::grant request_grant_{};
    operation current_operation_{operation::dns};
    void *worker_handle_{};
    uint64_t worker_token_{};
    void *synchronization_{};
    bool initialized_{};
    bool stop_pending_{};
};

application &global_application();

} // namespace cyberdeck_netdiag
