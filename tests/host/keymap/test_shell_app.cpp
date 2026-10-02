// Host test for the foreground shell application (Fase 7 do plano OS-TRANSFORMATION).
//
// Cobre a propriedade central da fase: o prompt e a linha de comando pertencem
// a `cyberdeck.shell`, o supervisor e o unico dono do ciclo de vida do console,
// SSH e um modo de sessao do console (nao um console separado) e o
// supervisor nunca captura um comando legado.
//
// Nao usa LVGL, FreeRTOS, ESP-IDF, hardware ou simulador: o host de sessao
// e falso e apenas o modulo puro do console e o runtime compilado sao ligados.

#include "apps/runtime/cyberdeck_app_runtime.h"
#include "apps/shell/cyberdeck_shell_app.h"
#include "apps/shell/cyberdeck_shell_console.h"
#include "apps/shell/cyberdeck_shell_session.h"
#include "apps/shell/cyberdeck_local_shell.h"
#include "apps/shell/cyberdeck_edit_line.h"

#include <cstdio>
#include <string>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char *message)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", message);
    }
}

/* Host falso: registra apenas o que a aplicacao precisa para compor o prompt
 * e o modo de sessao.  Todo o resto e no-op porque estes testes exercitam a
 * propriedade de ownership, nao os fluxos de servico. */
class fake_host final : public cyberdeck_shell_session::host {
public:
    void append_output_line(const std::string &) override {}
    void write_output(const char *, std::size_t) override {}
    void append_output_text(const std::string &) override {}
    void clear_output() override {}
    void render() override { ++renders; }

    cyberdeck_ble::state_machine &ble_model() override { return ble_; }
    cyberdeck_ble::device_list &ble_scan_devices() override { return devices_; }
    void clear_ble_notice() override {}
    void mark_ble_transient_uncommitted() override {}
    void submit_ble_actions() override {}
    void sync_ble_transient() override {}
    std::size_t copy_ble_bonds(ble_bond_snapshot_t *, std::size_t) override { return 0; }

    cyberdeck_shell_session::wifi_ui_state_t &wifi_state() override { return wifi_state_; }
    cyberdeck_wifi::state_machine &wifi_model() override { return wifi_; }
    cyberdeck_wifi_search_menu &wifi_search_menu() override { return search_menu_; }
    cyberdeck_wifi_saved_menu &wifi_saved_menu() override { return saved_menu_; }
    std::uint64_t &wifi_scan_generation() override { return scan_generation_; }
    bool wifi_begin_scan(std::uint64_t) override { return true; }
    void wifi_cancel_scan() override {}
    std::string build_wifi_audit_save_path() override { return {}; }
    void wifi_audit_begin() override {}
    bool wifi_audit_save(const std::string &) override { return true; }

    esp_err_t wifi_connect(const char *, const char *) override { return ESP_OK; }
    esp_err_t wifi_cancel_connection() override { return ESP_OK; }
    void wifi_forget(const char *) override {}
    bool wifi_enabled() const override { return true; }
    std::uint64_t wifi_current_token() const override { return 1; }
    bool wifi_status(wifi_status_t *) override { return false; }
    bool wifi_storage_ready() override { return false; }
    bool wifi_storage_load_all(wifi_saved_list_t *) override { return false; }
    bool wifi_storage_find(const char *, char *, std::size_t) override { return false; }

    cyberdeck_session_state ssh_phase() const override { return ssh; }
    esp_err_t ssh_send_data(const char *, std::size_t) override { return ESP_OK; }
    esp_err_t ssh_send_password(const char *) override { return ESP_OK; }
    void ssh_accept_host_key() override {}

    void set_ssh_visible(bool) override {}
    void reset_ssh_filter() override {}
    void discard_ssh_composer() override {}
    cyberdeck_ssh_line_composer &ssh_composer() override { return composer_; }
    esp_err_t ssh_connect(const char *, const char *, int) override { return ESP_OK; }

    void screen_turn_on() override {}
    void screen_turn_off() override {}
    esp_err_t screen_set_timeout_minutes(std::uint16_t) override { return ESP_OK; }

    bool battery_protection_set_enabled(bool) override { return true; }
    bool battery_protection_started() const override { return false; }
    bool battery_protection_snapshot(cyberdeck_battery_protection::snapshot *) const override
    {
        return false;
    }

