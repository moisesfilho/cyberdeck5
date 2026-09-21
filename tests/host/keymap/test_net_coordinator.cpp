/*
 * Testes de REGRESSAO host-side para o coordenador PURO do teardown
 * Wi-Fi/SSH e gating dos timers (contrato em cyberdeck_net_coordinator.h,
 * implementacao futura em components/cyberdeck/src/core/
 * cyberdeck_net_coordinator.cpp).
 *
 * Bug aprovado B sob teste:
 *   - desconexao IDEMPOTENTE (teardown executa exatamente TEARDOWN_SSH +
 *     TEARDOWN_WIFI, uma vez);
 *   - erro de socket -> offline notificado UMA unica vez;
 *   - callbacks NAO sao chamados apos o teardown concluir (sink inert);
 *   - retry/scan sao sinalizados sem executar trabalho pesado dentro do
 *     callback Timer (tick retorna void, nao consome pendencia, trabalho so
 *     sai de drain_pending(), em contexto de worker);
 *   - nenhum caminho do timer executa API Wi-Fi (invariante comportamental +
 *     contrato estrutural: tick apenas enfileira).
 *
 * TDD (mesmo padrao de test_terminal_filter/test_wifi_indicator): enquanto
 * cyberdeck_net_coordinator.cpp nao existir, `make test_net_coordinator`
 * falha por modulo ausente ("No rule to make target
 * ...cyberdeck_net_coordinator.cpp"). Nenhuma implementacao fake e usada.
 *
 * Estruturado segundo AAA (Arrange, Act, Assert) e principios F.I.R.S.T.
 *
 * Build: make test_net_coordinator -> ver Makefile (so g++/make).
 */
#include "cyberdeck_net_coordinator.h"

#include <array>
#include <cstdio>
#include <utility>

