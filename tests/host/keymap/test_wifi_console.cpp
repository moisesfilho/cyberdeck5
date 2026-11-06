// Behavioral tests for the `wifi` console application (pure layer + manifest).

#include <cstdint>
#include <cstdio>
#include <string>

#include "apps/wifi/cyberdeck_wifi_console.h"
#include "apps/wifi/cyberdeck_wifi_console_app.h"

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

struct fake_port final : cyberdeck_wifi_console::port {
    status current{};
    search_outcome search = search_outcome::started;
    saved_outcome saved = saved_outcome::opened;
    save_outcome save = save_outcome::requested;
    survey_start_outcome survey = survey_start_outcome::started;
    survey_action_outcome action = survey_action_outcome::applied;

    int status_calls = 0;
    int search_calls = 0;
    int saved_calls = 0;
    int audit_begin_calls = 0;
    int audit_save_calls = 0;
    int survey_start_calls = 0;
    std::uint32_t survey_seconds = 0;
    int cancel_calls = 0;
    int export_calls = 0;
    bool export_confirmed = false;
    bool export_masked = true;

    status current_status() override
    {
        ++status_calls;
        return current;
    }
    search_outcome begin_search() override
    {
        ++search_calls;
        return search;
    }
    saved_outcome open_saved() override
    {
        ++saved_calls;
        return saved;
    }
    void audit_begin() override { ++audit_begin_calls; }
    save_outcome audit_save() override
    {
        ++audit_save_calls;
        return save;
    }
    survey_start_outcome survey_start(std::uint32_t duration_seconds) override
    {
        ++survey_start_calls;
        survey_seconds = duration_seconds;
        return survey;
    }
    survey_action_outcome survey_cancel() override
    {
        ++cancel_calls;
        return action;
    }
    survey_action_outcome survey_export(bool confirm, bool masked) override
    {
        ++export_calls;
        export_confirmed = confirm;
        export_masked = masked;
        return action;
    }
};

struct fixture {
    fake_port port;
    cyberdeck_wifi_console::state state;
    std::string run(const char *args) { return cyberdeck_wifi_console::execute(port, args, state); }
};

void test_status()
{
    fixture f;
    f.port.current.available = true;
    f.port.current.enabled = true;
    f.port.current.connected = true;
    std::snprintf(f.port.current.ip, sizeof(f.port.current.ip), "%s", "10.0.0.5");
    check_eq(f.run(""), "wifi: enabled connected 10.0.0.5\n", "wifi reports the enabled, connected state");

    fixture disabled;
    disabled.port.current.available = true;
    disabled.port.current.enabled = false;
    check_eq(disabled.run(""), "wifi: disabled disconnected\n", "wifi reports a disabled radio");

    fixture disconnected;
    disconnected.port.current.available = true;
    disconnected.port.current.enabled = true;
    check_eq(disconnected.run(""), "wifi: enabled disconnected\n", "wifi reports no connection");

    fixture unavailable;
    check_eq(unavailable.run(""), "wifi: unavailable\n", "wifi reports an unavailable manager");
}

void test_search()
{
    fixture started;
    check_eq(started.run("search"), "Scanning Wi-Fi networks...\n", "a scan announces itself");
    check(started.port.search_calls == 1, "the scan is requested once");

    fixture survey_active;
    survey_active.port.search = fake_port::search_outcome::survey_active;
    check_eq(survey_active.run("search"),
             "wifi survey is active; cancel it before scanning interactively\n",
             "an active survey blocks the interactive scan");

    fixture disabled;
    disabled.port.search = fake_port::search_outcome::disabled;
    check_eq(disabled.run("search"), "wifi: disabled\n", "a disabled radio refuses the scan");

    fixture failed;
    failed.port.search = fake_port::search_outcome::scan_failed;
    check_eq(failed.run("search"), "Scanning Wi-Fi networks...\nFailed to start Wi-Fi scan.\n",
             "a refused scan keeps both legacy lines in order");
}

void test_saved_and_audit()
{
    fixture opened;
    check_eq(opened.run("saved"), "", "opening a non-empty saved list prints nothing");
    check(opened.port.saved_calls == 1, "the saved list is opened through the port");

    fixture empty;
    empty.port.saved = fake_port::saved_outcome::empty;
    check_eq(empty.run("saved"), "No saved Wi-Fi networks.\n", "an empty saved list is reported");

    fixture audit;
    check_eq(audit.run("audit"), "", "audit prints nothing itself");
    check(audit.port.audit_begin_calls == 1, "audit starts the controller once");

    fixture save;
    check_eq(save.run("audit save"), "wifi audit save requested\n", "a save request is acknowledged");
    fixture save_failed;
    save_failed.port.save = fake_port::save_outcome::unavailable;
    check_eq(save_failed.run("audit save"), "wifi audit save unavailable\n", "a refused save is reported");
}

