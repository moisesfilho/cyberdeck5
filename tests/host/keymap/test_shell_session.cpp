// Behavioral host tests for the extracted shell session.
//
// The session is LVGL-free and reaches the platform only through
// cyberdeck_shell_session::host, so it links on the host against the fake
// host below.  This exercises handle_key()/execute_line()/text insertion as
// behavior, complementing the structural contracts that inspect the source.

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

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

/* Sonda de despacho: registra exatamente o comando e o operando (trim) que o
 * runtime entregou ao aplicativo.  Usada para provar que a sessao reescreve
 * apenas a linha executada e preserva a linha digitada em todo o resto. */
class file_operand_probe final : public cyberdeck_apps::application {
public:
    file_operand_probe(std::string_view id, std::string_view command)
    {
        manifest_.id = id;
        manifest_.name = "probe";
        manifest_.version = "1.0.0";
        manifest_.description = "dispatch probe";
        manifest_.command = command;
    }

    const cyberdeck_apps::manifest &get_manifest() const override { return manifest_; }
    bool start() override { return true; }
    bool stop() override { return true; }
    bool running() const override { return true; }

    cyberdeck_apps::result execute(std::string_view command, std::string_view args) override
    {
        ++executions;
        last_command.assign(command.data(), command.size());
        while (!args.empty() && (args.front() == ' ' || args.front() == '\t')) args.remove_prefix(1);
        while (!args.empty() && (args.back() == ' ' || args.back() == '\t')) args.remove_suffix(1);
        last_operand.assign(args.data(), args.size());
        return {cyberdeck_apps::result_status::handled, std::string{}};
    }

    int executions = 0;
    std::string last_command;
    std::string last_operand;

private:
    cyberdeck_apps::manifest manifest_{};
};

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
    int poweroff_admissions = 0;
    cyberdeck_poweroff::admission_result poweroff_admission = cyberdeck_poweroff::admission_result::admitted;
    cyberdeck_poweroff::admission_result admit_poweroff() override {
        ++poweroff_admissions;
        return poweroff_admission;
    }

    /* Local shell / cat. */
    bool cat_accepted = true;
    std::string cat_cwd;
    std::string cat_line;
    cyberdeck_sd_io::coordinator sd_io;
    cyberdeck_local_shell shell{std::string("/tmp/opencode/session-harness")};
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
    mutable int battery_diagnostics_reads{0};
    bool battery_diagnostics_ok{true};
    cyberdeck_battery_protection::hardware_diagnostics battery_diagnostics{};
    bool battery_hardware_diagnostics(cyberdeck_battery_protection::hardware_diagnostics *out) const override
    {
        ++battery_diagnostics_reads;
        if (out) *out = battery_diagnostics;
        return battery_diagnostics_ok;
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

    /* Completacao: registra o pedido e entrega candidatos CANNED (a varredura
     * real do SD e coberta por test_shell_completion, que linka o scanner). */
    cyberdeck_shell_completion::candidate_list completion_candidates;
    cyberdeck_shell_completion::request last_completion_request;
    std::uint64_t last_completion_token = 0;
    int completion_requests = 0;
    bool completion_async = false;
    bool completion_accept = true;
    bool completion_truncated = false;
    session *completion_owner = nullptr;

    bool completion_scan_async(const cyberdeck_shell_completion::request &request,
                               std::uint64_t token) override
    {
        ++completion_requests;
        last_completion_request = request;
        last_completion_token = token;
        if (!completion_accept) return false;
        if (completion_async) return true;
        deliver_completion(token);
        return true;
    }

    void deliver_completion(std::uint64_t token)
    {
        if (completion_owner == nullptr) return;
        completion_owner->apply_completion_result(token, completion_candidates.data(),
                                                 completion_candidates.size(),
                                                 completion_truncated);
    }

    cyberdeck_local_shell &local_shell() override { return shell; }
    cyberdeck_apps::runtime &app_runtime() override { return apps; }
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

std::vector<std::string> split_lines(const std::string &text)
{
    std::vector<std::string> lines;
    if (text.empty()) return lines;
    std::size_t begin = 0;
    while (begin < text.size()) {
        const std::size_t end = text.find('\n', begin);
        const std::size_t stop = end == std::string::npos ? text.size() : end;
        if (stop != begin) lines.push_back(text.substr(begin, stop - begin));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return lines;
}

/* TEST-HELP-CAT-PINNED (REQ-HELP-01..04): the pinned legacy help contract.
 * Entries, wording, order, [terminal] tags and every parameter explanation
 * are acceptance evidence; an intentional wording change must update this
 * pin, never skip the comparison. */
const char *kLegacyHelpPinned = R"-(help [terminal]
  help - show this help
clear [terminal]
  clear - clear the terminal
app [terminal]
  app [list|info|start|stop] [id] - manage compiled-in applications
  list: list registered applications
  info <id>: show application details
  start <id>: start an application
  stop <id>: stop an application
pwd [terminal]
  pwd - print working directory
cd [terminal]
  cd [path] - change working directory
  [path]: destination (default home directory)
ls [terminal]
  ls [-a] [path] - list directory contents
  [-a]: include hidden entries
  [path]: directory to list (default current directory)
cat [terminal]
  cat <file> - print a regular file
  <file>: file to display
touch [terminal]
  touch <file> - create an empty file
  <file>: file to create
mkdir [terminal]
  mkdir <directory> - create a directory
  <directory>: directory to create
rm [terminal]
  rm [-r] <path> - remove a file or directory
  [-r]: recursively remove a directory
  <path>: target to remove
rmdir [terminal]
  rmdir <directory> - remove an empty directory
  <directory>: empty directory to remove
poweroff [terminal]
  poweroff - power off this device after exact y confirmation
  Confirmation: type exactly y when prompted; any other input cancels
)-";

/* TEST-HELP-ORDER-01 (REQ-HELP-03): canonical catalog order, one row per
 * command. */
const char *const kLegacyOrder[] = {
    "help", "clear", "app", "pwd", "cd", "ls", "cat", "touch", "mkdir", "rm", "rmdir", "poweroff",
};

} // namespace

