#pragma once

#include <cstddef>
#include <string>

/*
 * Composicao local/remota da linha SSH (unidade PURA, host-testavel).
 *
 * CONTEXTO (composicao aprovada: banda local + eco remoto da linha SSH)
 * ----------------------------------------------------------------------
 * Em SSH_CONNECTED, cyberdeck_edit_line::enter() devolve o payload local
 * (`cmd\n` ou `\n`, SEM eco local); o terminal remoto costuma ecoar de volta
 * os mesmos bytes. Este compositor decide a FAIXA EXIBIDA de cada linha:
 * o comando local aparece SEM o '\n' final (o cursor permanece junto ao
 * comando). A supressao do eco em si segue a mesma comparacao byte a byte da
 * cyberdeck_ssh_echo_guard; este seam apenas retém o casamento do payload até
 * o eco ser confirmado ou divergir. Saida remota e LF recebido sao
 * preservados verbatim: o compositor nao fabrica separadores nem LF.
 *
 * POLITICA SERIALIZADA (plano aprovado: "Seriação do envio SSH", opcao 1)
 * -----------------------------------------------------------------------
 * O compositor mantem NO MAXIMO UM comando pendente por vez (estado unico
 * IDLE/PENDING). NAO existe fila FIFO nem type-ahead: enquanto uma linha
 * aguarda resolucao, begin() adicional e recusado: retorna 0, nao escreve
 * o comando, nao fabrica byte adicional e NAO consome/altera a pendencia (o
 * eco em curso permanece intacto). A pendencia e resolvida por:
 *   (a) eco remoto completo (propriedade 2);
 *   (b) divergencia da saida remota em relacao ao payload (propriedade 3);
 *   (c) flush() (propriedade 5).
 * Apos a resolucao o compositor volta a IDLE e begin() aceita a proxima
 * linha. O gate de Enter que retem/rejeita a digitacao durante PENDING vive
 * na UI (cyberdeck_ui.cpp); este compositor apenas recusa o begin().
 *
 * Escopo: este header documenta o contrato. A implementacao fica em
 * components/cyberdeck/src/apps/shell/cyberdeck_ssh_line_composer.cpp e e integrada
 * em execute_line()/on_ssh_data()/fim de sessao do TUI.
 *
 * ENTRADA: o compositor recebe o fluxo de saida remota DEPOIS do
 * cyberdeck_terminal_filter (ANSI/CSI/OSC ja removidos, CRLF ja normalizado
 * para LF, C0/DEL ja descartados). Ele NAO interpreta bytes de controle:
 * trata cada byte como opaco e compara byte a byte com o payload armado
 * (command + '\n').
 *
 * PROPRIEDADES DE CONTRATO (regressao)
 * ------------------------------------
 *  1. BEGIN: begin(command,len) arma uma
 *     linha cujo payload enviado ao remoto foi `command + "\n"` e escreve o
 *     comando local exatamente uma vez. O compositor aceita NO MAXIMO UMA
 *     linha pendente: se active() == true, begin() e recusado (retorna 0,
 *     nao escreve o comando e nao altera a pendencia).
 *     Para a linha vazia, command = "" e begin() nao escreve o comando;
 *     nenhum byte adicional e fabricado.
 *
 *  2. ECO SUPRIMIDO: enquanto armado, bytes remotos
 *     que coincidem byte a byte com `command + "\n"` sao retidos em silencio.
 *     Quando o eco COMPLETA (ultimo byte do payload casado, inclusive com o
 *     eco fatiado em chunks de 1 byte), o eco e descartado e o compositor
 *     volta a IDLE na mesma
 *     chamada. Nenhum byte e fabricado.
 *
 *  3. SEM ECO: se o fluxo divergir do payload na posicao k (0 <= k <
 *     payload.size()), o prefixo retido, o byte divergente e o restante do
 *     chunk sao emitidos verbatim. Nenhum byte e engolido ou fabricado.
 *     Divergencia no primeiro byte e emitida na MESMA chamada de feed(). O
 *     compositor volta a IDLE.
 *
 *  4. LF REMOTO: um '\n' divergente ou recebido apos o eco completo e saida
 *     remota legitima e e preservado integralmente, inclusive uma linha em
 *     branco real. O compositor nao insere LF adicional.
 *
 *  5. FIM DE FLUXO (flush): resolve a linha pendente — se o eco ficou
 *     incompleto (prefixo parcial do payload retido), esse prefixo ja foi
 *     exibido por begin() e NAO e reemitido (o comando nao duplica); nenhum
 *     byte e emitido e o
 *     compositor volta a IDLE. flush() tambem e o reset: depois dele,
 *     begin() arma a proxima linha. flush(out == nullptr e/ou out_cap == 0)
 *     ainda descarta a pendencia e retorna 0.
 *
 *  6. UM COMANDO PENDENTE / REARM: nao ha fila nem type-ahead. Enquanto ha
 *     linha armada, begin() e recusado (retorna 0 sem consumir nem escrever)
 *     e o casamento do eco pendente e preservado, inclusive com eco
 *     fragmentado. Apos a linha ser resolvida — eco completo (2),
 *     divergencia (3) ou flush() (5) — o compositor volta a IDLE e begin()
 *     aceita a proxima linha. Cada comando e exibido uma vez.
 *
 *  7. OPAQUEZ/UTF-8: bytes altos e qualquer byte nao-ASCII sao comparados e
 *     emitidos byte a byte, sem validacao nem re-encode; um codepoint pode
 *     ser fatiado entre chunks sem corromper a comparacao com o eco.
 *
 * LIMITES DE BUFFER DE SAIDA
 * --------------------------
 *   - begin(): escreve no maximo `len` bytes (o comando). Uma recusa escreve
 *     0. Com out_cap >= len, nenhum byte do comando e perdido; com out_cap
 *     menor, a escrita satura em out_cap (ret == out_cap) e o payload
 *     completo permanece armado.
 *   - feed(): uma unica chamada escreve no maximo `len` bytes (o prefixo
 *     retido, o byte divergente e o resto do chunk). Com out_cap >= len,
 *     nenhum byte e perdido; com out_cap
 *     menor, a escrita satura e os bytes excedentes sao descartados (o
 *     estado interno avanca normalmente).
 *   - flush(): nao escreve bytes.
 *
 * Observacao: begin() com command == nullptr (qualquer len) e recusado;
 * begin("", 0) e um COMANDO VAZIO valido (payload "\n"). feed() no-op com
 * data == nullptr e/ou len == 0. data/out nao podem se sobrepor.
 */