    void log_event(char, const char *, const char *) override {}
    std::string recent_events(std::size_t) override { return {}; }
    bool cat_enqueue(const char *, const char *) override { return true; }
    cyberdeck_local_shell &local_shell() override { return local_; }
    cyberdeck_apps::runtime &app_runtime() override { return runtime_; }

    std::size_t renders = 0;
    mutable cyberdeck_session_state ssh = cyberdeck_session_state::MENU;

private:
    cyberdeck_local_shell local_{"/tmp", "/"};
    cyberdeck_ble::state_machine ble_;
    cyberdeck_ble::device_list devices_;
    cyberdeck_shell_session::wifi_ui_state_t wifi_state_{
        cyberdeck_shell_session::wifi_ui_state_t::IDLE};
    cyberdeck_wifi::state_machine wifi_;
    cyberdeck_wifi_search_menu search_menu_;
    cyberdeck_wifi_saved_menu saved_menu_;
    std::uint64_t scan_generation_ = 0;
    cyberdeck_ssh_line_composer composer_;
    cyberdeck_apps::runtime runtime_;
};

/* Dependencia declarada do shell, usada para provar que `app stop` nao pode
 * remover o console em cascata. */
class stub_event_log;

/* Aplicacao auxiliar que declara comandos, inclusive um legado, para provar
 * que um comando legado nunca e capturado pelo supervisor. */
class command_app final : public cyberdeck_apps::application {
public:
    command_app() = default;

    command_app(std::string_view id, std::string_view command,
                std::initializer_list<std::string_view> commands)
    {
        manifest_.id = id;
        manifest_.name = "Command app";
        manifest_.command = command;
        for (const std::string_view declared : commands) {
            if (manifest_.command_count == manifest_.commands.size()) break;
            manifest_.commands[manifest_.command_count++] = declared;
        }
    }

    const cyberdeck_apps::manifest &get_manifest() const override { return manifest_; }
    bool start() override { running_ = true; return true; }
    bool stop() override { running_ = false; return true; }
    bool running() const override { return running_; }
    cyberdeck_apps::result execute(std::string_view, std::string_view) override
    {
        ++executions;
        return {cyberdeck_apps::result_status::handled, "ran\n"};
    }

    int executions = 0;

private:
    cyberdeck_apps::manifest manifest_{"helper.app", "Helper", "1.0.0", "command helper", {}};
    bool running_ = false;
};

void prompt_composition_contract() {
    cyberdeck_shell_console::surface_state state;
    state.cwd = "/data";
    cyberdeck_shell_console::line_input input;
    input.line = "pwd";
    input.visible_line = "pwd";
    input.cursor_bytes = input.line.size();

    const cyberdeck_shell_console::line_view menu = cyberdeck_shell_console::compose(state, input);
    check(menu.marker == "/data$ ", "local prompt is derived from the shell cwd");
    check(menu.fitted_line == "pwd", "the typed line is rendered after the prompt");
    check(menu.text() == "/data$ pwd", "prompt and line compose a single tail");
    check(menu.cursor_chars() == menu.marker.size() + input.line.size(),
          "the cursor counts codepoints of marker plus line");

    state.input_owned_elsewhere = true;
    const cyberdeck_shell_console::line_view owned = cyberdeck_shell_console::compose(state, input);
    check(owned.marker.empty(), "no local prompt while another surface owns input");
    check(owned.fitted_line == "pwd", "the line stays visible without a prompt");
    state.input_owned_elsewhere = false;

    state.password_pending = true;
    const cyberdeck_shell_console::line_view secret =
        cyberdeck_shell_console::compose(state, input);
    check(secret.marker == "Password: ", "password state uses its own marker");
    check(secret.fitted_line == "***", "the pending password is masked");
    check(secret.fitted_line.find('p') == std::string::npos, "masking hides the typed bytes");
    state.password_pending = false;

    state.ssh_connected = true;
    const cyberdeck_shell_console::line_view remote =
        cyberdeck_shell_console::compose(state, input);
    check(remote.marker.empty(), "SSH online renders no local prompt");
    check(remote.fitted_line == "pwd", "SSH online uses the editor visible line");
    state.ssh_connected = false;

    /* O cwd nunca pode vazar para os modos SSH ou senha. */
    state.cwd = "/very/secret/path";
    state.password_pending = true;
    check(cyberdeck_shell_console::compose(state, input).marker.find("secret") ==
              std::string::npos,
          "the local cwd must not leak into the password marker");
    state.password_pending = false;
    state.ssh_connected = true;
    check(cyberdeck_shell_console::compose(state, input).marker.find("secret") ==
              std::string::npos,
          "the local cwd must not leak into the SSH marker");
}

