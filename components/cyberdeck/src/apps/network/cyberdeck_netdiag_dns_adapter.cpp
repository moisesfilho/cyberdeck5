#include "apps/network/cyberdeck_netdiag_dns_adapter.h"

#include <cstring>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/dns.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip_addr.h"
#include "lwip/tcpip.h"

namespace cyberdeck_netdiag {
namespace {

enum class slot_state : uint8_t { free, queued, pending, done };

struct dns_slot {
    slot_state state{slot_state::free};
    bool abandoned{};
    bool succeeded{};
    char hostname[254]{};
    char ipv4[16]{};
};

dns_slot s_slot;
portMUX_TYPE s_slot_mux = portMUX_INITIALIZER_UNLOCKED;

void finish_locked(bool succeeded, const char *ipv4) {
    if (s_slot.abandoned) {
        s_slot = {};
        return;
    }
    s_slot.succeeded = succeeded;
    if (succeeded && ipv4 != nullptr)
        std::memcpy(s_slot.ipv4, ipv4, sizeof(s_slot.ipv4));
    s_slot.state = slot_state::done;
}

bool address_to_ipv4(const ip_addr_t *address, char output[16]) {
    if (address == nullptr || !IP_IS_V4(address))
        return false;
    return ip4addr_ntoa_r(ip_2_ip4(address), output, 16) != nullptr;
}

void dns_found(const char *, const ip_addr_t *address, void *) {
    char ipv4[16]{};
    const bool valid = address_to_ipv4(address, ipv4);
    portENTER_CRITICAL(&s_slot_mux);
    if (s_slot.state == slot_state::pending || s_slot.state == slot_state::queued)
        finish_locked(valid, ipv4);
    portEXIT_CRITICAL(&s_slot_mux);
}

void start_dns(void *) {
    char hostname[sizeof(s_slot.hostname)]{};
    portENTER_CRITICAL(&s_slot_mux);
    if (s_slot.state != slot_state::queued) {
        portEXIT_CRITICAL(&s_slot_mux);
        return;
    }
    std::memcpy(hostname, s_slot.hostname, sizeof(hostname));
    s_slot.state = slot_state::pending;
    portEXIT_CRITICAL(&s_slot_mux);

    ip_addr_t address{};
    const err_t result = dns_gethostbyname_addrtype(hostname, &address, dns_found, nullptr, LWIP_DNS_ADDRTYPE_IPV4);
    if (result == ERR_INPROGRESS)
        return;

    char ipv4[16]{};
    const bool valid = result == ERR_OK && address_to_ipv4(&address, ipv4);
    portENTER_CRITICAL(&s_slot_mux);
    if (s_slot.state == slot_state::pending)
        finish_locked(valid, ipv4);
    portEXIT_CRITICAL(&s_slot_mux);
}

bool valid_hostname(const char *hostname, size_t *length) {
    if (hostname == nullptr || length == nullptr)
        return false;
    size_t size = 0;
    while (size < sizeof(s_slot.hostname) && hostname[size] != '\0')
        ++size;
    if (size == 0 || size >= sizeof(s_slot.hostname))
        return false;
    for (size_t i = 0; i < size; ++i) {
        const unsigned char c = static_cast<unsigned char>(hostname[i]);
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-'))
            return false;
    }
    *length = size;
    return true;
}

} // namespace

bool resolve_ipv4(void *, const char *hostname, uint64_t deadline_ms, char output_ipv4[16]) {
    size_t hostname_length = 0;
    if (output_ipv4 == nullptr || !valid_hostname(hostname, &hostname_length))
        return false;
    output_ipv4[0] = '\0';

    portENTER_CRITICAL(&s_slot_mux);
    if (s_slot.state != slot_state::free) {
        portEXIT_CRITICAL(&s_slot_mux);
        return false;
    }
    s_slot = {};
    s_slot.state = slot_state::queued;
    std::memcpy(s_slot.hostname, hostname, hostname_length);
    portEXIT_CRITICAL(&s_slot_mux);

    if (tcpip_callback(start_dns, nullptr) != ERR_OK) {
        portENTER_CRITICAL(&s_slot_mux);
        s_slot = {};
        portEXIT_CRITICAL(&s_slot_mux);
        return false;
    }

    for (;;) {
        bool complete = false;
        bool succeeded = false;
        char resolved[sizeof(s_slot.ipv4)]{};
        portENTER_CRITICAL(&s_slot_mux);
        if (s_slot.state == slot_state::done) {
            complete = true;
            succeeded = s_slot.succeeded;
            std::memcpy(resolved, s_slot.ipv4, sizeof(resolved));
            s_slot = {};
        }
        portEXIT_CRITICAL(&s_slot_mux);
        if (complete) {
            if (succeeded)
                std::memcpy(output_ipv4, resolved, sizeof(resolved));
            return succeeded;
        }

        const uint64_t now_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);
        if (now_ms >= deadline_ms) {
            portENTER_CRITICAL(&s_slot_mux);
            if (s_slot.state == slot_state::done) {
                complete = true;
                succeeded = s_slot.succeeded;
                std::memcpy(resolved, s_slot.ipv4, sizeof(resolved));
                s_slot = {};
            } else {
                s_slot.abandoned = true;
            }
            portEXIT_CRITICAL(&s_slot_mux);
            if (complete) {
                if (succeeded)
                    std::memcpy(output_ipv4, resolved, sizeof(resolved));
                return succeeded;
            }
            return false;
        }
        vTaskDelay(1);
    }
}

} // namespace cyberdeck_netdiag
