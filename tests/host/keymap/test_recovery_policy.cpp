#include "apps/system/cyberdeck_recovery_policy.h"

#include <cstdio>
#include <string>

namespace {
int failures = 0;
#define CHECK(condition) do { if (!(condition)) { ++failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

void interrupted_boots_latch_after_three_attempts()
{
    cyberdeck_recovery::state value;
    CHECK(!cyberdeck_recovery::begin_boot(value));
    CHECK(value.boot_pending && value.interrupted_boots == 0);
    /* A boot that reaches the checkpoint is not an interruption. */
    CHECK(cyberdeck_recovery::commit_ready(value));
    CHECK(!value.boot_pending && value.interrupted_boots == 0);

    CHECK(!cyberdeck_recovery::begin_boot(value));
    CHECK(!cyberdeck_recovery::begin_boot(value));
    CHECK(!cyberdeck_recovery::begin_boot(value));
    CHECK(cyberdeck_recovery::begin_boot(value));
    CHECK(value.interrupted_boots == 3 && value.safe_mode_latched);
    CHECK(cyberdeck_recovery::begin_boot(value));
    CHECK(value.interrupted_boots == 4);
    CHECK(value.safe_mode_latched);
    CHECK(cyberdeck_recovery::commit_ready(value));
    CHECK(!value.boot_pending && value.interrupted_boots == 0);
    CHECK(value.safe_mode_latched); /* normal checkpoint does not clear latch */
}

void errors_are_per_app_and_bounded()
{
    cyberdeck_recovery::state value;
    CHECK(!cyberdeck_recovery::record_error(value, "", "bad"));
    CHECK(!cyberdeck_recovery::record_error(value, "app", std::string(cyberdeck_recovery::k_error_size, 'x')));
    CHECK(cyberdeck_recovery::record_error(value, "wifi", "first"));
    CHECK(cyberdeck_recovery::record_error(value, "wifi", "latest"));
    CHECK(cyberdeck_recovery::error_for(value, "wifi") == "latest");
    CHECK(cyberdeck_recovery::error_for(value, "missing").empty());
    for (std::size_t i = 0; i < cyberdeck_recovery::k_max_app_errors - 1; ++i) {
        CHECK(cyberdeck_recovery::record_error(value, "app" + std::to_string(i), "failure"));
    }
    CHECK(!cyberdeck_recovery::record_error(value, "overflow", "failure"));
}

void explicit_clear_is_the_only_policy_transition_out_of_safe_mode()
{
    cyberdeck_recovery::state value;
    value.safe_mode_latched = true;
    value.boot_pending = true;
    value.interrupted_boots = 3;
    CHECK(cyberdeck_recovery::begin_boot(value));
    CHECK(value.safe_mode_latched);
    CHECK(cyberdeck_recovery::clear_safe_mode(value));
    CHECK(!value.safe_mode_latched && !value.boot_pending && value.interrupted_boots == 0);
}
}

int main()
{
    interrupted_boots_latch_after_three_attempts();
    errors_are_per_app_and_bounded();
    explicit_clear_is_the_only_policy_transition_out_of_safe_mode();
    if (failures == 0) { std::puts("PASS: recovery policy TEST-10-01..04"); return 0; }
    std::printf("FAIL: %d recovery assertions\n", failures);
    return 1;
}