void utf8_and_mode_helpers_contract() {
    /* Contagem por codepoint, incluindo sequencias invalidas contadas como
     * um byte, e corte sempre no inicio de um codepoint. */
    check(cyberdeck_shell_console::utf8_char_count("aa\xC3\xA9zz") == 5,
          "a two-byte sequence counts as one codepoint");
    check(cyberdeck_shell_console::utf8_char_count("aa\xE2\x82\xACzz") == 5,
          "a three-byte sequence counts as one codepoint");
    check(cyberdeck_shell_console::utf8_char_count("aa\xF0\x9F\x94\xA5zz") == 5,
          "a four-byte sequence counts as one codepoint");
    check(cyberdeck_shell_console::utf8_char_count("\x80\x80") == 2,
          "stray continuation bytes are counted defensively");
    check(cyberdeck_shell_console::utf8_valid_start_offset("aa\xC3\xA9zz", 3) == 4,
          "the cut advances over continuation bytes to the next codepoint");
    check(cyberdeck_shell_console::utf8_valid_start_offset("aa", 99) == 2,
          "dropping more than the length clamps to the length");
    check(cyberdeck_shell_console::utf8_valid_start_offset("\xC3\xA9", 0) == 0,
          "dropping nothing keeps the first codepoint");

    /* A reserva de bytes do prompt vale para qualquer marker, inclusive um que
     * nao termine em "$ ". */
    const std::string oversized(cyberdeck_shell_console::k_terminal_limit + 10, 'z');
    const std::string plain = cyberdeck_shell_console::fit_prompt_marker(oversized);
    check(plain.size() <= cyberdeck_shell_console::k_terminal_limit,
          "an oversized marker without a shell terminator is still bounded");
    check(cyberdeck_shell_console::fit_prompt_marker("/$ ") == "/$ ",
          "a marker inside the bound is returned unchanged");

    check(std::string(cyberdeck_shell_console::session_mode_name(
              cyberdeck_shell_console::session_mode::menu)) == "menu",
          "the menu mode has a stable name");
    check(std::string(cyberdeck_shell_console::session_mode_name(
              cyberdeck_shell_console::session_mode::ssh_host_key)) == "ssh_host_key",
          "the host key mode has a stable name");
    check(std::string(cyberdeck_shell_console::session_mode_name(
              cyberdeck_shell_console::session_mode::ssh_password)) == "ssh_password",
          "the password mode has a stable name");
    check(std::string(cyberdeck_shell_console::session_mode_name(
              cyberdeck_shell_console::session_mode::ssh_interactive)) == "ssh_interactive",
          "the interactive mode has a stable name");
    check(cyberdeck_shell_console::mode_for(cyberdeck_session_state::MENU) ==
              cyberdeck_shell_console::session_mode::menu,
          "MENU maps to the menu mode");
    check(cyberdeck_shell_console::mode_for(static_cast<cyberdeck_session_state>(99)) ==
              cyberdeck_shell_console::session_mode::menu,
          "an unknown session state fails closed to the menu mode");
    check(std::string(cyberdeck_shell_console::session_mode_name(
              static_cast<cyberdeck_shell_console::session_mode>(99))) == "menu",
          "an unknown session mode name falls back to the menu name");

    /* reserved() e a soma exata do marker com a linha ajustada. */
    cyberdeck_shell_console::surface_state state;
    state.cwd = "/data";
    cyberdeck_shell_console::line_input input;
    input.line = "abcdef";
    input.visible_line = "abcdef";
    input.cursor_bytes = input.line.size();
    const cyberdeck_shell_console::line_view view =
        cyberdeck_shell_console::compose(state, input);
    check(view.reserved() == view.text().size(),
          "reserved bytes equal the composed tail");
}

