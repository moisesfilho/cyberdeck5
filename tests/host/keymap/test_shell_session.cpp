// Behavioral host tests for the extracted shell session.
//
// The session is LVGL-free and reaches the platform only through
// cyberdeck_shell_session::host, so it links on the host against the fake
// host below.  This exercises handle_key()/execute_line()/text insertion as
// behavior, complementing the structural contracts that inspect the source.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "features/shell/cyberdeck_shell_session.h"

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

struct fake_host final : cyberdeck_shell_session::host {
    /* Output. */
    std::string output;
    int render_count = 0;
    int clear_count = 0;

    /* Bluetooth. */
    cyberdeck_ble::state_machine ble;
    cyberdeck_ble::device_list scan_devices;
    int ble_notice_cleared = 0;
    int ble_uncommitted = 0;
    int ble_actions_submitted = 0;
    int ble_transient_synced = 0;
    std::vector<ble_bond_snapshot_t> bonds;

    /* Wi-Fi. */
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

    /* Wi-Fi service. */
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

    /* SSH. */
    cyberdeck_session_state phase = cyberdeck_session_state::MENU;
    std::string ssh_sent;
    std::string ssh_password;
    int host_key_accepted = 0;
    bool ssh_visible = false;
    int ssh_filter_resets = 0;
    int composer_discards = 0;
    cyberdeck_shell_session::host *self = nullptr;
    cyberdeck_ssh_line_composer composer;

    /* Screen. */
    int screen_on = 0;
    int screen_off = 0;
    int screen_timeout = -1;
    esp_err_t screen_timeout_result = ESP_OK;

    /* Battery. */
    bool battery_set_result = true;
    bool battery_started = true;
    bool battery_snapshot_ok = true;
    cyberdeck_battery_protection::snapshot battery_snapshot{};

    /* Event log. */
    std::vector<std::string> events;
    std::string recent;
    std::size_t recent_requested = 0;

    /* Local shell / cat. */
    bool cat_accepted = true;
    std::string cat_cwd;
    std::string cat_line;
    cyberdeck_local_shell shell{std::string("/tmp/opencode/session-harness")};

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
};

void type(session &s, const std::string &text)
{
    s.insert_physical_text(text.data(), text.size());
}

// Runs one full "type text then Enter" cycle the way the UI does.
void submit(session &s, const std::string &text)
{
    type(s, text);
    s.handle_key(key::enter);
}

} // namespace

