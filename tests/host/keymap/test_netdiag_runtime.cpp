#include "apps/network/cyberdeck_netdiag_core.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace cyberdeck_netdiag;

namespace {

int checks = 0;
int failures = 0;

void check(bool condition, const char *message) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct backend_spy {
    int submit_calls = 0;
    int cancel_calls = 0;
    bool accept = true;
    std::uint64_t last_token = 0;
    request last_request{};

    static bool submit(void *context, const request &work, std::uint64_t token) {
        auto &spy = *static_cast<backend_spy *>(context);
        ++spy.submit_calls;
        spy.last_token = token;
        spy.last_request = work;
        return spy.accept;
    }

    static void cancel(void *context, std::uint64_t token) {
        auto &spy = *static_cast<backend_spy *>(context);
        ++spy.cancel_calls;
        spy.last_token = token;
    }

    backend service() {
        return {this, submit, cancel};
    }
};

void parser_cases() {
    auto parsed = parse_command("netdiag dns example.com");
    check(parsed.failure == error::none && parsed.kind == operation::dns, "DNS accepted");
    check(std::string_view(parsed.work.target.data()) == "example.com", "DNS target retained");

    parsed = parse_command("netdiag tcp host.example 65535");
    check(parsed.failure == error::none && parsed.kind == operation::tcp, "TCP accepted");
    check(parsed.work.port == 65535 && std::string_view(parsed.work.target.data()) == "host.example",
          "TCP host and maximum port retained");

    parsed = parse_command("netdiag https https://example.com/path?q=1");
    check(parsed.failure == error::none && parsed.kind == operation::https, "HTTPS URL accepted");

    const std::string label63(63, 'a');
    const std::string label64(64, 'a');
    parsed = parse_command("netdiag dns " + label63 + ".example");
    check(parsed.failure == error::none, "DNS label length 63 accepted");
    parsed = parse_command("netdiag dns " + label64 + ".example");
    check(parsed.failure == error::invalid_target, "DNS label length 64 rejected");

    const std::string host253 = label63 + "." + label63 + "." + label63 + "." + std::string(61, 'a');
    const std::string host254 = label63 + "." + label63 + "." + label63 + "." + std::string(62, 'a');
    parsed = parse_command("netdiag dns " + host253);
    check(parsed.failure == error::none, "DNS hostname length 253 accepted");
    parsed = parse_command("netdiag dns " + host254);
    check(parsed.failure == error::invalid_target, "DNS hostname length 254 rejected");

    const std::vector<std::pair<std::string, error>> rejected{
        {"", error::invalid_command},
        {"netdiag dns ", error::invalid_target},
        {"netdiag dns -bad.example", error::invalid_target},
        {"netdiag dns example.com.", error::invalid_target},
        {"netdiag dns example.com/path", error::invalid_target},
        {"netdiag tcp host.example", error::invalid_target},
        {"netdiag tcp host.example 0", error::invalid_port},
        {"netdiag tcp host.example 65536", error::invalid_port},
        {"netdiag tcp host.example abc", error::invalid_port},
        {"netdiag https http://example.com", error::invalid_target},
        {"netdiag https https://user@example.com", error::invalid_target},
        {"netdiag https https://example.com:0", error::invalid_target},
        {"netdiag https https://example-.com", error::invalid_target},
        {"netdiag https https://example.com:abc", error::invalid_target},
        {"netdiag https https://:443", error::invalid_target},
        {"netdiag https https://example.com:443:80", error::invalid_target},
    };
    for (const auto &[command, expected] : rejected) {
        parsed = parse_command(command);
        if (parsed.failure != expected)
            std::cerr << "unexpected parser result for: " << command << '\n';
        check(parsed.failure == expected, "malformed command rejected with typed error");
    }

    const std::string oversized = "netdiag dns " + std::string(k_target_capacity + 1, 'a');
    check(parse_command(oversized).failure == error::invalid_target, "oversized target rejected");

    backend_spy spy;
    controller control;
    error failure = error::none;
    const auto invalid_label = parse_command("netdiag dns " + label64 + ".example");
    check(!control.admit(invalid_label, 1, true, spy.service(), failure) && failure == error::invalid_target,
          "invalid DNS label cannot be admitted");
    check(spy.submit_calls == 0, "invalid DNS label does not invoke backend");
    const auto invalid_authority = parse_command("netdiag https https://example.com:abc");
    check(!control.admit(invalid_authority, 1, true, spy.service(), failure) && failure == error::invalid_target,
          "malformed HTTPS authority cannot be admitted");
    check(spy.submit_calls == 0, "malformed HTTPS authority does not invoke backend");

    for (const char *command : {"netdiag status", "netdiag result", "netdiag cancel"}) {
        const auto non_work = parse_command(command);
        controller admission;
        error admission_error = error::none;
        backend_spy non_work_spy;
        check(non_work.failure == error::none && non_work.kind != operation::dns &&
                  !admission.admit(non_work, 1, true, non_work_spy.service(), admission_error) &&
                  admission_error == error::invalid_command,
              "status/result/cancel commands are not admissible work and return typed invalid_command");
        check(non_work_spy.submit_calls == 0 && admission.snapshot().status == state::idle,
              "non-work command admission has no backend or controller side effect");
    }
}

void admission_and_lifecycle_cases() {
    backend_spy spy;
    const backend service = spy.service();
    controller control;
    error failure = error::none;
    const auto dns = parse_command("netdiag dns example.com");

    check(control.snapshot().status == state::idle && !control.in_flight(), "initial state idle");
    check(!control.admit(dns, 10000, false, service, failure) && failure == error::grant_revoked,
          "grant denial rejects admission");
    check(spy.submit_calls == 0, "grant denial does not enqueue");

    const auto invalid = parse_command("netdiag tcp host.example 0");
    check(!control.admit(invalid, 10000, true, service, failure) && failure == error::invalid_port,
          "invalid request rejected before backend");
    check(spy.submit_calls == 0, "invalid request does not enqueue");

    check(control.admit(dns, 10000, true, service, failure), "valid request admitted");
    const std::uint64_t first_token = control.generation();
    check(spy.submit_calls == 1 && spy.last_token == first_token, "admission calls enqueue seam once");
    check(control.snapshot().status == state::queued, "admission publishes queued state");

    check(!control.admit(dns, 11000, true, service, failure) && failure == error::busy,
          "single-flight rejects second request");
    check(spy.submit_calls == 1, "busy request does not enqueue");
    check(control.mark_running(first_token), "current generation enters running");
    check(control.snapshot().status == state::running, "running state observable");
    check(control.cancel(service), "logical cancellation accepted");
    check(control.snapshot().status == state::cancelled && control.in_flight(),
          "cancelled slot remains held until callback");
    check(spy.cancel_calls == 1 && spy.last_token == first_token, "backend cancel called once for token");
    check(!control.cancel(service) && spy.cancel_calls == 1, "repeat cancellation is idempotently rejected");

    check(control.complete(first_token + 1, {state::complete, error::none, 1, 200}) == false,
          "stale generation completion rejected");
    check(control.snapshot().status == state::cancelled && control.in_flight(),
          "stale callback cannot release cancelled slot");
    check(control.complete(first_token, {state::complete, error::none, 17, 204}),
          "current callback releases cancelled slot");
    check(control.snapshot().status == state::cancelled, "late result cannot overwrite cancellation");
    check(!control.mark_running(first_token), "late running transition rejected");

    check(!control.admit(dns, 10999, true, service, failure) && failure == error::rate_limited,
          "rate limit applies after completion");
    check(control.admit(dns, 11000, true, service, failure), "request admitted at interval boundary");
    const std::uint64_t second_token = control.generation();
    check(second_token != first_token && spy.submit_calls == 2, "next generation admitted once");
    check(!control.complete(first_token, {state::failed, error::dns_failed, 3, 0}),
          "previous generation cannot complete new request");
    check(control.snapshot().status == state::queued, "old result does not replace new queued state");
    check(control.mark_running(second_token), "second generation enters running");
    check(control.complete(second_token, {state::complete, error::none, 23, 200}), "success completes");
    check(control.snapshot().status == state::complete && !control.in_flight(), "complete state released");

    controller failed_submit;
    backend_spy refusing;
    refusing.accept = false;
    check(!failed_submit.admit(dns, 1, true, refusing.service(), failure) && failure == error::backend_unavailable,
          "backend enqueue failure reported");
    check(failed_submit.snapshot().status == state::failed && !failed_submit.in_flight(),
          "failed enqueue leaves terminal failed state");
    check(refusing.submit_calls == 1, "failed enqueue attempted exactly once");
    backend_spy recovered;
    check(failed_submit.admit(dns, 1001, true, recovered.service(), failure),
          "failed submit does not consume the rate-limit interval");
    check(recovered.submit_calls == 1, "retry after failed submit reaches backend once");

    controller retry_after_failure;
    backend_spy succeeds;
    check(retry_after_failure.admit(dns, 0, true, succeeds.service(), failure), "initial monotonic time zero is valid");
    const auto zero_token = retry_after_failure.generation();
    check(retry_after_failure.complete(zero_token, {state::complete, error::none, 0, 204}),
          "zero-time request completes");
    check(!retry_after_failure.admit(dns, 999, true, succeeds.service(), failure) && failure == error::rate_limited,
          "999 ms after time zero is rate limited");
    check(retry_after_failure.admit(dns, 1000, true, succeeds.service(), failure), "exact 1000 ms boundary admits");
    const auto boundary_token = retry_after_failure.generation();
    check(retry_after_failure.complete(boundary_token, {state::complete, error::none, 0, 204}),
          "boundary request completes");
    const auto before_backward = retry_after_failure.snapshot();
    const int submits_before_backward = succeeds.submit_calls;
    check(!retry_after_failure.admit(dns, 999, true, succeeds.service(), failure) && failure == error::clock_regressed,
          "backward clock rejected distinctly");
    check(retry_after_failure.snapshot().status == before_backward.status &&
              retry_after_failure.generation() == boundary_token && succeeds.submit_calls == submits_before_backward,
          "backward clock rejection leaves controller state and backend unchanged");

    backend unavailable{};
    controller no_backend;
    check(!no_backend.admit(dns, 1, true, unavailable, failure) && failure == error::backend_unavailable,
          "missing backend rejected");
    check(no_backend.snapshot().status == state::idle, "missing backend does not publish queued work");
}

} // namespace

int main() {
    parser_cases();
    admission_and_lifecycle_cases();
    std::cout << "netdiag runtime: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
