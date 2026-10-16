#include "apps/network/cyberdeck_netdiag_dns_adapter.h"
#include "lwip/dns.h"
#include "lwip/tcpip.h"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace {
enum class dns_mode {
    inline_success,
    cache_success,
    inline_callback,
    pending,
    immediate_failure,
    wrong_family,
    null_result
};
dns_mode mode = dns_mode::inline_success;
tcpip_callback_fn queued_callback = nullptr;
dns_found_callback found_callback = nullptr;
ip_addr_t *found_address = nullptr;
void *found_arg = nullptr;
std::string submitted_name;
uint64_t now_ms = 0;
uint64_t callback_on_delay = UINT64_MAX;
bool enqueue_fails = false;
int callback_kind = 0;
unsigned delay_count = 0;
std::mutex gate_mutex;
std::condition_variable gate_cv;
bool delay_entered = false;
bool release_delay = false;

ip_addr_t ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return {true,
            {static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8) | (static_cast<uint32_t>(c) << 16) |
             (static_cast<uint32_t>(d) << 24)}};
}
void reset() {
    mode = dns_mode::inline_success;
    queued_callback = nullptr;
    found_callback = nullptr;
    found_address = nullptr;
    found_arg = nullptr;
    submitted_name.clear();
    now_ms = 0;
    callback_on_delay = UINT64_MAX;
    enqueue_fails = false;
    callback_kind = 0;
    delay_count = 0;
    delay_entered = false;
    release_delay = false;
}
void complete(ip_addr_t *address) {
    assert(found_callback != nullptr);
    found_callback(submitted_name.c_str(), address, found_arg);
}
} // namespace

int64_t esp_timer_get_time(void) {
    return static_cast<int64_t>(now_ms * 1000);
}
void vTaskDelay(unsigned) {
    ++delay_count;
    now_ms++;
    if (callback_on_delay == now_ms && found_callback != nullptr) {
        if (callback_kind == 2)
            complete(nullptr);
        else {
            auto address = callback_kind == 1 ? ip_addr_t{false, {}} : ipv4(203, 0, 113, 7);
            complete(&address);
        }
    }
    std::unique_lock lock(gate_mutex);
    if (delay_entered) {
        gate_cv.notify_all();
        gate_cv.wait(lock, [] { return release_delay; });
    }
}
char *ip4addr_ntoa_r(const ip4_addr_t *address, char *out, int size) {
    const auto v = address->addr;
    std::snprintf(out, static_cast<size_t>(size), "%u.%u.%u.%u", v & 255, (v >> 8) & 255, (v >> 16) & 255,
                  (v >> 24) & 255);
    return out;
}
err_t tcpip_callback(tcpip_callback_fn callback, void *arg) {
    if (enqueue_fails)
        return -1;
    queued_callback = callback;
    callback(arg);
    return ERR_OK;
}
err_t dns_gethostbyname_addrtype(const char *hostname, ip_addr_t *result, dns_found_callback callback, void *arg,
                                 uint8_t family) {
    submitted_name = hostname;
    assert(family == LWIP_DNS_ADDRTYPE_IPV4);
    found_callback = callback;
    found_arg = arg;
    if (mode == dns_mode::pending)
        return ERR_INPROGRESS;
    if (mode == dns_mode::inline_callback) {
        auto address = ipv4(198, 51, 100, 12);
        callback(hostname, &address, arg);
        return ERR_INPROGRESS;
    }
    if (mode == dns_mode::immediate_failure)
        return -2;
    *result = mode == dns_mode::wrong_family ? ip_addr_t{false, {}} : ipv4(192, 0, 2, 44);
    return ERR_OK;
}

