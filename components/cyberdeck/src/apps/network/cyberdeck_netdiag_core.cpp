#include "apps/network/cyberdeck_netdiag_core.h"

#include <cstring>

namespace cyberdeck_netdiag {
namespace {

constexpr std::uint64_t k_min_interval_ms = 1000;

bool valid_host(std::string_view host) {
    constexpr std::size_t k_max_hostname_length = 253;
    constexpr std::size_t k_max_label_length = 63;
    if (host.empty() || host.size() > k_max_hostname_length || host.front() == '.' || host.back() == '.')
        return false;
    std::size_t label_length = 0;
    bool previous_was_hyphen = false;
    for (const unsigned char c : host) {
        if (c == '.') {
            if (label_length == 0 || previous_was_hyphen)
                return false;
            label_length = 0;
            previous_was_hyphen = false;
            continue;
        }
        const bool is_alphanumeric = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!is_alphanumeric && c != '-')
            return false;
        if (label_length == 0 && c == '-')
            return false;
        if (++label_length > k_max_label_length)
            return false;
        previous_was_hyphen = c == '-';
    }
    return !previous_was_hyphen;
}

bool parse_port(std::string_view text, std::uint16_t &port) {
    if (text.empty() || text.size() > 5)
        return false;
    unsigned value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9')
            return false;
        value = value * 10u + static_cast<unsigned>(c - '0');
    }
    if (value == 0 || value > 65535)
        return false;
    port = static_cast<std::uint16_t>(value);
    return true;
}

bool valid_https_url(std::string_view url) {
    if (url.size() < 9 || url.size() > k_target_capacity || url.substr(0, 8) != "https://" ||
        url.find_first_of(" \t\r\n") != url.npos || url.find('#') != url.npos)
        return false;
    const std::size_t authority_start = 8;
    const std::size_t authority_end = url.find_first_of("/?", authority_start);
    const std::string_view authority =
        url.substr(authority_start, (authority_end == url.npos ? url.size() : authority_end) - authority_start);
    if (authority.empty() || authority.find('@') != authority.npos)
        return false;
    const std::size_t colon = authority.rfind(':');
    const std::string_view host = colon == authority.npos ? authority : authority.substr(0, colon);
    if (!valid_host(host))
        return false;
    if (colon == authority.npos)
        return true;
    std::uint16_t port = 0;
    return parse_port(authority.substr(colon + 1), port);
}

bool copy_target(request &work, std::string_view target) {
    if (target.empty() || target.size() > k_target_capacity)
        return false;
    std::memcpy(work.target.data(), target.data(), target.size());
    work.target[target.size()] = '\0';
    return true;
}

} // namespace

parsed_command parse_command(std::string_view command) {
    parsed_command parsed{};
    if (command == "netdiag status") {
        parsed.kind = operation::status;
        return parsed;
    }
    if (command == "netdiag result") {
        parsed.kind = operation::result;
        return parsed;
    }
    if (command == "netdiag cancel") {
        parsed.kind = operation::cancel;
        return parsed;
    }

    std::string_view target;
    if (command.substr(0, 12) == "netdiag dns ") {
        parsed.kind = operation::dns;
        target = command.substr(12);
        if (!valid_host(target))
            parsed.failure = error::invalid_target;
    } else if (command.substr(0, 12) == "netdiag tcp ") {
        parsed.kind = operation::tcp;
        const std::string_view args = command.substr(12);
        const std::size_t separator = args.find(' ');
        if (separator == args.npos || args.find(' ', separator + 1) != args.npos ||
            !valid_host(args.substr(0, separator))) {
            parsed.failure = error::invalid_target;
        } else {
            target = args.substr(0, separator);
            if (!parse_port(args.substr(separator + 1), parsed.work.port))
                parsed.failure = error::invalid_port;
        }
    } else if (command.substr(0, 14) == "netdiag https ") {
        parsed.kind = operation::https;
        target = command.substr(14);
        if (!valid_https_url(target))
            parsed.failure = error::invalid_target;
    } else {
        parsed.failure = error::invalid_command;
        return parsed;
    }
    parsed.work.kind = parsed.kind;
    if (parsed.failure == error::none && !copy_target(parsed.work, target))
        parsed.failure = error::invalid_target;
    return parsed;
}

bool controller::admit(const parsed_command &command, std::uint64_t now_ms, bool network_grant, const backend &service,
                       error &failure) {
    failure = command.failure;
    if (failure != error::none)
        return false;
    if (command.kind != operation::dns && command.kind != operation::tcp && command.kind != operation::https) {
        failure = error::invalid_command;
        return false;
    }
    if (!network_grant) {
        failure = error::grant_revoked;
        return false;
    }
    if (in_flight_) {
        failure = error::busy;
        return false;
    }
    if (has_admitted_ && now_ms < last_admitted_ms_) {
        failure = error::clock_regressed;
        return false;
    }
    if (has_admitted_ && now_ms - last_admitted_ms_ < k_min_interval_ms) {
        failure = error::rate_limited;
        return false;
    }
    if (service.submit == nullptr) {
        failure = error::backend_unavailable;
        return false;
    }

    ++generation_;
    if (generation_ == 0)
        ++generation_;
    outcome_ = {state::queued, error::none, 0, 0};
    cancelled_ = false;
    in_flight_ = true;
    if (!service.submit(service.context, command.work, generation_)) {
        in_flight_ = false;
        outcome_ = {state::failed, error::backend_unavailable, 0, 0};
        failure = error::backend_unavailable;
        return false;
    }
    has_admitted_ = true;
    last_admitted_ms_ = now_ms;
    return true;
}

bool controller::cancel(const backend &service) {
    if (!in_flight_ || cancelled_)
        return false;
    cancelled_ = true;
    outcome_ = {state::cancelled, error::cancelled, outcome_.duration_ms, outcome_.http_status};
    if (service.cancel != nullptr)
        service.cancel(service.context, generation_);
    return true;
}

bool controller::mark_running(std::uint64_t token) {
    if (!in_flight_ || cancelled_ || token != generation_)
        return false;
    outcome_.status = state::running;
    return true;
}

bool controller::complete(std::uint64_t token, outcome result) {
    if (!in_flight_ || token != generation_)
        return false;
    in_flight_ = false;
    if (!cancelled_)
        outcome_ = result;
    return true;
}

} // namespace cyberdeck_netdiag
