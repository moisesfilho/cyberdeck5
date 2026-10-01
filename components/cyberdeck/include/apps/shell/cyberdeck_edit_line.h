#pragma once

#include <cstddef>
#include <string>

/*
 * Modelo PURO da linha de edicao + sessao SSH (bug aprovado A: duplicacao de
 * caracteres na entrada fisica/virtual, backspace/setas e Enter).
 *
 * Logica extraida de cyberdeck_ui.cpp (namespace anonimo, acoplado ao LVGL):
 * local_key(), terminal_insert(), execute_line() e cyberdeck_keyboard_input()
 * operam direto sobre widgets e singletons; este contrato fixa o
 * comportamento correto em uma unidade pura e host-testavel (sem LVGL,
 * FreeRTOS ou ESP-IDF).
 *
 * PROPRIEDADES DE REGRESSAO fixadas por este contrato:
 *
 *  1. UM CARACTERE = UMA INSERCAO: insert*() acrescenta exatamente `len`
 *     bytes na posicao do cursor; o texto resultante contem cada byte
 *     exatamente uma vez. Nenhum caminho duplica nem perde caracteres.
 *
 *  2. PARIDADE FISICO/VIRTUAL: `insert_physical()` e `insert_virtual()` sao
 *     os dois pontos de entrada da UI (cyberdeck_keyboard_input e
 *     terminal_insert) e DEVEM delegar ao MESMO nucleo de insercao: para a
 *     mesma entrada produzem a mesma linha, o mesmo cursor e o mesmo texto
 *     visivel. A integracao na UI passa a ter um unico caminho canonico de
 *     edicao (impossivel inserir duas vezes pelo mesmo caractere).
 *
 *  3. EDICAO UTF-8 NA POSICAO REAL DO CURSOR:
 *     - backspace() remove o codepoint ANTERIOR ao cursor; del() remove o
 *       codepoint NA posicao do cursor; setas esq/dir movem por codepoint,
 *       com clamp nas bordas (paridade com a contagem de codepoints do TUI);
 *     - bytes UTF-8 invalidos/continuacao isolados contam como 1 byte cada
 *       (paridade com o ramo `else i += 1` do TUI);
 *     - inserir no meio da linha desloca o texto a direita e o cursor avanca
 *       pelos bytes inseridos (S12/S13 do roteiro manual).
 *
 *  4. ENTER PROTOCOLA E LIMPA A LINHA SEM DUPLICACAO: `enter()` retorna a
 *     ACAO + o PAYLOAD EXATO a transmitir + o ECO LOCAL EXATO a anexar ao
 *     terminal (uma unica vez). Em CONNECTED, o payload e a linha + um unico
 *     '\n' (comando+newline); se a linha ja foi enviada (line_already_sent),
 *     apenas o '\n' e retornado — o comando NUNCA e enviado duas vezes. A
 *     linha e SEMPRE limpa e o cursor volta a 0 (paridade com execute_line()).
 *
 *  5. LIMITE HARD: nenhuma insercao parcial — se a linha nao couber em
 *     `limit` bytes, insert*() retorna false e NAO altera nada (paridade com
 *     o guard `s_line.size() + length > TERMINAL_LIMIT` do TUI).
 *
 *  6. POLITICA DE ECO EXPLICITA (aprovada): o resultado de `enter()` declara
 *     em `echo` os bytes EXATOS que o caller deve anexar ao terminal LOCAL
 *     apos executar a acao. `echo` vazio = SEM eco local (somente o lado
 *     remoto ecoa):
 *       - MENU (linha nao-branca): eco local "$ <linha>\n" — paridade com o
 *         TUI que anexa "$ ", a linha e "\n" ao output;
 *       - CONNECTED (qualquer caso: comando, linha vazia ou
 *         line_already_sent): SEM eco local — o comando aparece no terminal
 *         apenas pelo echo do lado remoto (paridade com a correcao aprovada
 *         do echo SSH);
 *       - PASSWORD: SEM eco — a linha ja aparece mascarada durante a
 *         digitacao; nada e anexado ao output ao enviar a senha;
 *       - HOST_KEY: SEM eco — fluxo TOFU inalterado, nenhum dado anexado.
 *
 *  A implementacao em cyberdeck_edit_line.cpp existe; a UI aplica o contrato
 *  de eco descrito acima ao renderizar e enviar cada linha.
 */
enum class cyberdeck_session_state : unsigned char {
    MENU = 0,   /* shell local: prompt "$ " */
    PASSWORD,   /* SSH aguardando senha (linha mascarada) */
    HOST_KEY,   /* SSH aguardando confirmacao da host key */
    CONNECTED,  /* SSH online: Enter envia comando */
};

/* Acao resultante de enter(). O chamador executa/transmite a acao com o
 * payload EXATAMENTE uma vez e anexa `echo` ao terminal local (quando `echo`
 * nao for vazio). */
