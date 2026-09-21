#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Contrato da selecao de indices do anel "recent" do log de eventos.
 *
 * Extracao pura (host-testavel, sem FreeRTOS/LVGL/SD) da aritmetica hoje
 * embutida em event_log_latest() (event_log.cpp): escolher, em ordem
 * cronologica (mais antigo -> mais recente), os indices dos `count` eventos
 * mais recentes do anel. O nome/estado do anel espelham a producao:
 *
 *   - recent_count: entradas validas no anel (0..EVENT_LOG_RECENT_CAPACITY).
 *   - next: proximo slot de escrita (sempre em [0, EVENT_LOG_RECENT_CAPACITY)
 *           apos o wrap de remember_record(); o evento mais antigo vive em
 *           (next + capacity - recent_count) % capacity.
 *
 * A funcao NAO le os registros: escreve apenas os indices (slots) no buffer
 * de saida, permitindo que a implementacao de producao itere o anel
 * registro a registro sem copiar os RECENT_COUNT registros para a stack
 * (motivo do overflow de stack do comando `log`).
 *
 * Garantia de seguranca (propriedade principal do contrato): nenhum indice
 * fora de [0, capacity) e produzido, para qualquer entrada documentada; em
 * estado invalido a funcao retorna 0 sem escrever nada.
 */

/* Capacidade do anel "recent" (paridade com RECENT_COUNT = 10 em
 * event_log.cpp). O static_assert no teste quebra o build se a producao
 * divergir deste valor. */
#define EVENT_LOG_RECENT_CAPACITY 10

/*
 * Preenche `out_indices` com os indices (slots) dos `count` eventos mais
 * recentes do anel, em ordem cronologica (mais antigo primeiro), todos em
 * [0, capacity), e retorna quantos indices foram escritos.
 *
 * Parametros:
 *   recent_count  entradas validas no anel; invalido se > capacity.
 *   next          proximo slot de escrita; invalido se >= capacity.
 *   count         numero maximo de eventos mais recentes a selecionar
 *                 (qualquer valor >= 0; e limitado a recent_count).
 *   capacity      capacidade do anel (>= 1; use EVENT_LOG_RECENT_CAPACITY).
 *   out_indices   buffer com espaco para min(count, recent_count) size_t;
 *                 nullptr e aceito e resulta em retorno 0.
 *
 * Retorno (n): min(count, recent_count), o numero de indices escritos.
 *
 * Casos de retorno 0 (nenhum indice escrito, buffer intacto):
 *   - count == 0 (paridade com event_log_latest(max_events = 0));
 *   - recent_count == 0 (anel vazio);
 *   - out_indices == nullptr;
 *   - capacity == 0;
 *   - recent_count > capacity ou next >= capacity (estado invalido: falha
 *     deterministica e segura, nenhum acesso fora de [0, capacity)).
 *
 * Pos-condicoes quando n > 0:
 *   - out_indices[0] == (next + capacity - n) % capacity;
 *   - out_indices[i] == (out_indices[0] + i) % capacity para 0 <= i < n
 *     (sequencia cronologica continua, com wrap);
 *   - para todo i em [0, n): 0 <= out_indices[i] < capacity.
 */
size_t event_log_recent_indices(size_t recent_count, size_t next, size_t count,
                                size_t capacity, size_t *out_indices);

#ifdef __cplusplus
}
#endif