static void test_inline_and_cache_results() {
    reset();
    char output[16] = "stale";
    assert(cyberdeck_netdiag::resolve_ipv4(nullptr, "inline.example", 10, output));
    assert(std::strcmp(output, "192.0.2.44") == 0);
    assert(submitted_name == "inline.example");
    reset();
    mode = dns_mode::cache_success;
    output[0] = '\0';
    assert(cyberdeck_netdiag::resolve_ipv4(nullptr, "cache.example", 10, output));
    assert(std::strcmp(output, "192.0.2.44") == 0);
    reset();
    mode = dns_mode::inline_callback;
    output[0] = '\0';
    assert(cyberdeck_netdiag::resolve_ipv4(nullptr, "reentrant.example", 10, output));
    assert(std::strcmp(output, "198.51.100.12") == 0);
}
static void test_async_callbacks_and_invalid_results() {
    {
        reset();
        mode = dns_mode::pending;
        char output[16]{};
        callback_on_delay = 1;
        assert(cyberdeck_netdiag::resolve_ipv4(nullptr, "async.example", 8, output));
        assert(std::strcmp(output, "203.0.113.7") == 0);
    }
    for (int kind = 1; kind <= 2; ++kind) {
        reset();
        mode = dns_mode::pending;
        char output[16]{};
        callback_on_delay = 1;
        callback_kind = kind;
        assert(!cyberdeck_netdiag::resolve_ipv4(nullptr, "async-failure.example", 8, output));
    }
    reset();
    mode = dns_mode::wrong_family;
    char output[16]{};
    assert(!cyberdeck_netdiag::resolve_ipv4(nullptr, "v6.example", 10, output));
    reset();
    mode = dns_mode::immediate_failure;
    assert(!cyberdeck_netdiag::resolve_ipv4(nullptr, "error.example", 10, output));
    assert(!cyberdeck_netdiag::resolve_ipv4(nullptr, nullptr, 10, output));
    char too_long[255];
    std::memset(too_long, 'a', sizeof(too_long));
    too_long[254] = '\0';
    assert(!cyberdeck_netdiag::resolve_ipv4(nullptr, too_long, 10, output));
}
static void test_enqueue_failure_releases_slot() {
    reset();
    enqueue_fails = true;
    char output[16]{};
    assert(!cyberdeck_netdiag::resolve_ipv4(nullptr, "queue.example", 10, output));
    reset();
    assert(cyberdeck_netdiag::resolve_ipv4(nullptr, "after-queue.example", 10, output));
}
static void test_timeout_holds_context_until_late_callback() {
    reset();
    mode = dns_mode::pending;
    char first[16]{};
    std::thread worker([&] { assert(!cyberdeck_netdiag::resolve_ipv4(nullptr, "retained.example", 1, first)); });
    worker.join();
    assert(first[0] == '\0');
    char blocked[16]{};
    assert(!cyberdeck_netdiag::resolve_ipv4(nullptr, "must-not-overwrite.example", 20, blocked));
    assert(submitted_name == "retained.example");
    auto late = ipv4(203, 0, 113, 9);
    complete(&late);
    reset();
    char after[16]{};
    assert(cyberdeck_netdiag::resolve_ipv4(nullptr, "after-callback.example", 20, after));
    assert(std::strcmp(after, "192.0.2.44") == 0);
}
static void test_callback_timeout_boundary_is_safe_and_reusable() {
    reset();
    mode = dns_mode::pending;
    delay_entered = true;
    char result[16]{};
    std::thread resolver([&] { assert(cyberdeck_netdiag::resolve_ipv4(nullptr, "boundary.example", 1, result)); });
    {
        std::unique_lock lock(gate_mutex);
        assert(gate_cv.wait_for(lock, std::chrono::seconds(2), [] { return delay_count != 0; }));
    }
    auto boundary = ipv4(203, 0, 113, 7);
    complete(&boundary);
    {
        std::lock_guard lock(gate_mutex);
        release_delay = true;
    }
    gate_cv.notify_all();
    resolver.join();
    assert(std::strcmp(result, "203.0.113.7") == 0);
    reset();
    assert(cyberdeck_netdiag::resolve_ipv4(nullptr, "after-boundary.example", 10, result));
}

int main() {
    test_inline_and_cache_results();
    test_async_callbacks_and_invalid_results();
    test_enqueue_failure_releases_slot();
    test_timeout_holds_context_until_late_callback();
    test_callback_timeout_boundary_is_safe_and_reusable();
    std::puts("TEST-ND-02 DNS adapter behavioral tests passed");
}
