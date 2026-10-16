#include "apps/network/cyberdeck_netdiag_controller.h"

#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <thread>

using namespace cyberdeck_netdiag;

namespace {
int checks = 0;
int failures = 0;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(x)) {                                                                                                    \
            ++failures;                                                                                                \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);                                                   \
        }                                                                                                              \
    } while (0)

struct fixture {
    uint64_t now{100};
    bool grant{true}, dns_ok{true}, tcp_ok{true}, https_ok{true};
    bool block_dns{false}, expire_dns{false}, revoke_after_dns{false}, revoke_after_tcp{false};
    int dns_calls{0}, tcp_calls{0}, https_calls{0};
    uint64_t deadlines[3]{};
    std::mutex mutex;
    std::condition_variable cv;
    bool dns_entered{false}, release_dns{false};
    static uint64_t clock(void *p) {
        return static_cast<fixture *>(p)->now;
    }
    static bool permission(void *p) {
        return static_cast<fixture *>(p)->grant;
    }
    static bool resolve(void *p, const char *host, uint64_t deadline, char out[16]) {
        auto &f = *static_cast<fixture *>(p);
        ++f.dns_calls;
        f.deadlines[0] = deadline;
        CHECK(std::strcmp(host, "example.com") == 0);
        if (f.block_dns) {
            std::unique_lock<std::mutex> lock(f.mutex);
            f.dns_entered = true;
            f.cv.notify_all();
            f.cv.wait(lock, [&] { return f.release_dns; });
        }
        if (f.expire_dns)
            f.now = deadline;
        if (f.revoke_after_dns)
            f.grant = false;
        std::strcpy(out, "203.0.113.9");
        return f.dns_ok;
    }
    static bool connect(void *p, const char *ip, uint16_t port, uint64_t deadline) {
        auto &f = *static_cast<fixture *>(p);
        ++f.tcp_calls;
        f.deadlines[1] = deadline;
        CHECK(std::strcmp(ip, "203.0.113.9") == 0);
        CHECK(port == 443);
        if (f.revoke_after_tcp)
            f.grant = false;
        return f.tcp_ok;
    }
    static bool head(void *p, const char *url, const char *host, uint64_t deadline, uint16_t *status) {
        auto &f = *static_cast<fixture *>(p);
        ++f.https_calls;
        f.deadlines[2] = deadline;
        CHECK(std::strcmp(url, "https://example.com/health") == 0);
        CHECK(std::strcmp(host, "example.com") == 0);
        *status = 405;
        return f.https_ok;
    }
    ports io() {
        return {this, clock, permission, resolve, connect, head};
    }
};

request valid() {
    request r{};
    std::strcpy(r.hostname, "example.com");
    r.tcp_port = 443;
    std::strcpy(r.https_url, "https://example.com/health");
    return r;
}

request request_for(operation selected) {
    request r = valid();
    r.requested_operation = selected;
    return r;
}

void operation_dispatch_is_explicit() {
    for (const operation selected : {operation::dns, operation::tcp, operation::https}) {
        controller c;
        fixture f;
        uint64_t token{};
        snapshot s{};
        CHECK(c.submit(request_for(selected), true, 100, &token) == error::none);
        c.run(token, f.io());
        CHECK(c.snapshot_for(token, &s) == error::none && s.current_stage == stage::complete);
        CHECK(f.dns_calls == (selected == operation::https ? 0 : 1));
        CHECK(f.tcp_calls == (selected == operation::tcp ? 1 : 0));
        CHECK(f.https_calls == (selected == operation::https ? 1 : 0));
        CHECK(s.current_stage != stage::dns || selected == operation::dns);
        CHECK(s.current_stage != stage::tcp || selected == operation::tcp);
        CHECK(s.current_stage != stage::https || selected == operation::https);
        if (selected == operation::dns) {
            CHECK(std::strcmp(s.resolved_ipv4, "203.0.113.9") == 0);
            CHECK(s.http_status == 0);
        } else if (selected == operation::tcp) {
            CHECK(std::strcmp(s.resolved_ipv4, "203.0.113.9") == 0);
            CHECK(s.http_status == 0);
        } else {
            CHECK(s.resolved_ipv4[0] == '\0');
            CHECK(s.http_status == 405);
        }
    }
}

