// Behavioral host tests for REQ-HELP-DETAILS / AC-1..AC-6 and the expanded
// REQ-HELP-05 / AC-1..AC-6: the `<command> help` contract for legacy terms,
// registered app commands and aliases, without executing either; unknown
// commands; syntax rejection; general help flags; poweroff isolation; normal
// dispatch preservation; and — REQ-HELP-05 — the usage-metadata contract:
// `edit help`, `demo help`, `netdiag help` and `music help` must reveal every usage format
// and subcommand the application declares, the compiled-in inventory (12 apps:
// 4 command-bearing, 7 commandless services, shell excluded) must be filtered
// correctly, aliases/collisions resolve without shadowing, and help never
// starts or executes an application.
//
// The session is LVGL-free and reaches the platform only through
// cyberdeck_shell_session::host, so it links on the host against the fake
// host below.  This exercises execute_line() as behavior.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "apps/demo/cyberdeck_demo_app.h"
#include "apps/shell/cyberdeck_shell_help.h"
#include "apps/shell/cyberdeck_shell_session.h"

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
        std::printf("FAIL: %s\n  expected: %s\n  actual:   %s\n",
                    what, expected.c_str(), actual.c_str());
    }
}

using cyberdeck_shell_session::key;
using cyberdeck_shell_session::session;
using cyberdeck_shell_session::wifi_ui_state_t;

class help_manifest_app final : public cyberdeck_apps::application {
public:
    explicit help_manifest_app(cyberdeck_apps::manifest manifest)
        : manifest_(std::move(manifest)) {}

    const cyberdeck_apps::manifest &get_manifest() const override { return manifest_; }
    bool start() override { running_ = true; return true; }
    bool stop() override { running_ = false; return true; }
    bool running() const override { return running_; }
    cyberdeck_apps::result execute(std::string_view command, std::string_view) override
    {
        ++executions;
        return {cyberdeck_apps::result_status::handled, std::string(command) + " handled\n"};
    }

    int executions = 0;

private:
    cyberdeck_apps::manifest manifest_;
    bool running_ = false;
};

struct fake_host final : cyberdeck_shell_session::host {
    std::string output;
    int render_count = 0;
    int clear_count = 0;

    cyberdeck_ble::state_machine ble;
    cyberdeck_ble::device_list scan_devices;
    int ble_notice_cleared = 0;
    int ble_uncommitted = 0;
    int ble_actions_submitted = 0;
    int ble_transient_synced = 0;
    std::vector<ble_bond_snapshot_t> bonds;

    wifi_ui_state_t wifi = wifi_ui_state_t::IDLE;
    cyberdeck_wifi::state_machine wifi_state_machine;
    cyberdeck_wifi_search_menu search_menu;
    cyberdeck_wifi_saved_menu saved_menu;
    std::uint64_t scan_generation = 0;
    bool scan_started = false;
    int scan_cancelled = 0;
    std::string audit_save_path;
    int audit_begin_calls = 0;
    std::string audit_saved_path;
    bool survey_active = false;
    bool survey_start_result = true;
    bool survey_cancel_result = true;
    bool survey_export_result = true;
    std::uint32_t survey_duration = 0;
    int survey_starts = 0;
    int survey_cancels = 0;
    int survey_exports = 0;
    bool survey_export_confirmed = false;
    bool survey_export_masked = true;

    std::string connected_ssid;
    std::string connected_password;
    std::string ssh_user;
    int ssh_port = 0;
    esp_err_t connect_result = ESP_OK;
    std::uint64_t manager_token = 7;
    int cancel_connection_calls = 0;
    int forget_calls = 0;
    std::string forgotten_ssid;
    bool enabled = true;
    bool status_ok = true;
    wifi_status_t status{};
    bool storage_ready = true;
    wifi_saved_list_t saved_list{};
    std::string stored_password;
    bool has_stored_password = false;

    cyberdeck_session_state phase = cyberdeck_session_state::MENU;
    std::string ssh_sent;
    std::string ssh_password;
    int host_key_accepted = 0;
    bool ssh_visible = false;
    int ssh_filter_resets = 0;
    int composer_discards = 0;
    cyberdeck_shell_session::host *self = nullptr;
    cyberdeck_ssh_line_composer composer;

    int screen_on = 0;
    int screen_off = 0;
    int screen_timeout = -1;
    esp_err_t screen_timeout_result = ESP_OK;

    bool battery_set_result = true;
    bool battery_started = true;
    bool battery_snapshot_ok = true;
    cyberdeck_battery_protection::snapshot battery_snapshot{};

    std::vector<std::string> events;
    std::string recent;
    std::size_t recent_requested = 0;
    int poweroff_admissions = 0;
    cyberdeck_poweroff::admission_result poweroff_admission = cyberdeck_poweroff::admission_result::admitted;
    cyberdeck_poweroff::admission_result admit_poweroff() override {
        ++poweroff_admissions;
        return poweroff_admission;
    }

    bool cat_accepted = true;
    std::string cat_cwd;
    std::string cat_line;
    cyberdeck_sd_io::coordinator sd_io;
    cyberdeck_local_shell shell{std::string("/tmp/opencode/help-harness")};
    cyberdeck_apps::runtime apps;

    fake_host() { shell.bind_sd_io(sd_io); }

    void append_output_line(const std::string &line) override { output += line; }
    void write_output(const char *data, std::size_t length) override
    {
        if (data != nullptr) output.append(data, length);
    }
    void append_output_text(const std::string &text) override { output += text; }
    void clear_output() override
    {
        ++clear_count;
        output.clear();
    }
    void render() override { ++render_count; }