int main()
{
    for (const std::string answer : {"Y", "yes", " y", "y ", "", "pwd"}) {
        fake_host h;
        session s(h);
        submit(s, "poweroff");
        check(h.poweroff_admissions == 0, "poweroff alone never requests a physical cut");
        check(h.output.find("Type exactly y") != std::string::npos, "poweroff prompts for exact confirmation");
        submit(s, answer);
        check(h.poweroff_admissions == 0, "non-exact confirmations cancel without side effects");
        check(h.output.find("poweroff: cancelled") != std::string::npos, "cancellation is visible");
        submit(s, "y");
        check(h.poweroff_admissions == 0, "cancelled confirmation cannot be reused");
    }
    {
        fake_host h;
        session s(h);
        submit(s, "poweroff");
        submit(s, "y");
        check(h.poweroff_admissions == 1, "exact poweroff/y requests shutdown once");
        check(h.output.find("poweroff: admitted; quiescing storage") != std::string::npos,
              "typed successful admission reports asynchronous quiescence");
        submit(s, "poweroff");
        submit(s, "y");
        check(h.poweroff_admissions == 1, "shutdown cannot retry or admit subsequent commands");
    }
    {
        fake_host h;
        h.poweroff_admission = cyberdeck_poweroff::admission_result::task_unavailable;
        session s(h);
        submit(s, "poweroff"); submit(s, "y");
        check(h.output.find("shutdown task unavailable; reboot required") != std::string::npos,
              "immediate task-admission failure is reported distinctly");
        submit(s, "poweroff"); submit(s, "y");
        check(h.poweroff_admissions == 1, "admission rejection is not retried");
    }
    for (const auto &[reason, expected] :
         {std::pair{cyberdeck_poweroff::admission_result::unavailable, "coordinator unavailable"},
          std::pair{cyberdeck_poweroff::admission_result::already_requested, "request already consumed"},
          std::pair{cyberdeck_poweroff::admission_result::fence_failed, "storage fence unavailable"},
          std::pair{cyberdeck_poweroff::admission_result::watchdog_unavailable, "watchdog unavailable"},
          std::pair{cyberdeck_poweroff::admission_result::task_unavailable, "shutdown task unavailable"}}) {
        fake_host h;
        h.poweroff_admission = reason;
        session s(h);
        submit(s, "poweroff"); submit(s, "y");
        check(h.output.find(expected) != std::string::npos && h.output.find("reboot required") != std::string::npos,
              "typed admission rejection has its specific fail-closed diagnostic");
        submit(s, "poweroff"); submit(s, "y");
        check(h.poweroff_admissions == 1, "typed admission rejection remains one-shot");
    }
    {
        fake_host h;
        session s(h);
        submit(s, "poweroff"); s.handle_key(key::esc); submit(s, "y");
        check(h.poweroff_admissions == 0, "Escape clears the pending confirmation");
        submit(s, "poweroff --help"); submit(s, "y");
        check(h.poweroff_admissions == 0, "help never arms confirmation");
    }
    for (const std::string command : {" poweroff", "poweroff ", "poweroff y", "poweroffx"}) {
        fake_host h;
        session s(h);
        submit(s, command); submit(s, "y");
        check(h.poweroff_admissions == 0, "only an exact poweroff line arms confirmation");
    }
    {
        fake_host h;
        session s(h);
        submit(s, "poweroff");
        h.phase = cyberdeck_session_state::HOST_KEY;
        submit(s, "y");
        check(h.poweroff_admissions == 0 && h.host_key_accepted == 0,
              "context change cancels poweroff without accepting a remote host key");
    }
    {
        fake_host h;
        session s(h);
        bool enter = false;
        s.insert_virtual_text("poweroff\ny\nclear\npoweroff\ny\n", &enter);
        check(h.poweroff_admissions == 1 && h.clear_count == 0,
              "multiline paste requests once and discards its suffix after confirmation");
        check(!s.insert_physical_text("x", 1), "physical input is rejected after shutdown admission");
        check(!s.insert_virtual_text("x", &enter), "virtual input is rejected after shutdown admission");
        check(!s.insert_modified_key('c', 1), "modified input is rejected after shutdown admission");
    }
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

    /* TEST-REL-* (REQ-REL-01..09): o operando relativo de um comando de app e
     * resolvido contra o cwd virtual antes do runtime.  A sonda registra o
     * comando/operando executados; echo, historico e event log continuam
     * carregando a linha digitada. */
    {
        const std::string root = "/tmp/opencode/session-harness";
        std::filesystem::create_directories(root + "/data/fotos/sub");
        std::filesystem::create_directories(root + "/system/assets");
        std::filesystem::create_directories(root + "/tmp");
        std::filesystem::create_directories(root + "/apps");
        std::filesystem::create_directories(root + "/dev");
        /* Diretorios reais fora dos cinco namespaces: o ramo root-SD do
         * operando relativo (`/`, `/imagens`, `/fotos`, `/data2`, `/dev2`). */
        std::filesystem::create_directories(root + "/imagens/sub");
        std::filesystem::create_directories(root + "/imagens/dev");
        std::filesystem::create_directories(root + "/fotos");
        std::filesystem::create_directories(root + "/data2");
        std::filesystem::create_directories(root + "/dev2");

        fake_host h;
        file_operand_probe image("cyberdeck.image_viewer", "image");
        file_operand_probe editor("cyberdeck.editor", "edit");
        file_operand_probe music("cyberdeck.music", "music");
        check(h.apps.register_application(image), "image probe registers");
        check(h.apps.register_application(editor), "editor probe registers");
        check(h.apps.register_application(music), "music probe registers");
        session s(h);

        /* POS-REL-03: relativo resolvido contra o cwd do shell. */
        submit(s, "cd /data/fotos/sub");
        check_eq(h.shell.cwd(), "/data/fotos/sub", "probe cwd enters the data subdirectory");
        h.output.clear();
        submit(s, "image a.bmp");
        check_eq(image.last_operand, "/data/fotos/sub/a.bmp",
                 "POS-REL-03: a relative operand resolves against the session cwd");
        check_eq(h.output, "/data/fotos/sub$ image a.bmp\n",
                 "NEG-REL-34: the echo keeps the typed line, never the expanded one");

        /* NEG-REL-35/36: historico e event log preservam a linha digitada. */
        s.handle_key(key::up);
        check_eq(s.line(), "image a.bmp", "NEG-REL-35: history keeps the typed line");
        s.clear_editor();
        check(!h.events.empty() && h.events.back() == "I shell image a.bmp",
              "NEG-REL-36: the event log keeps the typed line");

        /* POS-REL-04: subcaminho relativo. */
        image.executions = 0;
        submit(s, "image sub/a.bmp");
        check_eq(image.last_operand, "/data/fotos/sub/sub/a.bmp",
                 "POS-REL-04: a relative subdirectory operand is joined to the cwd");

        /* POS-REL-01/02: separadores tabs/multiplos espacos nao mudam o operando. */
        submit(s, "image\ta.bmp");
        check_eq(image.last_operand, "/data/fotos/sub/a.bmp",
                 "POS-REL-01: a tab separator still resolves the operand");
        submit(s, "image   a.bmp");
        check_eq(image.last_operand, "/data/fotos/sub/a.bmp",
                 "POS-REL-02: repeated spaces still resolve the operand");

        /* NEG-REL-07: caminho absoluto e entregue como digitado. */
        submit(s, "image /data/b.bmp");
        check_eq(image.last_operand, "/data/b.bmp",
                 "NEG-REL-07: an absolute operand is never rebased");

        /* POS-REL-05: namespace de sistema tambem e filesystem. */
        submit(s, "cd /system/assets");
        submit(s, "image logo.png");
        check_eq(image.last_operand, "/system/assets/logo.png",
                 "POS-REL-05: a relative operand resolves inside /system");

        /* NEG-REL-08..14: nenhum destino fora de data/system e reescrito. */
        submit(s, "cd /data");
        submit(s, "image /sdcard/a.bmp");
        check_eq(image.last_operand, "/sdcard/a.bmp",
                 "NEG-REL-08: /sdcard stays literal (no virtual rebase)");
        submit(s, "image ../a.bmp");
        check_eq(image.last_operand, "../a.bmp",
                 "NEG-REL-09: traversal is not expanded");
        submit(s, "image ./a.bmp");
        check_eq(image.last_operand, "./a.bmp",
                 "NEG-REL-10: a dot component is not expanded");
        /* Under a subdirectory cwd the resolver would clamp `..`/`.` into a
         * valid sibling target, so the local guard is the only thing that keeps
         * the traversal semantics unchanged there. */
        submit(s, "cd /data/fotos");
        submit(s, "image ../a.bmp");
        check_eq(image.last_operand, "../a.bmp",
                 "NEG-REL-09b: traversal is refused under a subdirectory cwd");
        submit(s, "image ./a.bmp");
        check_eq(image.last_operand, "./a.bmp",
                 "NEG-REL-10b: a dot component is refused under a subdirectory cwd");
        submit(s, "cd /data");
        /* POS-REL2-01: a raiz virtual e um diretorio real do cartao e resolve o
         * operando relativo.  A versao v1 (NEG-REL-11: cwd `/` sem reescrita)
         * foi substituida por este requisito. */
        submit(s, "cd /");
        submit(s, "image a.jpg");
        check_eq(image.last_operand, "/a.jpg",
                 "POS-REL2-01: the root cwd resolves the relative operand");
        submit(s, "cd /tmp");
        submit(s, "image a.bmp");
        check_eq(image.last_operand, "a.bmp",
                 "NEG-REL2-01 (NEG-REL-12 v1): the tmp metadata backend never maps to the card");
        submit(s, "cd /apps");
        submit(s, "image a.bmp");
        check_eq(image.last_operand, "a.bmp",
                 "NEG-REL2-02 (NEG-REL-13 v1): the apps metadata backend does not resolve");
        submit(s, "cd /dev");
        submit(s, "image null");
        check_eq(image.last_operand, "null",
                 "NEG-REL2-03 (NEG-REL-14 v1): /dev is never rewritten (no physical /dev/null)");

        /* NEG-REL-18/19: a resolucao e lexical, portanto um symlink nunca e
         * seguido aqui; o que chega ao app e o caminho digitado sob o cwd. */
        std::error_code link_error;
        const std::string file_link = root + "/data/link.bmp";
        std::filesystem::remove(file_link, link_error);
        link_error.clear();
        std::filesystem::create_symlink("/system/assets/logo.png", file_link, link_error);
        check(!link_error, "fixture installs a file symlink inside /data");
        submit(s, "cd /data");
        submit(s, "image link.bmp");
        check_eq(image.last_operand, "/data/link.bmp",
                 "NEG-REL-18: the rewrite keeps the lexical path and never the symlink target");
        const std::string dir_link = root + "/data/linkdir";
        std::filesystem::remove(dir_link, link_error);
        link_error.clear();
        std::filesystem::create_directory_symlink(root + "/system/assets", dir_link, link_error);
        check(!link_error, "fixture installs a directory symlink inside /data");
        h.output.clear();
        submit(s, "cd /data/linkdir");
        /* Deterministic on the host: the local shell refuses a path whose
         * components include a symlink, so a cwd that is itself a symlink is
         * unreachable through the session and no rewrite can be observed for
         * it.  Assert the refusal instead of accepting both worlds. */
        check(h.output.find("cd:") != std::string::npos,
              "NEG-REL-19: cd through a symlink is refused by the local shell");
        check_eq(h.shell.cwd(), "/data",
                 "NEG-REL-19: the refused cd leaves the session cwd unchanged");

        /* NEG-REL-15..17: sem operando unico nao ha reescrita. */
        submit(s, "cd /data");
        image.executions = 0;
        submit(s, "image");
        check(image.executions == 1 && image.last_operand.empty(),
              "NEG-REL-15: a missing operand reaches the app unchanged");
        submit(s, "image   ");
        check(image.executions == 2 && image.last_operand.empty(),
              "NEG-REL-16: a separators-only operand reaches the app unchanged");
        submit(s, "image a.bmp b.bmp");
        check_eq(image.last_operand, "a.bmp b.bmp",
                 "NEG-REL-17: a second argument disables the rewrite");
        /* Quotes are not shell syntax: the operand stays literal (whatever the
         * prefixing decision), so no quote stripping can appear here. */
        submit(s, "image \"a.bmp\"");
        check(image.last_operand.size() >= 7 &&
                  image.last_operand.compare(image.last_operand.size() - 7, 7, "\"a.bmp\"") == 0,
              "NEG-REL-05: the operand keeps its literal quote characters");

        /* NEG-REL-22: o limite de 8 tokens do runtime permanece. */
        h.output.clear();
        submit(s, "image a b c d e f g h");
        check(h.output.find("unknown command; type help") != std::string::npos,
              "NEG-REL-22: more than eight tokens keeps the runtime rejection");

        /* POS-REL-07 / NEG-REL-23: componente de 64 bytes e limite inclusivo. */
        const std::string component64(64, 'a');
        submit(s, "image " + component64);
        check_eq(image.last_operand, "/data/" + component64,
                 "POS-REL-07: a 64-byte component still resolves");
        const std::string component65(65, 'b');
        submit(s, "image " + component65);
        check_eq(image.last_operand, component65,
                 "NEG-REL-23: a 65-byte component fails closed as pass-through");

        /* POS-REL-08 / NEG-REL-24: 16 componentes inclusivo, 17 rejeitado. */
        std::string fifteen_components;
        for (int part = 0; part < 15; ++part) {
            if (!fifteen_components.empty()) fifteen_components += '/';
            fifteen_components += "d";
        }
        submit(s, "image " + fifteen_components);
        check_eq(image.last_operand, "/data/" + fifteen_components,
                 "POS-REL-08: a resolved path with exactly 16 components resolves");
        submit(s, "image " + fifteen_components + "/e");
        check_eq(image.last_operand, fifteen_components + "/e",
                 "NEG-REL-24: a resolved path with 17 components is not rewritten");

        /* POS-REL-06 / NEG-REL-20 / NEG-REL-21: orcamento de linha do runtime
         * (256 bytes).  A linha executada e "image " + "/data/" + operando. */
        const auto budget_operand = [](std::size_t last_component) {
            std::string value;
            for (int part = 0; part < 3; ++part) {
                value += std::string(64, 'x');
                value += '/';
            }
            value += std::string(last_component, 'y');
            return value;
        };
        const std::string budget_prefix = "image ";
        const std::string budget_cwd = "/data/";
        const std::string at_budget = budget_operand(49);
        check(budget_prefix.size() + budget_cwd.size() + at_budget.size() == 256,
              "fixture expands to exactly 256 bytes");
        submit(s, budget_prefix + at_budget);
        check_eq(image.last_operand, budget_cwd + at_budget,
                 "POS-REL-06: an expansion of exactly 256 bytes is dispatched");
        const std::string over_budget = budget_operand(50);
        check(budget_prefix.size() + budget_cwd.size() + over_budget.size() == 257,
              "fixture expands to exactly 257 bytes");
        submit(s, budget_prefix + over_budget);
        check_eq(image.last_operand, over_budget,
                 "NEG-REL-20: a 257-byte expansion is passed through, not truncated");
        std::string long_typed = "image ";
        for (int part = 0; part < 126; ++part) long_typed += "z/";
        long_typed += "z";
        check(long_typed.size() > 256, "fixture typed line exceeds the runtime budget");
        h.output.clear();
        submit(s, long_typed);
        check(h.output.find("unknown command; type help") != std::string::npos,
              "NEG-REL-21: a typed line above 256 bytes keeps the baseline rejection");

        /* NEG-REL-20b: o orcamento usa o prefixo REAL da linha, nao um literal
         * de 6 bytes.  O mesmo resolvedor de 250 bytes e despachado com um
         * separador e recusado com dois, porque o prefixo cresce. */
        const std::string deep_cwd = "/data/" + std::string(64, 'p') + "/" + std::string(64, 'q') + "/" +
                                     std::string(64, 'r');
        const std::string deep_operand(49, 'w');
        check(deep_cwd.size() + 1 + deep_operand.size() == 250,
              "fixture resolves to exactly 250 bytes under a deep cwd");
        check(deep_cwd.size() + 1 + deep_operand.size() + std::string("image ").size() == 256,
              "fixture reaches exactly the runtime budget with one separator");
        check(deep_cwd.size() + 1 + deep_operand.size() + std::string("  image ").size() == 258,
              "fixture exceeds the runtime budget with two leading separators");
        std::error_code deep_error;
        std::filesystem::create_directories(root + deep_cwd, deep_error);
        check(!deep_error, "fixture creates the deep cwd directories");
        submit(s, "cd " + deep_cwd);
        check_eq(h.shell.cwd(), deep_cwd, "fixture enters the deep cwd");
        submit(s, "image " + deep_operand);
        check_eq(image.last_operand, deep_cwd + "/" + deep_operand,
                 "NEG-REL-20b: one separator keeps the expansion at the budget");
        submit(s, "  image " + deep_operand);
        check_eq(image.last_operand, deep_operand,
                 "NEG-REL-20b: the leading-space prefix counts against the budget");

        /* POS-REL-09 / NEG-REL-26..33: politica do editor em /data. */
        submit(s, "cd /data");
        submit(s, "edit a.txt");
        check_eq(editor.last_operand, "/data/a.txt",
                 "POS-REL-09: edit resolves a relative operand at /data");
        submit(s, "edit sub/a.txt");
        check_eq(editor.last_operand, "/data/sub/a.txt",
                 "POS-REL-11: edit joins a relative subpath under /data");
        submit(s, "cd /data/fotos");
        submit(s, "edit a.txt");
        check_eq(editor.last_operand, "a.txt",
                 "NEG-REL-26: edit keeps the fixed /data policy in a subdirectory");
        submit(s, "cd /");
        submit(s, "edit a.txt");
        check_eq(editor.last_operand, "a.txt",
                 "NEG-REL-27: edit keeps the fixed /data policy outside /data");
        submit(s, "cd /data");
        submit(s, "edit save");
        check_eq(editor.last_operand, "save",
                 "NEG-REL-28: the reserved save subcommand is never a path");
        submit(s, "edit save as x.txt");
        check_eq(editor.last_operand, "save as x.txt",
                 "NEG-REL-29: save as keeps its literal arguments");
        submit(s, "edit save as confirm");
        check_eq(editor.last_operand, "save as confirm",
                 "NEG-REL-33: the save-as confirmation keeps its literal arguments");
        submit(s, "edit /data/a.txt");
        check_eq(editor.last_operand, "/data/a.txt",
                 "NEG-REL-30: an absolute edit operand is unchanged");
        submit(s, "edit ../a.txt");
        check_eq(editor.last_operand, "../a.txt",
                 "NEG-REL-31: edit traversal is not expanded");
        editor.executions = 0;
        submit(s, "edit");
        check(editor.executions == 1 && editor.last_operand.empty(),
              "NEG-REL-32: edit without an operand reaches the app unrewritten");

        /* POS-REL2-02..09 / NEG-REL2-01..08: ramo root-SD.  O cwd `/imagens`
         * (caso reportado), outros diretorios reais e a raiz virtual compoem o
         * operando; metadata (`apps`/`tmp`/`dev`) e o alias `sdcard` nunca sao
         * alvo de reescrita, nem como cwd nem como primeiro componente. */
        h.output.clear();
        submit(s, "cd /imagens");
        check_eq(h.shell.cwd(), "/imagens", "probe cwd enters the SD image directory");
        h.output.clear();
        submit(s, "image a.jpg");
        check_eq(image.last_operand, "/imagens/a.jpg",
                 "POS-REL2-02: /imagens resolves the reported case");
        check_eq(h.output, "/imagens$ image a.jpg\n",
                 "NEG-REL2-37: the echo keeps the typed line in the root-SD branch");
        submit(s, "image sub/a.jpg");
        check_eq(image.last_operand, "/imagens/sub/a.jpg",
                 "POS-REL2-03: a nested relative operand is joined under /imagens");
        submit(s, "cd /fotos");
        submit(s, "image a.bmp");
        check_eq(image.last_operand, "/fotos/a.bmp",
                 "POS-REL2-04: another real SD directory resolves as well");
        submit(s, "cd /imagens/dev");
        submit(s, "image a.jpg");
        check_eq(image.last_operand, "/imagens/dev/a.jpg",
                 "POS-REL2-07: only the first component is filtered (subdir named dev is physical)");
        submit(s, "cd /data2");
        submit(s, "image a.jpg");
        check_eq(image.last_operand, "/data2/a.jpg",
                 "POS-REL2-08: /data2 is not a namespace (exact name, never a prefix)");
        submit(s, "cd /dev2");
        submit(s, "image a.jpg");
        check_eq(image.last_operand, "/dev2/a.jpg",
                 "POS-REL2-09: /dev2 is not the dev namespace (no prefix match)");
        submit(s, "cd /");
        submit(s, "image tmp/a.bmp");
        check_eq(image.last_operand, "tmp/a.bmp",
                 "NEG-REL2-04: a metadata first component is refused in the composed path");
        submit(s, "image apps/x.bmp");
        check_eq(image.last_operand, "apps/x.bmp",
                 "NEG-REL2-05: the apps namespace is refused in the composed path");
        submit(s, "image dev/null");
        check_eq(image.last_operand, "dev/null",
                 "NEG-REL2-06: /dev/null is never reachable from the root cwd");
        submit(s, "image sdcard/a.bmp");
        check_eq(image.last_operand, "sdcard/a.bmp",
                 "NEG-REL2-07: the physical /sdcard alias is refused in the composed path");

        /* NEG-REL2-09..21 / POS-REL2-10..12: forma do operando root-SD. */
        submit(s, "cd /imagens");
        submit(s, "image ../a.jpg");
        check_eq(image.last_operand, "../a.jpg",
                 "NEG-REL2-09: traversal is never expanded from /imagens");
        submit(s, "image ./a.jpg");
        check_eq(image.last_operand, "./a.jpg",
                 "NEG-REL2-10: a dot component is not expanded");
        submit(s, "image sub/../a.jpg");
        check_eq(image.last_operand, "sub/../a.jpg",
                 "NEG-REL2-11: internal traversal is kept literal");
        submit(s, "image sub/");
        check_eq(image.last_operand, "sub/",
                 "NEG-REL2-12: a trailing empty component is kept literal");
        submit(s, "image ../../etc/passwd");
        check_eq(image.last_operand, "../../etc/passwd",
                 "NEG-REL2-13: no relative path leaves the SD root");
        submit(s, "image /imagens/a.jpg");
        check_eq(image.last_operand, "/imagens/a.jpg",
                 "NEG-REL2-14: an absolute operand is untouched");
        submit(s, "image /sdcard/a.jpg");
        check_eq(image.last_operand, "/sdcard/a.jpg",
                 "NEG-REL2-15: an absolute /sdcard operand stays literal");
        submit(s, "image /");
        check_eq(image.last_operand, "/",
                 "NEG-REL2-16: a bare root operand is untouched");
        image.executions = 0;
        submit(s, "image");
        check(image.executions == 1 && image.last_operand.empty(),
              "NEG-REL2-18: a missing operand reaches the app unchanged");
        submit(s, "image   ");
        check(image.executions == 2 && image.last_operand.empty(),
              "NEG-REL2-18: a separators-only operand reaches the app unchanged");
        submit(s, "image a.jpg b.jpg");
        check_eq(image.last_operand, "a.jpg b.jpg",
                 "NEG-REL2-17: a second argument disables the rewrite");
        submit(s, "image \"a.jpg\"");
        check_eq(image.last_operand, "/imagens/\"a.jpg\"",
                 "NEG-REL2-19: quotes are not shell syntax (literal bytes; the app rejects them)");
        submit(s, "image a\\b.jpg");
        check_eq(image.last_operand, "a\\b.jpg",
                 "NEG-REL2-20: a backslash component is refused (shell path rule)");
        h.output.clear();
        submit(s, "image a b c d e f g h");
        check(h.output.find("unknown command; type help") != std::string::npos,
              "NEG-REL2-21: the eight-token runtime limit is preserved");
        submit(s, "image ..a.jpg");
        check_eq(image.last_operand, "/imagens/..a.jpg",
                 "POS-REL2-10: a name starting with dots is not traversal");
        submit(s, "image .a.jpg");
        check_eq(image.last_operand, "/imagens/.a.jpg",
                 "POS-REL2-11: a leading dot inside a name is preserved");
        submit(s, "image\ta.jpg");
        check_eq(image.last_operand, "/imagens/a.jpg",
                 "POS-REL2-12: a tab separator resolves the operand");
        submit(s, "image   a.jpg");
        check_eq(image.last_operand, "/imagens/a.jpg",
                 "POS-REL2-12: repeated spaces resolve the operand");
        submit(s, "  image a.jpg");
        check_eq(image.last_operand, "/imagens/a.jpg",
                 "POS-REL2-12: a leading-space prefix preserves the operand");
        submit(s, "image a.jpg   ");
        check_eq(image.last_operand, "/imagens/a.jpg",
                 "POS-REL2-12: trailing separators do not change the delivered operand");

        /* POS-REL2-13 / NEG-REL2-22: o orcamento de 256 bytes do runtime vale
         * igualmente no ramo root-SD.  A linha executada e
         * "image " + "/imagens/" + operando. */
        const std::string sd_prefix = "/imagens/";
        const std::string sd_at_budget(241, 'k');
        check(std::string("image ").size() + sd_prefix.size() + sd_at_budget.size() == 256,
              "fixture expands to exactly 256 bytes under /imagens");
        submit(s, "image " + sd_at_budget);
        check_eq(image.last_operand, sd_prefix + sd_at_budget,
                 "POS-REL2-13: an expansion of exactly 256 bytes is dispatched");
        const std::string sd_over_budget(242, 'm');
        submit(s, "image " + sd_over_budget);
        check_eq(image.last_operand, sd_over_budget,
                 "NEG-REL2-22: a 257-byte expansion is passed through, not truncated");

        /* POS-REL2-13b / NEG-REL2-22b: o orcamento conta o sufixo digitado que a
         * expansao preserva.  Sem isso uma linha final de 257 bytes chegaria ao
         * runtime e degradaria para `unknown command; type help`. */
        const std::string suffix_at_budget(240, 's');
        check(std::string("image ").size() + sd_prefix.size() + suffix_at_budget.size() + 1 == 256,
              "fixture keeps a one-byte suffix inside the exact budget");
        submit(s, "image " + suffix_at_budget + " ");
        check_eq(image.last_operand, sd_prefix + suffix_at_budget,
                 "POS-REL2-13b: the preserved suffix still fits the exact 256-byte budget");
        const std::string suffix_over_budget(241, 't');
        check(std::string("image ").size() + sd_prefix.size() + suffix_over_budget.size() + 1 == 257,
              "fixture pushes the final line to 257 bytes with the preserved suffix");
        h.output.clear();
        submit(s, "image " + suffix_over_budget + " ");
        check_eq(image.last_operand, suffix_over_budget,
                 "NEG-REL2-22b: a final line of 257 bytes is passed through, never truncated");
        check(h.output.find("unknown command") == std::string::npos,
              "NEG-REL2-22b: the oversized expansion never degrades to unknown command");

        /* POS-REL2-15..18 / NEG-REL2-27: o ramo root-SD nao aplica os tetos
         * 16/64 do VFS; vale a regra do shell (1..255 por componente).  O nome
         * do arquivo continua sendo decidido pelo app (5..128 com extensao). */
        const std::string long_dir(65, 'g');
        std::error_code long_error;
        std::filesystem::create_directories(root + "/imagens/" + long_dir, long_error);
        check(!long_error, "fixture creates a 65-byte directory under /imagens");
        submit(s, "cd /imagens/" + long_dir);
        check_eq(h.shell.cwd(), "/imagens/" + long_dir, "fixture enters the 65-byte directory");
        submit(s, "image a.jpg");
        check_eq(image.last_operand, "/imagens/" + long_dir + "/a.jpg",
                 "POS-REL2-15: a 65-byte component resolves in the root-SD branch");
        std::string sixteen_components;
        for (int part = 0; part < 16; ++part) {
            if (!sixteen_components.empty()) sixteen_components += '/';
            sixteen_components += "e";
        }
        submit(s, "cd /imagens");
        submit(s, "image " + sixteen_components);
        check_eq(image.last_operand, "/imagens/" + sixteen_components,
                 "POS-REL2-15: 17 components resolve (the VFS 16-component cap does not apply)");
        const std::string name128 = std::string(124, 'n') + ".jpg";
        check(name128.size() == 128, "fixture builds a 128-byte image name");
        submit(s, "image " + name128);
        check_eq(image.last_operand, "/imagens/" + name128,
                 "POS-REL2-16: a 128-byte name is handed to the app unchanged");
        const std::string name129 = std::string(125, 'o') + ".jpg";
        check(name129.size() == 129, "fixture builds a 129-byte image name");
        submit(s, "image " + name129);
        check_eq(image.last_operand, "/imagens/" + name129,
                 "NEG-REL2-27: a 129-byte name is not truncated (the app enforces 128)");
        submit(s, "image a\u00e7\u00e3o.jpg");
        check_eq(image.last_operand, "/imagens/a\u00e7\u00e3o.jpg",
                 "POS-REL2-17: UTF-8 bytes are preserved without normalization");
        submit(s, "image A.JPG");
        check_eq(image.last_operand, "/imagens/A.JPG",
                 "POS-REL2-18: case is preserved (no folding)");

        /* NEG-REL2-28 / NEG-REL2-33 / NEG-REL2-36: o editor fora de `/data`
         * mantem a politica fixa; a reescrita root-SD e lexical (symlink e cwd
         * removido sao resolvidos depois pelo backend, nao aqui). */
        submit(s, "edit a.txt");
        check_eq(editor.last_operand, "a.txt",
                 "NEG-REL2-28: edit is never rewritten outside /data");
        std::error_code link_error_rel2;
        std::filesystem::remove(root + "/imagens/link.jpg", link_error_rel2);
        link_error_rel2.clear();
        std::filesystem::create_symlink("/system/assets/logo.png", root + "/imagens/link.jpg",
                                        link_error_rel2);
        check(!link_error_rel2, "fixture installs a file symlink inside /imagens");
        submit(s, "image link.jpg");
        check_eq(image.last_operand, "/imagens/link.jpg",
                 "NEG-REL2-33: the rewrite keeps the lexical path and never the symlink target");
        std::filesystem::create_directories(root + "/imagens2");
        submit(s, "cd /imagens2");
        check_eq(h.shell.cwd(), "/imagens2", "fixture enters /imagens2");
        std::error_code removed_error;
        std::filesystem::remove(root + "/imagens2", removed_error);
        check(!removed_error, "fixture removes the cwd directory after cd");
        submit(s, "image a.jpg");
        check_eq(image.last_operand, "/imagens2/a.jpg",
                 "NEG-REL2-36: a removed cwd still composes; the backend fails closed");

        /* POS-REL2-10: sem estado residual entre execucoes. */
        submit(s, "cd /data");
        image.executions = 0;
        submit(s, "image a.bmp");
        const std::string first_operand = image.last_operand;
        submit(s, "image a.bmp");
        check(image.executions == 2 && image.last_operand == first_operand &&
                  first_operand == "/data/a.bmp",
              "POS-REL-10: repeated invocations resolve identically");

        /* NEG-REL-38: o help detalhado e resolvido pela sessao, nunca pelo app. */
        image.executions = 0;
        h.output.clear();
        submit(s, "image help");
        check(image.executions == 0 && h.output.find("image [app]") != std::string::npos,
              "NEG-REL-38: the detail help path never reaches the application");

        /* NEG-REL2-39 / NEG-REL-01..04 / 41 / REQ-REL-09: fora da whitelist
         * nada muda (o caso de `image` roda com cwd `/data`). */
        h.output.clear();
        submit(s, "Image a.bmp");
        check(h.output.find("unknown command; type help") != std::string::npos,
              "NEG-REL-01: the whitelist is case sensitive");
        h.output.clear();
        submit(s, "app image a.bmp");
        check(h.output.find("usage: app") != std::string::npos,
              "NEG-REL2-39: app never rewrites its operand");
        h.output.clear();
        submit(s, "image2 a.bmp");
        check(h.output.find("unknown command; type help") != std::string::npos,
              "NEG-REL-03: only the declared command is rewritten");
        submit(s, "music queue musica");
        check_eq(music.last_operand, "queue musica",
                 "NEG-REL2-40: music queue is not treated as a file operand");
        h.cat_line.clear();
        submit(s, "cat a.txt");
        check_eq(h.cat_line, "cat a.txt",
                 "NEG-REL2-41: the local cat command keeps its typed line");


        /* NEG-REL-39: poweroff (prompt e help) permanece intocado.  Fica por
         * ultimo porque arma a confirmacao da sessao. */
        h.output.clear();
        submit(s, "poweroff -h");
        check(h.output.find(cyberdeck_shell_help::command_text("poweroff")) != std::string::npos,
              "NEG-REL-39: poweroff -h keeps the pinned help block");
        h.output.clear();
        submit(s, "poweroff");
        check(h.output.find("Type exactly y") != std::string::npos,
              "NEG-REL-39: poweroff keeps its exact-y confirmation prompt");
    }

    /* NEG-REL-04 / NEG-REL-37: sem app registrado nao ha reescrita, e o modo
     * SSH continua enviando a linha digitada verbatim. */
    {
        fake_host h;
        session s(h);
        h.output.clear();
        submit(s, "image a.bmp");
        check(h.output.find("unknown command; type help") != std::string::npos,
              "NEG-REL-04: an unregistered app command is not rewritten");
        h.ssh_visible = true;
        h.phase = cyberdeck_session_state::CONNECTED;
        submit(s, "image a.bmp");
        check_eq(h.ssh_sent, "image a.bmp\n",
                 "NEG-REL-37: SSH sends the typed line verbatim");
    }

    /* TEST-ECHO-01/02: the local echo names the directory from before the
     * command.  In particular, `cd` must not rewrite its own prompt. */
    {
        std::filesystem::create_directories("/tmp/opencode/session-harness/child/nested");
        fake_host h;
        session s(h);
        submit(s, "cd child");
        check_eq(h.shell.cwd(), "/child", "cd enters a nested cwd");
        check(h.output.find("/$ cd child\n") != std::string::npos,
              "root command echo captures the root cwd");
        h.output.clear();
        submit(s, "cd nested");
        check_eq(h.shell.cwd(), "/child/nested", "a second cd enters a deeper cwd");
        check(h.output.find("/child$ cd nested\n") != std::string::npos,
              "cd echo captures the previous nested cwd");
    }

    /* TEST-HELP-CAT-PINNED / TEST-HELP-HIERARCHY-01 (REQ-HELP-01/02/03/04):
     * the session emits exactly the formatter catalog after its echo, and the
     * formatter matches the pinned contract entry by entry. */
    {
        fake_host h;
        session s(h);
        submit(s, "help");
        const std::string help_echo = "/$ help\n";
        check(h.output.compare(0, help_echo.size(), help_echo) == 0,
              "help command keeps its local terminal echo");
        /* Wiring only: the session output equals the formatter catalog, so a
         * different path cannot emit a second help list. */
        check(h.output.compare(help_echo.size(), cyberdeck_shell_help::text().size(),
                               cyberdeck_shell_help::text()) == 0,
              "help prints the shared catalog after its echo");
        /* TEST-HELP-CAT-PINNED (REQ-HELP-01..04): full content pin. */
        check_eq(cyberdeck_shell_help::text(), std::string(kLegacyHelpPinned),
                 "legacy catalog content, tags, parameter explanations and order are pinned");
        /* TEST-HELP-HIERARCHY-01 (REQ-HELP-01): command-tag header line above
         * an indented usage-description line, per entry. */
        check(h.output.find("ls [terminal]\n  ls [-a] [path] - list directory contents\n") != std::string::npos &&
              h.output.find("cat [terminal]\n  cat <file> - print a regular file\n") != std::string::npos,
              "legacy ls and cat render a tagged header over an indented usage-description line");
        check(h.output.find("help [terminal]\n  help - show this help\n") != std::string::npos,
              "legacy help entry is tagged terminal");
    }

    /* TEST-HELP-ORDER-01, TEST-HELP-ROWS-01 (REQ-HELP-03/04): every command
     * appears once, in canonical order, under its own [terminal] header, with
     * two-space-indented parameter rows and no blank or ragged lines. */
    {
        const std::string legacy = cyberdeck_shell_help::text();
        std::size_t position = 0;
        for (const char *command : kLegacyOrder) {
            const std::string header = std::string(command) + " [terminal]\n";
            const std::size_t found = legacy.find(header, position);
            check(found != std::string::npos,
                  "each legacy command has a [terminal] header in canonical order");
            position = found + 1;
        }
        check(legacy.find("\n\n") == std::string::npos && legacy.back() == '\n',
              "help catalog has no blank lines and ends with a single newline");
        for (const std::string &line : split_lines(legacy)) {
            if (line[0] == ' ')
                check(line.compare(0, 2, "  ") == 0 && line[2] != ' ',
                      "every legibility row is indented by exactly kParameterIndent");
        }
    }

    /* TEST-HELP-CMD-01 (REQ-HELP-01/02): the recorded hierarchy and parameter
     * explanations reach all live help paths: poweroff -h/--help handled in
     * the session, local-shell -h rows, and every help variant. */
    {
        fake_host h;
        session s(h);
        submit(s, "poweroff --help");
        check_eq(h.output, "/$ poweroff --help\n" + cyberdeck_shell_help::command_text("poweroff"),
                 "poweroff --help prints the pinned poweroff block");
        check(h.poweroff_admissions == 0, "per-command help never arms poweroff confirmation");
        h.output.clear();
        submit(s, "poweroff -h");
        check_eq(h.output, "/$ poweroff -h\n" + cyberdeck_shell_help::command_text("poweroff"),
                 "poweroff -h prints the same pinned block");
        h.output.clear();
        submit(s, "ls -h");
        check_eq(h.output, "/$ ls -h\n" + cyberdeck_shell_help::command_text("ls"),
                 "local shell -h forwards the pinned ls block through the session");
        h.output.clear();
        submit(s, "help -h");
        check_eq(h.output, "/$ help -h\n" + cyberdeck_shell_help::text(),
                 "help -h prints the full catalog through the session");
        h.output.clear();
        submit(s, "help --help");
        check_eq(h.output, "/$ help --help\n" + cyberdeck_shell_help::text(),
                 "help --help prints the same full catalog");
        /* Negative row: partial help requests fail closed with the exact usage
         * error instead of a partial catalog. */
        h.output.clear();
        submit(s, "help wifi");
        check_eq(h.output, "/$ help wifi\nusage: help\n",
                 "help with an unknown target reports the exact usage error");
    }

    /* TEST-HELP-LEGACY-PARITY, RUNTIME-MANIFESTS, DISTINCTION, SAFE-MODE,
     * COLLISION-PRECEDENCE and DETERMINISM: dynamic help enumerates registered
     * manifest metadata, not lifecycle state, and filters duplicate/colliding
     * commands while preserving the full legacy catalog. */
    {
        fake_host h;
        cyberdeck_apps::manifest editor_manifest{};
        editor_manifest.id = "cyberdeck.editor";
        editor_manifest.command = "edit";
        help_manifest_app editor(editor_manifest);
        cyberdeck_apps::manifest demo_manifest{};
        demo_manifest.id = "cyberdeck.demo";
        demo_manifest.type = cyberdeck_apps::app_type::demo;
        demo_manifest.command = "demo";
        demo_manifest.commands[0] = "demo-tool";
        demo_manifest.commands[1] = "ls";
        demo_manifest.commands[2] = "demo-tool";
        demo_manifest.command_count = 3;
        help_manifest_app demo(demo_manifest);
        cyberdeck_apps::manifest no_command_manifest{};
        no_command_manifest.id = "no-command";
        help_manifest_app no_command(no_command_manifest);
        check(h.apps.register_application(editor), "editor manifest registers");
        check(h.apps.register_application(demo), "demo manifest registers");
        check(h.apps.register_application(no_command), "commandless manifest registers");
        check(h.apps.state("cyberdeck.editor") == cyberdeck_apps::app_state::registered,
              "editor lifecycle remains stopped before help");
        session s(h);
        submit(s, "help");
        const std::string first = h.output;
        const std::string expected_output =
            "/$ help\n" + std::string(cyberdeck_shell_help::text()) +
            "edit [app]\n"
            "  start compiled application\n"
            "demo [app]\n"
            "  start compiled application\n"
            "demo-tool [app]\n"
            "  start compiled application\n";
        check_eq(first, expected_output,
                 "help echo and terminal/app entries render exactly in stable order");
        /* TEST-HELP-APP-HIERARCHY-01 (REQ-HELP-01/02/03/04): dynamic [app]
         * entries now render as a hierarchical block — a `<command> [app]`
         * header at column 0 followed by the indented description line —
         * mirroring the legacy [terminal] two-line contract. */
        check(first.find("edit [app]\n  start compiled application\n") != std::string::npos,
              "registered editor command renders hierarchical app block");
        check(first.find("demo [app]\n  start compiled application\n") != std::string::npos &&
              first.find("demo-tool [app]\n  start compiled application\n") != std::string::npos,
              "manifest command and declared commands render hierarchical app blocks");
        check(first.find("no-command [app]") == std::string::npos,
              "registered manifest without command is omitted");
        check(first.find("ls [app]") == std::string::npos,
              "legacy collision is not duplicated as an application command");
        check(first.find("demo-tool [app]\n  start compiled application\n", first.find("demo-tool") + 1) == std::string::npos,
              "duplicate declarations produce one help row");
        check(first.find("[terminal]") != std::string::npos && first.find("[app]") != std::string::npos,
              "terminal and app entries have distinct tags");
        check(h.apps.state("cyberdeck.editor") == cyberdeck_apps::app_state::registered,
              "help does not start registered applications");
        h.output.clear();
        submit(s, "help");
        check_eq(h.output, first, "manifest help order and serialization are deterministic");
        h.output.clear();
        submit(s, "ls");
        check(editor.executions == 0 && demo.executions == 0,
              "legacy command collision remains owned by terminal dispatcher");
        check(h.output.find("unknown command") == std::string::npos,
              "legacy ls dispatch does not fall through to the app runtime");
    }

    {
        fake_host h;
        session s(h);
        check(h.sd_io.state() == cyberdeck_sd_io::gate_state::open,
              "session fixture starts with an OPEN SD coordinator");
        const auto owner = h.sd_io.begin_close();
        submit(s, "cd /");
        check(h.output.find("cd: SD storage is closing or unavailable") != std::string::npos,
              "session rejects SD reads while CLOSING");
        check_eq(h.shell.cwd(), "/", "rejected cd preserves cwd");
        check(h.sd_io.active_leases() == 0, "rejected session read acquires no lease");
        check(h.sd_io.drain(owner, std::chrono::steady_clock::now()) ==
                  cyberdeck_sd_io::drain_result::drained,
              "session fixture drains to CLOSED");
        h.output.clear();
        submit(s, "touch /data/poweroff-fence-test");
        check(h.output.find("touch: SD storage is closing or unavailable") != std::string::npos,
              "session rejects SD writes while CLOSED");
        check(h.sd_io.state() == cyberdeck_sd_io::gate_state::closed &&
                  h.sd_io.active_leases() == 0,
              "session commands cannot reopen the gate or acquire leases");
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

    /* TEST-ECHO-05: Wi-Fi credentials are owned by the connection flow, not
     * by the terminal echo. */
    {
        fake_host h;
        session s(h);
        h.search_menu.add_ap("lab", -40, 3, false);
        h.wifi = wifi_ui_state_t::SEARCH_SELECT;
        s.handle_key(key::enter);
        check(h.wifi == wifi_ui_state_t::SEARCH_PASSWORD,
              "secured Wi-Fi selection enters the password flow");
        submit(s, "wifi-secret");
        check_eq(h.connected_password, "wifi-secret", "Wi-Fi password reaches the service port");
        check(h.output.find("wifi-secret") == std::string::npos,
              "Wi-Fi password is never echoed to terminal output");
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

    {
        fake_host h;
        h.phase = cyberdeck_session_state::PASSWORD;
        session s(h);
        submit(s, "s3cret");
        check_eq(h.ssh_password, "s3cret", "a password phase sends the password");
        check(h.output.find("s3cret") == std::string::npos,
              "password input is never echoed to terminal output");
    }

    {
        fake_host h;
        h.phase = cyberdeck_session_state::HOST_KEY;
        session s(h);
        submit(s, "");
        check(h.host_key_accepted == 1, "host key phase accepts on enter");
        check(h.output.find("confirma") == std::string::npos,
              "host-key input is never echoed to terminal output");
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
            check(h.output.find("123456") == std::string::npos,
                  "BLE passkey input is never echoed to terminal output");
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
            check(h.output.find("12") == std::string::npos,
                  "BLE passkey fallback input is never echoed to terminal output");
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
        /* The command echo also contains '/', so count the pwd result line. */
        std::size_t hits = 0;
        for (std::size_t at = h.output.find("/\n"); at != std::string::npos;
             at = h.output.find("/\n", at + 1)) ++hits;
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

    /* --- completacao por Tab (REQ-TAB-01..05) --------------------------- */
    /* Teclado virtual/tecla dedicada: primeiro Tab insere o prefixo comum. */
    {
        fake_host h;
        session s(h);
        h.completion_owner = &s;
        type(s, "r");
        s.handle_key(key::next_tab);
        check_eq(s.line(), "rm", "Tab inserts the common prefix of the catalog commands");
        check(h.completion_requests == 0,
                      "command completion needs no SD scan");
        /* Segundo Tab sem edicao lista os candidatos bounded. */
        s.handle_key(key::next_tab);
        check(h.output.find("rmdir") != std::string::npos,
              "the second Tab lists the command candidates");
        check_eq(s.line(), "rm", "listing does not edit the line");
    }

    /* Teclado fisico entrega Tab como texto ('\t'). */
    {
        fake_host h;
        session s(h);
        h.completion_owner = &s;
        type(s, "rmd");
        check(s.insert_physical_text("\t", 1), "a single Tab byte is consumed by the shell");
        check_eq(s.line(), "rmdir", "the physical Tab completes the command");
    }

    /* Zero candidatos preservam a linha. */
    {
        fake_host h;
        session s(h);
        h.completion_owner = &s;
        type(s, "zzz");
        const std::string before = h.output;
        s.handle_key(key::next_tab);
        check_eq(s.line(), "zzz", "an unknown command prefix is not rewritten");
        check_eq(h.output, before, "zero command candidates add no terminal output");
    }

    /* Caminho: `cd` pede somente diretorios e completa com '/' final. */
    {
        fake_host h;
        session s(h);
        h.completion_owner = &s;
        h.completion_candidates.push_back(cyberdeck_shell_completion::candidate{"albuns", true});
        type(s, "cd al");
        s.handle_key(key::next_tab);
        check(h.completion_requests == 1,
                      "a path completion is dispatched to the async port");
        check_eq(h.last_completion_request.directory, "/",
                 "the relative path resolves against the virtual cwd");
        check(h.last_completion_request.directory_only, "cd requests directories only");
        check_eq(s.line(), "cd albuns/", "the directory completion keeps the trailing slash");
    }

    /* Resultado obsoleto (token superado) e descartado. */
    {
        fake_host h;
        session s(h);
        h.completion_owner = &s;
        h.completion_async = true;
        h.completion_candidates.push_back(cyberdeck_shell_completion::candidate{"albuns", true});
        type(s, "cd al");
        s.handle_key(key::next_tab);
        const std::uint64_t token = h.last_completion_token;
        check(token != 0, "the pending request carries a token");
        check_eq(s.line(), "cd al", "an asynchronous Tab leaves the line untouched");
        h.deliver_completion(token + 1);
        check_eq(s.line(), "cd al", "a stale token is discarded");
        h.deliver_completion(token);
        check_eq(s.line(), "cd albuns/", "the matching token completes the token");
        h.deliver_completion(token);
        check_eq(s.line(), "cd albuns/", "a replayed delivery is discarded");
    }

    /* Resultado cuja linha mudou durante a enumeracao e descartado. */
    {
        fake_host h;
        session s(h);
        h.completion_owner = &s;
        h.completion_async = true;
        h.completion_candidates.push_back(cyberdeck_shell_completion::candidate{"albuns", true});
        type(s, "cd al");
        s.handle_key(key::next_tab);
        const std::uint64_t token = h.last_completion_token;
        type(s, "b");
        h.deliver_completion(token);
        check_eq(s.line(), "cd alb", "a result for an edited line is discarded");
    }

    /* Sem worker (porta recusa): a linha fica intacta. */
    {
        fake_host h;
        session s(h);
        h.completion_owner = &s;
        h.completion_accept = false;
        type(s, "cd al");
        s.handle_key(key::next_tab);
        check_eq(s.line(), "cd al", "a refused completion request preserves the line");
    }

    /* SSH/password mantem o Tab literal: o byte pertence ao PTY remoto. */
    {
        fake_host h;
        session s(h);
        h.completion_owner = &s;
        h.phase = cyberdeck_session_state::CONNECTED;
        type(s, "cat ");
        s.handle_key(key::next_tab);
        check_eq(s.line(), "cat \t", "a connected session inserts a literal tab");
        check(h.completion_requests == 0,
                      "SSH never triggers the completion port");
    }
    {
        fake_host h;
        session s(h);
        h.completion_owner = &s;
        h.phase = cyberdeck_session_state::PASSWORD;
        s.handle_key(key::next_tab);
        check_eq(std::string(s.editor().line()), "\t",
                 "the password line keeps the tab literal");
        check(h.completion_requests == 0,
                      "the password prompt never triggers completion");
    }

    /* Wi-Fi pendente (senha de rede) tambem permanece literal. */
    {
        fake_host h;
        session s(h);
        h.completion_owner = &s;
        h.wifi = wifi_ui_state_t::SEARCH_PASSWORD;
        s.handle_key(key::next_tab);
        check_eq(std::string(s.editor().line()), "\t",
                 "the Wi-Fi password prompt keeps the tab literal");
        check(h.completion_requests == 0,
                      "the Wi-Fi password flow never triggers completion");
    }

    /* Lista BLE com posse da entrada nao completa o menu. */
    {
        fake_host h;
        session s(h);
        h.completion_owner = &s;
        h.ble.begin_search();
        s.handle_key(key::next_tab);
        check(h.completion_requests == 0,
                      "no completion while the BLE list owns the input");
    }

    /* Tab fisico em SSH tambem permanece literal (o byte vai para o PTY). */
    {
        fake_host h;
        session s(h);
        h.completion_owner = &s;
        h.phase = cyberdeck_session_state::CONNECTED;
        type(s, "cat ");
        check(s.insert_physical_text("\t", 1), "the literal Tab byte is accepted by the line");
        check_eq(s.line(), "cat \t", "the physical Tab is inserted literally over SSH");
        check(h.completion_requests == 0,
                      "the physical Tab never reaches the completion port over SSH");
    }

    /* Texto fisico com Tab embutido nunca dispara completacao (so um byte). */
    {
        fake_host h;
        session s(h);
        h.completion_owner = &s;
        check(s.insert_physical_text("a\tb", 3), "multi-byte physical text is accepted");
        check_eq(s.line(), "a\tb", "only a single Tab byte triggers completion");
        check(h.completion_requests == 0,
                      "embedded Tab bytes are inserted literally");
    }

    /* Cursor no meio do token: completa o token inteiro e mantem o resto. */
    {
        fake_host h;
        session s(h);
        h.completion_owner = &s;
        h.completion_candidates.push_back(cyberdeck_shell_completion::candidate{"albuns", true});
        type(s, "cd al");
        s.handle_key(key::left);
        check(s.cursor() == 4, "the cursor moves inside the token");
        s.handle_key(key::next_tab);
        check_eq(s.line(), "cd albuns/", "an interior cursor completes the whole token");
        check(s.cursor() == 10, "the cursor lands after the inserted name");
    }

    /* Lista truncada: o primeiro Tab nunca estreita; o segundo apenas lista. */
    {
        fake_host h;
        session s(h);
        h.completion_owner = &s;
        h.completion_truncated = true;
        h.completion_candidates.push_back(cyberdeck_shell_completion::candidate{"albuns", true});
        h.completion_candidates.push_back(cyberdeck_shell_completion::candidate{"album-canvas", true});
        type(s, "cd al");
        s.handle_key(key::next_tab);
        check_eq(s.line(), "cd al", "a truncated multi-candidate set never narrows the token");
        s.handle_key(key::next_tab);
        check_eq(s.line(), "cd al", "listing keeps the truncated token untouched");
        check(h.output.find("album-canvas") != std::string::npos,
              "the truncated set is listed");
        check(h.output.find(cyberdeck_shell_completion::k_truncated_note.data()) != std::string::npos,
              "the truncated listing is announced");
    }

    /* Cache do segundo Tab e invalidado por edicao e por uma lista ja exibida. */
    {
        fake_host h;
        session s(h);
        h.completion_owner = &s;
        h.completion_candidates.push_back(cyberdeck_shell_completion::candidate{"albuns", true});
        h.completion_candidates.push_back(cyberdeck_shell_completion::candidate{"album-canvas", true});
        type(s, "cd al");
        s.handle_key(key::next_tab);
        check_eq(s.line(), "cd albu", "the common prefix is inserted on the first Tab");
        check(h.completion_requests == 1, "the first Tab enumerated once");
        s.handle_key(key::next_tab);
        check(h.completion_requests == 1, "the second Tab reuses the cached set");
        check(h.output.find("albuns/") != std::string::npos, "the second Tab lists the cached set");
        s.handle_key(key::next_tab);
        check(h.completion_requests == 2,
                      "a third Tab re-enumerates instead of repeating the list");
        /* Edicao invalida o cache: o proximo Tab volta a enumerar. */
        s.handle_key(key::backspace);
        s.handle_key(key::next_tab);
        check(h.completion_requests == 3, "editing the line invalidates the cached set");
    }

    /* Lista de um unico candidato nao deixa cache para listar depois. */
    {
        fake_host h;
        session s(h);
        h.completion_owner = &s;
        h.completion_candidates.push_back(cyberdeck_shell_completion::candidate{"albuns", true});
        type(s, "cd al");
        s.handle_key(key::next_tab);
        check_eq(s.line(), "cd albuns/", "a single path candidate completes the token");
        const std::string before = h.output;
        s.handle_key(key::next_tab);
        check_eq(h.output, before, "a completed single candidate adds no listing");
        check(h.completion_requests == 2,
                      "the next Tab re-enumerates the completed path");
    }

    std::printf("shell session: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