void operation_specific_validation() {
    controller c;
    fixture f;
    uint64_t token = 17;
    request bad = request_for(operation::dns);
    bad.tcp_port = 0;
    CHECK(c.submit(bad, true, 100, &token) == error::none); // DNS ignores TCP port.
    c.run(token, f.io());
    CHECK(f.dns_calls == 1 && f.tcp_calls == 0 && f.https_calls == 0);

    for (const operation selected : {operation::dns, operation::tcp, operation::https}) {
        request invalid = request_for(selected);
        if (selected == operation::dns)
            std::strcpy(invalid.hostname, "bad host");
        if (selected == operation::tcp) {
            invalid.tcp_port = 0;
        }
        if (selected == operation::https)
            std::strcpy(invalid.https_url, "https://example.net/");
        controller rejected;
        uint64_t untouched = 91;
        CHECK(rejected.submit(invalid, true, 100, &untouched) == error::invalid_request);
        CHECK(untouched == 91);
    }

    request tcp = request_for(operation::tcp);
    std::strcpy(tcp.https_url, "not a URL");
    controller tcp_controller;
    uint64_t tcp_token{};
    CHECK(tcp_controller.submit(tcp, true, 100, &tcp_token) == error::none);
    fixture tcp_fixture;
    tcp_controller.run(tcp_token, tcp_fixture.io());
    CHECK(tcp_fixture.dns_calls == 1 && tcp_fixture.tcp_calls == 1 && tcp_fixture.https_calls == 0);

    request https = request_for(operation::https);
    https.tcp_port = 0;
    controller https_controller;
    uint64_t https_token{};
    CHECK(https_controller.submit(https, true, 100, &https_token) == error::none);
    fixture https_fixture;
    https_controller.run(https_token, https_fixture.io());
    CHECK(https_fixture.dns_calls == 0 && https_fixture.tcp_calls == 0 && https_fixture.https_calls == 1);
}

void admission_and_validation() {
    controller c;
    fixture f;
    uint64_t token = 99;
    CHECK(c.submit(valid(), false, 100, &token) == error::grant_revoked);
    CHECK(token == 99);
    request bad = request_for(operation::tcp);
    bad.tcp_port = 0;
    CHECK(c.submit(bad, true, 100, &token) == error::invalid_request);
    bad = request_for(operation::https);
    std::strcpy(bad.https_url, "https://example.net/");
    CHECK(c.submit(bad, true, 100, &token) == error::invalid_request);
    bad = valid();
    std::memset(bad.hostname, 'a', sizeof(bad.hostname));
    CHECK(c.submit(bad, true, 100, &token) == error::invalid_request);
    bad = valid();
    std::strcpy(bad.hostname, "bad host");
    CHECK(c.submit(bad, true, 100, &token) == error::invalid_request);
    bad = request_for(operation::https);
    std::strcpy(bad.https_url, "https://example.com#fragment");
    CHECK(c.submit(bad, true, 100, &token) == error::invalid_request);
    controller root_url;
    std::strcpy(bad.https_url, "https://example.com");
    CHECK(root_url.submit(bad, true, 100, &token) == error::none);
    CHECK(root_url.cancel(token, 100) == error::none);
    CHECK(c.submit(valid(), true, 100, nullptr) == error::invalid_request);
    CHECK(f.dns_calls == 0 && f.tcp_calls == 0 && f.https_calls == 0);
    CHECK(c.submit(request_for(operation::dns), true, 100, &token) == error::none);
    const uint64_t first = token;
    CHECK(c.submit(valid(), true, 101, &token) == error::busy);
    snapshot s{};
    CHECK(c.snapshot_for(first, &s) == error::none && s.current_stage == stage::queued);
    c.run(first, f.io());
    CHECK(c.snapshot_for(first, &s) == error::none);
    CHECK(s.current_stage == stage::complete && s.failure == error::none);
    CHECK(s.http_status == 0);
    CHECK(std::strcmp(s.resolved_ipv4, "203.0.113.9") == 0);
    CHECK(f.dns_calls == 1 && f.tcp_calls == 0 && f.https_calls == 0);
    CHECK(f.deadlines[0] == 5100);
    snapshot copied{};
    CHECK(c.snapshot_for(first, &copied) == error::none);
    copied.http_status = 200;
    CHECK(c.snapshot_for(first, &s) == error::none && s.http_status == 0);
    CHECK(c.submit(valid(), true, 100 + k_rate_limit_ms - 1, &token) == error::rate_limited);
    CHECK(c.submit(valid(), true, 100 + k_rate_limit_ms, &token) == error::none);
}

