// Behavioral tests for the `battery` console application (pure layer + manifest).
// Links the production status formatter so the emitted line is the real one.

#include <cstdint>
#include <cstdio>
#include <string>

#include "apps/battery/cyberdeck_battery_console.h"
#include "apps/battery/cyberdeck_battery_console_app.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char *what)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

void check_eq(const std::string &actual, const std::string &expected, const char *what)
{
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::printf("FAIL: %s\n  expected: %s\n  actual:   %s\n", what, expected.c_str(), actual.c_str());
    }
}

bool contains(const std::string &text, const char *needle) { return text.find(needle) != std::string::npos; }

struct fake_port final : cyberdeck_battery_console::port {
    bool started = true;
    bool set_result = true;
    bool snapshot_ok = true;
    bool diagnostics_ok = true;
    int set_calls = 0;
    int snapshot_calls = 0;
    int diagnostic_calls = 0;
    bool last_enabled = false;
    cyberdeck_battery_protection::snapshot status{};
    cyberdeck_battery_protection::hardware_diagnostics hardware{};

    bool protection_started() override { return started; }
    bool set_protection(bool enabled) override
    {
        ++set_calls;
        last_enabled = enabled;
        return set_result;
    }
    bool snapshot(cyberdeck_battery_protection::snapshot &out) override
    {
        ++snapshot_calls;
        out = status;
        return snapshot_ok;
    }
    bool hardware_diagnostics(cyberdeck_battery_protection::hardware_diagnostics &out) override
    {
        ++diagnostic_calls;
        out = hardware;
        return diagnostics_ok;
    }
};

void test_on_off()
{
    fake_port port;
    check_eq(cyberdeck_battery_console::execute(port, " protection on"), "battery protection enabled\n",
             "protection on is acknowledged");
    check(port.set_calls == 1 && port.last_enabled, "protection on reaches the port enabled");

    check_eq(cyberdeck_battery_console::execute(port, " protection off"), "battery protection disabled\n",
             "protection off is acknowledged");
    check(port.set_calls == 2 && !port.last_enabled, "protection off reaches the port disabled");

    fake_port failing;
    failing.set_result = false;
    check_eq(cyberdeck_battery_console::execute(failing, " protection off"), "battery protection unavailable\n",
             "an unavailable adapter is reported");
}

void test_status_unavailable()
{
    fake_port port;
    port.started = false;
    check_eq(cyberdeck_battery_console::execute(port, " protection status"), "battery protection unavailable\n",
             "status refuses without a started policy");
    check(port.snapshot_calls == 0 && port.diagnostic_calls == 0,
          "status never samples hardware without a started policy");
}

void test_status_line_and_hardware()
{
    fake_port port;
    port.status.available = true;
    port.status.state = cyberdeck_battery_protection::battery_state::charging;
    port.status.charge = cyberdeck_battery_protection::charge_signal::charging;
    port.status.percentage = 77;
    port.status.bus_voltage_mv = 8010;
    port.status.current_ma = -380;
    port.status.protection_active = false;
    port.status.charger_enabled = false;

    port.hardware.captured_at_us = 2'000'000;
    port.hardware.policy_sampled_at_us = 1'500'000;
    port.hardware.expander_available = true;
    port.hardware.expander_registers_valid = true;
    port.hardware.expander_register_origin = 0x05;
    port.hardware.expander_direction = 0x03;
    port.hardware.expander_output_latch = 0x25;
    port.hardware.expander_hiz = 0x07;
    port.hardware.chg_stat_available = true;
    port.hardware.chg_stat_raw_low = true;
    port.hardware.ina_available = true;
    port.hardware.ina_bus_raw = 0x1F4A;
    port.hardware.ina_current_raw = 0xFE68;
    port.hardware.ina_bus_voltage_mv = 8010;
    port.hardware.ina_current_ma = -380;
    port.hardware.ina_sampled_at_us = 1'000'000;
    port.hardware.chg_stat_sampled_at_us = 1'500'000;

    const std::string output = cyberdeck_battery_console::execute(port, " protection status");
    check(contains(output, "battery: state=charging charge=charging available=true voltage_mv=8010 "
                           "current_ma=-380 percentage=77 protection=inactive charger=off\n"),
          "the production status line is emitted unchanged");
    check(contains(output, "battery hardware: expander=available origin=0x05 direction=read latch=read hiz=read"),
          "the hardware line reports the expander state");
    check(contains(output, "CHG_QC_EN_latch=high CHG_EN_latch=low CHG_STAT_raw=low INA226=available"),
          "the hardware line reports the latch and charger evidence");
    check(contains(output, "bus_raw=0x1F4A current_raw=0xFE68"), "raw INA registers are reported in hex");
    check(contains(output, "ina_age_ms=1000 stat_age_ms=500 snapshot_age_ms=500"),
          "cached signal and measurement ages are reported");
    check(contains(output, "latch_is_not_pad_voltage") && contains(output, "input_status_outputs_low") &&
              contains(output, "sequential_register_sample"),
          "register semantics and non-atomic sampling stay documented in the line");
    check(contains(output, "expander_raw: 03=03 05=25 07=07\n"), "the raw expander registers precede the line");
    check(port.snapshot_calls == 1 && port.diagnostic_calls == 1, "status samples each source exactly once");
}

