#include "apps/wifi/cyberdeck_wifi_survey_export_controller.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
using namespace cyberdeck_wifi_survey_export;

void require(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

struct fake_port final : port {
    admission_result next_admission{admission_result::accepted};
    int queued{0};
    int commits{0};
    std::uint32_t committed_token{0};

    admission_result admit(std::uint32_t, bool) override {
        if (next_admission != admission_result::accepted)
            return next_admission;
        if (queued == 1)
            return admission_result::full;
        ++queued;
        return admission_result::accepted;
    }

    void commit(std::uint32_t token) override {
        ++commits;
        committed_token = token;
    }
};

void confirmation_validation() {
    fake_port output;
    controller flow(output);
    require(flow.confirm(false, 0) == admission_result::missing_confirmation, "missing confirmation rejected");
    flow.request_confirmation(true, 10);
    require(flow.confirm(false, 10) == admission_result::missing_confirmation, "mode mismatch rejected");
    require(!flow.confirmation_pending(), "mode mismatch consumes confirmation");
    flow.request_confirmation(true, 10);
    require(flow.confirm(true, 11) == admission_result::expired, "expired confirmation rejected");
    require(!flow.confirmation_pending(), "expiry consumes confirmation");
    flow.request_confirmation(true, 10);
    require(flow.confirm(true, 10) == admission_result::accepted, "confirmation is valid at exact deadline");
    require(flow.confirm(false, 10) == admission_result::missing_confirmation, "duplicate confirmation rejected");
    require(output.commits == 1, "accepted confirmation committed exactly once");
}

void queue_and_rearm() {
    fake_port output;
    controller flow(output);
    flow.request_confirmation(false, 100);
    require(flow.confirm(false, 0) == admission_result::accepted, "first bounded queue slot accepted");
    flow.request_confirmation(false, 100);
    require(flow.confirm(false, 1) == admission_result::missing_confirmation,
            "active export cannot manufacture a second confirmation");
    require(output.commits == 1, "duplicate confirmation does not commit another job");
    require(flow.complete(output.committed_token, false) == completion_result::ignored,
            "completion before start is stale");
    require(flow.started(output.committed_token), "worker start admitted for current token");
    require(flow.complete(output.committed_token, false) == completion_result::failed,
            "failed persistence completes failed");
    require(!flow.busy(), "failed job releases controller slot");
    output.queued = 0;
    output.next_admission = admission_result::full;
    flow.request_confirmation(false, 100);
    require(flow.confirm(false, 2) == admission_result::full, "port queue-full failure is returned");
    require(output.commits == 1 && flow.confirmation_pending(), "queue-full does not commit and leaves retry armed");
    output.next_admission = admission_result::unavailable;
    require(flow.confirm(false, 3) == admission_result::unavailable, "unavailable worker admission is returned");
    require(output.commits == 1 && flow.confirmation_pending(),
            "failed admission does not commit and remains retryable");
    output.next_admission = admission_result::accepted;
    require(flow.confirm(false, 4) == admission_result::accepted, "controller rearms after queue failure");
    require(output.commits == 2, "rearmed admission commits exactly once");
}

void busy_request_preserves_active_confirmation_state() {
    fake_port output;
    controller flow(output);
    flow.request_confirmation(false, 100);
    require(flow.confirm(false, 1) == admission_result::accepted, "first export owns controller");
    const auto active_token = output.committed_token;
    require(flow.started(active_token), "accepted export becomes active");

    require(!flow.request_confirmation(true, 200), "busy export rejects another confirmation request");
    require(!flow.confirmation_pending() && flow.busy(),
            "busy request does not rearm confirmation or surrender active ownership");
    require(flow.confirm(true, 2) == admission_result::missing_confirmation,
            "rejected busy request cannot be confirmed as pending work");
    require(output.commits == 1 && output.committed_token == active_token,
            "busy request does not commit or replace the accepted token");
}

void lifecycle_and_ack() {
    fake_port output;
    controller flow(output);
    flow.request_confirmation(true, 100);
    require(flow.confirm(true, 5) == admission_result::accepted, "request accepted");
    const auto token = output.committed_token;
    require(!flow.started(token + 1), "wrong token cannot start accepted job");
    require(flow.started(token), "accepted request transitions to started");
    output.queued = 0;
    require(!flow.started(token), "duplicate start rejected");
    int acknowledgements = 0;
    auto publish = [&](std::uint32_t completion_token, bool persisted) {
        if (flow.complete(completion_token, persisted) == completion_result::published)
            ++acknowledgements;
    };
    publish(token + 1, true);
    require(acknowledgements == 0 && flow.busy(), "stale successful result emits no ACK and keeps active job");
    publish(token, false);
    require(acknowledgements == 0 && !flow.busy(), "failed persistence emits no success ACK and releases slot");

    flow.request_confirmation(false, 100);
    require(flow.confirm(false, 6) == admission_result::accepted, "new request accepted after failure");
    const auto next = output.committed_token;
    require(flow.started(next), "next request started");
    output.queued = 0;
    publish(next, true);
    require(acknowledgements == 1, "published result emits exactly one ACK");
    publish(next, true);
    require(acknowledgements == 1, "duplicate completion cannot duplicate ACK");
}

void rejected_and_stale_transitions() {
    fake_port output;
    controller flow(output);
    require(flow.confirm(false, 0) == admission_result::missing_confirmation, "missing confirmation cannot admit work");
    flow.request_confirmation(true, 10);
    require(flow.confirm(false, 10) == admission_result::missing_confirmation,
            "confirmation mode mismatch is rejected");
    flow.request_confirmation(false, 10);
    require(flow.confirm(false, 11) == admission_result::expired, "confirmation strictly after deadline expires");

    flow.request_confirmation(false, 20);
    output.next_admission = admission_result::unavailable;
    require(flow.confirm(false, 12) == admission_result::unavailable,
            "unavailable app-lifetime worker rejects admission");
    require(output.commits == 0 && flow.confirmation_pending() && !flow.busy(),
            "unavailable admission does not commit and confirmation can be retried");
    output.next_admission = admission_result::accepted;
    require(flow.confirm(false, 13) == admission_result::accepted, "confirmation rearms after unavailable admission");
    const auto accepted = output.committed_token;
    require(flow.started(accepted), "accepted request starts once");
    require(flow.complete(accepted + 1, true) == completion_result::ignored,
            "stale completion cannot publish after accepted work starts");
    require(flow.busy(), "stale completion retains active controller ownership");
    require(flow.complete(accepted, true) == completion_result::published,
            "current successful persistence publishes once");
    require(flow.complete(accepted, true) == completion_result::ignored,
            "completion retained/replayed across UI reinit cannot publish twice");
}

void admitted_terminal_failure_and_stale_handoff() {
    fake_port output;
    controller flow(output);
    flow.request_confirmation(true, 100);
    require(flow.confirm(true, 1) == admission_result::accepted, "failure-path export admitted");
    const auto token = output.committed_token;
    int failure_reports = 0;
    auto pump_failure = [&](std::uint32_t result_token) {
        if (flow.fail_admitted(result_token) == completion_result::failed)
            ++failure_reports;
    };
    pump_failure(token + 1);
    require(flow.busy() && failure_reports == 0,
            "stale terminal handoff neither releases active controller nor reports failure");
    pump_failure(token);
    require(!flow.busy() && failure_reports == 1,
            "pump reports current terminal failure and releases ownership exactly once");
    pump_failure(token);
    require(!flow.busy() && failure_reports == 1, "replayed failure handoff cannot report or release a second time");
}
} // namespace

int main() {
    confirmation_validation();
    queue_and_rearm();
    busy_request_preserves_active_confirmation_state();
    lifecycle_and_ack();
    rejected_and_stale_transitions();
    admitted_terminal_failure_and_stale_handoff();
    std::puts("wifi survey export controller: PASS");
}