void dispatcher_failure_closed_contract() {
    /* Sem runtime o dispatcher falha fechado para o console. */
    const cyberdeck_shell_console::dispatcher detached(nullptr);
    check(detached.resolve("app list") ==
              cyberdeck_shell_console::dispatch_target::supervisor,
          "app still belongs to the supervisor without a runtime");
    check(detached.resolve("svc") == cyberdeck_shell_console::dispatch_target::console,
          "a declared command cannot be routed without a runtime");
    std::array<std::string_view, cyberdeck_shell_console::k_max_collisions> collisions{};
    check(detached.collisions(collisions) == 0,
          "no runtime means no collisions to report");

    /* Uma aplicacao que declara um comando legado e reportada como colisao,
     * e o comando legado continua pertencendo ao console. */
    cyberdeck_apps::runtime runtime;
    command_app shadow{"test.declared", "svc2", {"wifi", "log", "demo"}};
    check(runtime.register_application(shadow), "the shadowing app registers");
    const cyberdeck_shell_console::dispatcher dispatcher(&runtime);
    std::array<std::string_view, cyberdeck_shell_console::k_max_collisions> reported{};
    const std::size_t count = dispatcher.collisions(reported);
    check(count == 2, "both declared legacy collisions are reported");
    check(count == 2 && reported[0] == "wifi" && reported[1] == "log",
          "collisions are reported in manifest order");
    check(dispatcher.resolve("wifi") == cyberdeck_shell_console::dispatch_target::console,
          "a declared legacy command is still owned by the console");
    check(dispatcher.resolve("log") == cyberdeck_shell_console::dispatch_target::console,
          "a second declared legacy command is still owned by the console");
    check(dispatcher.resolve("demo") == cyberdeck_shell_console::dispatch_target::supervisor,
          "a declared non-legacy command is still routed to the supervisor");

    /* A reserva de colisoes e bounded: nunca escreve fora do array.  Os ids
     * vivem em armazenamento estatico porque o manifesto guarda string_view. */
    static command_app bulk[cyberdeck_apps::k_max_applications];
    static std::string bulk_id_storage[cyberdeck_apps::k_max_applications];
    static cyberdeck_apps::runtime full;
    for (std::size_t index = 0; index < cyberdeck_apps::k_max_applications; ++index) {
        bulk_id_storage[index] = "test.bulk" + std::to_string(index);
        bulk[index] = command_app{bulk_id_storage[index], "", {"wifi", "log", "clear", "help"}};
        check(full.register_application(bulk[index]), "bulk application registers");
    }
    const cyberdeck_shell_console::dispatcher full_dispatcher(&full);
    std::array<std::string_view, cyberdeck_shell_console::k_max_collisions> bounded{};
    const std::size_t bounded_count = full_dispatcher.collisions(bounded);
    check(bounded_count <= cyberdeck_shell_console::k_max_collisions,
          "collision reporting stays bounded by the output array");
    check(bounded_count == cyberdeck_apps::k_max_applications * 4,
          "every declared legacy command is reported");
    check(dispatcher.resolve("wifi") == cyberdeck_shell_console::dispatch_target::console,
          "a full registry still cannot capture a legacy command");
}

void bounded_composition_contract() {
    cyberdeck_shell_console::surface_state state;
    state.cwd = "/";
    const std::string long_line(cyberdeck_shell_console::k_terminal_limit + 100, 'x');

    cyberdeck_shell_console::line_input input;
    input.line = long_line;
    input.visible_line = long_line;
    input.cursor_bytes = 1;
    const cyberdeck_shell_console::line_view view = cyberdeck_shell_console::compose(state, input);
    check(view.text().size() <= cyberdeck_shell_console::k_terminal_limit,
          "the composed tail never exceeds the terminal bound");
    check(view.line_start > 0, "an oversized line is left-truncated");
    check(view.cursor_bytes == view.line_start,
          "a cursor inside the hidden prefix clamps to the visible window");

    input.cursor_bytes = long_line.size() + 10;
    check(cyberdeck_shell_console::compose(state, input).cursor_bytes <= long_line.size(),
          "a cursor beyond the line clamps to the line size");

    const std::string long_cwd(cyberdeck_shell_console::k_terminal_limit + 50, 'c');
    cyberdeck_shell_console::line_input short_input;
    const std::string marker = cyberdeck_shell_console::fit_prompt_marker(long_cwd + "$ ");
    check(marker.size() <= cyberdeck_shell_console::k_terminal_limit,
          "an oversized prompt is bounded");
    check(marker.compare(marker.size() - 2, 2, "$ ") == 0,
          "an oversized prompt keeps the shell terminator");
    check(short_input.line.empty(), "placeholder input stays empty");
}

