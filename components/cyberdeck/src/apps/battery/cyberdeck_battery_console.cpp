#include "apps/battery/cyberdeck_battery_console.h"

#include <cinttypes>
#include <cstdio>

#include "apps/console/cyberdeck_console_args.h"

namespace cyberdeck_battery_console {
namespace {

constexpr std::string_view kUnknownCommand = "unknown command; type help\n";

std::int64_t age_ms(std::int64_t sampled_at_us, const cyberdeck_battery_protection::hardware_diagnostics &diagnostic)
{
    return sampled_at_us > 0 && diagnostic.captured_at_us >= sampled_at_us
               ? (diagnostic.captured_at_us - sampled_at_us) / 1000
               : -1;
}

std::string status_text(port &io)
{
    if (!io.protection_started()) return "battery protection unavailable\n";

    cyberdeck_battery_protection::snapshot value{};
    if (!io.snapshot(value)) value.available = false;

    char buffer[256] = {};
    cyberdeck_battery_protection::format_status_line(buffer, sizeof(buffer), value);
    std::string output(buffer);

    cyberdeck_battery_protection::hardware_diagnostics diagnostic{};
    if (!io.hardware_diagnostics(diagnostic)) {
        output += "battery hardware: unavailable\n";
        return output;
    }

    char bus_raw[8] = "none";
    char current_raw[8] = "none";
    if (diagnostic.ina_available) {
        std::snprintf(bus_raw, sizeof(bus_raw), "0x%04X", diagnostic.ina_bus_raw);
        std::snprintf(current_raw, sizeof(current_raw), "0x%04X", diagnostic.ina_current_raw);
    }
    /* CHG_QC_EN is active-low: P5 LOW asserts QC. This label reports the latch
     * LEVEL (bit 5 of 0x05) so it cannot contradict expander_raw=05, while
     * chg_qc_enabled keeps its active-low meaning for the policy. */
    const char *chg_qc_latch = "unavailable";
    if (diagnostic.expander_available && diagnostic.expander_registers_valid)
        chg_qc_latch = (diagnostic.expander_output_latch & 0x20U) != 0 ? "high" : "low";

    char hardware[512] = {};
    std::snprintf(hardware, sizeof(hardware),
                  "battery hardware: expander=%s origin=0x%02X direction=%s latch=%s hiz=%s P5_P7_input_status=not_sampled CHG_QC_EN_latch=%s CHG_EN_latch=%s CHG_STAT_raw=%s INA226=%s bus_raw=%s current_raw=%s voltage_mv=%ld current_ma=%ld ina_age_ms=%" PRId64 " stat_age_ms=%" PRId64 " snapshot_age_ms=%" PRId64 " latch_is_not_pad_voltage; input_status_outputs_low; sequential_register_sample\n",
                  !diagnostic.expander_available ? "unavailable"
                                                 : !diagnostic.expander_registers_valid ? "unavailable" : "available",
                  diagnostic.expander_register_origin,
                  diagnostic.expander_registers_valid ? "read" : "unavailable",
                  diagnostic.expander_registers_valid ? "read" : "unavailable",
                  diagnostic.expander_registers_valid ? "read" : "unavailable", chg_qc_latch,
                  !diagnostic.expander_available
                      ? "unavailable"
                      : !diagnostic.expander_registers_valid ? "unavailable" : diagnostic.chg_en_enabled ? "high" : "low",
                  diagnostic.chg_stat_available ? (diagnostic.chg_stat_raw_low ? "low" : "high") : "unavailable",
                  diagnostic.ina_available ? "available" : diagnostic.ina_error ? "error" : "unavailable", bus_raw,
                  current_raw, static_cast<long>(diagnostic.ina_bus_voltage_mv),
                  static_cast<long>(diagnostic.ina_current_ma),
                  age_ms(diagnostic.ina_sampled_at_us, diagnostic),
                  age_ms(diagnostic.chg_stat_sampled_at_us, diagnostic),
                  age_ms(diagnostic.policy_sampled_at_us, diagnostic));

    if (diagnostic.expander_registers_valid) {
        char registers[80]{};
        std::snprintf(registers, sizeof(registers), "expander_raw: 03=%02X 05=%02X 07=%02X\n",
                      diagnostic.expander_direction, diagnostic.expander_output_latch, diagnostic.expander_hiz);
        output += registers;
    }
    output += hardware;
    return output;
}

} // namespace

std::string execute(port &io, std::string_view args)
{
    const std::string normalized = cyberdeck_console::collapse_spaces(cyberdeck_console::strip(args));
    const std::string_view verb(normalized);

    if (verb.compare(0, 10, "protection") == 0 && (verb.size() == 10 || verb[10] == ' ')) {
        const std::string_view action = verb.size() == 10 ? std::string_view{} : verb.substr(11);
        if (action == "on")
            return io.set_protection(true) ? "battery protection enabled\n" : "battery protection unavailable\n";
        if (action == "off")
            return io.set_protection(false) ? "battery protection disabled\n" : "battery protection unavailable\n";
        if (action == "status") return status_text(io);
    }
    return std::string(kUnknownCommand);
}

} // namespace cyberdeck_battery_console