    cyberdeck_ble::state_machine &ble_model() override { return ble; }
    cyberdeck_ble::device_list &ble_scan_devices() override { return scan_devices; }
    void clear_ble_notice() override { ++ble_notice_cleared; }
    void mark_ble_transient_uncommitted() override { ++ble_uncommitted; }
    void submit_ble_actions() override { ++ble_actions_submitted; }
    void sync_ble_transient() override { ++ble_transient_synced; }
    std::size_t copy_ble_bonds(ble_bond_snapshot_t *out, std::size_t capacity) override
    {
        const std::size_t count = bonds.size() < capacity ? bonds.size() : capacity;
        for (std::size_t i = 0; i < count; ++i) out[i] = bonds[i];
        return count;
    }

    wifi_ui_state_t &wifi_state() override { return wifi; }
    cyberdeck_wifi::state_machine &wifi_model() override { return wifi_state_machine; }
    cyberdeck_wifi_search_menu &wifi_search_menu() override { return search_menu; }
    cyberdeck_wifi_saved_menu &wifi_saved_menu() override { return saved_menu; }
    std::uint64_t &wifi_scan_generation() override { return scan_generation; }
    bool wifi_begin_scan(std::uint64_t generation) override
    {
        scan_generation = generation;
        scan_started = true;
        return true;
    }
    void wifi_cancel_scan() override { ++scan_cancelled; }
    std::string build_wifi_audit_save_path() override { return audit_save_path; }
    void wifi_audit_begin() override { ++audit_begin_calls; }
    bool wifi_audit_save(const std::string &path) override
    {
        audit_saved_path = path;
        return !path.empty();
    }
    bool wifi_survey_active() const override { return survey_active; }
    bool wifi_survey_start(std::uint32_t duration) override
    {
        ++survey_starts;
        survey_duration = duration;
        return survey_start_result;
    }
    bool wifi_survey_cancel() override { ++survey_cancels; return survey_cancel_result; }
    bool wifi_survey_export(bool confirm, bool masked) override
    {
        ++survey_exports;
        survey_export_confirmed = confirm;
        survey_export_masked = masked;
        return survey_export_result;
    }

    esp_err_t wifi_connect(const char *ssid, const char *password) override
    {
        connected_ssid = ssid != nullptr ? ssid : "";
        connected_password = password != nullptr ? password : "";
        return connect_result;
    }
    esp_err_t wifi_cancel_connection() override
    {
        ++cancel_connection_calls;
        return ESP_OK;
    }
    void wifi_forget(const char *ssid) override
    {
        ++forget_calls;
        forgotten_ssid = ssid != nullptr ? ssid : "";
    }
    bool wifi_enabled() const override { return enabled; }
    std::uint64_t wifi_current_token() const override { return manager_token; }
    bool wifi_status(wifi_status_t *out) override
    {
        if (!status_ok || out == nullptr) return false;
        *out = status;
        return true;
    }
    bool wifi_storage_ready() override { return storage_ready; }
    bool wifi_storage_load_all(wifi_saved_list_t *list) override
    {
        if (list == nullptr) return false;
        *list = saved_list;
        return true;
    }
    bool wifi_storage_find(const char *ssid, char *out_password, std::size_t max_len) override
    {
        if (!has_stored_password || ssid == nullptr || out_password == nullptr || max_len == 0)
            return false;
        const std::size_t n = stored_password.size() < max_len - 1
                             ? stored_password.size() : max_len - 1;
        memcpy(out_password, stored_password.data(), n);
        out_password[n] = '\0';
        return true;
    }

    cyberdeck_session_state ssh_phase() const override { return phase; }
    esp_err_t ssh_send_data(const char *data, std::size_t length) override
    {
        if (data != nullptr) ssh_sent.append(data, length);
        return ESP_OK;
    }
    esp_err_t ssh_send_password(const char *password) override
    {
        ssh_password = password != nullptr ? password : "";
        return ESP_OK;
    }
    void ssh_accept_host_key() override { ++host_key_accepted; }
    void set_ssh_visible(bool visible) override { ssh_visible = visible; }
    void reset_ssh_filter() override { ++ssh_filter_resets; }
    void discard_ssh_composer() override { ++composer_discards; }
    cyberdeck_ssh_line_composer &ssh_composer() override { return composer; }
    esp_err_t ssh_connect(const char *user, const char *host, int port) override
    {
        ssh_user = user != nullptr ? user : "";
        connected_ssid = host != nullptr ? host : "";
        ssh_port = port;
        return ESP_OK;
    }

    void screen_turn_on() override { ++screen_on; }
    void screen_turn_off() override { ++screen_off; }
    esp_err_t screen_set_timeout_minutes(std::uint16_t minutes) override
    {
        screen_timeout = minutes;
        return screen_timeout_result;
    }

    bool battery_protection_set_enabled(bool enabled) override
    {
        (void)enabled;
        return battery_set_result;
    }
    bool battery_protection_started() const override { return battery_started; }
    bool battery_protection_snapshot(cyberdeck_battery_protection::snapshot *out) const override
    {
        if (!battery_snapshot_ok) return false;
        if (out != nullptr) *out = battery_snapshot;
        return true;
    }

    void log_event(char level, const char *tag, const char *message) override
    {
        std::string entry;
        entry.push_back(level);
        entry.push_back(' ');
        entry += tag != nullptr ? tag : "";
        entry.push_back(' ');
        entry += message != nullptr ? message : "";
        events.push_back(entry);
    }
    std::string recent_events(std::size_t count) override
    {
        recent_requested = count;
        return recent;
    }

    bool cat_enqueue(const char *cwd, const char *line) override
    {
        cat_cwd = cwd != nullptr ? cwd : "";
        cat_line = line != nullptr ? line : "";
        return cat_accepted;
    }
    cyberdeck_local_shell &local_shell() override { return shell; }
    cyberdeck_apps::runtime &app_runtime() override { return apps; }
};

void submit(session &s, const std::string &text)
{
    s.insert_physical_text(text.data(), text.size());
    s.handle_key(key::enter);
}