void dispatch_contract() {
    cyberdeck_apps::runtime runtime;
    command_app service{"test.service", "svc", {"svc"}};
    command_app legacy_shadow{"test.shadow", "wifi", {}};
    command_app demo{"test.demo", "", {"demo"}};
    check(runtime.register_application(service), "service app registers");
    check(runtime.register_application(legacy_shadow), "shadowing app registers");
    check(runtime.register_application(demo), "declared-command app registers");

    const cyberdeck_shell_console::dispatcher dispatcher(&runtime);

    /* `app` pertence exclusivamente ao supervisor, mesmo malformado. */
    check(dispatcher.resolve("app list") ==
              cyberdeck_shell_console::dispatch_target::supervisor,
          "app commands belong to the supervisor");
    check(dispatcher.resolve("app") == cyberdeck_shell_console::dispatch_target::supervisor,
          "a bare app verb still belongs to the supervisor");

    /* Um comando declarado e roteado; um legado nunca e interceptado, mesmo
     * declarado por outra aplicacao. */
    check(dispatcher.resolve("demo") == cyberdeck_shell_console::dispatch_target::supervisor,
          "a declared application command is routed to the supervisor");
    check(dispatcher.resolve("svc") == cyberdeck_shell_console::dispatch_target::supervisor,
          "the primary command is routed to the supervisor");
    check(dispatcher.resolve("wifi") == cyberdeck_shell_console::dispatch_target::console,
          "a legacy command is never captured by the supervisor");
    for (const char *legacy : {"help", "log", "clear", "screen", "battery", "bluetooth",
                               "ssh", "pwd", "cd", "ls", "cat", "touch", "mkdir", "rm",
                               "rmdir"}) {
        check(dispatcher.resolve(legacy) == cyberdeck_shell_console::dispatch_target::console,
              "every legacy command stays with the console");
    }

    /* Espaco, linha vazia e token desconhecido pertencem ao console. */
    check(dispatcher.resolve("") == cyberdeck_shell_console::dispatch_target::console,
          "an empty line is handled by the console");
    check(dispatcher.resolve("   \t ") == cyberdeck_shell_console::dispatch_target::console,
          "a blank line is handled by the console");
    check(dispatcher.resolve("nope") == cyberdeck_shell_console::dispatch_target::console,
          "an unknown verb stays with the console");

    /* Uma linha acima do limite nao e roteada por um dispatcher que nao a
     * entendeu: falha fechada para o console, sem claimed de supervisor. */
    const std::string oversized(300, 'a');
    check(dispatcher.resolve(oversized) == cyberdeck_shell_console::dispatch_target::console,
          "an oversized line falls closed to the console");

    /* Colisoes com comandos legados sao diagnosticadas, nunca roteadas. */
    /* Only declared commands participate: a legacy name used as the primary
     * command is reported through the catalog reservation, not as a duplicate
     * of the declared list. */
    std::array<std::string_view, cyberdeck_shell_console::k_max_collisions> collisions{};
    const std::size_t count = dispatcher.collisions(collisions);
    check(count == 0, "a primary legacy name is not reported as a declared collision");
    check(dispatcher.resolve("wifi") == cyberdeck_shell_console::dispatch_target::console,
          "a shadowing primary command is still never routed to the supervisor");

    /* A reserva vem do catalogo de ajuda, entao nao pode divergir dele. */
    check(cyberdeck_shell_console::is_legacy_command("app"),
          "app is a legacy catalog entry");
    check(cyberdeck_shell_console::is_legacy_command("bluetooth"),
          "bluetooth is a legacy catalog entry");
    check(!cyberdeck_shell_console::is_legacy_command("demo"),
          "a new command is not legacy until the catalog declares it");
    check(!cyberdeck_shell_console::is_legacy_command(""),
          "an empty verb is never legacy");
}

