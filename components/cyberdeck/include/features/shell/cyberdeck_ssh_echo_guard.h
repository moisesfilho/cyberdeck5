#pragma once

#include <cstddef>
#include <string>

/*
 * Guarda incremental de eco remoto do SSH (unidade PURA, host-testavel).
 *
 * CONTEXTO (feature aprovada: historico/eco remoto SSH)
 * ----------------------------------------------------
 * Em SSH_CONNECTED, cyberdeck_edit_line::enter() devolve o payload local exato
 * (`cmd\n` ou `\n`) SEM eco local; o terminal remoto ecoa de volta os mesmos
 * bytes. Como a linha digitada ja aparece na superficie de edicao, esse eco
 * remoto pode ser exibido em duplicidade. Este guard e o ponto unico que
 * decide, de forma incremental, se o comeco do fluxo remoto (JA FILTRADO por
 * cyberdeck_terminal_filter) e o eco do payload local (e deve ser suprimido)
 * ou e saida legitima do servidor (e deve ser preservada integralmente).
 *
 * Escopo: este header documenta o contrato; a implementacao fica em
 * components/cyberdeck/src/features/shell/cyberdeck_ssh_echo_guard.cpp e e integrada
 * em on_ssh_data().
 *
 * ENTRADA: o guard recebe o fluxo de saida remota DEPOIS do
 * cyberdeck_terminal_filter (ANSI/CSI/OSC ja removidos, CRLF ja normalizado
 * para LF, C0/DEL ja descartados). Ele NAO interpreta bytes de controle: trata
 * cada byte como opaco e compara byte a byte com o payload armado.
 *
 * PROPRIEDADES DE CONTRATO (regressao)
 * ------------------------------------
 *  1. ARMAR: arm(payload, len) copia exatamente `len` bytes do payload local
 *     (tipicamente "cmd\n", ou "\n" para linha vazia). Enquanto armado, o
 *     guard pode reter os primeiros bytes do remoto que ainda podem ser o eco.
 *
 *  2. ECO SUPRIMIDO: se os primeiros bytes do fluxo remoto forem IGUAIS ao
 *     payload armado, byte a byte, esses bytes sao consumidos e NAO aparecem
 *     na saida. Isso vale mesmo com fragmentacao arbitraria (o eco pode ser
 *     entregue em chunks de 1 byte).
 *
 *  3. SAIDA PRESERVADA (sem eco): se o fluxo remoto divergir do payload em
 *     qualquer posicao, todos os bytes ja retidos mais o byte divergente e
 *     todo o restante do fluxo sao emitidos em ordem, sem perda. Um stream
 *     que nao e o eco nunca e descartado.
 *
 *  4. SAIDA IMEDIATA: se o PRIMEIRO byte do chunk ja diverge do payload, ele e
 *     emitido na MESMA chamada de feed(), sem espera adicional.
 *
 *  5. DIVERGENCIA PARCIAL: se k bytes iniciais casarem e o byte k+1 divergir,
 *     emite-se o prefixo retido (payload[0..k)) seguido do byte divergente e do
 *     resto. O prefixo retido NUNCA e engolido.
 *
 *  6. FIM DE FLUXO: flush() libera um prefixo parcial retido (preserva saida
 *     quando o eco nao completou) e devolve o guard a IDLE, reutilizavel.
 *
 *  7. MULTIPLOS COMANDOS: assim que o ultimo byte do payload e consumido
 *     (match total, mesmo que isso encerre o chunk) ou a divergencia ocorre, o
 *     guard volta a IDLE e passa bytes direto; pode ser armado de novo para o
 *     proximo comando. A transicao para IDLE e imediata: armed() ja retorna
 *     false na mesma chamada que conclui o eco.
 *
 *  8. OPAQUEZ/UTF-8: bytes altos (UTF-8) e qualquer byte nao-ASCII sao
 *     comparados e emitidos byte a byte, sem validacao nem re-encode; um
 *     codepoint pode ser fatiado entre chunks sem corromper a comparacao.
 *
 * LIMITES DE BUFFER DE SAIDA
 * --------------------------
 * Uma unica chamada de feed() escreve NO MAXIMO len + payload.size() bytes: o
 * prefixo retido (<= payload.size() - 1) pode ser liberado junto com o byte
 * divergente do chunk atual. Portanto, com out_cap >= len + payload.size(),
 * nenhum byte e perdido. Se out_cap for menor, os bytes excedentes sao descartados
 * e feed() satura a escrita em out_cap (ret == out_cap); o estado interno avanca
 * normalmente. Para garantia absoluta contra perda de bytes, a UI deve sempre
 * fornecer out_cap >= len + payload.size().
 *
 * Observacao: chamar arm() com o guard ainda armado (retencao pendente) e
 * recusado (retorna false) para nunca descartar saida silenciosamente; o
 * chamador deve resolver a deteccao (feed/flush) antes de rearmar.
 */
class cyberdeck_ssh_echo_guard {
public:
    /* IDLE, sem payload armado e sem retencao. */
    cyberdeck_ssh_echo_guard();

    /*
     * Arma o guard com o payload local (bytes exatos enviados ao remoto).
     *
     * Retorna true quando o payload foi copiado e a deteccao comeca em IDLE.
     * Retorna false SEM ALTERAR NADA quando:
     *   - payload == nullptr com len > 0 (entrada invalida);
     *   - len == 0 (nao ha eco a suprimir nesta arma);
     *   - o guard ja esta armado (retencao pendente) — evita descartar saida.
     */
    bool arm(const char *payload, size_t len);

    /*
     * Consome os `len` bytes remotos (ja filtrados) e escreve em `out` (ate
     * `out_cap` bytes) os bytes que devem ser exibidos, suprimindo o eco
     * correspondente ao payload armado. Retorna os bytes escritos.
     *
 *   - IDLE: copia os bytes direto (pass-through opaco);
 *   - ARMADO: retem bytes que casam com o payload; ao completar o payload,
 *     descarta o eco e passa o restante direto (fica IDLE imediatamente, mesmo
 *     que o ultimo byte do payload encerre o chunk); na divergencia, emite o
 *     prefixo retido + o byte divergente + o restante;
     *   - data == nullptr e/ou len == 0: no-op, retorna 0 (nada muda);
     *   - data e out nao podem se sobrepor.
     */
    size_t feed(const char *data, size_t len, char *out, size_t out_cap);

    /*
     * Fim do fluxo: se houver prefixo parcial retido (eco incompleto), emite-o
     * (sujeito a out_cap) e volta a IDLE. Retorna os bytes escritos. Se nao
     * houver retencao, retorna 0. Depois de flush() o guard segue utilizavel.
     * flush(out == nullptr e/ou out_cap == 0) ainda descarta a retencao e
     * retorna 0.
     */
    size_t flush(char *out, size_t out_cap);

    /* True enquanto ha payload armado aguardando resolucao (match/divergencia/
     * flush). False em IDLE. */
    bool armed() const;

private:
    std::string m_payload;    /* payload local armado (bytes exatos) */
    size_t m_matched = 0;     /* bytes do payload ja casados e retidos */
    bool m_armed = false;     /* deteccao em andamento */
};
