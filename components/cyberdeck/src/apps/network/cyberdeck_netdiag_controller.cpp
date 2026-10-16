#include "apps/network/cyberdeck_netdiag_controller.h"

#include <cstring>

namespace cyberdeck_netdiag {
namespace {

template <typename Function> class scope_exit {
  public:
    explicit scope_exit(Function function) : function_(function) {}
    ~scope_exit() {
        function_();
    }
    scope_exit(const scope_exit &) = delete;
    scope_exit &operator=(const scope_exit &) = delete;

  private:
    Function function_;
};

bool bounded_length(const char *text, uint32_t capacity, uint32_t *length) {
    if (text == nullptr)
        return false;
    uint32_t count = 0;
    while (count < capacity && text[count] != '\0')
        ++count;
    if (count == 0 || count == capacity)
        return false;
    *length = count;
    return true;
}

bool valid_hostname(const request &value, uint32_t *host_length) {
    if (!bounded_length(value.hostname, sizeof(value.hostname), host_length))
        return false;
    for (uint32_t i = 0; i < *host_length; ++i) {
        const unsigned char c = static_cast<unsigned char>(value.hostname[i]);
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '.' && c != '-')
            return false;
    }
    return true;
}

bool valid_https_url(const request &value, uint32_t host_length) {
    uint32_t url_length = 0;
    if (!bounded_length(value.https_url, sizeof(value.https_url), &url_length))
        return false;
    constexpr char prefix[] = "https://";
    if (url_length < sizeof(prefix) - 1 + host_length ||
        std::memcmp(value.https_url, prefix, sizeof(prefix) - 1) != 0 ||
        std::memcmp(value.https_url + sizeof(prefix) - 1, value.hostname, host_length) != 0)
        return false;
    if (url_length == sizeof(prefix) - 1 + host_length)
        return true;
    const char boundary = value.https_url[sizeof(prefix) - 1 + host_length];
    if (boundary != '/' && boundary != '?')
        return false;
    return true;
}

bool valid_request(const request &value) {
    uint32_t host_length = 0;
    switch (value.requested_operation) {
    case operation::dns:
        return valid_hostname(value, &host_length);
    case operation::tcp:
        return value.tcp_port != 0 && valid_hostname(value, &host_length);
    case operation::https:
        return valid_hostname(value, &host_length) && valid_https_url(value, host_length);
    }
    return false;
}

} // namespace

error controller::submit(const request &value, bool grant_valid, uint64_t now_ms, uint64_t *token) {
    if (token == nullptr || !valid_request(value))
        return error::invalid_request;
    if (!grant_valid)
        return error::grant_revoked;
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_ || worker_active_ || snapshot_.current_stage == stage::queued ||
        snapshot_.current_stage == stage::dns || snapshot_.current_stage == stage::tcp ||
        snapshot_.current_stage == stage::https)
        return error::busy;
    if (has_submitted_ && now_ms >= last_submit_ms_ && now_ms - last_submit_ms_ < k_rate_limit_ms)
        return error::rate_limited;
    request_ = value;
    ++generation_;
    if (generation_ == 0)
        ++generation_;
    snapshot_ = {};
    snapshot_.token = generation_;
    snapshot_.current_stage = stage::queued;
    started_ms_ = now_ms;
    last_submit_ms_ = now_ms;
    has_submitted_ = true;
    running_ = true;
    *token = generation_;
    return error::none;
}

error controller::cancel(uint64_t token, uint64_t now_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (token == 0 || token != snapshot_.token)
        return error::stale_token;
    if (snapshot_.current_stage == stage::complete || snapshot_.current_stage == stage::failed ||
        snapshot_.current_stage == stage::cancelled)
        return error::none;
    snapshot_.current_stage = stage::cancelled;
    snapshot_.failure = error::cancelled;
    snapshot_.duration_ms = now_ms >= started_ms_ ? static_cast<uint32_t>(now_ms - started_ms_) : 0;
    if (!worker_active_)
        running_ = false;
    return error::none;
}

error controller::snapshot_for(uint64_t token, snapshot *output) const {
    if (output == nullptr)
        return error::invalid_request;
    std::lock_guard<std::mutex> lock(mutex_);
    if (token == 0 || token != snapshot_.token)
        return error::stale_token;
    *output = snapshot_;
    return error::none;
}