void foreground_manifest_contract() {
    cyberdeck_shell_app::application shell;
    const cyberdeck_apps::manifest &item = shell.get_manifest();
    check(item.id == "cyberdeck.shell", "the shell application keeps its id");
    check(item.type == cyberdeck_apps::app_type::foreground,
          "the shell application is a foreground application");
    check(item.dependency_count == 1 && item.dependencies[0] == "cyberdeck.event_log",
          "the shell depends on the event log");
    check(item.command.empty(),
          "the shell exposes no command verb: the console is driven by input");
    check(item.stack_bytes > 0 && item.queue_depth > 0,
          "the shell declares its stack and queue");

    /* Sem host de composicao o start falha fechado: nao existe console sem
     * dono do terminal. */
    check(!shell.start(), "start without a composition host fails closed");
    check(!shell.running(), "a failed start does not report running");
    check(!shell.console_ready(), "no console exists without a host");

    fake_host host;
    shell.attach_console(host);
    check(shell.start(), "start with a composition host succeeds");
    check(shell.running() && shell.console_ready(), "a started shell owns a console");
    check(host.renders > 0, "start repaints so the first frame belongs to a running shell");
    check(shell.start(), "start is idempotent");

    /* O modo do console acompanha o cliente SSH: SSH e um modo, nao um
     * console separado. */
    check(shell.mode() == cyberdeck_shell_console::session_mode::menu,
          "the console starts in the menu mode");
    host.ssh = cyberdeck_session_state::CONNECTED;
    check(shell.mode() == cyberdeck_shell_console::session_mode::ssh_interactive,
          "SSH connected is a mode of the same console");
    host.ssh = cyberdeck_session_state::PASSWORD;
    check(shell.mode() == cyberdeck_shell_console::session_mode::ssh_password,
          "SSH password is a mode of the same console");
    host.ssh = cyberdeck_session_state::HOST_KEY;
    check(shell.mode() == cyberdeck_shell_console::session_mode::ssh_host_key,
          "host key confirmation is a mode of the same console");
    host.ssh = cyberdeck_session_state::MENU;

    /* O prompt e a linha vem da aplicacao, nao da view. */
    cyberdeck_shell_console::surface_state state;
    state.cwd = "/data";
    const cyberdeck_shell_console::line_view view = shell.compose_line(state);
    check(view.marker == "/data$ ", "the application composes the local prompt");
    check(view.fitted_line.empty(), "the composed line is empty until something is typed");
    check(shell.insert_physical_text("pwd", 3), "the application accepts typed text");
    const cyberdeck_shell_console::line_view typed = shell.compose_line(state);
    check(typed.text() == "/data$ pwd", "the application owns prompt plus command line");
    check(typed.cursor_chars() == typed.marker.size() + typed.fitted_line.size(),
          "the application owns the cursor position");

    /* Editar e apagar sao Owned pelo console da aplicacao. */
    shell.handle_key(cyberdeck_shell_session::key::backspace);
    check(shell.compose_line(state).fitted_line == "pw",
          "the application routes editing keys to its console");
    shell.clear_editor();
    check(shell.compose_line(state).fitted_line.empty(), "the application clears its line");

    /* Fachada nula-segura: sem console nada quebra e nada e roteado. */
    check(shell.stop(), "stop succeeds");
    check(!shell.running() && !shell.console_ready(), "stop releases the console");
    check(shell.stop(), "stop is idempotent");
    check(!shell.insert_physical_text("x", 1), "input is dropped without a console");
    check(!shell.insert_modified_key('a', 1), "modified input is dropped without a console");
    bool virtual_enter = true;
    check(!shell.insert_virtual_text("x", &virtual_enter), "virtual input is dropped");
    check(!virtual_enter, "a dropped virtual chunk never claims Enter");
    shell.handle_key(cyberdeck_shell_session::key::enter);
    shell.execute_line();
    shell.sync_editor();
    shell.sync_line();
    shell.clear_editor();
    shell.clear_ble_auth_input();
    shell.invalidate_wifi_connection();
    check(shell.ble_auth_input_size() == 0, "a detached console has no passkey buffer");
    check(shell.wifi_connection_token() == 0, "a detached console has no Wi-Fi token");
    check(shell.compose_line(state).marker == "/data$ ",
          "composition without a console still resolves the prompt");

    /* Fachada coerente com o console vivo. */
    shell.start();
    check(shell.console_ready(), "restart creates a console again");
    check(shell.console() != nullptr, "the application exposes its owned console");
    const cyberdeck_shell_console::line_view restarted = shell.compose_line(state);
    check(restarted.fitted_line.empty(),
          "a restarted console starts with an empty line, not the previous one");

    /* Tokens e estado de sessao vem do console possuido, nao de um segundo estado
     espelhado na aplicacao: a fachada so faz proxy e a invalidacao e observavel
     no mesmo objeto que o supervisor闯 guardou. */
    shell.sync_line();
    check(shell.insert_physical_text("x", 1), "the restarted console accepts input");
    cyberdeck_shell_session::session *const owned = shell.console();
    check(shell.wifi_connection_token() == owned->wifi_connection_token(),
          "the Wi-Fi token is read from the owned console");
    check(shell.wifi_model_connection_token() == owned->wifi_model_connection_token(),
          "the model token is read from the same owned console");
    shell.invalidate_wifi_connection();
    check(owned->wifi_connection_token() == 0 && shell.wifi_connection_token() == 0,
          "invalidating the connection resets the owned token, not a facade copy");
    check(shell.console() == owned, "the facade never swapped the owned console");
    check(shell.ble_auth_input_size() == 0, "the passkey buffer starts empty");

    /* Submitting a line with no console must not crash nor reach a service. */
    shell.execute_line();
    check(shell.compose_line(state).fitted_line.empty(),
          "an executed line leaves the console prompt empty");

    /* O console exposto e o mesmo objeto que a aplicacao possui. */
    check(shell.console() != nullptr &&
              shell.console()->line() == shell.compose_line(state).visible_line,
          "the exposed console is the owned one");
    shell.detach_console();
    check(!shell.console_ready() && !shell.running(),
          "detach drops the console and stops the application");

    /* A instancia global e um singleton real: duas chamadas devolvem o mesmo
     * endereco e o supervisor registra exatamente esse endereco. */
    cyberdeck_shell_app::application &first_instance = cyberdeck_shell_app::global_application();
    cyberdeck_shell_app::application &second_instance = cyberdeck_shell_app::global_application();
    check(&first_instance == &second_instance,
          "the foreground shell application is a single instance");
    cyberdeck_apps::runtime global_holder;
    check(global_holder.register_application(first_instance),
          "the global instance registers with a supervisor");
    check(global_holder.at(0) == &first_instance,
          "the supervisor registers the very instance returned by the singleton");
}