enum class cyberdeck_enter_action : unsigned char {
    NONE = 0,           /* nada a enviar/executar (linha em branco no menu) */
    LOCAL_COMMAND,      /* linha e comando local (caller ecoa "$ line\n" e executa) */
    SEND_PASSWORD,      /* enviar a linha como senha (sem '\n') */
    ACCEPT_HOST_KEY,    /* aceitar a host key (TOFU); sem dados */
    SEND_LINE_NEWLINE,  /* CONNECTED: payload = linha + '\n' (enviar uma vez) */
    SEND_NEWLINE,       /* CONNECTED: so o '\n' (linha vazia ou ja enviada) */
};

struct cyberdeck_enter_result {
    cyberdeck_enter_action action = cyberdeck_enter_action::NONE;
    std::string payload; /* bytes EXATOS a transmitir ("" salvo se definido) */
    /* Bytes EXATOS a ecoar no terminal LOCAL ("" = SEM eco local; somente o
     * remoto ecoa). O caller anexa `echo` ao output DEPOIS de executar a
     * acao. Politica: "$ <linha>\n" no MENU; "" em CONNECTED/PASSWORD/
     * HOST_KEY (ver regras de enter() abaixo). */
    std::string echo;
};

class cyberdeck_edit_line {
public:
    /* Limite HARD do TUI (paridade com TERMINAL_LIMIT = 12288). */
    static constexpr size_t limit = 12288;

    cyberdeck_edit_line() = default;

    /* Estado da sessao corrente (afeta enter() e visible_line()). */
    void set_session(cyberdeck_session_state state);

    /* ------------------------------------------------------------------ */
    /* Insercao (exatamente `len` bytes em cursor; cursor avanca).         */
    /* Retorna false SEM ALTERAR NADA quando: text == nullptr com len > 0  */
    /* ou a linha excederia `limit` (sem insercao parcial).                */
    /* ------------------------------------------------------------------ */
    bool insert(const char *text, size_t len);
    /* Entrada do TECLADO FISICO (cyberdeck_keyboard_input). */
    bool insert_physical(const char *text, size_t len);
    /* Entrada do TECLADO VIRTUAL (terminal_insert/LV_EVENT_INSERT). */
    bool insert_virtual(const char *text, size_t len);

    /* ------------------------------------------------------------------ */
    /* Teclas de edicao (aware de UTF-8; no-op nas bordas).                */
    /* ------------------------------------------------------------------ */
    void backspace();   /* remove o codepoint anterior ao cursor */
    void del();         /* remove o codepoint na posicao do cursor */
    void cursor_left(); /* move 1 codepoint para a esquerda (clamp) */
    void cursor_right();/* move 1 codepoint para a direita (clamp) */
    void cursor_home(); /* cursor = 0 */
    void cursor_end();  /* cursor = size() */

    /* ------------------------------------------------------------------ */
    /* Observacao                                                          */
    /* ------------------------------------------------------------------ */
    const std::string &line() const { return m_line; }
    size_t cursor() const { return m_cursor; }
    size_t size() const { return m_line.size(); }
    /* Numero de codepoints (UTF-8 aproximado, paridade com o TUI). */
    size_t utf8_length() const;
    /* Linha para exibicao: em PASSWORD, um '*' por byte (paridade com
     * `std::string(s_line.size(), '*')` do TUI); nos demais estados, crua. */
    std::string visible_line() const;

    /* ------------------------------------------------------------------ */
    /* Enter: consome a linha EXATAMENTE uma vez (limpa e cursor = 0) e    */
    /* devolve a acao + payload + echo para o caller transmitir/ecoar.    */
    /* Regras (paridade com execute_line(); politica de eco explicita):    */
    /*   HOST_KEY    -> ACCEPT_HOST_KEY (mesmo com linha vazia); eco "";   */
    /*   CONNECTED   -> linha vazia ou line_already_sent: SEND_NEWLINE      */
    /*                  ("\n" apenas; eco ""); senao SEND_LINE_NEWLINE      */
    /*                  (linha+"\n"; eco "" — SEM eco local em CONNECTED);  */
    /*   branca      -> NONE  [antes de PASSWORD/MENU, paridade do blank   */
    /*                  guard `find_first_not_of(" \t") == npos`]; eco ""; */
    /*   PASSWORD    -> SEND_PASSWORD (payload = linha, sem '\n'); eco ""; */
    /*   MENU        -> LOCAL_COMMAND (payload = linha; echo "$ "+linha+   */
    /*                  "\n").                                              */
    /* ------------------------------------------------------------------ */
    cyberdeck_enter_result enter(bool line_already_sent = false);

    /* Limpa a linha e o cursor (sem semantica de zeroing de memoria:
     * higiene de senha do buffer e responsabilidade do chamador). */
    void clear();

private:
    std::string m_line;
    size_t m_cursor = 0;
    cyberdeck_session_state m_session = cyberdeck_session_state::MENU;
};