namespace {

int s_failures = 0;
int s_checks = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        ++s_checks;                                                                    \
        if (!(cond)) {                                                                 \
            ++s_failures;                                                              \
            std::printf("FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond);          \
        }                                                                              \
    } while (0)

/* Sink de notificacao de teste: conta chamadas e o ultimo notice/ctx. */
int s_notify_calls = 0;
cyberdeck_net_notice s_last_notice = cyberdeck_net_notice::NONE;
void *s_last_ctx = nullptr;

void counting_sink(cyberdeck_net_notice notice, void *ctx)
{
    ++s_notify_calls;
    s_last_notice = notice;
    s_last_ctx = ctx;
}

/* Zera as estatisticas do sink entre testes (as assercoes de contagem sao
 * absolutas dentro de cada teste). */
void reset_sink()
{
    s_notify_calls = 0;
    s_last_notice = cyberdeck_net_notice::NONE;
    s_last_ctx = nullptr;
}

/* Cria um coordenador com o sink contador e zera as estatisticas. */
cyberdeck_net_coordinator make_coordinator()
{
    reset_sink();
    cyberdeck_net_coordinator co;
    co.set_notify(counting_sink, reinterpret_cast<void *>(0x1234));
    return co;
}

/* Liga uma sessao ficticia (connecting -> online), se aplicavel. */
void go_online(cyberdeck_net_coordinator &co)
{
    co.on_session_connecting();
    co.on_session_online();
    CHECK(co.phase() == cyberdeck_net_phase::ONLINE);
}

/* Drena a fila inteira e devolve o numero de itens consumidos. */
size_t drain_all(cyberdeck_net_coordinator &co)
{
    size_t n = 0;
    for (;;) {
        const cyberdeck_net_drain_result r = co.drain_pending();
        if (r.work == cyberdeck_net_work::NONE && r.notice == cyberdeck_net_notice::NONE) {
            return n;
        }
        ++n;
    }
}

/* ------------------------------------------------------------------ testes */

/* REGRESSAO (B1): desconexao idempotente — o teardown enfileira exatamente
 * TEARDOWN_SSH + TEARDOWN_WIFI, nesta ordem, uma unica vez. */
void test_disconnect_idempotent()
{
    cyberdeck_net_coordinator co = make_coordinator();
    go_online(co);

    co.request_disconnect();
    co.request_disconnect(); /* no-op */
    co.request_disconnect(); /* no-op */

    CHECK(co.phase() == cyberdeck_net_phase::TEARDOWN_IN_PROGRESS);

    const cyberdeck_net_drain_result r1 = co.drain_pending();
    CHECK(r1.work == cyberdeck_net_work::TEARDOWN_SSH);
    CHECK(r1.notice == cyberdeck_net_notice::NONE);
    CHECK(co.phase() == cyberdeck_net_phase::TEARDOWN_IN_PROGRESS);

    const cyberdeck_net_drain_result r2 = co.drain_pending();
    CHECK(r2.work == cyberdeck_net_work::TEARDOWN_WIFI);
    CHECK(r2.notice == cyberdeck_net_notice::NONE);
    /* A virada para DONE acontece ao drenar TEARDOWN_WIFI. */
    CHECK(co.phase() == cyberdeck_net_phase::TEARDOWN_DONE);
    CHECK(co.callbacks_active() == false);

    const cyberdeck_net_drain_result r3 = co.drain_pending();
    CHECK(r3.work == cyberdeck_net_work::NONE);
    CHECK(r3.notice == cyberdeck_net_notice::NONE);
    CHECK(drain_all(co) == 0); /* fila vazia de forma estavel */
}

/* REGRESSAO (B1): pedidos repetidos de desconexao pos-DONE sao no-op. */
void test_disconnect_after_done_noop()
{
    cyberdeck_net_coordinator co = make_coordinator();
    go_online(co);
    co.request_disconnect();
    CHECK(drain_all(co) == 2);

    co.request_disconnect();
    co.request_disconnect();
    CHECK(co.phase() == cyberdeck_net_phase::TEARDOWN_DONE);
    CHECK(drain_all(co) == 0);
}

/* REGRESSAO (B2): erro de socket -> offline notificado UMA vez, mesmo com
 * multiplas fontes de erro (phase CONNECTING). */
void test_socket_error_offline_once_connecting()
{
    cyberdeck_net_coordinator co = make_coordinator();
    co.on_session_connecting();
    CHECK(co.phase() == cyberdeck_net_phase::CONNECTING);

    co.on_socket_error();
    co.on_socket_error(); /* mesma falha relatada por 2a fonte */
    co.on_socket_error(); /* e por uma 3a */

    CHECK(co.phase() == cyberdeck_net_phase::OFFLINE_ERROR);
    CHECK(s_notify_calls == 0); /* sink so e invocado no drain (worker) */

    const cyberdeck_net_drain_result r1 = co.drain_pending();
    CHECK(r1.notice == cyberdeck_net_notice::SSH_OFFLINE);
    CHECK(r1.work == cyberdeck_net_work::NONE);
    CHECK(s_notify_calls == 1); /* exatamente uma entrega */
    CHECK(s_last_notice == cyberdeck_net_notice::SSH_OFFLINE);

    CHECK(drain_all(co) == 0);
    CHECK(s_notify_calls == 1); /* e continua uma so */

    /* Erro apos o OFFLINE_ERROR: no-op, sem segunda notificacao. */
    co.on_socket_error();
    CHECK(co.phase() == cyberdeck_net_phase::OFFLINE_ERROR);
    CHECK(s_notify_calls == 1);
    CHECK(co.has_pending_notice() == false);
}

/* REGRESSAO (B2): mesmo cenario a partir do ONLINE. */
void test_socket_error_offline_once_online()
{
    cyberdeck_net_coordinator co = make_coordinator();
    go_online(co);

    co.on_socket_error();
    co.on_socket_error();

    const cyberdeck_net_drain_result r = co.drain_pending();
    CHECK(r.notice == cyberdeck_net_notice::SSH_OFFLINE);
    CHECK(s_notify_calls == 1);
    CHECK(co.phase() == cyberdeck_net_phase::OFFLINE_ERROR);
    CHECK(drain_all(co) == 0);
    CHECK(s_notify_calls == 1);
}

/* REGRESSAO (B2): erro de socket com o coordenador ja OFFLINE (sem sessao)
 * e no-op — nao existe socket ativo para reportar. */
void test_socket_error_ignored_while_offline()
{
    cyberdeck_net_coordinator co = make_coordinator();
    CHECK(co.phase() == cyberdeck_net_phase::OFFLINE);
    co.on_socket_error();
    CHECK(co.phase() == cyberdeck_net_phase::OFFLINE);
    CHECK(co.has_pending_notice() == false);
    CHECK(drain_all(co) == 0);
    CHECK(s_notify_calls == 0);
}

/* REGRESSAO (B3): callbacks NAO sao chamados apos o teardown concluir. */
void test_no_callbacks_after_teardown()
{
    cyberdeck_net_coordinator co = make_coordinator();
    go_online(co);
    co.request_disconnect();
    CHECK(drain_all(co) == 2);
    CHECK(s_notify_calls == 0);
    CHECK(co.callbacks_active() == false);

    /* Apos DONE: todos os eventos/tick viram no-op — nada enfileira. */
    co.on_socket_error();
    co.request_scan();
    co.request_retry();
    co.on_scan_timer_tick(true, false, false);
    co.on_retry_timer_tick(true, true, false);
    co.on_session_connecting();
    co.on_session_online();

    CHECK(co.has_pending_work() == false);
    CHECK(co.has_pending_notice() == false);
    CHECK(drain_all(co) == 0);
    CHECK(s_notify_calls == 0); /* o sink nunca mais e invocado */
    CHECK(co.phase() == cyberdeck_net_phase::TEARDOWN_DONE);
}

/* REGRESSAO (B3): evento enfileirado ANTES do teardown ainda e entregue
 * (flush da historia, ordem FIFO) — antes dos tokens de teardown. */
void test_pre_teardown_notice_flushed_before_teardown()
{
    cyberdeck_net_coordinator co = make_coordinator();
    go_online(co);

    co.on_socket_error();          /* enfileira SSH_OFFLINE */
    co.request_disconnect();       /* enfileira teardown em seguida */

    const cyberdeck_net_drain_result r1 = co.drain_pending();
    CHECK(r1.notice == cyberdeck_net_notice::SSH_OFFLINE);
    CHECK(s_notify_calls == 1);

    const cyberdeck_net_drain_result r2 = co.drain_pending();
    CHECK(r2.work == cyberdeck_net_work::TEARDOWN_SSH);

    const cyberdeck_net_drain_result r3 = co.drain_pending();
    CHECK(r3.work == cyberdeck_net_work::TEARDOWN_WIFI);
    CHECK(co.phase() == cyberdeck_net_phase::TEARDOWN_DONE);

    CHECK(drain_all(co) == 0);
    CHECK(s_notify_calls == 1); /* nunca mais apos o teardown */
}

/* REGRESSAO (B4): erro e teardown nao podem se intercalar — durante
 * TEARDOWN_IN_PROGRESS nenhum evento novo e aceito. */
void test_events_ignored_during_teardown()
{
    cyberdeck_net_coordinator co = make_coordinator();
    go_online(co);
    co.request_disconnect();
    CHECK(co.phase() == cyberdeck_net_phase::TEARDOWN_IN_PROGRESS);

    co.on_socket_error();
    co.request_scan();
    co.request_retry();
    co.on_scan_timer_tick(true, false, false);
    co.on_retry_timer_tick(true, true, false);
    co.on_session_online();

    /* A fila continua com exatamente os 2 tokens de teardown. */
    CHECK(drain_all(co) == 2);
    CHECK(s_notify_calls == 0);
    CHECK(co.phase() == cyberdeck_net_phase::TEARDOWN_DONE);
}

/* REGRESSAO (B4): scan sinalizado NAO executa trabalho no timer — o pedido
 * sobrevive ao tick e sai UMA vez de drain_pending() (worker). */
void test_scan_flagged_not_executed_in_timer()
{
    cyberdeck_net_coordinator co = make_coordinator();

    /* Pedido explicito (UI): 3 pedidos coalescem em UMA execucao. */
    co.request_scan();
    co.request_scan();
    co.request_scan();
    CHECK(co.has_pending_work() == true);

    const cyberdeck_net_drain_result r = co.drain_pending();
    CHECK(r.work == cyberdeck_net_work::RUN_SCAN);
    CHECK(r.notice == cyberdeck_net_notice::NONE);
    CHECK(s_notify_calls == 0); /* scan nao e notificacao */

    CHECK(co.drain_pending().work == cyberdeck_net_work::NONE); /* uma vez so */

    /* Tick do timer: agenda (nao executa) e nao consome pendencia ja
     * existente — o trabalho sinalizado antes do tick continua la. */
    cyberdeck_net_coordinator co2 = make_coordinator();
    co2.request_scan();
    co2.on_scan_timer_tick(true, false, false);
    CHECK(co2.has_pending_work() == true);
    CHECK(co2.drain_pending().work == cyberdeck_net_work::RUN_SCAN);
    CHECK(co2.drain_pending().work == cyberdeck_net_work::NONE);

    /* Tick puro (sem pedido anterior): sinaliza e o trabalho pesado so sai
     * no drain — estrutura: tick() retorna void e executa nada. */
    cyberdeck_net_coordinator co3 = make_coordinator();
    co3.on_scan_timer_tick(true, false, false);
    CHECK(co3.has_pending_work() == true);
    CHECK(s_notify_calls == 0); /* tick jamais entrega notificacao */
    CHECK(co3.drain_pending().work == cyberdeck_net_work::RUN_SCAN);
    CHECK(co3.drain_pending().work == cyberdeck_net_work::NONE);
}

/* REGRESSAO (B4): retry sinalizado NAO executa trabalho no timer. */
void test_retry_flagged_not_executed_in_timer()
{
    cyberdeck_net_coordinator co = make_coordinator();

    co.request_retry();
    co.request_retry(); /* coalesce */
    CHECK(co.drain_pending().work == cyberdeck_net_work::RUN_RETRY);
    CHECK(co.drain_pending().work == cyberdeck_net_work::NONE);

    /* Tick de retry: agenda e o drain executa fora do timer. */
    cyberdeck_net_coordinator co2 = make_coordinator();
    co2.on_retry_timer_tick(true, true, false);
    CHECK(co2.has_pending_work() == true);
    CHECK(co2.drain_pending().work == cyberdeck_net_work::RUN_RETRY);
    CHECK(co2.drain_pending().work == cyberdeck_net_work::NONE);

    /* Pedido anterior sobrevive ao tick (nada e consumido/duplicado). */
    cyberdeck_net_coordinator co3 = make_coordinator();
    co3.request_retry();
    co3.on_retry_timer_tick(true, true, false);
    CHECK(co3.drain_pending().work == cyberdeck_net_work::RUN_RETRY);
    CHECK(co3.drain_pending().work == cyberdeck_net_work::NONE);
}

/* REGRESSAO (B4/B5): gating dos ticks (paridade com o wifi_mgr atual) —
 * scan so com wifi habilitado, SEM rede salva e DESCONECTADO; retry so com
 * wifi habilitado, COM rede salva e DESCONECTADO. */
void test_timer_tick_gating()
{
    cyberdeck_net_coordinator co = make_coordinator();

    /* Scan: condicao `s_wifi_enabled && !s_has_cfg && !s_connected`. */
    co.on_scan_timer_tick(false, false, false); /* wifi desabilitado */
    CHECK(co.has_pending_work() == false);
    co.on_scan_timer_tick(true, true, false);   /* rede salva existe */
    CHECK(co.has_pending_work() == false);
    co.on_scan_timer_tick(true, false, true);   /* ja conectado */
    CHECK(co.has_pending_work() == false);
    co.on_scan_timer_tick(true, false, false);  /* unica combinacao que agenda */
    CHECK(co.has_pending_work() == true);
    CHECK(co.drain_pending().work == cyberdeck_net_work::RUN_SCAN);

    /* Retry: condicao inversa (rede salva e necessaria). */
    cyberdeck_net_coordinator co2 = make_coordinator();
    co2.on_retry_timer_tick(false, true, false); /* wifi desabilitado */
    CHECK(co2.has_pending_work() == false);
    co2.on_retry_timer_tick(true, false, false); /* sem rede salva */
    CHECK(co2.has_pending_work() == false);
    co2.on_retry_timer_tick(true, true, true);   /* ja conectado */
    CHECK(co2.has_pending_work() == false);
    co2.on_retry_timer_tick(true, true, false);  /* unica combinacao que agenda */
    CHECK(co2.has_pending_work() == true);
    CHECK(co2.drain_pending().work == cyberdeck_net_work::RUN_RETRY);
}

/* REGRESSAO (B2/B4): FIFO — itens sao drenados na ordem em que os eventos os
 * enfileiraram, um por chamada, sem perder nem duplicar nenhum. */
void test_fifo_order_single_item_per_drain()
{
    cyberdeck_net_coordinator co = make_coordinator();
    go_online(co);

    co.request_retry();      /* 1o: RUN_RETRY */
    co.on_socket_error();    /* 2o: SSH_OFFLINE */
    co.request_scan();       /* 3o: RUN_SCAN */

    const cyberdeck_net_drain_result r1 = co.drain_pending();
    CHECK(r1.work == cyberdeck_net_work::RUN_RETRY);
    CHECK(r1.notice == cyberdeck_net_notice::NONE);

    const cyberdeck_net_drain_result r2 = co.drain_pending();
    CHECK(r2.work == cyberdeck_net_work::NONE);
    CHECK(r2.notice == cyberdeck_net_notice::SSH_OFFLINE);
    CHECK(s_notify_calls == 1);

    const cyberdeck_net_drain_result r3 = co.drain_pending();
    CHECK(r3.work == cyberdeck_net_work::RUN_SCAN);
    CHECK(r3.notice == cyberdeck_net_notice::NONE);

    CHECK(drain_all(co) == 0);
    CHECK(s_notify_calls == 1);
}

/* Transicoes de sessao: connecting/online validas e invalidas no-op. */
void test_session_transitions()
{
    cyberdeck_net_coordinator co = make_coordinator();

    /* OFFLINE -> CONNECTING valido; repeticao no-op. */
    co.on_session_connecting();
    CHECK(co.phase() == cyberdeck_net_phase::CONNECTING);
    co.on_session_connecting();
    CHECK(co.phase() == cyberdeck_net_phase::CONNECTING);

    /* CONNECTING -> ONLINE valido. */
    co.on_session_online();
    CHECK(co.phase() == cyberdeck_net_phase::ONLINE);
    co.on_session_online(); /* ja online: no-op */
    CHECK(co.phase() == cyberdeck_net_phase::ONLINE);

    /* Erro -> OFFLINE_ERROR; reconectar e permitido daqui. */
    co.on_socket_error();
    CHECK(co.phase() == cyberdeck_net_phase::OFFLINE_ERROR);
    co.on_session_connecting();
    CHECK(co.phase() == cyberdeck_net_phase::CONNECTING);

    /* ONLINE direto de OFFLINE (sem connecting): no-op. */
    cyberdeck_net_coordinator co2 = make_coordinator();
    co2.on_session_online();
    CHECK(co2.phase() == cyberdeck_net_phase::OFFLINE);

    /* Apos DONE: connecting/online sao no-op. */
    cyberdeck_net_coordinator co3 = make_coordinator();
    go_online(co3);
    co3.request_disconnect();
    CHECK(drain_all(co3) == 2);
    co3.on_session_connecting();
    CHECK(co3.phase() == cyberdeck_net_phase::TEARDOWN_DONE);
}

/* Determinismo: a mesma sequencia de eventos produz a mesma sequencia de
 * drains (a partir de coordenadores novos). */
void test_deterministic()
{
    const auto run_once = []() {
        cyberdeck_net_coordinator co;
        co.set_notify(counting_sink, nullptr);
        co.on_session_connecting();
        co.on_session_online();
        co.request_retry();
        co.on_socket_error();
        co.request_disconnect();
        size_t works = 0;
        size_t notices = 0;
        for (;;) {
            const cyberdeck_net_drain_result r = co.drain_pending();
            if (r.work == cyberdeck_net_work::NONE && r.notice == cyberdeck_net_notice::NONE) {
                break;
            }
            if (r.work != cyberdeck_net_work::NONE) ++works;
            if (r.notice != cyberdeck_net_notice::NONE) ++notices;
        }
        return works * 100 + notices;
    };
    CHECK(run_once() == run_once());
}

/* REGRESSAO (final): sob saturacao, pedidos normais podem ser descartados ou
 * coalescidos.  CANCEL_CONNECT e WIFI_DISCONNECT, porem, sao reservados e nao
 * podem desaparecer quando a fila tambem recebe o teardown. */
void test_full_queue_delivers_cancel_and_wifi_disconnect()
{
    cyberdeck_net_coordinator co = make_coordinator();

    co.request_scan();
    co.request_retry();
    co.request_cancel_connect();
    co.request_wifi_disconnect();
    go_online(co);
    co.on_socket_error();       /* aviso normal: pode ser descartado */
    co.request_disconnect();    /* TEARDOWN_SSH + TEARDOWN_WIFI */

    /* Repeticoes dos comandos criticos devem ser coalescidas, sem substituir
     * os itens criticos ja pendentes. Nao se exige que todos os normais
     * produzidos antes da saturacao sejam aceitos. */
    co.request_cancel_connect();
    co.request_wifi_disconnect();

    CHECK(co.phase() == cyberdeck_net_phase::TEARDOWN_IN_PROGRESS);
    CHECK(co.has_pending_work(cyberdeck_net_work::CANCEL_CONNECT) == true);
    CHECK(co.has_pending_work(cyberdeck_net_work::WIFI_DISCONNECT) == true);
    CHECK(co.drain_pending().work == cyberdeck_net_work::RUN_SCAN);
    CHECK(co.drain_pending().work == cyberdeck_net_work::RUN_RETRY);
    CHECK(co.drain_pending().work == cyberdeck_net_work::CANCEL_CONNECT);
    CHECK(co.drain_pending().work == cyberdeck_net_work::WIFI_DISCONNECT);
    CHECK(co.drain_pending().work == cyberdeck_net_work::TEARDOWN_SSH);
    CHECK(co.drain_pending().work == cyberdeck_net_work::TEARDOWN_WIFI);
    CHECK(co.phase() == cyberdeck_net_phase::TEARDOWN_DONE);
    CHECK(co.drain_pending().work == cyberdeck_net_work::NONE);
}

/* REGRESSAO (final): cancelamento e desconexao solicitados sob saturacao
 * devem produzir sempre a mesma sequencia, preservando os dois comandos
 * criticos. Eventos normais repetidos podem ser coalescidos/descartados; o
 * contrato host-side observa somente a fila/coordenador. */
void test_saturated_control_sequence_is_deterministic()
{
    const auto run_once = []() {
        cyberdeck_net_coordinator co;
        co.request_scan();
        co.request_retry();
        co.request_cancel_connect();
        co.request_wifi_disconnect();
        co.request_disconnect();
        std::array<cyberdeck_net_work, 8> sequence{};
        size_t count = 0;
        for (;;) {
            const auto r = co.drain_pending();
            if (r.work == cyberdeck_net_work::NONE && r.notice == cyberdeck_net_notice::NONE)
                break;
            if (r.work != cyberdeck_net_work::NONE && count < 8)
                sequence[count++] = r.work;
        }
        return std::pair<size_t, std::array<cyberdeck_net_work, 8>>(count, sequence);
    };

    const auto first = run_once();
    const auto second = run_once();
    CHECK(first == second);
    CHECK(first.first == 6);
    CHECK(first.second[0] == cyberdeck_net_work::RUN_SCAN);
    CHECK(first.second[1] == cyberdeck_net_work::RUN_RETRY);
    CHECK(first.second[2] == cyberdeck_net_work::CANCEL_CONNECT);
    CHECK(first.second[3] == cyberdeck_net_work::WIFI_DISCONNECT);
    CHECK(first.second[4] == cyberdeck_net_work::TEARDOWN_SSH);
    CHECK(first.second[5] == cyberdeck_net_work::TEARDOWN_WIFI);
}

/* FRONTEIRA DA POLITICA: a capacidade nao reservada e quatro itens. Os
 * pedidos normais abaixo sao distintos enquanto eventos (scan explicito,
 * retry explicito, aviso de socket e uma nova solicitacao de scan); o quarto
 * pedido de scan e coalescido pelo contrato do tipo de trabalho. O proximo
 * normal tambem nao pode consumir a reserva. Em seguida, os tres comandos
 * criticos devem permanecer na fila e ser drenados em ordem deterministica.
 * Nao ha qualquer expectativa de oito itens normais. */
void test_four_normal_boundary_preserves_reserved_controls()
{
    cyberdeck_net_coordinator co = make_coordinator();
    go_online(co);

    co.request_scan();                         /* normal 1: RUN_SCAN */
    co.request_retry();                        /* normal 2: RUN_RETRY */
    co.on_socket_error();                      /* normal 3: SSH_OFFLINE */
    co.request_scan();                         /* normal 4: coalescido */
    co.request_retry();                        /* proximo normal: coalescido */

    co.request_cancel_connect();
    co.request_wifi_disconnect();
    co.request_disconnect();

    CHECK(co.phase() == cyberdeck_net_phase::TEARDOWN_IN_PROGRESS);
    CHECK(co.has_pending_work(cyberdeck_net_work::CANCEL_CONNECT) == true);
    CHECK(co.has_pending_work(cyberdeck_net_work::WIFI_DISCONNECT) == true);

    const cyberdeck_net_drain_result r1 = co.drain_pending();
    CHECK(r1.work == cyberdeck_net_work::RUN_SCAN);
    CHECK(r1.notice == cyberdeck_net_notice::NONE);

    const cyberdeck_net_drain_result r2 = co.drain_pending();
    CHECK(r2.work == cyberdeck_net_work::RUN_RETRY);
    CHECK(r2.notice == cyberdeck_net_notice::NONE);

    const cyberdeck_net_drain_result r3 = co.drain_pending();
    CHECK(r3.work == cyberdeck_net_work::NONE);
    CHECK(r3.notice == cyberdeck_net_notice::SSH_OFFLINE);

    const cyberdeck_net_drain_result r4 = co.drain_pending();
    CHECK(r4.work == cyberdeck_net_work::CANCEL_CONNECT);
    CHECK(r4.notice == cyberdeck_net_notice::NONE);

    const cyberdeck_net_drain_result r5 = co.drain_pending();
    CHECK(r5.work == cyberdeck_net_work::WIFI_DISCONNECT);
    CHECK(r5.notice == cyberdeck_net_notice::NONE);

    const cyberdeck_net_drain_result r6 = co.drain_pending();
    CHECK(r6.work == cyberdeck_net_work::TEARDOWN_SSH);
    CHECK(r6.notice == cyberdeck_net_notice::NONE);

    const cyberdeck_net_drain_result r7 = co.drain_pending();
    CHECK(r7.work == cyberdeck_net_work::TEARDOWN_WIFI);
    CHECK(r7.notice == cyberdeck_net_notice::NONE);
    CHECK(co.phase() == cyberdeck_net_phase::TEARDOWN_DONE);
    CHECK(co.drain_pending().work == cyberdeck_net_work::NONE);
}

} // namespace

/* Rodador: reinicia as estatisticas do sink antes de cada teste. */
#define RUN_TEST(name)   \
    do {                 \
        reset_sink();    \
        name();          \
    } while (0)

int main()
{
    RUN_TEST(test_disconnect_idempotent);
    RUN_TEST(test_disconnect_after_done_noop);
    RUN_TEST(test_socket_error_offline_once_connecting);
    RUN_TEST(test_socket_error_offline_once_online);
    RUN_TEST(test_socket_error_ignored_while_offline);
    RUN_TEST(test_no_callbacks_after_teardown);
    RUN_TEST(test_pre_teardown_notice_flushed_before_teardown);
    RUN_TEST(test_events_ignored_during_teardown);
    RUN_TEST(test_scan_flagged_not_executed_in_timer);
    RUN_TEST(test_retry_flagged_not_executed_in_timer);
    RUN_TEST(test_timer_tick_gating);
    RUN_TEST(test_fifo_order_single_item_per_drain);
    RUN_TEST(test_session_transitions);
    RUN_TEST(test_deterministic);
    RUN_TEST(test_full_queue_delivers_cancel_and_wifi_disconnect);
    RUN_TEST(test_saturated_control_sequence_is_deterministic);
    RUN_TEST(test_four_normal_boundary_preserves_reserved_controls);

    if (s_failures == 0) {
        std::printf("PASS: cyberdeck_net_coordinator (%d checks)\n", s_checks);
        return 0;
    }
    std::printf("FAIL: %d de %d checks falharam\n", s_failures, s_checks);
    return 1;
}