void port_failures_and_grants() {
    for (int mode = 0; mode < 7; ++mode) {
        controller c;
        fixture f;
        uint64_t t{};
        const operation selected = mode == 2 || mode == 6 ? operation::tcp
                                   : mode == 3            ? operation::https
                                                          : operation::dns;
        CHECK(c.submit(request_for(selected), true, 100, &t) == error::none);
        if (mode == 0)
            f.grant = false;
        if (mode == 1)
            f.dns_ok = false;
        if (mode == 2)
            f.tcp_ok = false;
        if (mode == 3)
            f.https_ok = false;
        if (mode == 4) {
            f.dns_ok = false;
            f.expire_dns = true;
        }
        if (mode == 5)
            f.revoke_after_dns = true;
        if (mode == 6)
            f.revoke_after_tcp = true;
        c.run(t, f.io());
        snapshot s{};
        CHECK(c.snapshot_for(t, &s) == error::none);
        if (mode == 0)
            CHECK(s.failure == error::grant_revoked && f.dns_calls == 0);
        if (mode == 1)
            CHECK(s.failure == error::dns_failed && f.tcp_calls == 0 && f.https_calls == 0);
        if (mode == 2)
            CHECK(s.failure == error::tcp_failed && f.https_calls == 0);
        if (mode == 3)
            CHECK(s.failure == error::https_failed && f.dns_calls == 0 && f.tcp_calls == 0);
        if (mode == 4)
            CHECK(s.failure == error::timeout);
        if (mode == 5)
            CHECK(s.failure == error::grant_revoked && f.tcp_calls == 0 && f.https_calls == 0);
        if (mode == 6)
            CHECK(s.failure == error::grant_revoked && f.https_calls == 0);
    }
    controller c;
    fixture f;
    uint64_t t{};
    CHECK(c.submit(valid(), true, 100, &t) == error::none);
    ports missing = f.io();
    missing.resolve_ipv4 = nullptr;
    c.run(t, missing);
    snapshot s{};
    CHECK(c.snapshot_for(t, &s) == error::none && s.failure == error::invalid_ports);
}

void cancel_keeps_operation_active() {
    controller c;
    fixture f;
    f.block_dns = true;
    uint64_t t{};
    CHECK(c.submit(valid(), true, 100, &t) == error::none);
    std::thread worker([&] { c.run(t, f.io()); });
    {
        std::unique_lock<std::mutex> lock(f.mutex);
        f.cv.wait(lock, [&] { return f.dns_entered; });
    }
    CHECK(c.cancel(t, 120) == error::none);
    uint64_t another{};
    CHECK(c.submit(valid(), true, 100 + k_rate_limit_ms, &another) == error::busy);
    snapshot s{};
    CHECK(c.snapshot_for(t, &s) == error::none && s.current_stage == stage::cancelled);
    {
        std::lock_guard<std::mutex> lock(f.mutex);
        f.release_dns = true;
    }
    f.cv.notify_all();
    worker.join();
    CHECK(c.submit(valid(), true, 100 + k_rate_limit_ms, &another) == error::none);
    CHECK(c.cancel(t, 130) == error::stale_token);
    CHECK(c.snapshot_for(0, &s) == error::stale_token);
    CHECK(c.cancel(another, 140) == error::none);
}
} // namespace

int main() {
    operation_dispatch_is_explicit();
    operation_specific_validation();
    admission_and_validation();
    port_failures_and_grants();
    cancel_keeps_operation_active();
    std::printf("netdiag controller: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
