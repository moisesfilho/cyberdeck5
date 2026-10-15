#include "apps/network/cyberdeck_netdiag_dns.h"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "lwip/dns.h"
#include "lwip/tcpip.h"

using namespace cyberdeck_netdiag::dns;

namespace {
int checks = 0;
int failures = 0;
void check(bool condition, const char *message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

enum class resolver_mode { immediate_v4, immediate_error, inline_v6_callback, delayed };
struct queued_call {
    tcpip_callback_fn fn;
    void *arg;
};
struct dns_call {
    dns_found_callback fn;
    void *arg;
    ip_addr_t *out;
    std::string host;
    int family;
};
std::vector<std::string> events;
std::vector<queued_call> tcp_queue;
dns_call last_dns{};
err_t schedule_result = ERR_OK;
err_t resolver_result = ERR_INPROGRESS;
resolver_mode mode = resolver_mode::delayed;
int dns_calls = 0;
std::mutex fake_mutex;
std::condition_variable fake_changed;
bool callback_started = false;
bool callback_continue = false;
bool callback_finished = false;

ip_addr_t v4(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d) {
    ip_addr_t ip{};
    ip.type = IPADDR_TYPE_V4;
    ip.v4.bytes[0] = a;
    ip.v4.bytes[1] = b;
    ip.v4.bytes[2] = c;
    ip.v4.bytes[3] = d;
    return ip;
}
ip_addr_t v6() {
    ip_addr_t ip{};
    ip.type = IPADDR_TYPE_V6;
    for (int i = 0; i < 16; ++i)
        ip.v6.addr[i] = static_cast<std::uint8_t>(i + 1);
    return ip;
}
void reset_fake(resolver_mode next = resolver_mode::delayed) {
    events.clear();
    tcp_queue.clear();
    last_dns = {};
    dns_calls = 0;
    schedule_result = ERR_OK;
    resolver_result = ERR_INPROGRESS;
    mode = next;
    callback_started = false;
    callback_continue = false;
    callback_finished = false;
}
void run_tcpip() {
    check(tcp_queue.size() == 1, "exactly one tcpip callback queued");
    auto call = tcp_queue.front();
    tcp_queue.erase(tcp_queue.begin());
    events.emplace_back("trampoline");
    call.fn(call.arg);
}
void fire_dns(const ip_addr_t *address) {
    dns_found_callback callback;
    void *arg;
    std::string hostname;
    {
        std::lock_guard<std::mutex> lock(fake_mutex);
        check(last_dns.fn != nullptr, "DNS callback retained by resolver");
        callback = last_dns.fn;
        arg = last_dns.arg;
        hostname = last_dns.host;
        last_dns.fn = nullptr;
    }
    events.emplace_back("dns_callback");
    callback(hostname.c_str(), address, arg);
}

void fire_dns_concurrently(const ip_addr_t *address) {
    dns_found_callback callback;
    void *arg;
    std::string hostname;
    {
        std::lock_guard<std::mutex> lock(fake_mutex);
        callback = last_dns.fn;
        arg = last_dns.arg;
        hostname = last_dns.host;
    }
    {
        std::unique_lock<std::mutex> lock(fake_mutex);
        callback_started = true;
        fake_changed.notify_all();
        const bool released = fake_changed.wait_for(lock, std::chrono::seconds(2), [] { return callback_continue; });
        if (!released) {
            callback_finished = true;
            lock.unlock();
            fake_changed.notify_all();
            return;
        }
    }
    callback(hostname.c_str(), address, arg);
    {
        std::lock_guard<std::mutex> lock(fake_mutex);
        callback_finished = true;
    }
    fake_changed.notify_all();
}
void expect_v4(const result &got) {
    check(got.code == result_code::resolved && got.family == address_family::ipv4, "IPv4 result typed resolved");
    check(got.address[0] == 192 && got.address[1] == 0 && got.address[2] == 2 && got.address[3] == 9,
          "IPv4 bytes copied in network order");
    for (std::size_t i = 4; i < got.address.size(); ++i)
        check(got.address[i] == 0, "IPv4 tail remains zero");
}
} // namespace

err_t tcpip_callback(tcpip_callback_fn function, void *argument) {
    events.emplace_back("tcpip_callback");
    if (schedule_result != ERR_OK)
        return schedule_result;
    tcp_queue.push_back({function, argument});
    return ERR_OK;
}

err_t dns_gethostbyname_addrtype(const char *hostname, ip_addr_t *address, dns_found_callback callback, void *argument,
                                 int address_type) {
    events.emplace_back("dns_start");
    ++dns_calls;
    last_dns = {callback, argument, address, hostname == nullptr ? "" : hostname, address_type};
    if (mode == resolver_mode::immediate_v4) {
        *address = v4(192, 0, 2, 9);
        return ERR_OK;
    }
    if (mode == resolver_mode::immediate_error)
        return ERR_MEM;
    if (mode == resolver_mode::inline_v6_callback) {
        const ip_addr_t ip = v6();
        callback(hostname, &ip, argument);
        return ERR_INPROGRESS;
    }
    return resolver_result;
}

namespace {
void admission_and_inline_cases() {
    reset_fake(resolver_mode::immediate_v4);
    check(start("inline.example", 10) == start_result::accepted, "immediate resolver accepted");
    check(busy() && dns_calls == 0, "slot reserved before scheduled trampoline runs");
    check(events == std::vector<std::string>{"tcpip_callback"}, "schedule occurs after admission publication");
    check(start("other.example", 11) == start_result::busy, "single slot rejects competing lookup");
    run_tcpip();
    check(events == std::vector<std::string>{"tcpip_callback", "trampoline", "dns_start"}, "call order exact");
    check(last_dns.host == "inline.example" && last_dns.family == LWIP_DNS_ADDRTYPE_DEFAULT && last_dns.fn != nullptr,
          "resolver receives hostname, default family and callback");
    result out{};
    check(poll(10, out) == poll_state::complete, "immediate ERR_OK completes");
    expect_v4(out);
    check(!busy(), "completion consumption releases slot");

    reset_fake(resolver_mode::inline_v6_callback);
    check(start("callback.example", 12) == start_result::accepted, "inline callback lookup accepted");
    run_tcpip();
    check(dns_calls == 1 && events.size() == 3, "inline callback is invoked once");
    const auto duplicate_callback = last_dns.fn;
    check(duplicate_callback != nullptr && last_dns.arg == nullptr,
          "adapter callback context ownership is singleton and explicit null");
    const ip_addr_t duplicate = v4(203, 0, 113, 7);
    duplicate_callback(last_dns.host.c_str(), &duplicate, last_dns.arg);
    check(poll(12, out) == poll_state::complete, "inline DNS callback completes once despite INPROGRESS");
    check(out.code == result_code::resolved && out.family == address_family::ipv6, "IPv6 callback result typed");
    for (int i = 0; i < 16; ++i)
        check(out.address[i] == i + 1, "IPv6 bytes copied");
    check(poll(12, out) == poll_state::unknown_token, "consumed inline completion cannot replay");

    reset_fake(resolver_mode::immediate_error);
    start("error.example", 13);
    run_tcpip();
    check(poll(13, out) == poll_state::complete && out.code == result_code::failed &&
              out.family == address_family::none,
          "immediate resolver error yields typed failed completion");
}

void hostname_boundary_cases() {
    reset_fake();
    std::string label63(63, 'a');
    std::string label64(64, 'a');
    std::string hostname253 = label63 + "." + label63 + "." + label63 + "." + std::string(61, 'b');
    std::string hostname254 = label63 + "." + label63 + "." + label63 + "." + std::string(62, 'b');
    check(start(hostname253.c_str(), 80) == start_result::accepted, "adapter accepts maximum 253-byte hostname");
    check(start("blocked.example", 81) == start_result::busy, "maximum-length hostname reserves singleton slot");
    run_tcpip();
    result out{};
    check(cancel(80) && poll(80, out) == poll_state::cancelled,
          "boundary hostname request can be cancelled and consumed");
    fire_dns(nullptr);
    check(!busy(), "boundary cancellation releases after resolver callback");

    reset_fake();
    const std::vector<std::string> invalid_hostnames{hostname254, label64 + ".example", "-bad.example", "bad-.example",
                                                     "bad..example"};
    for (const std::string &invalid : invalid_hostnames) {
        check(start(invalid.c_str(), 82) == start_result::invalid_target,
              "adapter rejects invalid hostname boundary or label");
        check(!busy() && tcp_queue.empty(), "invalid hostname never reserves or schedules work");
    }
    check(!busy() && tcp_queue.empty() && dns_calls == 0,
          "invalid hostname never reserves adapter slot or schedules network work");
    reset_fake();
    check(start("example.com.", 85) == start_result::invalid_target, "adapter rejects DNS hostname with trailing dot");
    check(!busy() && tcp_queue.empty() && dns_calls == 0,
          "trailing-dot rejection has no slot or network scheduling side effect");
    check(start(nullptr, 83) == start_result::invalid_target && start("", 84) == start_result::invalid_target,
          "adapter rejects null and empty hostname");
}

void delayed_and_cancel_cases() {
    result out{};
    reset_fake();
    start("delayed.example", 20);
    check(poll(20, out) == poll_state::pending && dns_calls == 0, "poll before trampoline remains pending");
    run_tcpip();
    check(poll(20, out) == poll_state::pending && busy(), "ERR_INPROGRESS stays pending and holds slot");
    check(start("busy.example", 21) == start_result::busy, "active resolver excludes second query");
    const ip_addr_t success = v4(192, 0, 2, 9);
    fire_dns(&success);
    check(poll(20, out) == poll_state::complete, "delayed callback becomes available");
    expect_v4(out);
    check(!busy() && poll(20, out) == poll_state::unknown_token, "slot released only when completion consumed");

    reset_fake();
    start("cancel-queued.example", 30);
    check(cancel(30) && poll(30, out) == poll_state::cancelled && busy(),
          "queued cancellation is logical and quarantined");
    check(start("blocked.example", 31) == start_result::busy, "cancelled queued slot not reused before trampoline");
    run_tcpip();
    check(dns_calls == 0 && !busy(), "queued trampoline observes cancel and releases without DNS");

    reset_fake();
    start("cancel-running.example", 40);
    run_tcpip();
    check(cancel(40) && poll(40, out) == poll_state::cancelled && busy(),
          "running cancellation returns immediately but retains context");
    check(start("blocked.example", 41) == start_result::busy, "running cancelled resolver remains exclusive");
    const ip_addr_t late = v4(192, 0, 2, 9);
    fire_dns(&late);
    check(!busy() && poll(40, out) == poll_state::unknown_token,
          "late callback releases without publishing stale result");
    check(start("after-cancel.example", 42) == start_result::accepted,
          "query admitted after callback reaps cancelled context");
    run_tcpip();
    const ip_addr_t done = v4(192, 0, 2, 9);
    fire_dns(&done);
    check(poll(42, out) == poll_state::complete, "post-cancel lookup completes and releases singleton");
}

void omission_and_failure_cases() {
    result out{};
    reset_fake();
    schedule_result = ERR_MEM;
    check(start("schedule-fail.example", 60) == start_result::scheduling_failed, "tcpip scheduling failure typed");
    schedule_result = ERR_OK;
    check(!busy() && start("next.example", 61) == start_result::accepted,
          "schedule failure releases slot for next request");
    run_tcpip();
    check(poll(61, out) == poll_state::pending, "next request proceeds after scheduling failure");
    const ip_addr_t *no_address = nullptr;
    fire_dns(no_address);
    check(poll(61, out) == poll_state::complete && out.code == result_code::failed,
          "null DNS callback address yields typed failed result");

    reset_fake();
    start("omitted.example", 50);
    run_tcpip();
    check(poll(50, out) == poll_state::pending && busy(), "omitted callback leaves slot quarantined indefinitely");
    check(start("not-reused.example", 51) == start_result::busy, "omitted callback context is never reused");
}

void callback_poll_concurrency_cases() {
    result out{};
    reset_fake();
    check(start("threaded.example", 70) == start_result::accepted, "threaded callback lookup admitted");
    run_tcpip();

    const ip_addr_t answer = v4(192, 0, 2, 9);
    std::thread callback_thread(fire_dns_concurrently, &answer);
    {
        std::unique_lock<std::mutex> lock(fake_mutex);
        const bool entered = fake_changed.wait_for(lock, std::chrono::seconds(2), [] { return callback_started; });
        check(entered, "callback thread reaches deterministic gate");
    }

    check(poll(70, out) == poll_state::pending && busy(),
          "owner poll and busy retain unresolved slot before publication");
    check(start("before-callback.example", 71) == start_result::busy,
          "slot cannot be reused before callback publication");
    {
        std::lock_guard<std::mutex> lock(fake_mutex);
        callback_continue = true;
    }
    fake_changed.notify_all();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    poll_state state = poll_state::pending;
    while (std::chrono::steady_clock::now() < deadline) {
        state = poll(70, out);
        if (state != poll_state::pending)
            break;
        std::unique_lock<std::mutex> lock(fake_mutex);
        fake_changed.wait_until(lock, deadline, [] { return callback_finished; });
    }
    callback_thread.join();
    check(state == poll_state::complete, "callback/poll race eventually completes exactly once");
    expect_v4(out);
    check(poll(70, out) == poll_state::unknown_token && !busy(),
          "concurrent completion is consumed once and releases slot");
    check(start("after-callback.example", 72) == start_result::accepted,
          "slot is reusable only after callback and result consumption");
    run_tcpip();
    check(cancel(72), "follow-up lookup can be cancelled");
    check(poll(72, out) == poll_state::cancelled, "follow-up cancellation publishes logical cancelled result");
    fire_dns(&answer);
    check(poll(72, out) == poll_state::unknown_token && !busy(),
          "late callback after cancellation cannot publish stale result");
}

} // namespace

int main() {
    admission_and_inline_cases();
    hostname_boundary_cases();
    delayed_and_cancel_cases();
    callback_poll_concurrency_cases();
    omission_and_failure_cases();
    std::cout << "netdiag DNS runtime: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
