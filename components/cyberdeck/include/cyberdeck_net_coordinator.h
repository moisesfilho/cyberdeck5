#pragma once

#include <cstddef>

/*
 * Coordenador PURO do teardown Wi-Fi/SSH e do gating dos timers
 * (bug aprovado B: desconexao idempotente, offline por erro de socket uma
 * unica vez, callbacks inertes apos o teardown e timers que apenas
 * sinalizam trabalho, sem executar APIs Wi-Fi pesadas).
 *
 * Logica de coordenacao hoje espalhada em wifi_mgr.cpp (retry_timer_cb e
 * scan_timer_cb chamam esp_wifi_connect()/wifi_mgr_scan() DENTRO do callback
 * do timer; wifi_mgr_disconnect() nao e idempotente; o offline pode
 * notificar mais de uma vez) e ssh_client.cpp. Este contrato fixa a correcao
 * em uma unidade pura e host-testavel.
 *
 * PROPRIEDADES DE REGRESSAO fixadas por este contrato:
 *
 *  1. DESCONEXAO IDEMPOTENTE: request_disconnect() repetido e no-op. O
 *     teardown enfileira EXATAMENTE dois itens consecutivos — TEARDOWN_SSH,
 *     TEARDOWN_WIFI — executados uma unica vez por drain_pending().
 *
 *  2. ERRO DE SOCKET -> OFFLINE UMA VEZ: on_socket_error() (em CONNECTING/
 *     ONLINE) enfileira a notificacao SSH_OFFLINE uma unica vez, mesmo que o
 *     erro seja reportado por varias fontes/threads; apos a fase
 *     OFFLINE_ERROR, novos erros sao no-op.
 *
 *  3. CALLBACKS INERTES APOS TEARDOWN: ao drenar TEARDOWN_WIFI, a fase passa
 *     a TEARDOWN_DONE e callbacks_active() a false; NENHUM evento/tick
 *     posterior enfileira trabalho ou notificacao e o sink de notificacao
 *     nunca mais e invocado. Eventos enfileirados ANTES do teardown ainda sao
 *     entregues (flush da historia, ordem FIFO): como nenhum evento novo e
 *     aceito durante TEARDOWN_IN_PROGRESS, TEARDOWN_WIFI e sempre o ultimo
 *     item enfileirado, e a virada para DONE no seu drain nunca descarta item
 *     pendente legitimo.
 *
 *  4. TIMERS APENAS SINALIZAM: os callbacks dos timers so podem chamar
 *     on_scan_timer_tick()/on_retry_timer_tick(), que retornam void e apenas
 *     marcam um pedido pendente (coalescente — N pedidos -> 1 execucao — com
 *     o mesmo gating do wifi_mgr atual). O trabalho pesado (RUN_SCAN/
 *     RUN_RETRY) so existe via drain_pending(), que o worker executa FORA do
 *     contexto do timer.
 *
 *  5. NENHUM CAMINHO DO TIMER EXECUTA API Wi-Fi: estrutural por construcao
 *     (tick retorna void; trabalho so sai de drain_pending()). Invariante
 *     comportamental testado: tick NUNCA consome nem executa trabalho
 *     pendente — o item sinalizado sobrevive ao tick e e executado
 *     exatamente uma vez pelo drain.
 *
 * FILA: UMA unica fila FIFO de itens (trabalhos e notificacoes na ordem em
 * que os eventos os enfileiraram). Cada chamada de drain_pending() consome e
 * devolve EXATAMENTE UM item (trabalho OU notificacao, nunca ambos). O sink
 * de notificacao so e invocado dentro de drain_pending() (contexto do
 * worker), nunca nos eventos de entrada nem nos ticks.
 *
 * ACEITE DE EVENTOS:
 *   - callbacks_active() == false (pos-DONE) -> TODOS os eventos/tick sao
 *     no-op;
 *   - TEARDOWN_IN_PROGRESS -> on_socket_error/request_scan/request_retry/
 *     ticks sao no-op (nada novo entra no meio do teardown);
 *   - on_socket_error: apenas de CONNECTING/ONLINE; fase -> OFFLINE_ERROR e
 *     SSH_OFFLINE enfileirada uma vez;
 *   - on_session_connecting: apenas de OFFLINE/OFFLINE_ERROR -> CONNECTING;
 *   - on_session_online: apenas de CONNECTING -> ONLINE;
 *   - on_scan_timer_tick: agenda RUN_SCAN apenas se wifi_enabled &&
 *     !has_saved_network && !connected (paridade com a condicao
 *     `s_wifi_enabled && !s_has_cfg && !s_connected` do scan_timer_cb);
 *   - on_retry_timer_tick: agenda RUN_RETRY apenas se wifi_enabled &&
 *     has_saved_network && !connected.
 *
 * A implementacao em cyberdeck_net_coordinator.cpp e integrada em
 * wifi_mgr.cpp/ssh_client.cpp/ciclo da UI; este header documenta o contrato.
 */
enum class cyberdeck_net_phase : unsigned char {
    OFFLINE = 0,          /* idle: nada ativo */
    CONNECTING,           /* sessao SSH conectando */
    ONLINE,               /* sessao SSH online */
    TEARDOWN_IN_PROGRESS, /* teardown em andamento (callbacks ainda ativos) */
    TEARDOWN_DONE,        /* teardown concluido: callbacks INERTES */
    OFFLINE_ERROR,        /* offline por erro de socket (notificado uma vez) */
};

/* Trabalho pesado a executar pelo worker. Nenhum destes itens pode ser
 * executado dentro de callback de timer — somente em drain_pending(). */