void console_isolation_contract() {
    fake_host first_host;
    fake_host second_host;
    cyberdeck_shell_app::application first;
    cyberdeck_shell_app::application second;
    first.attach_console(first_host);
    second.attach_console(second_host);
    first.start();
    second.start();

    check(first.insert_physical_text("aaa", 3), "first console accepts text");
    check(second.insert_physical_text("bbb", 3), "second console accepts text");

    cyberdeck_shell_console::surface_state state;
    state.cwd = "/";
    check(first.compose_line(state).fitted_line == "aaa", "first console keeps its own line");
    check(second.compose_line(state).fitted_line == "bbb",
          "second console is isolated from the first");

    first.clear_ble_auth_input();
    first.stop();
    check(second.console_ready(), "stopping one console keeps the other alive");
    check(second.compose_line(state).fitted_line == "bbb",
          "the surviving console keeps its state");
    second.stop();
}

class stub_event_log final : public cyberdeck_apps::application {
public:
    const cyberdeck_apps::manifest &get_manifest() const override { return manifest_; }
    bool start() override { return true; }
    bool stop() override { return true; }
    bool running() const override { return true; }
    cyberdeck_apps::result execute(std::string_view, std::string_view) override { return {}; }

private:
    const cyberdeck_apps::manifest manifest_{
        "cyberdeck.event_log", "Event log", "1.0.0", "stub log", {}};
};

