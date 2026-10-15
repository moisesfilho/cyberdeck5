#include "apps/network/cyberdeck_netdiag_dns.h"

#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/tcpip.h"

namespace cyberdeck_netdiag::dns {
namespace {

enum class phase : std::uint8_t { free, queued, resolving, waiting, ready, cancelled_queued, cancelled_waiting };

struct slot {
    phase current = phase::free;
    std::uint64_t token = 0;
    std::uint64_t id = 0;
    char hostname[256]{};
    result answer{};
    bool resolver_active = false;
    bool callback_seen = false;
};

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
slot s_slot{};
std::uint64_t s_next_id = 0;

void publish_address(const ip_addr_t *address, result &out) {
    if (address == nullptr)
        return;
    if (IP_IS_V4(address)) {
        const ip4_addr_t *v4 = ip_2_ip4(address);
        out.family = address_family::ipv4;
        out.address[0] = ip4_addr1(v4);
        out.address[1] = ip4_addr2(v4);
        out.address[2] = ip4_addr3(v4);
        out.address[3] = ip4_addr4(v4);
        out.code = result_code::resolved;
    } else if (IP_IS_V6(address)) {
        const ip6_addr_t *v6 = ip_2_ip6(address);
        out.family = address_family::ipv6;
        std::memcpy(out.address.data(), v6->addr, out.address.size());
        out.code = result_code::resolved;
    }
}

void dns_found(const char *, const ip_addr_t *address, void *) {
    portENTER_CRITICAL(&s_mux);
    if (s_slot.current == phase::cancelled_waiting) {
        if (s_slot.resolver_active)
            s_slot.callback_seen = true;
        else
            s_slot = {};
    } else if (s_slot.current == phase::waiting || s_slot.current == phase::resolving) {
        s_slot.callback_seen = true;
        result answer{};
        publish_address(address, answer);
        s_slot.answer = answer;
        s_slot.current = phase::ready;
    }
    portEXIT_CRITICAL(&s_mux);
}

void resolve_in_tcpip_thread(void *) {
    std::uint64_t id = 0;
    portENTER_CRITICAL(&s_mux);
    if (s_slot.current == phase::cancelled_queued) {
        s_slot = {};
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    if (s_slot.current != phase::queued) {
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    s_slot.current = phase::resolving;
    id = s_slot.id;
    s_slot.resolver_active = true;
    portEXIT_CRITICAL(&s_mux);

    ip_addr_t address{};
    const err_t status =
        dns_gethostbyname_addrtype(s_slot.hostname, &address, dns_found, nullptr, LWIP_DNS_ADDRTYPE_DEFAULT);

    portENTER_CRITICAL(&s_mux);
    if (s_slot.id != id) {
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    s_slot.resolver_active = false;
    if (s_slot.current == phase::cancelled_waiting) {
        if (s_slot.callback_seen || status != ERR_INPROGRESS)
            s_slot = {};
    } else if (s_slot.current == phase::ready) {
        // Defensive only: lwIP returns ERR_OK with the address for literal/cache
        // hits; its callback is reserved for asynchronous completion.
    } else if (s_slot.current == phase::resolving) {
        if (status == ERR_OK) {
            result answer{};
            publish_address(&address, answer);
            s_slot.answer = answer;
            s_slot.current = phase::ready;
        } else if (status == ERR_INPROGRESS) {
            s_slot.current = phase::waiting;
        } else {
            s_slot.answer = {};
            s_slot.current = phase::ready;
        }
    }
    portEXIT_CRITICAL(&s_mux);
}

} // namespace

start_result start(const char *hostname, std::uint64_t token) {
    if (hostname == nullptr)
        return start_result::invalid_target;
    const std::size_t length = strnlen(hostname, 255);
    if (length == 0 || length > 253)
        return start_result::invalid_target;
    std::size_t label_length = 0;
    bool previous_hyphen = false;
    for (std::size_t i = 0; i < length; ++i) {
        const unsigned char c = static_cast<unsigned char>(hostname[i]);
        if (c == '.') {
            if (label_length == 0 || previous_hyphen)
                return start_result::invalid_target;
            label_length = 0;
            previous_hyphen = false;
            continue;
        }
        const bool alphanumeric = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if ((!alphanumeric && c != '-') || (label_length == 0 && c == '-') || ++label_length > 63)
            return start_result::invalid_target;
        previous_hyphen = c == '-';
    }
    if (label_length == 0 || previous_hyphen)
        return start_result::invalid_target;

    portENTER_CRITICAL(&s_mux);
    if (s_slot.current != phase::free) {
        portEXIT_CRITICAL(&s_mux);
        return start_result::busy;
    }
    s_slot = {};
    s_slot.current = phase::queued;
    s_slot.token = token;
    ++s_next_id;
    if (s_next_id == 0)
        ++s_next_id;
    s_slot.id = s_next_id;
    const std::uint64_t id = s_slot.id;
    std::memcpy(s_slot.hostname, hostname, length + 1);
    portEXIT_CRITICAL(&s_mux);

    const err_t scheduled = tcpip_callback(resolve_in_tcpip_thread, nullptr);
    if (scheduled == ERR_OK)
        return start_result::accepted;

    portENTER_CRITICAL(&s_mux);
    if (s_slot.id == id && (s_slot.current == phase::queued || s_slot.current == phase::cancelled_queued)) {
        s_slot = {};
        portEXIT_CRITICAL(&s_mux);
        return start_result::scheduling_failed;
    }
    portEXIT_CRITICAL(&s_mux);
    return start_result::accepted;
}

bool cancel(std::uint64_t token) {
    portENTER_CRITICAL(&s_mux);
    if (s_slot.current == phase::free || s_slot.token != token) {
        portEXIT_CRITICAL(&s_mux);
        return false;
    }
    if (s_slot.current == phase::ready) {
        if (s_slot.resolver_active)
            s_slot.current = phase::cancelled_waiting;
        else
            s_slot = {};
    } else if (s_slot.current == phase::queued) {
        s_slot.current = phase::cancelled_queued;
    } else if (s_slot.current == phase::resolving || s_slot.current == phase::waiting) {
        s_slot.current = phase::cancelled_waiting;
    } else {
        portEXIT_CRITICAL(&s_mux);
        return false;
    }
    portEXIT_CRITICAL(&s_mux);
    return true;
}

poll_state poll(std::uint64_t token, result &resolved) {
    portENTER_CRITICAL(&s_mux);
    if (s_slot.current == phase::free || s_slot.token != token) {
        portEXIT_CRITICAL(&s_mux);
        return poll_state::unknown_token;
    }
    if (s_slot.current == phase::ready && !s_slot.resolver_active) {
        resolved = s_slot.answer;
        s_slot = {};
        portEXIT_CRITICAL(&s_mux);
        return poll_state::complete;
    }
    if (s_slot.current == phase::cancelled_queued || s_slot.current == phase::cancelled_waiting) {
        portEXIT_CRITICAL(&s_mux);
        return poll_state::cancelled;
    }
    portEXIT_CRITICAL(&s_mux);
    return poll_state::pending;
}

bool busy() {
    portENTER_CRITICAL(&s_mux);
    const bool occupied = s_slot.current != phase::free;
    portEXIT_CRITICAL(&s_mux);
    return occupied;
}

} // namespace cyberdeck_netdiag::dns
