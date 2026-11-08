#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#include <cstddef>
#include <cstdint>

/*
 * Servico do canal HTTP de comandos na LAN (`cyberdeck.screenshot`, porta 80).
 *
 * As rotas /control/... sao registradas pelo servidor LAN existente
 * (screenshot_server.cpp) e reutilizam a mesma checagem de peer local. O
 * comando NAO roda na task HTTP: ele e validado, autenticado com chave
 * dedicada e enfileirado; uma task propria injeta o texto pela mesma entrada de
 * teclado usada por `ui.type` da ponte USB. Assim o `idf.py monitor` pode ficar
 * aberto durante a validacao.
 *
 * `POST /control/upload` compartilha a chave e o nonce, mas nao passa pela fila
 * de teclado: ele transmite um BMP para `/sdcard/screenshots` em streaming com
 * buffer fixo, sob lease WRITE do coordenador de SD. O adapter HTTP autentica
 * os metadados (tamanho + SHA-256) antes de abrir qualquer arquivo.
 *
 * `start()` prepara a chave em NVS (gerando na primeira vez), a fila bounded e
 * a task de injecao. Falha de NVS nao derruba o boot: as rotas respondem
 * not_ready ate uma nova inicializacao bem-sucedida.
 */
namespace cyberdeck_control {

constexpr std::size_t k_serial_key_chars = 2 * 32 + 1;
/* Capacidade da fila de injecao e orcamento do snapshot textual do terminal. */
constexpr std::size_t k_input_queue_depth = 4;
constexpr std::size_t k_terminal_max_bytes = 2048;

esp_err_t start();
esp_err_t stop(unsigned timeout_ms);
bool ready();

/* Somente pela ponte USB Serial-JTAG (provisionamento). Nunca expor pela LAN,
 * pelo terminal, pelo log ou pela captura de tela. */
esp_err_t serial_key(char *hex, std::size_t capacity);

/* Handlers registrados por screenshot_server.cpp. */
esp_err_t challenge_handler(httpd_req_t *req);
esp_err_t type_handler(httpd_req_t *req);
esp_err_t terminal_handler(httpd_req_t *req);
esp_err_t upload_handler(httpd_req_t *req);

} // namespace cyberdeck_control