void supervisor_ownership_contract() {
    fake_host host;
    cyberdeck_shell_app::application shell;
    shell.attach_console(host);
    stub_event_log event_log;
    cyberdeck_apps::runtime runtime;
    check(runtime.register_application(event_log), "the event log dependency registers");
    check(runtime.register_application(shell), "the shell registers with the supervisor");
    /* Aplicacao sem relacao com o console, para provar que a deteccao de
     * cascata nao e indiscriminada. */
    static command_app unrelated{"test.unrelated", "unrelated", {}};
    check(runtime.register_application(unrelated), "an unrelated application registers");

    /* Fail closed: sem a dependencia declarada o shell nao sobe. */
    cyberdeck_apps::runtime orphan_runtime;
    cyberdeck_shell_app::application orphan;
    orphan.attach_console(host);
    check(orphan_runtime.register_application(orphan), "a shell without its log registers");
    check(!orphan_runtime.start_application("cyberdeck.shell"),
          "the shell does not start while its declared dependency is missing");
    check(orphan_runtime.state("cyberdeck.shell") == cyberdeck_apps::app_state::failed,
          "a missing dependency leaves the shell failed, not silently running");
    check(!orphan.console_ready(), "a failed start creates no console");
    check(runtime.start_application("cyberdeck.shell"),
          "the supervisor starts the foreground shell");
    check(runtime.state("cyberdeck.shell") == cyberdeck_apps::app_state::running,
          "the shell reports running through the supervisor");
    check(shell.console_ready(), "the supervisor start hook owns console creation");
    check(runtime.start_application("cyberdeck.shell"),
          "starting an already running shell is idempotent");

    /* O console e o unico caminho para os comandos, portanto o proprio console
     * nao pode se desligar: parar a si mesmo removeria a unica forma de
     * religa-lo. */
    const cyberdeck_apps::result self_stop = runtime.execute_line("app stop cyberdeck.shell");
    check(self_stop.status == cyberdeck_apps::result_status::rejected,
          "the console refuses to stop itself");
    check(shell.running(), "a refused self-stop leaves the console running");

    /* Cascata: `stop_index` para dependentes antes do alvo, entao parar a
     * dependencia declarada do shell destruiria o console sem nunca nomea-lo.
     * Esse e o mesmo lockout, e por isso tambem e recusado. */
    check(runtime.stops_console_owner("cyberdeck.shell"),
          "the console owner is recognized directly");
    check(runtime.stops_console_owner("cyberdeck.event_log"),
          "a dependency that would cascade into the console is recognized");
    const cyberdeck_apps::result cascade_stop =
        runtime.execute_line("app stop cyberdeck.event_log");
    check(cascade_stop.status == cyberdeck_apps::result_status::rejected,
          "a stop that would cascade into the console is rejected");
    check(shell.running() && shell.console_ready(),
          "a rejected cascade leaves the console alive");
    check(runtime.state("cyberdeck.event_log") == cyberdeck_apps::app_state::running,
          "a rejected cascade leaves the dependency running");

    check(runtime.stop_application("cyberdeck.shell"),
          "the supervisor stops the shell through its lifecycle API");
    check(!shell.console_ready(), "the supervisor stop hook releases the console");
    check(runtime.execute_line("app start cyberdeck.shell").status ==
              cyberdeck_apps::result_status::handled,
          "the shell can be started again");
    check(shell.console_ready(), "restarting rebuilds the console");

    /* Enumeracao bounded, usada pela politica de despacho. */
    check(runtime.at(0) == &event_log && runtime.at(1) == &shell &&
              runtime.at(2) == &unrelated,
          "at() exposes registration order");
    check(runtime.at(runtime.size()) == nullptr, "at() is bounded by the registry size");
    check(runtime.at(4096) == nullptr, "at() rejects an out-of-range index");
    check(!runtime.stops_console_owner("missing"),
          "an unknown id cascades into nothing");
    check(!runtime.stops_console_owner("test.demo"),
          "an unrelated application cascades into no console");
}

} // namespace

int main()
{
    prompt_composition_contract();
    utf8_and_mode_helpers_contract();
    bounded_composition_contract();
    dispatch_contract();
    dispatcher_failure_closed_contract();
    foreground_manifest_contract();
    console_isolation_contract();
    supervisor_ownership_contract();

    if (failures == 0) {
        std::printf("PASS: shell application (%d checks)\n", checks);
        return 0;
    }
    std::printf("FAIL: %d of %d shell application checks failed\n", failures, checks);
    return 1;
}
