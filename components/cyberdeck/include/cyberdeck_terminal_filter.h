#pragma once

#include <cstddef>

/*
 * Filtro puro e incremental da saida remota do SSH (plano aprovado).
 *
 * Objetivo: transformar o fluxo bruto de bytes recebido do canal SSH
 * (stdout/stderr) em texto "limpo" para o terminal TUI, sem emular um
 * terminal VT100 completo:
 *
 *   - remove sequencias ANSI/CSI (ESC [ ... final) e OSC (ESC ] ... ST|BEL),
 *     alem de escapes curtos (ESC c, ESC 7, ESC ( B, ESC # 8, ...);
 *   - normaliza quebras de linha: CRLF e CR isolado viram LF;
 *   - remove bytes de controle C0 (exceto \r, \n e \t) e DEL (0x7F);
 *   - preserva texto e UTF-8 byte a byte (transparente: sem validacao nem
 *     re-encode; sequencias de 2, 3 ou 4 bytes, inclusive truncadas ou
 *     invalidas, passam intactas);
 *   - suporta fragmentacao: o fluxo pode ser entregue em chunks de qualquer
 *     tamanho; uma sequencia de escape/OSC cortada entre chunks e retomada
 *     nas chamadas seguintes de feed() e, se o fluxo terminar no meio dela,
 *     descartada por flush(). Um '\r' no fim de um chunk aguarda o proximo
 *     byte para decidir entre CRLF (um LF) e CR isolado (um LF).
 *
 * Unidade pura e host-testavel (sem LVGL/FreeRTOS/ESP-IDF): toda a logica
 * vive aqui; a UI apenas consome o texto devolvido por feed()/flush().
 * O estado interno e minusculo e constante (maquina de sequencias + flag de
 * CR pendente; sem filas, sem alocacao), e o filtro e trivialmente copiavel.
 *
 * A implementacao em cyberdeck_terminal_filter.cpp e integrada ao fluxo
 * on_ssh_data()/append_output(); este header documenta o contrato.
 */
class cyberdeck_terminal_filter {
public:
    /* Estado inicial: GROUND, sem pendencia alguma. */
    cyberdeck_terminal_filter();

    /*
     * Consome os `len` bytes de entrada e escreve em `out` (ate `out_cap`
     * bytes) o texto filtrado correspondente a esses bytes. Retorna o total
     * de bytes escritos.
     *
     * Garantias do contrato:
     *   - uma chamada de feed() escreve NO MAXIMO len + 1 bytes (o byte extra
     *     e o LF resultante de um '\r' pendente do chunk anterior, resolvido
     *     pelo primeiro byte do chunk atual); portanto, com out_cap >= len + 1,
     *     nenhum texto filtrado e perdido (buffer de saida len + 1 sempre
     *     basta);
     *   - se out_cap < len + 1 (ou out == nullptr e/ou out_cap == 0), a entrada
     *     e consumida normalmente, o estado avanca, e os bytes filtrados que
     *     nao couberem sao descartados (truncamento detectavel: ret <= out_cap);
     *     a UI de producao deve sempre usar out_cap >= len + 1;
     *   - data == nullptr e/ou len == 0: no-op, retorna 0 (nada muda);
     *   - data e out nao podem se sobrepor;
     *   - sequencias de controle incompletas no fim do chunk ficam pendentes
     *     e emitidas/concluidas na proxima feed(); nada e emitido por elas
     *     ate que a sequencia complete (e entao e descartada) ou aborte.
     */
    size_t feed(const char *data, size_t len, char *out, size_t out_cap);

    /*
     * Fim do fluxo: descarta sequencia de controle incompleta pendente
     * (ESC/CSI/OSC cortados no ultimo chunk) e, se houver '\r' pendente,
     * emite um '\n'. Retorna os bytes escritos. Depois de flush() o filtro
     * volta ao estado inicial e permanece utilizavel (feed() segue normal).
     * flush(out == nullptr e/ou out_cap == 0) ainda descarta o estado
     * pendente e retorna 0.
     */
    size_t flush(char *out, size_t out_cap);

    /*
     * Gramatica das sequencias (referencia para a implementacao e os testes;
     * estados internos abaixo):
     *
     *   GROUND (texto): unico estado que produz saida. Em GROUND:
     *     - '\r': fica pendente (aguarda o proximo byte: '\n' -> CRLF vira
     *       um '\n'; qualquer outro byte -> emite '\n' antes dele; outro
     *       '\r' -> emite '\n' e mantem o novo CR pendente);
     *     - '\n' isolado: preservado como '\n';
     *     - '\t': preservado;
     *     - demais C0 (0x00-0x1F) e DEL (0x7F): removidos;
     *     - 0x1B (ESC): entra em ESC;
     *     - 0x20-0x7E (imprimiveis) e 0x80-0xFF (UTF-8/bytes altos):
     *       preservados byte a byte.
     *
     *   ESC (apos ESC, aguardando o proximo byte):
     *     - '[' -> CSI; ']' -> OSC;
     *     - intermediate 0x20-0x2F -> ESC_INT (um ou mais);
     *     - final 0x30-0x7E -> sequencia curta concluida (ESC + final),
     *       tudo descartado, volta a GROUND;
     *     - C0 (0x00-0x1F)/DEL/0x80-0xFF -> sequencia parcial descartada e o
     *       byte atual reprocessado em GROUND.
     *
     *   ESC_INT (ESC + um ou mais intermediates 0x20-0x2F):
     *     - 0x20-0x2F -> continua (mais um intermediate);
     *     - final 0x30-0x7E -> sequencia concluida (ex.: ESC ( B, ESC # 8,
     *       ESC $ ( C), tudo descartado, volta a GROUND;
     *     - qualquer outro byte -> sequencia parcial descartada e o byte
     *       atual reprocessado em GROUND.
     *
     *   CSI (ESC [): consome 0x20-0x3F (params/intermediates em qualquer
     *     ordem) ate um final 0x40-0x7E: sequencia concluida, tudo
     *     descartado, volta a GROUND. C0/DEL/0x80-0xFF: sequencia parcial
     *     descartada e o byte atual reprocessado em GROUND.
     *
*   OSC (ESC ]): consome todos os bytes ate o terminador BEL (0x07) ou ST
 *     (a sequencia ESC '\\', dois bytes); tudo e descartado, inclusive o
 *     terminador. Um ESC dentro do OSC comeca a deteccao de ST (sub-estado
 *     OSC_ST); se o proximo byte nao for '\\', o OSC e descartado e o byte e
 *     reprocessado em GROUND. No flush(), um OSC pendente (sem terminador) e
 *     descartado.
 */

private:
    enum seq_state_t : unsigned char {
        SEQ_GROUND = 0,
        SEQ_ESC,     /* ESC visto, aguardando o proximo byte */
        SEQ_ESC_INT, /* ESC + um ou mais intermediates, aguardando final */
        SEQ_CSI,     /* ESC [, aguardando final 0x40-0x7E */
        SEQ_OSC,     /* ESC ], aguardando BEL ou ST */
        SEQ_OSC_ST,  /* ESC dentro do OSC: aguardando '\\' do ST */
    };

    seq_state_t m_seq = SEQ_GROUND;
    bool m_pending_cr = false; /* '\r' no fim do ultimo chunk (aguarda lookahead) */
};