enum class cyberdeck_net_work : unsigned char {
    NONE = 0,
    TEARDOWN_SSH,  /* encerrar sessao SSH (ssh_client_disconnect + task) */
    TEARDOWN_WIFI, /* desconectar/reconfigurar Wi-Fi (wifi_mgr_disconnect) */
    RUN_SCAN,      /* executar scan Wi-Fi (wifi_mgr_scan) */
    RUN_RETRY,     /* tentar reconexao Wi-Fi */
    CANCEL_CONNECT,/* cancelar somente a tentativa Wi-Fi ativa */
    WIFI_DISCONNECT,/* desconectar Wi-Fi sem teardown permanente */
};

/* Notificacao a entregar ao sink (equivalente dos state callbacks da UI).
 * Entregue APENAS em drain_pending(), em contexto de worker. */
enum class cyberdeck_net_notice : unsigned char {
    NONE = 0,
    SSH_OFFLINE, /* sessao SSH ficou offline (erro de socket) */
};

typedef void (*cyberdeck_net_notify_fn)(cyberdeck_net_notice notice, void *ctx);

struct cyberdeck_net_drain_result {
    cyberdeck_net_work work = cyberdeck_net_work::NONE;
    cyberdeck_net_notice notice = cyberdeck_net_notice::NONE;
};

class cyberdeck_net_coordinator {
public:
    cyberdeck_net_coordinator() = default;

    /* Sink de notificacao: invocado somente por drain_pending(). */
    void set_notify(cyberdeck_net_notify_fn fn, void *ctx);

    /* ------------------------------------------------------------------ */
    /* Eventos de sessao/tarefas (NUNCA chamados do callback do timer).    */
    /* ------------------------------------------------------------------ */
    /* Sessao SSH iniciou conexao: apenas de OFFLINE ou OFFLINE_ERROR. */
    void on_session_connecting();
    /* Sessao SSH ficou online: apenas de CONNECTING. */
    void on_session_online();
    /* Erro de socket na sessao ativa (CONNECTING/ONLINE): fase
     * OFFLINE_ERROR e notificacao SSH_OFFLINE enfileirada UMA vez. */
    void on_socket_error();
    /* Pedido de desconexao (usuario/UI): idempotente. Enfileira
     * TEARDOWN_SSH e TEARDOWN_WIFI (nesta ordem, consecutivos) e vai a
     * TEARDOWN_IN_PROGRESS. Repeticoes sao no-op. */
    void request_disconnect();
    void request_wifi_disconnect();
    /* Pedido de scan/retry: coalescente (N pedidos -> 1 item na fila).
     * Ignorado durante teardown ou apos DONE. */
    void request_scan();
    void request_retry();
    /* Cancela somente a tentativa Wi-Fi corrente; o coordenador permanece
     * reutilizavel para scan, retry e uma nova conexao. */
    void request_cancel_connect();

    /* ------------------------------------------------------------------ */
    /* CAMINHO DO TIMER: os callbacks dos timers SO podem chamar estes      */
    /* metodos. Retornam void e apenas marcam pedidos pendentes (com o      */
    /* mesmo gating do wifi_mgr atual); NUNCA executam trabalho pesado,     */
    /* NUNCA entregam notificacao e NUNCA consomem pedidos pendentes.       */
    /* ------------------------------------------------------------------ */
    void on_scan_timer_tick(bool wifi_enabled, bool has_saved_network, bool connected);
    void on_retry_timer_tick(bool wifi_enabled, bool has_saved_network, bool connected);

    /* ------------------------------------------------------------------ */
    /* WORKER (contexto de task, NUNCA do callback do timer): consome e     */
    /* devolve EXATAMENTE UM item pendente (FIFO) e entrega a notificacao    */
    /* via sink, quando aplicavel. Ao drenar TEARDOWN_WIFI, a fase passa a   */
    /* TEARDOWN_DONE e callbacks_active() a false (sem passo extra). Apos    */
    /* DONE, sempre NONE/NONE.                                              */
    /* ------------------------------------------------------------------ */
    cyberdeck_net_drain_result drain_pending();

    /* Observacao. */
    cyberdeck_net_phase phase() const { return m_phase; }
    /* False apos o teardown concluir: entradas passam a ser no-op. */
    bool callbacks_active() const { return m_callbacks_active; }
    bool has_pending_work() const { return m_count != 0; }
    /* Usado pelos chamadores void para confirmar que um pedido critico foi
     * aceito (inclusive quando ja estava coalescido na fila). */
    bool has_pending_work(cyberdeck_net_work work) const;
    /* True se ha SSH_OFFLINE na fila (coalescido/ainda nao drenado). */
    bool has_pending_notice() const;
    void reset();

private:
    /* A fila reserva quatro posicoes para comandos criticos. Assim, scans,
     * retries e notificacoes nunca conseguem consumir a capacidade necessaria
     * para CANCEL_CONNECT/WIFI_DISCONNECT ou para o par de teardown. */
    enum item_kind : unsigned char {
        ITEM_TEARDOWN_SSH = 1,
        ITEM_TEARDOWN_WIFI,
        ITEM_RUN_SCAN,
        ITEM_RUN_RETRY,
        ITEM_CANCEL_CONNECT,
        ITEM_WIFI_DISCONNECT,
        ITEM_SSH_OFFLINE,
    };

    bool contains(item_kind item) const;
    bool enqueue(item_kind item, bool critical);

    cyberdeck_net_phase m_phase = cyberdeck_net_phase::OFFLINE;
    bool m_callbacks_active = true;
    cyberdeck_net_notify_fn m_notify_fn = nullptr;
    void *m_notify_ctx = nullptr;
    item_kind m_queue[8] = {};
    size_t m_head = 0;
    size_t m_tail = 0;
    size_t m_count = 0;
};