void controller::run(uint64_t token, const ports &io) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (token != snapshot_.token || !running_ || worker_active_)
            return;
        worker_active_ = true;
    }
    scope_exit release_worker([this]() {
        std::lock_guard<std::mutex> lock(mutex_);
        worker_active_ = false;
        if (snapshot_.current_stage == stage::cancelled || snapshot_.current_stage == stage::failed ||
            snapshot_.current_stage == stage::complete)
            running_ = false;
    });
    if (io.now_ms == nullptr || io.network_grant_valid == nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (token == snapshot_.token && running_) {
            snapshot_.current_stage = stage::failed;
            snapshot_.failure = error::invalid_ports;
            running_ = false;
        }
        return;
    }

    auto active_stage = [this, token](stage next) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (token != snapshot_.token || !running_ || snapshot_.current_stage == stage::cancelled)
            return false;
        snapshot_.current_stage = next;
        return true;
    };
    auto fail = [this, token, &io](error reason) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (token != snapshot_.token || !running_ || snapshot_.current_stage == stage::cancelled)
            return;
        snapshot_.current_stage = stage::failed;
        snapshot_.failure = reason;
        const uint64_t now = io.now_ms(io.context);
        snapshot_.duration_ms = now >= started_ms_ ? static_cast<uint32_t>(now - started_ms_) : 0;
        running_ = false;
    };
    auto permitted = [&io]() { return io.network_grant_valid(io.context); };
    uint64_t stage_deadline = 0;
    auto begin_deadline = [&io, &stage_deadline]() {
        stage_deadline = io.now_ms(io.context) + k_stage_timeout_ms;
        return stage_deadline;
    };

    char ipv4[16]{};
    uint16_t status = 0;
    request current{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (token != snapshot_.token || !running_)
            return;
        current = request_;
    }
    if (!permitted()) {
        fail(error::grant_revoked);
        return;
    }
    const auto resolve = [&]() {
        if (io.resolve_ipv4 == nullptr) {
            fail(error::invalid_ports);
            return false;
        }
        if (!active_stage(stage::dns))
            return false;
        if (!io.resolve_ipv4(io.context, current.hostname, begin_deadline(), ipv4)) {
            fail(io.now_ms(io.context) >= stage_deadline ? error::timeout : error::dns_failed);
            return false;
        }
        if (!permitted()) {
            fail(error::grant_revoked);
            return false;
        }
        return true;
    };
    switch (current.requested_operation) {
    case operation::dns:
        if (!resolve())
            return;
        break;
    case operation::tcp:
        if (!resolve())
            return;
        if (io.tcp_connect == nullptr) {
            fail(error::invalid_ports);
            return;
        }
        if (!active_stage(stage::tcp))
            return;
        if (!io.tcp_connect(io.context, ipv4, current.tcp_port, begin_deadline())) {
            fail(io.now_ms(io.context) >= stage_deadline ? error::timeout : error::tcp_failed);
            return;
        }
        if (!permitted()) {
            fail(error::grant_revoked);
            return;
        }
        break;
    case operation::https:
        if (io.https_head == nullptr) {
            fail(error::invalid_ports);
            return;
        }
        if (!active_stage(stage::https))
            return;
        if (!io.https_head(io.context, current.https_url, current.hostname, begin_deadline(), &status)) {
            fail(io.now_ms(io.context) >= stage_deadline ? error::timeout : error::https_failed);
            return;
        }
        if (!permitted()) {
            fail(error::grant_revoked);
            return;
        }
        break;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (token != snapshot_.token || !running_ || snapshot_.current_stage == stage::cancelled)
        return;
    snapshot_.current_stage = stage::complete;
    snapshot_.failure = error::none;
    snapshot_.http_status = status;
    if (current.requested_operation == operation::dns || current.requested_operation == operation::tcp)
        std::memcpy(snapshot_.resolved_ipv4, ipv4, sizeof(snapshot_.resolved_ipv4));
    const uint64_t now = io.now_ms(io.context);
    snapshot_.duration_ms = now >= started_ms_ ? static_cast<uint32_t>(now - started_ms_) : 0;
    running_ = false;
}

} // namespace cyberdeck_netdiag
