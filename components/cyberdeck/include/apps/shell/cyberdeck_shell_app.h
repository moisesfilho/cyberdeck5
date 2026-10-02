#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include "apps/runtime/cyberdeck_app_runtime.h"
#include "apps/shell/cyberdeck_shell_console.h"
#include "apps/shell/cyberdeck_shell_session.h"

/*
 * `cyberdeck.shell` como aplicacao de primeiro plano (Fase 7 do plano
 * OS-TRANSFORMATION).
 *
 * O supervisor passa a ser dono do ciclo de vida do console: cada `start`
 * cria uma sessao nova e isolada (historico, edicao, tokens e buffer de
 * passkey recem-zerados) e cada `stop` descarta a sessao apos apagar o
 * passkey.  O modo da sessao tambem pertence a aplicacao: SSH e um modo do
 * console do shell, nao um segundo console.
 *
 * A aplicacao nao conhece LVGL, o terminal, o cliente SSH nem qualquer
 * backend: recebe um `host` de sessao da composicao e expoe uma fachada
 * nula-segura para a view.  Isso a torna linkavel no host contra um host falso.
 */
namespace cyberdeck_shell_app {

class application final : public cyberdeck_apps::application {
public:
    application() = default;
    application(const application &) = delete;
    application &operator=(const application &) = delete;

    /* Contrato do supervisor. */
    const cyberdeck_apps::manifest &get_manifest() const override;
    bool init() override;
    bool start() override;
    bool stop() override;
    bool teardown() override;
    bool running() const override;
    /* O console e dirigido pela entrada, nao por um verbo de comando: o
     * supervisor nunca deve reivindicar uma linha para esta aplicacao. */
    cyberdeck_apps::result execute(std::string_view command, std::string_view args) override;

    /* Portas de composicao.  O host e apenas loanado: o console e criado no
     * start(), o que mantem o isolamento de sessao por ciclo de vida. */
    void attach_console(cyberdeck_shell_session::host &host);
    void detach_console();

    cyberdeck_shell_session::session *console();
    bool console_ready() const;

    /* Modo da sessao do console; SSH e um modo, nao um console separado. */
    cyberdeck_shell_console::session_mode mode() const;

    /* Fachada nula-segura usada pela view. */
    void execute_line(bool line_already_sent = false);
    void handle_key(cyberdeck_shell_session::key pressed);
    void sync_editor();
    void sync_line();
    void clear_editor();
    void clear_ble_auth_input();
    std::size_t ble_auth_input_size() const;
    bool insert_physical_text(const char *text, std::size_t length);
    bool insert_modified_key(char character, std::uint8_t modifier);
    bool insert_virtual_text(const char *text, bool *virtual_enter_handled);
    std::uint64_t wifi_connection_token() const;
    std::uint64_t wifi_model_connection_token() const;
    void invalidate_wifi_connection();

    /* Prompt e linha de comando pertencem a aplicacao: a view entrega somente
     * o estado de superficie e recebe a composicao final. */
    cyberdeck_shell_console::line_view compose_line(
        const cyberdeck_shell_console::surface_state &state);

private:
    cyberdeck_shell_console::line_input current_input() const;

    cyberdeck_shell_session::host *host_ = nullptr;
    std::unique_ptr<cyberdeck_shell_session::session> console_;
    bool running_ = false;
};

/* Instancia unica registrada no supervisor; a composicao da UI apenas
 * empresta seu host de sessao. */
application &global_application();

} // namespace cyberdeck_shell_app