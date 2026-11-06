#include "apps/wifi/cyberdeck_wifi_console.h"

#include "apps/console/cyberdeck_console_args.h"

namespace cyberdeck_wifi_console {
namespace {

constexpr std::string_view kUnknownCommand = "unknown command; type help\n";
constexpr std::string_view kSurveyActive =
    "wifi survey is active; cancel it before scanning interactively\n";
constexpr std::string_view kScanning = "Scanning Wi-Fi networks...\n";
constexpr std::string_view kScanFailed = "Failed to start Wi-Fi scan.\n";
constexpr std::string_view kDisabled = "wifi: disabled\n";
constexpr std::string_view kNoSaved = "No saved Wi-Fi networks.\n";
constexpr std::string_view kSurveyRefused =
    "wifi survey start refused (another scan/session may be active)\n";
constexpr std::string_view kSurveyStarted =
    "wifi survey started; passive scans may briefly degrade Wi-Fi and SSH\n";
constexpr std::string_view kSurveyExportExpired =
    "wifi survey export confirmation unavailable or expired\n";

bool parse_duration(std::string_view value, std::uint32_t &seconds)
{
    if (value.empty()) return false;
    std::uint32_t parsed = 0;
    for (const char item : value) {
        if (item < '0' || item > '9') return false;
        parsed = parsed * 10U + static_cast<std::uint32_t>(item - '0');
        if (parsed > cyberdeck_wifi_survey::max_duration_seconds) return false;
    }
    if (parsed < cyberdeck_wifi_survey::min_duration_seconds) return false;
    seconds = parsed;
    return true;
}

} // namespace

std::string execute(port &io, std::string_view args, state &current)
{
    const std::string_view verb = cyberdeck_console::strip(args);

    if (verb.empty()) {
        const port::status status = io.current_status();
        if (!status.available) return "wifi: unavailable\n";
        std::string output = "wifi: ";
        output += status.enabled ? "enabled" : "disabled";
        output += status.connected ? " connected " : " disconnected";
        if (status.connected) output += status.ip;
        output += '\n';
        return output;
    }

    if (verb == "search") {
        switch (io.begin_search()) {
        case port::search_outcome::survey_active:
            return std::string(kSurveyActive);
        case port::search_outcome::disabled:
            return std::string(kDisabled);
        case port::search_outcome::scan_failed:
            return std::string(kScanning) + std::string(kScanFailed);
        case port::search_outcome::started:
            break;
        }
        return std::string(kScanning);
    }

    if (verb == "saved")
        return io.open_saved() == port::saved_outcome::opened ? std::string{} : std::string(kNoSaved);

    if (verb == "audit") {
        io.audit_begin();
        return {};
    }

    if (verb == "audit save")
        return io.audit_save() == port::save_outcome::requested ? "wifi audit save requested\n"
                                                               : "wifi audit save unavailable\n";

    if (verb == "survey") {
        switch (io.survey_start(current.survey_duration_seconds)) {
        case port::survey_start_outcome::disabled:
            return std::string(kDisabled);
        case port::survey_start_outcome::refused:
            return std::string(kSurveyRefused);
        case port::survey_start_outcome::started:
            break;
        }
        return std::string(kSurveyStarted);
    }

    if (verb == "survey cancel")
        return io.survey_cancel() == port::survey_action_outcome::applied
                   ? "wifi survey cancelled; late scan results will be discarded\n"
                   : "wifi survey is not active\n";

    if (verb == "survey export")
        return io.survey_export(false, true) == port::survey_action_outcome::applied
                   ? "wifi survey export pending; run 'wifi survey export confirm' to save masked identifiers\n"
                   : "wifi survey export unavailable\n";

    if (verb == "survey export unmasked")
        return io.survey_export(false, false) == port::survey_action_outcome::applied
                   ? "wifi survey export pending with identifying SSID/BSSID; run 'wifi survey export confirm unmasked' to save\n"
                   : "wifi survey export unavailable\n";

    if (verb == "survey export confirm")
        return io.survey_export(true, true) == port::survey_action_outcome::applied
                   ? "wifi survey export confirmed; SSID and BSSID are masked\n"
                   : std::string(kSurveyExportExpired);

    if (verb == "survey export confirm unmasked")
        return io.survey_export(true, false) == port::survey_action_outcome::applied
                   ? "wifi survey export confirmed with identifying SSID/BSSID\n"
                   : std::string(kSurveyExportExpired);

    if (verb.compare(0, 16, "survey duration ") == 0) {
        std::uint32_t seconds = 0;
        if (!parse_duration(verb.substr(16), seconds))
            return "wifi survey duration: expected 10..120 seconds\n";
        current.survey_duration_seconds = seconds;
        return "wifi survey duration set to " + std::to_string(seconds) + " seconds\n";
    }

    return std::string(kUnknownCommand);
}

} // namespace cyberdeck_wifi_console