void test_survey()
{
    fixture f;
    check_eq(f.run("survey"), "wifi survey started; passive scans may briefly degrade Wi-Fi and SSH\n",
             "the survey warns about connectivity impact");
    check(f.port.survey_seconds == 30, "the default duration is 30 seconds");

    (void)f.run("survey duration 10");
    (void)f.run("survey");
    check(f.port.survey_seconds == 10, "the configured duration is used by the next survey");

    (void)f.run("survey duration 120");
    (void)f.run("survey");
    check(f.port.survey_seconds == 120, "the maximum duration is accepted");

    check_eq(f.run("survey duration 9"), "wifi survey duration: expected 10..120 seconds\n",
             "a below-minimum duration is rejected");
    check_eq(f.run("survey duration 121"), "wifi survey duration: expected 10..120 seconds\n",
             "an above-maximum duration is rejected");
    check_eq(f.run("survey duration abc"), "wifi survey duration: expected 10..120 seconds\n",
             "a non-numeric duration is rejected");
    check_eq(f.run("survey duration 10x"), "wifi survey duration: expected 10..120 seconds\n",
             "a trailing-character duration is rejected");
    (void)f.run("survey");
    check(f.port.survey_seconds == 120, "a rejected duration never replaces the setting");

    fixture disabled;
    disabled.port.survey = fake_port::survey_start_outcome::disabled;
    check_eq(disabled.run("survey"), "wifi: disabled\n", "a disabled radio refuses the survey");

    fixture refused;
    refused.port.survey = fake_port::survey_start_outcome::refused;
    check_eq(refused.run("survey"), "wifi survey start refused (another scan/session may be active)\n",
             "a concurrent session reports the refusal");

    fixture cancel;
    check_eq(cancel.run("survey cancel"), "wifi survey cancelled; late scan results will be discarded\n",
             "cancel is acknowledged");
    fixture cancel_failed;
    cancel_failed.port.action = fake_port::survey_action_outcome::unavailable;
    check_eq(cancel_failed.run("survey cancel"), "wifi survey is not active\n",
             "cancel failure reports an inactive session");
}

void test_survey_export()
{
    fixture masked;
    check_eq(masked.run("survey export"),
             "wifi survey export pending; run 'wifi survey export confirm' to save masked identifiers\n",
             "the masked export only stages a confirmation");
    check(!masked.port.export_confirmed && masked.port.export_masked, "staging never confirms and stays masked");

    check_eq(masked.run("survey export confirm"), "wifi survey export confirmed; SSID and BSSID are masked\n",
             "the masked confirmation is explicit");
    check(masked.port.export_confirmed && masked.port.export_masked, "confirmation keeps the mask");

    check_eq(masked.run("survey export unmasked"),
             "wifi survey export pending with identifying SSID/BSSID; run 'wifi survey export confirm unmasked' to save\n",
             "the unmasked export requires its own confirmation");
    check(!masked.port.export_confirmed && !masked.port.export_masked, "the unmasked staging is not confirmed");

    check_eq(masked.run("survey export confirm unmasked"),
             "wifi survey export confirmed with identifying SSID/BSSID\n",
             "the unmasked confirmation is explicit");
    check(masked.port.export_confirmed && !masked.port.export_masked, "confirmation keeps the identifier opt-in");

    fixture unavailable;
    unavailable.port.action = fake_port::survey_action_outcome::unavailable;
    check_eq(unavailable.run("survey export"), "wifi survey export unavailable\n",
             "a busy export is not reported as pending");
    check_eq(unavailable.run("survey export unmasked"), "wifi survey export unavailable\n",
             "a busy unmasked export is not reported as pending");
    check_eq(unavailable.run("survey export confirm"),
             "wifi survey export confirmation unavailable or expired\n",
             "a failed confirmation reports expiry");
    check_eq(unavailable.run("survey export confirm unmasked"),
             "wifi survey export confirmation unavailable or expired\n",
             "a failed unmasked confirmation reports expiry");
}

void test_unknown_and_session_reset()
{
    fixture f;
    for (const char *unknown : {" nope", " survey nope", " search extra", " saved now", " audit save now",
                                " survey duration", " survey export extra"}) {
        check_eq(f.run(unknown), "unknown command; type help\n",
                 "an unsupported shape keeps the legacy unknown message");
    }
    check(f.port.search_calls == 0 && f.port.saved_calls == 0 && f.port.export_calls == 0,
          "an unsupported shape reaches no port");

    auto &app = cyberdeck_wifi_console_app::global_application();
    fake_port port;
    app.attach(port);
    (void)app.execute("wifi", " survey duration 15");
    (void)app.execute("wifi", " survey");
    check(port.survey_seconds == 15, "the application keeps the session duration");
    app.reset_session_state();
    (void)app.execute("wifi", " survey");
    check(port.survey_seconds == 30, "a new session restores the default duration");
}

void test_manifest_registration_shape()
{
    const cyberdeck_apps::manifest &manifest = cyberdeck_wifi_console_app::global_application().get_manifest();
    check_eq(std::string(manifest.id), "cyberdeck.wifi.shell", "the wifi console owns its own id");
    check(manifest.command_count == 1 && manifest.commands[0] == "wifi", "the manifest declares the wifi command");
    check(!manifest.owns_console, "the wifi console never owns the console");
    check(manifest.dependency_count == 0, "the wifi console depends on no service");
    check(manifest.command_help_entries[0].usage == "wifi [search|saved|audit|survey]",
          "help keeps the approved usage");
}

} // namespace

int main()
{
    test_status();
    test_search();
    test_saved_and_audit();
    test_survey();
    test_survey_export();
    test_unknown_and_session_reset();
    test_manifest_registration_shape();

    std::printf("test_wifi_console: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
