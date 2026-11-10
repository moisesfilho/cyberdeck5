#pragma once

#include <cstddef>
#include <cstdint>

#include "apps/shell/cyberdeck_shell_completion.h"
#include "platform/storage/cyberdeck_sd_io_coordinator.h"

/*
 * Enumeracao assincrona de caminhos para o Tab (REQ-TAB-05).
 *
 * A varredura do SD nunca roda na task LVGL: a sessao enfileira o pedido e o
 * worker (task propria, fila bounded) publica o resultado de volta no contexto
 * LVGL.  Cada pedido carrega o token monotônico da sessao, que descarta
 * resultados obsoletos; o worker ainda invalida entregas no teardown, como o
 * worker do `cat`.
 */
namespace cyberdeck_completion_worker {

/* Transferencia bounded: no maximo `k_max_candidates` nomes de ate
 * `k_max_name_bytes` bytes terminados em NUL. */
struct result {
    std::uint64_t token = 0;
    bool truncated = false;
    std::size_t count = 0;
    cyberdeck_shell_completion::candidate items[cyberdeck_shell_completion::k_max_candidates];
};

/* Executado no contexto LVGL, uma vez por pedido aceito. */
using callback = void (*)(result &completed, void *context);

/* O agendamento na task do display fica fora deste TU, na porta de plataforma
 * `platform/display/cyberdeck_ui_post.h`: nenhuma camada de aplicacao inclui
 * LVGL. */

/* Liga o coordenador SD (lease READ por varredura).  Idempotente; chamar antes
 * de `start()`. */
void bind_sd_io(cyberdeck_sd_io::coordinator &coordinator);

bool start(const char *host_root, callback deliver, void *context);
/* Falso quando o worker nao esta pronto ou a fila esta cheia; a sessao entao
 * preserva a linha. */
bool enqueue(const cyberdeck_shell_completion::request &request, std::uint64_t token);
/* Idempotente: para e junta o worker antes de liberar fila/estado. */
void teardown();
/* Fence permanente do poweroff; timeout conserva recursos vivos. */
bool quiesce(unsigned timeout_ms);

} // namespace cyberdeck_completion_worker