void check_contains(const std::string &haystack, const std::string &needle, const char *what)
{
    ++g_checks;
    if (haystack.find(needle) == std::string::npos) {
        ++g_failures;
        std::printf("FAIL: %s\n  missing: %s\n", what, needle.c_str());
    }
}

/* REQ-HELP-05: a help query is read-only.  None of the platform ports may
 * fire while rendering metadata. */
void check_no_side_effects(const fake_host &h, const char *what)
{
    check(!h.scan_started && h.survey_starts == 0 && h.cat_line.empty() && h.poweroff_admissions == 0 &&
              h.screen_on == 0 && h.screen_off == 0 && h.audit_begin_calls == 0 && h.forget_calls == 0 &&
              h.ssh_sent.empty() && h.ssh_password.empty() && h.host_key_accepted == 0,
          what);
}

/* Literal mirrors of the production usage-metadata declarations in
 * cyberdeck_editor_app.cpp and cyberdeck_netdiag_app.cpp.  The goldens below
 * are hand-written and never derived from production formatting code, so a
 * manifest drift or a dropped help entry shows up against an independent
 * expectation instead of echoing production back to itself. */
cyberdeck_apps::manifest editor_usage_manifest()
{
    cyberdeck_apps::manifest item{};
    item.id = "cyberdeck.editor";
    item.name = "Text editor";
    item.version = "1.0.0";
    item.description = "Bounded UTF text editor";
    item.type = cyberdeck_apps::app_type::foreground;
    item.commands[0] = "edit";
    item.command_count = 1;
    item.command_help_entries[0] = {"edit <path>", "path: up to 256 bytes; a missing file may be created\n"};
    item.command_help_entries[1] = {"edit save", "save the current document; requires an assigned path\n"};
    item.command_help_entries[2] = {"edit save as <path>", "path: 1..256 bytes; requests confirmation before saving\n"};
    item.command_help_entries[3] = {"edit save as confirm", "confirm the pending save-as request\n"};
    return item;
}

cyberdeck_apps::manifest netdiag_usage_manifest()
{
    cyberdeck_apps::manifest item{};
    item.id = "cyberdeck.netdiag";
    item.name = "Network diagnostics";
    item.version = "1.0.0";
    item.description = "Explicit-target connectivity diagnostics";
    item.type = cyberdeck_apps::app_type::foreground;
    item.commands[0] = "netdiag";
    item.command_count = 1;
    item.command_help_entries[0] = {
        "netdiag [status|dns <host>|tcp <host> <port>|https <https-url>|result|cancel]",
        "status: show Wi-Fi interface status\n"
        "dns <host>: resolve an explicit host\n"
        "tcp <host> <port>: test a TCP connection\n"
        "https <https-url>: issue an HTTPS HEAD request\n"
        "result: report the current operation; cancel: request cancellation\n"
        "host: 1..253 ASCII letters, digits, dots, or hyphens; port: decimal 1..65535\n"
        "URL: https, at most 512 bytes; no userinfo, fragment, whitespace, or explicit port in authority\n"};
    return item;
}

cyberdeck_apps::manifest music_usage_manifest()
{
    cyberdeck_apps::manifest item{};
    item.id = "cyberdeck.music";
    item.name = "Music";
    item.version = "1.0.0";
    item.description = "Background local MP3 player";
    item.type = cyberdeck_apps::app_type::background;
    item.commands[0] = "music";
    item.command_count = 1;
    item.command_help_entries[0] = {
        "music queue <directory> | list | play [index] | pause | resume | stop | next | prev | seek <seconds> | volume <0-50> | shuffle | repeat | status",
        "MP3 playback from SD; volume is capped at 50\n"};
    return item;
}

cyberdeck_apps::manifest commandless_manifest(const char *id, const char *name, const char *description,
                                              cyberdeck_apps::app_type type = cyberdeck_apps::app_type::service)
{
    cyberdeck_apps::manifest item{};
    item.id = id;
    item.name = name;
    item.version = "1.0.0";
    item.description = description;
    item.type = type;
    return item;
}

} // namespace

