#pragma once

#include "esp_err.h"
#include "esp_wifi_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_SCAN_MAX_APS 16
#define WIFI_SCAN_SYNC_TIMEOUT_MS 8000

typedef void (*wifi_scan_cb_t)(const wifi_ap_record_t *aps, int count, void *ctx);

typedef struct {
    bool connected;
    bool has_ip;
    char ssid[33];
    char ip[16];
    uint64_t connection_token;
} wifi_status_t;

typedef void (*wifi_state_cb_t)(const wifi_status_t *status, bool enabled, void *ctx);

esp_err_t wifi_mgr_start(void);

#define WIFI_MGR_MAX_STATE_LISTENERS 4

/* Registra o observador do estado (adiciona listener). O callback recebe um snapshot e pode ser
 * chamado pela task de eventos; o contexto permanece sob responsabilidade do
 * chamador. */
esp_err_t wifi_mgr_add_state_callback(wifi_state_cb_t cb, void *ctx);

/* Compatibilidade: registra observador do estado. */
esp_err_t wifi_mgr_set_state_callback(wifi_state_cb_t cb, void *ctx);

/* Delivers coalesced state snapshots from the display/UI task.  Listener
 * callbacks are never invoked by the Wi-Fi event or persistence tasks. */
void wifi_mgr_process_state_callbacks(void);

/* Salva a config no SD e conecta na rede */
esp_err_t wifi_mgr_connect(const char *ssid, const char *password);

/* Dispara um scan; chama cb com os APs encontrados (copia valida ate o retorno) */
esp_err_t wifi_mgr_scan(wifi_scan_cb_t cb, void *ctx);

/* Cancela o scan identificado pelo callback. Retorna true quando o callback
 * ja foi destacado e ainda possui o contexto; nesse caso o chamador nao pode
 * libera-lo. Retorna false quando o contexto foi destacado sem callback e
 * permanece sob responsabilidade do chamador. */
bool wifi_mgr_cancel_scan(wifi_scan_cb_t cb, void *ctx);

/* Desconecta da rede ativa e interrompe tentativas de reconexao */
esp_err_t wifi_mgr_disconnect(void);
esp_err_t wifi_mgr_cancel_connection(void);
uint64_t wifi_mgr_connection_token(void);

/* Esquece uma rede (remove do SD) e desconecta se for a rede atual */
esp_err_t wifi_mgr_forget(const char *ssid);

/* Estado atual da conexao */
esp_err_t wifi_mgr_get_status(wifi_status_t *status);

/* Habilitar / Desabilitar subsistema Wi-Fi */
esp_err_t wifi_mgr_set_enabled(bool enabled);
bool wifi_mgr_is_enabled(void);

/* Eventos leves usados pelo cliente SSH para compartilhar o mesmo
 * coordenador de teardown/reconexao. Nao executam APIs Wi-Fi. */
void wifi_mgr_net_session_connecting(void);
void wifi_mgr_net_session_online(void);
void wifi_mgr_net_session_socket_error(void);

#ifdef __cplusplus
}
#endif