int main()
{
    /* --- editor and history ------------------------------------------- */
    {
        fake_host h;
        session s(h);
        type(s, "pwd");
        check_eq(s.line(), "pwd", "typed text reaches the session line");
        check(s.cursor() == 3, "cursor tracks the typed length");
        s.handle_key(key::backspace);
        check_eq(s.line(), "pw", "backspace edits the session line");
        s.handle_key(key::enter);
        check(s.line().empty(), "enter clears the line after execution");
        submit(s, "pwd");
        check(h.output.find("/") != std::string::npos, "the first command ran");
        type(s, "ls");
        s.handle_key(key::up);
        check_eq(s.line(), "pwd", "history restores the last executed command");
    }

    /* --- local command dispatch ---------------------------------------- */
    {
        fake_host h;
        session s(h);
        submit(s, "pwd");
        check(h.output.find("/") != std::string::npos,
              "pwd prints the confined root through the local shell");
        check(!h.events.empty(), "a local command writes an event log entry");
    }

    {
        fake_host h;
        session s(h);
        submit(s, "definitely-not-a-command");
        check(h.output.find("unknown command") != std::string::npos,
              "an unknown command reports the parser error");
    }

    {
        fake_host h;
        session s(h);
        submit(s, "help");
        check(h.output.find("help - show this help") != std::string::npos,
              "help prints the shared catalog");
    }

    /* --- screen commands go through the host port ---------------------- */
    {
        fake_host h;
        session s(h);
        submit(s, "screen off");
        check(h.screen_off == 1, "screen off reaches the screen port");
        check(h.output.find("screen off") != std::string::npos, "screen off is acknowledged");
        submit(s, "screen on");
        check(h.screen_on == 1, "screen on reaches the screen port");
    }

    {
        fake_host h;
        session s(h);
        submit(s, "screen timeout 300");
        check(h.screen_timeout == 300, "screen timeout forwards the parsed minutes");
        check(h.output.find("300 minutes") != std::string::npos,
              "screen timeout confirms the applied value");
    }

    {
        fake_host h;
        session s(h);
        submit(s, "screen timeout 99999");
        check(h.screen_timeout == -1, "an out-of-range timeout never reaches the port");
        check(h.output.find("expected an integer") != std::string::npos,
              "an out-of-range timeout is rejected");
    }

    {
        fake_host h;
        h.screen_timeout_result = ESP_FAIL;
        session s(h);
        submit(s, "screen timeout 5");
        check(h.output.find("unable to persist") != std::string::npos,
              "a failed persist is reported");
    }

    /* --- wifi status and scan ------------------------------------------ */
    {
        fake_host h;
        h.status.connected = true;
        h.status.has_ip = true;
        strncpy(h.status.ssid, "lab", sizeof(h.status.ssid) - 1);
        strncpy(h.status.ip, "10.0.0.5", sizeof(h.status.ip) - 1);
        session s(h);
        submit(s, "wifi");
        check(h.output.find("enabled") != std::string::npos, "wifi reports the enabled state");
        check(h.output.find("10.0.0.5") != std::string::npos, "wifi reports the address");
    }

    {
        fake_host h;
        h.enabled = false;
        session s(h);
        submit(s, "wifi search");
        check(h.output.find("wifi: disabled") != std::string::npos,
              "scan is refused while the radio is disabled");
        check(!h.scan_started, "no scan is queued while the radio is disabled");
    }

    {
        fake_host h;
        session s(h);
        submit(s, "wifi search");
        check(h.scan_started, "wifi search starts a scan through the port");
        check(h.wifi == wifi_ui_state_t::SCANNING, "the flow enters the scanning state");
    }

    /* Escape cancels an in-flight scan and invalidates the connection. */
    {
        fake_host h;
        session s(h);
        submit(s, "wifi search");
        s.handle_key(key::esc);
        check(h.scan_cancelled == 1, "escape cancels the scan");
        check(h.wifi == wifi_ui_state_t::IDLE, "escape returns the flow to idle");
    }

    /* --- Wi-Fi connection token is read back from the manager ----------- */
    /* Starting a connection must publish the manager's token, not the model
     * token: the UI pump matches manager callbacks against the former. */
    {
        fake_host h;
        h.manager_token = 4242;
        session s(h);
        h.saved_list.count = 1;
        strncpy(h.saved_list.items[0].ssid, "lab", sizeof(h.saved_list.items[0].ssid) - 1);
        submit(s, "wifi saved");
        s.handle_key(key::enter);
        s.handle_key(key::enter);
        check_eq(h.forgotten_ssid, "lab", "the saved flow runs to completion");
    }

    {
        fake_host h;
        h.manager_token = 4242;
        session s(h);
        h.saved_list.count = 1;
        strncpy(h.saved_list.items[0].ssid, "lab", sizeof(h.saved_list.items[0].ssid) - 1);
        submit(s, "wifi saved");
        check(h.wifi == wifi_ui_state_t::SAVED_SELECT, "saved networks open the menu");
        s.handle_key(key::enter);
        check(h.wifi == wifi_ui_state_t::SAVED_CONFIRM, "the first enter asks for confirmation");
        s.handle_key(key::enter);
        check(h.forget_calls == 1, "the second enter forgets the network");
        check_eq(h.forgotten_ssid, "lab", "the selected SSID is the one forgotten");
    }

    {
        fake_host h;
        session s(h);
        submit(s, "wifi saved");
        check(h.output.find("No saved Wi-Fi") != std::string::npos,
              "an empty saved list reports it");
    }

    {
        fake_host h;
        h.storage_ready = false;
        session s(h);
        submit(s, "wifi saved");
        check(h.output.find("No saved Wi-Fi") != std::string::npos,
              "an unavailable store reports no saved networks");
    }

    /* Opening a connection publishes two distinct tokens: the model's attempt
     * token and the manager's token that the UI pump matches against.  They
     * must never be the same value. */
    {
        fake_host h;
        h.manager_token = 4242;
        session s(h);
        /* Put a connectable open network in the search menu and select it. */
        h.search_menu.add_ap("lab", -40, 0, true);
        h.wifi = wifi_ui_state_t::SEARCH_SELECT;
        s.handle_key(key::enter);
        check_eq(h.connected_ssid, "lab", "the selected open network is connected");
        check_eq(h.connected_password, "", "an open network connects without a password");
        check(h.wifi == wifi_ui_state_t::CONNECTING, "the flow waits for the connection");
        check(s.wifi_connection_token() == 4242,
              "the manager token is published for the UI pump to match");
        check(s.wifi_model_connection_token() != 0,
              "the model attempt token is published separately");
    }

    /* A failed connect must clear both tokens and report it. */
    {
        fake_host h;
        h.manager_token = 99;
        h.connect_result = ESP_FAIL;
        session s(h);
        h.search_menu.add_ap("lab", -40, 0, true);
        h.wifi = wifi_ui_state_t::SEARCH_SELECT;
        s.handle_key(key::enter);
        check(h.output.find("could not be started") != std::string::npos,
              "a refused connection is reported");
        check(h.wifi == wifi_ui_state_t::IDLE, "a refused connection returns to idle");
        check(s.wifi_connection_token() == 0, "a refused connection clears the manager token");
        check(s.wifi_model_connection_token() == 0,
              "a refused connection clears the model token");
    }

    /* Escape while connecting invalidates the tokens before the worker runs. */
    {
        fake_host h;
        h.manager_token = 7;
        session s(h);
        h.search_menu.add_ap("lab", -40, 0, true);
        h.wifi = wifi_ui_state_t::SEARCH_SELECT;
        s.handle_key(key::enter);
        check(s.wifi_connection_token() == 7, "the manager token was published");
        s.handle_key(key::esc);
        check(h.cancel_connection_calls == 1, "escape cancels the connection");
        check(s.wifi_connection_token() == 0,
              "escape invalidates the manager token so no late callback matches");
        check(s.wifi_model_connection_token() == 0, "escape clears the model token");
        check(h.wifi == wifi_ui_state_t::IDLE, "escape returns to idle");
    }

    /* --- log and battery ----------------------------------------------- */
    {
        fake_host h;
        h.recent = "evt one\n";
        session s(h);
        submit(s, "log");
        check(h.recent_requested == 20, "log requests the configured 20 recent lines");
        check(h.output.find("ultimos eventos") != std::string::npos, "log prints a header");
        check(h.output.find("evt one") != std::string::npos, "log prints the events");
    }

    {
        fake_host h;
        session s(h);
        submit(s, "log");
        check(h.recent_requested == 20, "empty log also requests the configured 20 lines");
        check(h.output.find("(nenhum evento") != std::string::npos,
              "an empty log reports the absence of events");
    }

    /* The configured default is session-local: a valid override is temporary,
     * queries expose the current value, and invalid requests do not mutate it. */
    {
        fake_host h;
        h.recent = "evt\n";
        session s(h);
        submit(s, "log lines");
        check(h.output.find("log lines: 20") != std::string::npos,
              "log lines reports the configured default");
        h.output.clear();
        submit(s, "log lines 1");
        check(h.output.find("log lines set to 1") != std::string::npos,
              "log lines 1 accepts the lower boundary");
        h.output.clear();
        submit(s, "log");
        check(h.recent_requested == 1, "override controls the recent event request");

        for (const char *invalid : {"log lines 0", "log lines 65",
                                    "log lines nope", "log lines 4 extra"}) {
            h.output.clear();
            submit(s, invalid);
            check(h.output.find("usage: log [lines <1-64>]") != std::string::npos,
                  "invalid log-lines syntax reports usage");
            h.output.clear();
            submit(s, "log");
            check(h.recent_requested == 1,
                  "invalid log-lines syntax does not alter the session value");
        }

        h.output.clear();
        submit(s, "log lines 64");
        check(h.output.find("log lines set to 64") != std::string::npos,
              "log lines 64 accepts the upper boundary");
        h.output.clear();
        submit(s, "log");
        check(h.recent_requested == 64, "upper-bound override is used");
        h.output.clear();
        submit(s, "log lines");
        check(h.output.find("log lines: 64") != std::string::npos,
              "log lines exposes the active override");
    }

    /* Parser + session integration: requesting 40 lines is valid even though
     * the Kconfig default remains 20, and the host port receives the complete
     * available window (up to the requested count). */
    {
        fake_host h;
        for (int index = 1; index <= 40; ++index) {
            h.recent += "event-" + std::to_string(index) + "\n";
        }
        session s(h);
        submit(s, "log lines 40");
        check(h.output.find("log lines set to 40") != std::string::npos,
              "log lines 40 is accepted by the session parser");
        h.output.clear();
        submit(s, "log");
        check(h.recent_requested == 40,
              "log requests the configured session window of 40 lines");
        for (int index = 1; index <= 40; ++index) {
            check(h.output.find("event-" + std::to_string(index) + "\n") != std::string::npos,
                  "log returns every available event up to the requested 40 lines");
        }
    }

    {
        fake_host h;
        session first(h);
        submit(first, "log lines 7");
        session recreated(h);
        submit(recreated, "log lines");
        check(h.output.find("log lines: 20") != std::string::npos,
              "recreating a session resets the override to Kconfig default");
    }

    {
        fake_host h;
        session s(h);
        submit(s, "battery protection on");
        check(h.output.find("enabled") != std::string::npos, "battery protection enables");
        h.battery_set_result = false;
        submit(s, "battery protection off");
        check(h.output.find("unavailable") != std::string::npos,
              "an unavailable adapter is reported");
    }

    {
        fake_host h;
        h.battery_started = false;
        session s(h);
        submit(s, "battery protection status");
        check(h.output.find("unavailable") != std::string::npos,
              "a stopped adapter reports unavailable");
    }

    /* --- bluetooth ------------------------------------------------------ */
    {
        fake_host h;
        session s(h);
        submit(s, "bluetooth search");
        check(h.ble_notice_cleared == 1, "a search clears the previous notice");
        check(h.output.find("Bluetooth search started") != std::string::npos,
              "a search is acknowledged");
    }

    {
        fake_host h;
        ble_bond_snapshot_t bond{};
        strncpy(bond.name, "kbd", sizeof(bond.name) - 1);
        h.bonds.push_back(bond);
        session s(h);
        submit(s, "bluetooth paired");
        check(h.ble_transient_synced > 0, "the paired screen synchronizes the transient block");
    }

    /* --- SSH ------------------------------------------------------------ */
    {
        fake_host h;
        session s(h);
        submit(s, "ssh user@10.0.0.9:2222");
        check(h.ssh_visible, "ssh shows the session chrome");
        check_eq(h.connected_ssid, "10.0.0.9", "ssh targets the parsed host");
    }

    {
        fake_host h;
        session s(h);
        submit(s, "ssh");
        check(h.output.find("usage: ssh") != std::string::npos,
              "ssh without a target prints the usage");
    }

    {
        fake_host h;
        h.phase = cyberdeck_session_state::PASSWORD;
        session s(h);
        submit(s, "s3cret");
        check_eq(h.ssh_password, "s3cret", "a password phase sends the password");
    }

    {
        fake_host h;
        h.phase = cyberdeck_session_state::HOST_KEY;
        session s(h);
        submit(s, "");
        check(h.host_key_accepted == 1, "host key phase accepts on enter");
    }

    {
        fake_host h;
        h.phase = cyberdeck_session_state::CONNECTED;
        session s(h);
        submit(s, "uname -a");
        check(h.ssh_sent.find("uname -a") != std::string::npos,
              "a connected session sends the command with a newline");
    }

    /* A modified key is an SSH escape, never editor text. */
    {
        fake_host h;
        h.phase = cyberdeck_session_state::CONNECTED;
        session s(h);
        check(s.insert_modified_key('c', 0x01), "a modified key is sent in a connected session");
        check_eq(h.ssh_sent, "\x03", "Ctrl-C produces exactly ETX");
        check(s.line().empty(), "a modified key is not inserted as text");
        h.ssh_sent.clear();
        check(s.insert_modified_key('d', 0x01), "Ctrl-D is sent");
        check_eq(h.ssh_sent, "\x04", "Ctrl-D produces exactly EOT");
        h.ssh_sent.clear();
        /* Alt is bit 0x04, so it prefixes the control byte with ESC. */
        check(s.insert_modified_key('a', 0x05), "Ctrl-Alt-A is sent");
        check_eq(h.ssh_sent, "\x1B\x01", "Ctrl-Alt-A is exactly ESC + SOH");
        h.ssh_sent.clear();
        /* Alt without Ctrl prefixes the plain character with ESC, matching
         * the terminal's Alt-as-Meta convention. */
        check(s.insert_modified_key('q', 0x04), "Alt-Q is sent");
        check_eq(h.ssh_sent, "\x1Bq", "Alt-Q is exactly ESC + the literal key");
        h.ssh_sent.clear();
        check(s.insert_modified_key('@', 0x01), "Ctrl-@ is sent");
        check(h.ssh_sent.size() == 1 && h.ssh_sent[0] == '\0',
              "Ctrl-@ produces exactly one NUL byte");
    }

    {
        fake_host h;
        session s(h);
        check(!s.insert_modified_key('c', 0x01),
              "a modified key is ignored outside a connected session");
    }

    /* --- BLE passkey buffer is owned by the session --------------------- */
    {
        fake_host h;
        session s(h);
        s.append_ble_auth_digit('1');
        s.append_ble_auth_digit('2');
        s.append_ble_auth_digit('x');      /* rejected: not a digit */
        check_eq(s.ble_auth_input(), "12", "only digits reach the passkey buffer");
        for (int i = 0; i < 8; ++i) s.append_ble_auth_digit('0');
        check(s.ble_auth_input().size() == cyberdeck_ble::k_passkey_digits,
              "the passkey buffer is bounded to the expected digit count");
        s.clear_ble_auth_input();
        check(s.ble_auth_input().empty(), "the passkey buffer can be wiped");
    }

    /* The session diverts typed digits to the passkey buffer while the model
     * owns an auth input, and never to the shell line. */
    {
        fake_host h;
        session s(h);
        h.ble.begin_search();
        h.ble.advance_time(2000);
        cyberdeck_ble::device peer;
        peer.address = "AA:BB:CC:DD:EE:FF";
        peer.connectable = true;
        peer.kind = cyberdeck_ble::device_kind::keyboard;
        h.ble.scan_finished(h.ble.active_scan_token(), {peer});
        h.ble.press(cyberdeck_ble::key::enter);
        const auto actions = h.ble.take_actions();
        check(!actions.empty(), "enter on a connectable result emits a pair action");

        /* Drive the model to an auth input deterministically. */
        h.ble.auth_requested(actions.front().token,
                             cyberdeck_ble::auth_request_kind::passkey, 0,
                             cyberdeck_ble::auth_io_action::input);
        if (h.ble.current_screen() != cyberdeck_ble::screen::auth) {
            /* Fall back to the buffer contract alone when the model refuses the
             * challenge, so the ownership rule is still covered. */
            h.ble.auth_requested(actions.front().token,
                                 cyberdeck_ble::auth_request_kind::numeric_compare, 123456,
                                 cyberdeck_ble::auth_io_action::numeric_compare);
        }
        const bool on_auth = h.ble.current_screen() == cyberdeck_ble::screen::auth;
        if (on_auth) {
            check(h.ble.owns_input(), "the auth screen owns input");
            type(s, "123456");
            check_eq(s.ble_auth_input(), "123456", "typed digits reach the passkey buffer");
            check(s.line().empty(), "typed digits never reach the shell line");
            check(!s.insert_physical_text("a", 1), "a non-digit is rejected in the auth screen");
            check_eq(s.ble_auth_input(), "123456", "the rejected digit left the buffer untouched");
            s.handle_key(key::backspace);
            check_eq(s.ble_auth_input(), "12345", "backspace edits the passkey buffer");
        } else {
            /* The buffer contract is independent of the model transition. */
            s.append_ble_auth_digit('1');
            s.append_ble_auth_digit('2');
            check_eq(s.ble_auth_input(), "12", "the session owns the passkey buffer");
            check(s.line().empty(), "the passkey buffer is not the shell line");
        }
        s.clear_ble_auth_input();
        check(s.ble_auth_input().empty(), "the passkey buffer can be wiped");
    }

    /* --- virtual insertion ---------------------------------------------- */
    {
        fake_host h;
        session s(h);
        bool enter_handled = false;
        s.insert_virtual_text("abc", &enter_handled);
        check_eq(s.line(), "abc", "virtual text reaches the session line");
        check(!enter_handled, "a chunk without a newline does not claim the enter");
    }

    /* A control byte is an editing action, not text. */
    {
        fake_host h;
        session s(h);
        bool enter_handled = false;
        s.insert_virtual_text("ab", &enter_handled);
        check_eq(s.line(), "ab", "virtual text is inserted verbatim");
        s.insert_virtual_text("\x7F", &enter_handled);
        check_eq(s.line(), "a", "an isolated 0x7F is routed as backspace");
        s.insert_virtual_text("z", &enter_handled);
        check_eq(s.line(), "az", "text insertion resumes after an editing key");
        /* del removes forward from the cursor, so put the cursor before the
         * last character first. */
        s.handle_key(key::left);
        s.insert_virtual_text("\x08", &enter_handled);
        check_eq(s.line(), "a", "an isolated 0x08 is routed as delete");
        /* A control byte inside a multi-byte chunk is not an editing action:
         * only an isolated control byte is. */
        const std::string before_chunk = s.line();
        s.insert_virtual_text("xy\x7F", &enter_handled);
        check(s.line() == before_chunk + "xy\x7F",
              "a control byte inside a chunk is inserted literally, not as an action");
    }

    /* A paste with a trailing newline executes exactly once. */
    {
        fake_host h;
        session s(h);
        bool enter_handled = false;
        s.insert_virtual_text("pwd\n", &enter_handled);
        check(enter_handled, "a chunk ending in a newline claims the enter");
        check(h.output.find("/") != std::string::npos, "the pasted line executed once");
        check(s.line().empty(), "the line is consumed by the execution");
    }

    /* A paste carrying several newlines executes each line exactly once. */
    {
        fake_host h;
        session s(h);
        bool enter_handled = false;
        s.insert_virtual_text("pwd\npwd\n", &enter_handled);
        /* The root path is printed once per executed line. */
        std::size_t hits = 0;
        for (std::size_t at = h.output.find('/'); at != std::string::npos;
             at = h.output.find('/', at + 1)) ++hits;
        check(hits == 2, "each pasted line executed exactly once");
        check(enter_handled, "a paste ending in a newline claims the enter");
        check(s.line().empty(), "every pasted line was consumed");
    }

    /* A connected session accepts only one pasted line while its echo is
     * pending; the remainder stays in the editor instead of being sent. */
    {
        fake_host h;
        h.phase = cyberdeck_session_state::CONNECTED;
        session s(h);
        bool enter_handled = false;
        s.insert_virtual_text("one\ntwo\n", &enter_handled);
        check_eq(s.line(), "two", "the second pasted line stays editable");
        check(h.ssh_sent.find("one") != std::string::npos,
              "the first pasted line was sent");
        check(h.ssh_sent.find("two") == std::string::npos,
              "the second pasted line was not sent while the echo is pending");
    }

    /* --- clear ---------------------------------------------------------- */
    {
        fake_host h;
        session s(h);
        submit(s, "pwd");
        submit(s, "clear");
        check(h.clear_count == 1, "clear empties the terminal through the host");
    }

    std::printf("shell session: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