class cyberdeck_ssh_line_composer {
public:
    /* IDLE, sem linha pendente. */
    cyberdeck_ssh_line_composer();

    /*
     * Arma o compositor para a linha cujo payload enviado ao remoto foi
     * `command + "\n"` e escreve em `out` (ate `out_cap`) o comando local.
     * Retorna os bytes escritos (o comando).
     * Retorna 0 SEM ALTERAR NADA quando:
     *   - command == nullptr (entrada invalida; inclusive com len == 0);
     *   - active() == true (ja existe linha pendente): a recusa nao escreve
     *     o comando e nao consome a pendencia (o casamento
     *     do eco em curso permanece intacto).
     * Apos a pendencia ser resolvida (eco/divergencia/flush), begin() e
     * aceito normalmente.
     */
    size_t begin(const char *command, size_t len, char *out, size_t out_cap);

    /*
     * Consome os `len` bytes remotos (ja filtrados) e escreve em `out` (ate
     * `out_cap`) os bytes a exibir, suprimindo o eco correspondente ao
     * payload armado e preservando a saida remota verbatim.
     * Retorna os bytes escritos.
     *
     *   - IDLE: copia os bytes direto (pass-through opaco);
     *   - PENDING (eco): retem bytes que casam com o payload; ao completar o
     *     payload, descarta o eco e processa o restante em IDLE;
     *   - PENDING (divergencia em k): emite o prefixo retido payload[0..k),
     *     o byte divergente e o restante do chunk sem inserir bytes;
     *   - data == nullptr e/ou len == 0: no-op, retorna 0 (nada muda);
     *   - data e out nao podem se sobrepor.
     */
    size_t feed(const char *data, size_t len, char *out, size_t out_cap);

    /*
     * Fim do fluxo/reset: resolve a linha pendente. O prefixo parcial retido
     * (ja exibido por begin()) nao e reemitido e nenhum byte e emitido. Volta
     * a IDLE e e reutilizavel por
     * begin(). Em IDLE retorna 0. flush(out == nullptr e/ou out_cap == 0)
     * descarta a pendencia e retorna 0.
     */
    size_t flush(char *out, size_t out_cap);

    /* True enquanto ha uma linha armada aguardando resolucao (eco/divergencia/
     * flush). False em IDLE. */
    bool active() const;

private:
    bool m_active = false;          /* IDLE/PENDING: no maximo um comando */
    std::string m_payload;          /* command + "\n" */
    size_t m_matched = 0;           /* bytes do payload ja casados */
};
