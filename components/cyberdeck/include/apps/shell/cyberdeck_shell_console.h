#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include "apps/runtime/cyberdeck_app_runtime.h"
#include "apps/shell/cyberdeck_edit_line.h"

/*
 * Politica pura do console do shell (Fase 7 do plano OS-TRANSFORMATION).
 *
 * O prompt e a linha de comando pertencem a aplicacao de primeiro plano
 * `cyberdeck.shell`.  Este modulo concentra a composicao do prompt, a janela
 * visivel da linha, o cursor em codepoints e a decisao de despacho de comandos.
 * Ele nao conhece LVGL, ESP-IDF, FreeRTOS, o terminal fisico nem a sessao
 * local: recebe apenas o estado de superficie ja resolvido e devolve a visao
 * textual.  A camada de view (cyberdeck_ui) limita-se a aplicar o resultado.
 *
 * As helpers de UTF-8 e de ajuste de largura foram movidas do TU anônimo da
 * UI para cá, sem alterar o comportamento, para que o console seja testável
 * no host e tenha uma unica autoria sobre os limites de 12288 bytes.
 */
namespace cyberdeck_shell_console {

/* Limite duro do console.  Mesma origem do limite de edicao, portanto prompt,
 * linha e scrollback nunca divergem. */
constexpr std::size_t k_terminal_limit = cyberdeck_edit_line::limit;

/* ---------------------------------------------------------------------- */
/* Helpers UTF-8 e de largura (movidas da UI, comportamento preservado).    */
/* ---------------------------------------------------------------------- */

/* Numero de codepoints de `text`.  Bytes invalidos contam como um byte. */
std::size_t utf8_char_count(std::string_view text);

/* Offset do primeiro byte inicial de codepoint em ou apos `drop_bytes`. */
std::size_t utf8_valid_start_offset(std::string_view text, std::size_t drop_bytes);

/* Mantem no maximo `max_bytes` bytes finais, sem cortar uma sequencia UTF-8. */
std::string truncate_left_utf8(std::string_view text, std::size_t max_bytes);

/* Ajusta o prompt ao limite preservando o sufixo "$ " do shell local. */
std::string fit_prompt_marker(std::string_view marker);

/* Ajusta a linha visivel ao budget restante depois do prompt. */
std::string fit_visible_line(std::string_view line, std::size_t marker_bytes,
                             std::size_t limit = k_terminal_limit);

/* ---------------------------------------------------------------------- */
/* Modos de sessao do console.                                             */
/* ---------------------------------------------------------------------- */

/* SSH e um modo de sessao do shell, nao um console separado: os quatro modos
 * abaixo descrevem quem possui a entrada da linha no shell de primeiro plano. */
enum class session_mode {
    menu,             /* console local: prompt "$ " */
    ssh_host_key,     /* aguardando confirmacao da host key (TOFU) */
    ssh_password,     /* aguardando senha: linha mascarada */
    ssh_interactive,  /* SSH online: Enter envia a linha ao PTY remoto */
};

const char *session_mode_name(session_mode mode);

/* Deriva o modo a partir do estado do cliente SSH publicado pelo host. */
session_mode mode_for(cyberdeck_session_state state);

/* ---------------------------------------------------------------------- */
/* Composicao do prompt e da linha.                                        */
/* ---------------------------------------------------------------------- */

/* Estado de superficie ja resolvido pela composicao (view): quem possui a
 * entrada e qual o contexto do shell local.  Nenhum dado do servico e
 * consultado aqui. */
struct surface_state {
    bool ssh_connected = false;
    bool password_pending = false;
    bool input_owned_elsewhere = false;
    std::string cwd;
};

/* Linha e cursor corrente, publicados pelo console da aplicacao shell. */
struct line_input {
    std::string line;
    std::string visible_line;
    std::size_t cursor_bytes = 0;
};

/* Visao textual do prompt + linha pronta para o terminal. */
struct line_view {
    std::string marker;
    std::string visible_line;
    std::string fitted_line;
    std::size_t line_start = 0;
    std::size_t cursor_bytes = 0;

    /* Bytes exatos a exibir depois do scrollback (marker + linha ajustada). */
    std::string text() const;
    /* Posicao do cursor em codepoints, relativa ao inicio de `text()`. */
    std::size_t cursor_chars() const;
    /* Bytes reservados por `text()`, usado para aparar o scrollback. */
    std::size_t reserved() const;
};

/* Mascara a linha quando a senha esta pendente, suprime o prompt local quando
 * outro dono controla a entrada e mantem o cursor dentro da janela visivel. */
line_view compose(const surface_state &state, const line_input &input,
                  std::size_t limit = k_terminal_limit);

/* ---------------------------------------------------------------------- */
/* Despacho de comandos.                                                   */
/* ---------------------------------------------------------------------- */

enum class dispatch_target {
    console,    /* shell local ou parser legado de servicos */
    supervisor, /* runtime de aplicacoes compiladas */
};

/* Primeiro token significantemente nao branco de `line`, limitado a 256 bytes.
 * Devolve string_view vazia quando a linha nao tem token utilizavel. */
std::string_view first_token(std::string_view line);

/* Verdadeiro quando `command` pertence ao catalogo de ajuda, isto e, quando
 * ja existe um comando legado.  O catalogo e a unica fonte dessa reserva, de
 * modo que a lista nao pode divergir do `help` exibido ao usuario. */
bool is_legacy_command(std::string_view command);

/* Verbo que pertence exclusivamente ao supervisor. */
constexpr std::string_view k_supervisor_command = "app";

constexpr std::size_t k_max_collisions =
    cyberdeck_apps::k_max_applications * (cyberdeck_apps::k_max_commands + 1);

/* Decide o dono do primeiro token de `line`:
 *  - `app` pertence exclusivamente ao supervisor e nunca desce para o parser
 *    legado;
 *  - um comando legado nunca e interceptado por uma aplicacao, mesmo que ela
 *    declare o mesmo nome no manifesto;
 *  - um comando declarado por uma aplicacao registrada e roteado ao
 *    supervisor;
 *  - qualquer outro token, ou uma linha nao parseavel, fica com o console. */
class dispatcher {
public:
    explicit dispatcher(const cyberdeck_apps::runtime *runtime) : runtime_(runtime) {}

    dispatch_target resolve(std::string_view line) const;

    /* Comandos declarados que colidem com um comando legado.  Colisoes nunca
     * sao roteadas ao supervisor; a lista existe para diagnostico. */
    std::size_t collisions(std::array<std::string_view, k_max_collisions> &out) const;

private:
    const cyberdeck_apps::runtime *runtime_;
    bool declares_command(std::string_view command) const;
};

} // namespace cyberdeck_shell_console
