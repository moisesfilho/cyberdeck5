#pragma once

#include <mutex>
#include <stdint.h>

namespace cyberdeck_netdiag {

constexpr uint32_t k_rate_limit_ms = 30000;
constexpr uint32_t k_stage_timeout_ms = 5000;
constexpr uint32_t k_max_host_length = 253;
constexpr uint32_t k_max_url_length = 512;

enum class stage : uint8_t { idle, queued, dns, tcp, https, complete, failed, cancelled };
enum class operation : uint8_t { dns, tcp, https };
enum class error : uint8_t {
    none,
    invalid_request,
    busy,
    rate_limited,
    grant_revoked,
    dns_failed,
    tcp_failed,
    https_failed,
    timeout,
    cancelled,
    stale_token,
    invalid_ports
};

struct request {
    operation requested_operation{operation::dns};
    char hostname[k_max_host_length + 1]{};
    uint16_t tcp_port{};
    char https_url[k_max_url_length + 1]{};
};

struct snapshot {
    uint64_t token{};
    stage current_stage{stage::idle};
    error failure{error::none};
    uint32_t duration_ms{};
    uint16_t http_status{};
    char resolved_ipv4[16]{};
};

struct ports {
    void *context{};
    uint64_t (*now_ms)(void *context){};
    bool (*network_grant_valid)(void *context){};
    // DNS adapters retain their callback context until the DNS callback fires,
    // including after logical cancellation/timeout. Returning does not transfer
    // ownership of that context to this controller.
    bool (*resolve_ipv4)(void *context, const char *hostname, uint64_t deadline_ms, char output_ipv4[16]){};
    bool (*tcp_connect)(void *context, const char *ipv4, uint16_t port, uint64_t deadline_ms){};
    // HTTPS must validate CA/hostname, preserve SNI and Host, send HEAD with
    // zero body, disable redirects, and never fall back to GET.
    bool (*https_head)(void *context, const char *url, const char *hostname, uint64_t deadline_ms,
                       uint16_t *http_status){};
};

class controller {
  public:
    error submit(const request &value, bool network_grant_valid, uint64_t now_ms, uint64_t *token);
    error cancel(uint64_t token, uint64_t now_ms);
    error snapshot_for(uint64_t token, snapshot *output) const;
    void run(uint64_t token, const ports &io);

  private:
    mutable std::mutex mutex_;
    request request_{};
    snapshot snapshot_{};
    uint64_t last_submit_ms_{};
    uint64_t started_ms_{};
    uint64_t generation_{};
    bool has_submitted_{};
    bool running_{};
    bool worker_active_{};
};

} // namespace cyberdeck_netdiag