int main()
{
    /* --- AC-1: every legacy term resolves to its own help block ------------ */
    for (const auto &item : cyberdeck_shell_help::kCatalog) {
        fake_host h;
        session s(h);
        std::string request(item.command);
        request += " help";
        submit(s, request);
        const std::string expected_echo = "/$ " + request + "\n";
        check(h.output.compare(0, expected_echo.size(), expected_echo) == 0,
              "legacy <command> help keeps its local terminal echo");
        check_eq(h.output, expected_echo + cyberdeck_shell_help::command_text(std::string(item.command).c_str()),
                 "legacy <command> help prints exactly that command's help block");
    }

    /* --- AC-1 boundary: -h/--help is NOT a detail-help token ---------------
     * The detail-help parser only accepts the literal keyword "help".
     * "<command> -h/--help" falls through to normal dispatch; the local
     * shell handles -h/--help for its own commands (ls, cd, etc.), and
     * other commands reject it as unknown. */
    for (const auto &item : cyberdeck_shell_help::kCatalog) {
        for (const char *flag : {"-h", "--help"}) {
            fake_host h;
            session s(h);
            std::string request(item.command);
            request += ' ';
            request += flag;
            submit(s, request);
            /* The echo is always present, but the detail-help block is NOT
             * printed: -h/--help is not a detail-help keyword. */
            const std::string expected_echo = "/$ " + request + "\n";
            check(h.output.compare(0, expected_echo.size(), expected_echo) == 0,
                  "legacy <command> -h/--help keeps its local terminal echo");
            check(h.output.find("no details available") == std::string::npos,
                  "legacy <command> -h/--help is not treated as a detail-help request");
        }
    }

    /* --- AC-2: registered app command and alias resolve without action --- */
    {
        fake_host h;
        cyberdeck_apps::manifest editor_manifest{};
        editor_manifest.id = "cyberdeck.editor";
        editor_manifest.name = "Text Editor";
        editor_manifest.command = "edit";
        editor_manifest.description = "edit files";
        help_manifest_app editor(editor_manifest);
        cyberdeck_apps::manifest demo_manifest{};
        demo_manifest.id = "cyberdeck.demo";
        demo_manifest.name = "Demo App";
        demo_manifest.type = cyberdeck_apps::app_type::demo;
        demo_manifest.command = "demo";
        demo_manifest.commands[0] = "demo-tool";
        demo_manifest.command_count = 1;
        help_manifest_app demo(demo_manifest);
        check(h.apps.register_application(editor), "editor manifest registers");
        check(h.apps.register_application(demo), "demo manifest registers");
        session s(h);

        submit(s, "edit help");
        const std::string edit_expected =
            "/$ edit help\n"
            "edit [app]\n"
            "  Text Editor (cyberdeck.editor)\n"
            "  edit files\n";
        check_eq(h.output, edit_expected,
                 "app command help prints name, id and description without version");
        check(editor.executions == 0, "app command help does not execute the application");
        check(h.apps.state("cyberdeck.editor") == cyberdeck_apps::app_state::registered,
              "app command help leaves the application stopped");

        h.output.clear();
        submit(s, "demo help");
        const std::string demo_expected =
            "/$ demo help\n"
            "demo [app]\n"
            "  Demo App (cyberdeck.demo)\n";
        check_eq(h.output, demo_expected,
                 "app command help omits empty description");
        check(demo.executions == 0, "app command help does not execute the application");

        h.output.clear();
        submit(s, "demo-tool help");
        const std::string alias_expected =
            "/$ demo-tool help\n"
            "demo-tool [app]\n"
            "  Demo App (cyberdeck.demo)\n";
        check_eq(h.output, alias_expected,
                 "declared alias help resolves to the owning application manifest");
        check(demo.executions == 0, "alias help does not execute the application");
    }

    /* --- AC-2: version is shown when present -------------------------------- */
    {
        fake_host h;
        cyberdeck_apps::manifest versioned_manifest{};
        versioned_manifest.id = "cyberdeck.versioned";
        versioned_manifest.name = "Versioned App";
        versioned_manifest.version = "2.1.0";
        versioned_manifest.command = "versioned";
        versioned_manifest.description = "has version";
        help_manifest_app versioned(versioned_manifest);
        check(h.apps.register_application(versioned), "versioned manifest registers");
        session s(h);

        submit(s, "versioned help");
        const std::string expected =
            "/$ versioned help\n"
            "versioned [app]\n"
            "  Versioned App (cyberdeck.versioned) version 2.1.0\n"
            "  has version\n";
        check_eq(h.output, expected,
                 "app command help includes version when present");
        check(versioned.executions == 0, "versioned app help does not execute");
    }

    /* --- AC-3: unknown command produces explicit error --------------------- */
    for (const char *unknown : {"nonexistent", "frobnicate", "not-a-cmd"}) {
        fake_host h;
        session s(h);
        std::string request(unknown);
        request += " help";
        submit(s, request);
        const std::string expected_echo = "/$ " + request + "\n";
        const std::string expected = expected_echo +
            "help: no details available for command '" + unknown + "'\n";
        check_eq(h.output, expected,
                 "unknown <command> help produces explicit no-details error");
    }

    /* --- AC-3: unknown command via -h falls to normal dispatch -------------- */
    for (const char *unknown : {"nonexistent", "frobnicate"}) {
        fake_host h;
        session s(h);
        std::string request(unknown);
        request += " -h";
        submit(s, request);
        /* -h is not a detail-help keyword, so the line falls through to
         * normal dispatch and reports an unknown command. */
        check(h.output.find("unknown command") != std::string::npos,
              "unknown <command> -h falls through to normal dispatch");
        check(h.output.find("no details available") == std::string::npos,
              "unknown <command> -h is not treated as a detail-help request");
    }

    /* --- AC-4: syntax rejection -------------------------------------------- */
    {
        fake_host h;
        session s(h);
        submit(s, "ls help extra");
        check(h.output.find("no details available") == std::string::npos,
              "<command> help with trailing token is not a detail request");
        check(h.output.find("unknown command") == std::string::npos,
              "<command> help extra is not treated as unknown command");
        check(h.output.find("ls help extra") != std::string::npos,
              "<command> help extra keeps its echo and falls through to local dispatch");
    }
    {
        fake_host h;
        session s(h);
        submit(s, "poweroff -x");
        check(h.output.find("no details available") == std::string::npos,
              "unknown flag is not treated as help");
        check(h.output.find("poweroff -x") != std::string::npos,
              "unknown flag keeps its echo and falls through to normal dispatch");
    }
    {
        fake_host h;
        session s(h);
        submit(s, "ls -x");
        check(h.output.find("ls -x") != std::string::npos,
              "unknown flag on legacy command keeps its echo and falls through to normal dispatch");
    }

    /* --- AC-5: general help flags preserved -------------------------------- */
    {
        fake_host h;
        session s(h);
        submit(s, "help -h");
        check_eq(h.output, "/$ help -h\n" + cyberdeck_shell_help::text(),
                 "help -h prints the full catalog");
        h.output.clear();
        submit(s, "help --help");
        check_eq(h.output, "/$ help --help\n" + cyberdeck_shell_help::text(),
                 "help --help prints the same full catalog");
    }

    /* --- AC-6: poweroff isolation ------------------------------------------ */
    {
        fake_host h;
        session s(h);
        submit(s, "poweroff -h");
        check(h.poweroff_admissions == 0,
              "poweroff -h never arms poweroff confirmation");
        check(h.output.find("Type exactly y") == std::string::npos,
              "poweroff -h does not print the confirmation prompt");
        h.output.clear();
        submit(s, "poweroff --help");
        check(h.poweroff_admissions == 0,
              "poweroff --help never arms poweroff confirmation");
        check(h.output.find("Type exactly y") == std::string::npos,
              "poweroff --help does not print the confirmation prompt");
    }

    /* --- AC-6: poweroff help (literal keyword) isolates correctly ---------- */
    {
        fake_host h;
        session s(h);
        submit(s, "poweroff help");
        check(h.poweroff_admissions == 0,
              "poweroff help never arms poweroff confirmation");
        check(h.output.find("Type exactly y") == std::string::npos,
              "poweroff help does not print the confirmation prompt");
        check(h.output.find("power off this device") != std::string::npos,
              "poweroff help prints the poweroff help block");
    }

    /* --- Dispatch preservation: <command> help does not execute ------------- */
    {
        fake_host h;
        session s(h);
        submit(s, "ls help");
        check(h.shell.cwd() == "/", "ls help does not change directory");
        check(h.output.find("unknown command") == std::string::npos,
              "ls help does not fall through to unknown command");
        check_eq(h.output, "/$ ls help\n" + cyberdeck_shell_help::command_text("ls"),
                 "ls help prints the ls help block");
    }
    {
        fake_host h;
        session s(h);
        submit(s, "wifi help");
        check(h.output.find("enabled") == std::string::npos &&
              h.output.find("disabled") == std::string::npos,
              "wifi help does not print Wi-Fi status");
        check_eq(h.output, "/$ wifi help\n" + cyberdeck_shell_help::command_text("wifi"),
                 "wifi help prints the wifi help block");
    }
    {
        fake_host h;
        session s(h);
        submit(s, "cat help");
        check(h.cat_line.empty(), "cat help does not enqueue a cat job");
        check_eq(h.output, "/$ cat help\n" + cyberdeck_shell_help::command_text("cat"),
                 "cat help prints the cat help block");
    }
    {
        fake_host h;
        session s(h);
        submit(s, "clear help");
        check(h.clear_count == 0, "clear help does not clear the terminal");
        check_eq(h.output, "/$ clear help\n" + cyberdeck_shell_help::command_text("clear"),
                 "clear help prints the clear help block");
    }

    /* === REQ-HELP-05 / AC-1..AC-6 (expanded): usage metadata contract ======
     * Each main query must reveal every usage format and subcommand its
     * application declares; the inventory of compiled-in apps is filtered;
     * aliases/collisions resolve without shadowing; help never starts or
     * executes an application. */

    /* --- TEST-HELP-APP-USAGE-EDIT: `edit help` reveals EVERY usage format --
     * edit <path>, edit save, edit save as <path> and edit save as confirm,
     * each with its declared explanation, plus name/id/version/description. */
    {
        fake_host h;
        help_manifest_app editor(editor_usage_manifest());
        check(h.apps.register_application(editor),
              "TEST-HELP-APP-USAGE-EDIT: editor manifest registers");
        session s(h);
        submit(s, "edit help");
        const std::string edit_expected =
            "/$ edit help\n"
            "edit [app]\n"
            "  Text editor (cyberdeck.editor) version 1.0.0\n"
            "  Bounded UTF text editor\n"
            "  usage: edit <path>\n"
            "  path: up to 256 bytes; a missing file may be created\n"
            "  usage: edit save\n"
            "  save the current document; requires an assigned path\n"
            "  usage: edit save as <path>\n"
            "  path: 1..256 bytes; requests confirmation before saving\n"
            "  usage: edit save as confirm\n"
            "  confirm the pending save-as request\n";
        check_eq(h.output, edit_expected,
                 "TEST-HELP-APP-USAGE-EDIT: edit help reveals every declared usage format (full golden)");
        check_contains(h.output, "  usage: edit <path>\n",
                       "TEST-HELP-APP-USAGE-EDIT: reveals usage edit <path>");
        check_contains(h.output, "  usage: edit save\n",
                       "TEST-HELP-APP-USAGE-EDIT: reveals usage edit save");
        check_contains(h.output, "  usage: edit save as <path>\n",
                       "TEST-HELP-APP-USAGE-EDIT: reveals usage edit save as <path>");
        check_contains(h.output, "  usage: edit save as confirm\n",
                       "TEST-HELP-APP-USAGE-EDIT: reveals usage edit save as confirm");
        check_contains(h.output, "save the current document; requires an assigned path",
                       "TEST-HELP-APP-USAGE-EDIT: edit save keeps its explanation");
        check_contains(h.output, "path: 1..256 bytes; requests confirmation before saving",
                       "TEST-HELP-APP-USAGE-EDIT: edit save as <path> keeps its explanation");
        check_contains(h.output, "confirm the pending save-as request",
                       "TEST-HELP-APP-USAGE-EDIT: edit save as confirm keeps its explanation");
        check(h.output.find("start compiled application") == std::string::npos,
              "TEST-HELP-APP-USAGE-EDIT: declared metadata replaces the generic fallback");
        check(editor.executions == 0,
              "TEST-HELP-APP-USAGE-EDIT: help never executes the editor");
        check(h.apps.state("cyberdeck.editor") == cyberdeck_apps::app_state::registered,
              "TEST-HELP-APP-USAGE-EDIT: help never starts the editor");
        check_no_side_effects(h, "TEST-HELP-APP-USAGE-EDIT: help touches no platform port");
    }

    /* --- TEST-HELP-APP-USAGE-NETDIAG: `netdiag help` reveals every declared
     * subcommand (status, dns, tcp, https, result, cancel). */
    {
        fake_host h;
        help_manifest_app netdiag(netdiag_usage_manifest());
        check(h.apps.register_application(netdiag),
              "TEST-HELP-APP-USAGE-NETDIAG: netdiag manifest registers");
        session s(h);
        submit(s, "netdiag help");
        /* The handler renders `usage:` and prefixes the parameters blob with a
         * single indent; the blob's own newlines are preserved verbatim, so
         * continuation lines stay at column 0 exactly as declared. */
        const std::string netdiag_expected =
            "/$ netdiag help\n"
            "netdiag [app]\n"
            "  Network diagnostics (cyberdeck.netdiag) version 1.0.0\n"
            "  Explicit-target connectivity diagnostics\n"
            "  usage: netdiag [status|dns <host>|tcp <host> <port>|https <https-url>|result|cancel]\n"
            "  status: show Wi-Fi interface status\n"
            "dns <host>: resolve an explicit host\n"
            "tcp <host> <port>: test a TCP connection\n"
            "https <https-url>: issue an HTTPS HEAD request\n"
            "result: report the current operation; cancel: request cancellation\n"
            "host: 1..253 ASCII letters, digits, dots, or hyphens; port: decimal 1..65535\n"
            "URL: https, at most 512 bytes; no userinfo, fragment, whitespace, or explicit port in authority\n";
        check_eq(h.output, netdiag_expected,
                 "TEST-HELP-APP-USAGE-NETDIAG: netdiag help renders the full declared block (full golden)");
        for (const char *sub : {"status: show Wi-Fi interface status", "dns <host>", "tcp <host> <port>",
                                "https <https-url>", "result:", "cancel:"})
            check_contains(h.output, sub, "TEST-HELP-APP-USAGE-NETDIAG: reveals a declared subcommand");
        check(h.output.find("start compiled application") == std::string::npos,
              "TEST-HELP-APP-USAGE-NETDIAG: declared metadata replaces the generic fallback");
        check(netdiag.executions == 0,
              "TEST-HELP-APP-USAGE-NETDIAG: help never executes netdiag");
        check(h.apps.state("cyberdeck.netdiag") == cyberdeck_apps::app_state::registered,
              "TEST-HELP-APP-USAGE-NETDIAG: help never starts netdiag");
        check_no_side_effects(h, "TEST-HELP-APP-USAGE-NETDIAG: help touches no platform port");
    }

    /* --- TEST-HELP-APP-USAGE-MUSIC: exact approved manifest usage and no execution. */
    {
        fake_host h;
        help_manifest_app music(music_usage_manifest());
        check(h.apps.register_application(music), "TEST-HELP-APP-USAGE-MUSIC: music manifest registers");
        session s(h);
        submit(s, "music help");
        const std::string expected =
            "/$ music help\n"
            "music [app]\n"
            "  Music (cyberdeck.music) version 1.0.0\n"
            "  Background local MP3 player\n"
            "  usage: music queue <directory> | list | play [index] | pause | resume | stop | next | prev | seek <seconds> | volume <0-50> | shuffle | repeat | status\n"
            "  MP3 playback from SD; volume is capped at 50\n";
        check_eq(h.output, expected, "TEST-HELP-APP-USAGE-MUSIC: complete independent golden");
        check(music.executions == 0 && h.apps.state("cyberdeck.music") == cyberdeck_apps::app_state::registered,
              "TEST-HELP-APP-USAGE-MUSIC: help does not execute or start music");
        check_no_side_effects(h, "TEST-HELP-APP-USAGE-MUSIC: no platform side effects");
    }

    /* --- TEST-HELP-APP-USAGE-DEMO: `demo help` on the REAL demo application
     * declares no arguments and keeps the faithful note about ignored extras. */
    {
        fake_host h;
        cyberdeck_apps::demo_application demo;
        check(h.apps.register_application(demo),
              "TEST-HELP-APP-USAGE-DEMO: real demo application registers");
        session s(h);
        submit(s, "demo help");
        const std::string demo_expected =
            "/$ demo help\n"
            "demo [app]\n"
            "  Demo application (cyberdeck.demo) version 0.1.0\n"
            "  Proof application for the compiled-in runtime\n"
            "  usage: demo\n"
            "  arguments are not declared; extra arguments are currently ignored\n";
        check_eq(h.output, demo_expected,
                 "TEST-HELP-APP-USAGE-DEMO: demo help renders the real declared block (full golden)");
        check_contains(h.output, "  usage: demo\n",
                       "TEST-HELP-APP-USAGE-DEMO: the bare command is the only declared usage");
        check_contains(h.output, "arguments are not declared; extra arguments are currently ignored",
                       "TEST-HELP-APP-USAGE-DEMO: keeps the faithful note about ignored extras");
        check(!demo.running() &&
                  h.apps.state("cyberdeck.demo") == cyberdeck_apps::app_state::registered,
              "TEST-HELP-APP-USAGE-DEMO: help never starts or executes the real demo app");
        check_no_side_effects(h, "TEST-HELP-APP-USAGE-DEMO: help touches no platform port");
    }

    /* --- TEST-HELP-APP-INVENTORY: the compiled-in inventory mirrors the
     * production k_apps registration — 12 applications = 7 commandless
     * services + the console-owning shell (excluded from app commands) +
     * 4 command-bearing apps (demo, editor, netdiag, music), in registration order. */
    {
        fake_host h;
        help_manifest_app event_log(commandless_manifest(
            "cyberdeck.event_log", "Event log service", "Persistent bounded event log for applications"));
        cyberdeck_apps::manifest shell_manifest =
            commandless_manifest("cyberdeck.shell", "Terminal shell",
                                 "Primary foreground terminal application",
                                 cyberdeck_apps::app_type::foreground);
        shell_manifest.owns_console = true;
        help_manifest_app shell_app(shell_manifest);
        help_manifest_app wifi(
            commandless_manifest("cyberdeck.wifi", "Wi-Fi service", "Wi-Fi connectivity and network management"));
        help_manifest_app serial(
            commandless_manifest("cyberdeck.serial", "Serial bridge", "USB Serial-JTAG NDJSON control bridge"));
        help_manifest_app ssh(
            commandless_manifest("cyberdeck.ssh", "SSH service", "Asynchronous SSH client service",
                                 cyberdeck_apps::app_type::background));
        help_manifest_app screenshot(commandless_manifest(
            "cyberdeck.screenshot", "Screenshot service", "Local-network screenshot HTTP endpoint"));
        help_manifest_app bluetooth(
            commandless_manifest("cyberdeck.bluetooth", "Bluetooth service", "ESP-Hosted BLE manager",
                                 cyberdeck_apps::app_type::background));
        help_manifest_app time_sync(commandless_manifest(
            "cyberdeck.time_sync", "UTC time synchronization", "Bounded HTTPS UTC clock synchronization",
            cyberdeck_apps::app_type::background));
        cyberdeck_apps::demo_application demo;
        help_manifest_app editor(editor_usage_manifest());
        help_manifest_app netdiag(netdiag_usage_manifest());
        help_manifest_app music(music_usage_manifest());
        cyberdeck_apps::application *const registered[] = {
            &event_log, &shell_app, &wifi,     &serial, &ssh,   &screenshot, &bluetooth,
            &time_sync, &demo,      &editor,   &netdiag, &music};
        for (cyberdeck_apps::application *app : registered)
            check(h.apps.register_application(*app),
                  "TEST-HELP-APP-INVENTORY: inventory application registers");
        check(h.apps.size() == 12,
              "TEST-HELP-APP-INVENTORY: registers all 12 compiled-in applications");

        std::size_t command_bearing = 0;
        std::size_t commandless_services = 0;
        std::size_t console_owners = 0;
        std::vector<std::string> bearing_ids;
        for (std::size_t index = 0; index < h.apps.size(); ++index) {
            const cyberdeck_apps::application *app = h.apps.at(index);
            check(app != nullptr, "TEST-HELP-APP-INVENTORY: bounded enumeration returns every app");
            if (app == nullptr) continue;
            const cyberdeck_apps::manifest &item = app->get_manifest();
            bool bears = !item.command.empty();
            for (std::size_t command = 0; command < item.command_count; ++command)
                if (!item.commands[command].empty()) bears = true;
            if (bears) {
                ++command_bearing;
                bearing_ids.emplace_back(item.id);
            } else if (item.owns_console) {
                ++console_owners;
            } else {
                ++commandless_services;
            }
        }
        check(command_bearing == 4,
              "TEST-HELP-APP-INVENTORY: exactly 4 applications are command-bearing");
        check(bearing_ids == std::vector<std::string>{"cyberdeck.demo", "cyberdeck.editor", "cyberdeck.netdiag", "cyberdeck.music"},
              "TEST-HELP-APP-INVENTORY: command-bearing apps are demo, editor, netdiag and music in registration order");
        check(commandless_services == 7,
              "TEST-HELP-APP-INVENTORY: exactly 7 services declare no shell command");
        check(console_owners == 1,
              "TEST-HELP-APP-INVENTORY: the shell is the single console owner excluded from app commands");

        session s(h);
        submit(s, "help");
        const std::string inventory_expected =
            "/$ help\n" + std::string(cyberdeck_shell_help::text()) +
            "demo [app]\n"
            "  start compiled application\n"
            "edit [app]\n"
            "  start compiled application\n"
            "netdiag [app]\n"
            "  start compiled application\n"
            "music [app]\n"
            "  start compiled application\n";
        check_eq(h.output, inventory_expected,
                 "TEST-HELP-APP-INVENTORY: general help appends exactly the 3 command-bearing rows once, in order");
        for (const char *service_id : {"cyberdeck.event_log", "cyberdeck.wifi", "cyberdeck.serial",
                                       "cyberdeck.ssh", "cyberdeck.screenshot", "cyberdeck.bluetooth",
                                       "cyberdeck.time_sync"})
            check(h.output.find(service_id) == std::string::npos,
                  "TEST-HELP-APP-INVENTORY: a commandless service never appears in help");
        check(h.output.find("shell [app]") == std::string::npos,
              "TEST-HELP-APP-INVENTORY: the console-owning shell contributes no app command row");

        bool any_running = false;
        for (std::size_t index = 0; index < h.apps.size(); ++index) {
            const cyberdeck_apps::application *app = h.apps.at(index);
            if (app != nullptr && app->running()) any_running = true;
        }
        check(!any_running, "TEST-HELP-APP-NO-EXEC: general help starts no registered application");

        h.output.clear();
        submit(s, "cyberdeck.wifi help");
        check_eq(h.output,
                 "/$ cyberdeck.wifi help\nhelp: no details available for command 'cyberdeck.wifi'\n",
                 "TEST-HELP-APP-INVENTORY: commandless services are not addressable by id in detail help");
        check_no_side_effects(h, "TEST-HELP-APP-INVENTORY: help touches no platform port");
    }

    /* --- TEST-HELP-APP-ALIAS / TEST-HELP-APP-COLLISION: an alias resolves to
     * its owning app (empty metadata keeps the generic fallback), the first
     * registered app owns a colliding command, and a legacy term is never
     * shadowed by an application declaration. */
    {
        fake_host h;
        cyberdeck_apps::manifest aliased{};
        aliased.id = "cyberdeck.aliased";
        aliased.name = "Aliased app";
        aliased.version = "1.2.3";
        aliased.description = "declares a primary command and an alias";
        aliased.type = cyberdeck_apps::app_type::foreground;
        aliased.commands[0] = "dual";
        aliased.commands[1] = "dual-tool";
        aliased.command_count = 2;
        aliased.command_help_entries[0] = {"dual <file>", "file: target of the primary command\n"};
        help_manifest_app dual(aliased);
        cyberdeck_apps::manifest collide{};
        collide.id = "cyberdeck.collide";
        collide.name = "Colliding app";
        collide.description = "declares a command already owned by an earlier app";
        collide.type = cyberdeck_apps::app_type::foreground;
        collide.command = "dual";
        help_manifest_app colliding(collide);
        cyberdeck_apps::manifest legacy_shadow{};
        legacy_shadow.id = "cyberdeck.legacy-shadow";
        legacy_shadow.name = "Legacy shadow";
        legacy_shadow.description = "declares the legacy ls command";
        legacy_shadow.type = cyberdeck_apps::app_type::foreground;
        legacy_shadow.command = "ls";
        help_manifest_app shadow(legacy_shadow);
        check(h.apps.register_application(dual), "TEST-HELP-APP-ALIAS: aliased app registers");
        check(h.apps.register_application(colliding), "TEST-HELP-APP-COLLISION: colliding app registers");
        check(h.apps.register_application(shadow), "TEST-HELP-APP-COLLISION: legacy-shadow app registers");
        session s(h);

        submit(s, "dual help");
        const std::string dual_expected =
            "/$ dual help\n"
            "dual [app]\n"
            "  Aliased app (cyberdeck.aliased) version 1.2.3\n"
            "  declares a primary command and an alias\n"
            "  usage: dual <file>\n"
            "  file: target of the primary command\n";
        check_eq(h.output, dual_expected,
                 "TEST-HELP-APP-COLLISION: the first registered app owns the colliding command (full golden)");
        check(h.output.find("(cyberdeck.collide)") == std::string::npos,
              "TEST-HELP-APP-COLLISION: the later colliding app never answers the help query");

        h.output.clear();
        submit(s, "dual-tool help");
        const std::string alias_expected =
            "/$ dual-tool help\n"
            "dual-tool [app]\n"
            "  Aliased app (cyberdeck.aliased) version 1.2.3\n"
            "  declares a primary command and an alias\n";
        check_eq(h.output, alias_expected,
                 "TEST-HELP-APP-ALIAS: an alias with empty metadata keeps the generic block (full golden)");
        check(h.output.find("usage:") == std::string::npos,
              "TEST-HELP-APP-ALIAS: empty alias metadata prints no usage line");

        h.output.clear();
        submit(s, "ls help");
        check_eq(h.output, "/$ ls help\n" + cyberdeck_shell_help::command_text("ls"),
                 "TEST-HELP-APP-COLLISION: a legacy term is never shadowed by an app declaration");

        h.output.clear();
        submit(s, "help");
        const std::string alias_help_expected =
            "/$ help\n" + std::string(cyberdeck_shell_help::text()) +
            "dual [app]\n"
            "  start compiled application\n"
            "dual-tool [app]\n"
            "  start compiled application\n";
        check_eq(h.output, alias_help_expected,
                 "TEST-HELP-APP-COLLISION: general help lists a colliding command once and omits the legacy shadow");
        check(h.output.find("ls [app]") == std::string::npos,
              "TEST-HELP-APP-COLLISION: the app-declared legacy term adds no app row");
        check(h.output.find("collide [app]") == std::string::npos &&
                  h.output.find("legacy-shadow [app]") == std::string::npos,
              "TEST-HELP-APP-COLLISION: fully shadowed declarations add no app rows");

        check(dual.executions == 0 && colliding.executions == 0 && shadow.executions == 0,
              "TEST-HELP-APP-NO-EXEC: alias and collision help queries execute no application");
        check(h.apps.state("cyberdeck.aliased") == cyberdeck_apps::app_state::registered &&
                  h.apps.state("cyberdeck.collide") == cyberdeck_apps::app_state::registered &&
                  h.apps.state("cyberdeck.legacy-shadow") == cyberdeck_apps::app_state::registered,
              "TEST-HELP-APP-NO-EXEC: alias and collision help queries start no application");
        check_no_side_effects(h, "TEST-HELP-APP-ALIAS: help touches no platform port");
    }

    /* --- TEST-HELP-APP-SYNTAX: only the exact `<command> help` form is a
     * detail request; trailing tokens, subcommand forms and -h fall through to
     * normal app dispatch, which demand-starts a foreground app (the real
     * handler rule of runtime::execute_line). */
    {
        fake_host h;
        help_manifest_app editor(editor_usage_manifest());
        check(h.apps.register_application(editor),
              "TEST-HELP-APP-SYNTAX: editor manifest registers");
        session s(h);

        submit(s, "edit help extra");
        check(h.output.find("no details available") == std::string::npos &&
                  h.output.find("usage: edit") == std::string::npos,
              "TEST-HELP-APP-SYNTAX: a trailing token is not a detail-help request");
        check(editor.executions == 1,
              "TEST-HELP-APP-SYNTAX: a trailing token falls through to normal app dispatch");

        h.output.clear();
        submit(s, "edit save help");
        check(h.output.find("usage: edit") == std::string::npos,
              "TEST-HELP-APP-SYNTAX: a subcommand plus help is not a detail-help request");
        check(editor.executions == 2,
              "TEST-HELP-APP-SYNTAX: a subcommand plus help dispatches to the application");

        h.output.clear();
        submit(s, "edit -h");
        check(h.output.find("no details available") == std::string::npos &&
                  h.output.find("usage: edit") == std::string::npos,
              "TEST-HELP-APP-SYNTAX: -h is not a detail-help keyword for app commands");
        check(editor.executions == 3,
              "TEST-HELP-APP-SYNTAX: -h falls through to normal app dispatch");

        check(h.apps.state("cyberdeck.editor") == cyberdeck_apps::app_state::running,
              "TEST-HELP-APP-SYNTAX: fall-through dispatch demand-starts the foreground app (real handler rule)");
        check_no_side_effects(h, "TEST-HELP-APP-SYNTAX: fall-through touches no other platform port");
    }

    std::printf("shell help: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