void test_status_partial_diagnostics()
{
    fake_port invalid_registers;
    invalid_registers.hardware.expander_available = true;
    invalid_registers.hardware.expander_registers_valid = false;
    invalid_registers.hardware.ina_error = true;
    const std::string invalid = cyberdeck_battery_console::execute(invalid_registers, " protection status");
    check(contains(invalid, "CHG_QC_EN_latch=unavailable CHG_EN_latch=unavailable"),
          "a partial expander register read invalidates the whole raw group");
    check(contains(invalid, "bus_raw=none current_raw=none"),
          "a failed INA sample never exposes stale raw registers");
    check(!contains(invalid, "expander_raw:"), "an invalid register group prints no raw line");

    fake_port absent;
    absent.hardware.expander_available = false;
    absent.hardware.ina_available = false;
    const std::string output = cyberdeck_battery_console::execute(absent, " protection status");
    check(contains(output, "CHG_QC_EN_latch=unavailable CHG_EN_latch=unavailable CHG_STAT_raw=unavailable "
                           "INA226=unavailable"),
          "a missing expander and INA sample report unavailable, not pin errors");
}

void test_status_api_failure()
{
    fake_port port;
    port.diagnostics_ok = false;
    const std::string output = cyberdeck_battery_console::execute(port, " protection status");
    check(contains(output, "battery hardware: unavailable\n"),
          "a diagnostics API failure is reported without suppressing policy status");
    check(contains(output, "battery: state="), "the policy status line survives the failure");
    check(port.diagnostic_calls == 1, "the failure is not retried");

    fake_port no_snapshot;
    no_snapshot.snapshot_ok = false;
    const std::string snapshot_failure = cyberdeck_battery_console::execute(no_snapshot, " protection status");
    check(contains(snapshot_failure, "available=false"),
          "an unavailable snapshot is published as unavailable instead of stale data");
}

void test_unknown_and_whitespace()
{
    fake_port port;
    for (const char *unknown : {"", " on", " protection", " protection maybe", " charge on"}) {
        check_eq(cyberdeck_battery_console::execute(port, unknown), "unknown command; type help\n",
                 "an unsupported shape keeps the legacy unknown message");
    }
    check(port.set_calls == 0, "an unsupported shape reaches no port");

    check_eq(cyberdeck_battery_console::execute(port, "  protection\t\ton "), "battery protection enabled\n",
             "separators between tokens are normalized like the legacy parser");
}

void test_manifest_registration_shape()
{
    const cyberdeck_apps::manifest &manifest = cyberdeck_battery_console_app::global_application().get_manifest();
    check_eq(std::string(manifest.id), "cyberdeck.battery.shell", "the battery console owns its own id");
    check(manifest.command_count == 1 && manifest.commands[0] == "battery",
          "the manifest declares the battery command");
    check(manifest.resource_count == 1 && manifest.resources[0] == "battery", "only the battery grant is declared");
    check(!manifest.owns_console, "the battery console never owns the console");
    check(manifest.dependency_count == 0, "the battery console depends on no service");
    check(manifest.command_help_entries[0].usage == "battery [protection on|off|status]",
          "help keeps the approved usage");
}

} // namespace

int main()
{
    test_on_off();
    test_status_unavailable();
    test_status_line_and_hardware();
    test_status_partial_diagnostics();
    test_status_api_failure();
    test_unknown_and_whitespace();
    test_manifest_registration_shape();

    std::printf("test_battery_console: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
