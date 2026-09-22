# cyberdeck5 Code Map

Mapa de navegacao do firmware monolitico ESP-IDF para o M5Stack Tab5
(ESP32-P4). Os caminhos abaixo sao relativos a raiz do repositorio.

## Visao geral

- Plataforma: ESP-IDF 5.5.5, LVGL 9.x, BSP `m5stack_tab5`.
- Aplicacao: uma unica tela TUI LVGL, com header, relogio, indicador Wi-Fi e terminal.
- Organizacao: `components/cyberdeck/src/features/` contem fluxos de produto;
  `components/cyberdeck/src/platform/` contem integracoes de hardware e runtime.
- A logica pura e extraida para testes host; `main/app_main.cpp` faz a composicao
  das implementacoes dependentes do ESP-IDF.

## Pontos de entrada e fluxo de boot

| Ponto | Arquivo | Responsabilidade |
| --- | --- | --- |
| `app_main()` | `main/app_main.cpp` | Monta SD, valida o handle, inicia log/NVS, display/LVGL, IMU, UI, protecao de tela, teclado, brilho, servidor de screenshot e Wi-Fi. |
| `cyberdeck_ui_init()` | `components/cyberdeck/src/platform/display/cyberdeck_ui.cpp` | Cria a tela TUI, header, terminal, callbacks e estado de entrada. |
| `wifi_mgr_start()` | `components/cyberdeck/src/features/wifi/wifi_mgr.cpp` | Inicia o gerenciamento de Wi-Fi e reconexao. |
| `screenshot_server_init()` | `components/cyberdeck/src/features/screenshot/screenshot_server.cpp` | Prepara o servidor HTTP; a disponibilidade depende do estado Wi-Fi. |
| `tab5_keyboard_init()` | `components/cyberdeck/src/platform/input/tab5_keyboard.cpp` | Inicia a task do teclado fisico e entrega eventos a `cyberdeck_keyboard_input`. |

Ordem relevante de inicializacao:

1. `bsp_sdcard_mount()` e validacao de `bsp_sdcard_get_handle()`.
2. `event_log_init()` e `nvs_flash_init()`.
3. `bsp_display_start()`.
4. Sob lock do display: `imu_reader_start()`, `cyberdeck_ui_init()` e `screen_off_init()`.
5. Callback do teclado, `tab5_keyboard_init()` e brilho.
6. `screenshot_server_init()`, callback de estado do screenshot e `wifi_mgr_start()`.

## Funcionalidades

### UI e terminal

| Arquivo | Simbolos/funcao | Papel |
| --- | --- | --- |
| `components/cyberdeck/src/platform/display/cyberdeck_ui.cpp` | `cyberdeck_ui_init`, `cyberdeck_keyboard_input`, callbacks de SSH/Wi-Fi | Compoe a TUI, roteia Enter por estado, atualiza LVGL sob lock e integra shell, SSH, Wi-Fi e log. |
| `components/cyberdeck/include/platform/display/cyberdeck_ui.h` | API publica da UI | Contrato usado por `app_main` e pelo driver de teclado. |
| `components/cyberdeck/src/platform/display/cyberdeck_font.c` | Fonte monoespaciada | Recurso visual do terminal/header. |
| `components/cyberdeck/src/platform/display/cyberdeck_clock.cpp` | `cyberdeck_clock_from_utc`, `cyberdeck_format_clock` | Conversao/formato do relogio GMT-3. |
| `components/cyberdeck/src/platform/display/cyberdeck_wifi_indicator.cpp` | `cyberdeck_wifi_indicator_is_lit` | Regra pura: claro somente com `enabled && connected && has_ip`. |
| `components/cyberdeck/src/platform/display/cyberdeck_wifi_icon.cpp` | layout, criacao, resize e cor do icone | Desenha tres arcos e ponto; recalcula posicao em resize. |
| `components/cyberdeck/src/platform/display/screen_off.cpp` | `screen_off_init` | Timeout de 120 s e duplo toque para religar a tela. |

O header usa grade 30/40/30 para titulo, relogio e Wi-Fi. Nao exibe SSID nem
estado SSH. Estados SSH vao para o terminal e event log; diagnostico Wi-Fi e
obtido pelo comando `wifi`.

### Shell local

| Arquivo | Simbolos/contrato | Papel |
| --- | --- | --- |
| `components/cyberdeck/src/features/shell/cyberdeck_local_shell.cpp` | classe `cyberdeck_local_shell` | Shell confinado ao root virtual `/sdcard`; implementa `pwd`, `cd`, `ls`, `touch`, `mkdir`, `rm`, `rmdir` e ajuda. |
| `components/cyberdeck/include/features/shell/cyberdeck_local_shell.h` | API do shell local | Contrato usado pela UI e testes. |
| `components/cyberdeck/src/features/shell/cyberdeck_shell_utils.cpp` | `cyberdeck_help_text`, `parse_ssh_target` | Ajuda comum e parser de `ssh [user@]host[:port]`. |
| `components/cyberdeck/src/features/shell/cyberdeck_history.cpp` | classe `cyberdeck_history` | Historico limitado a 64 linhas, com navegacao e duplicatas preservadas. |
| `components/cyberdeck/src/features/shell/cyberdeck_edit_line.cpp` | classe `cyberdeck_edit_line` | Linha UTF-8, cursor, backspace, Enter e comportamento por sessao. |

O shell local aceita `..` somente em `cd`, faz clamp no `/sdcard`, rejeita
traversal nos demais comandos e protege contra symlinks. `ls` e `ls -a` usam a
raiz fisica do SD sem adicionar `/.`; limites de entradas, nome e saida sao
bounded.

### SSH

| Arquivo | Simbolos/contrato | Papel |
| --- | --- | --- |
| `components/cyberdeck/src/features/ssh/ssh_client.cpp` | `ssh_client_connect`, `ssh_client_send_data`, `ssh_client_disconnect` | Cliente libssh assincrono, task FreeRTOS, PTY, senha, host key e callbacks de estado/dados. |
| `components/cyberdeck/include/features/ssh/ssh_client.h` | `ssh_client_state_t` e API SSH | Contrato entre UI e cliente. |
| `components/cyberdeck/src/features/shell/cyberdeck_terminal_filter.cpp` | classe/funcoes do filtro incremental | Remove ANSI/CSI/OSC, normaliza CR/LF, descarta C0/DEL e preserva UTF-8. |
| `components/cyberdeck/src/features/shell/cyberdeck_ssh_echo_guard.cpp` | `cyberdeck_ssh_echo_guard` | Suprime somente o eco remoto que corresponde ao payload enviado. |
| `components/cyberdeck/src/features/shell/cyberdeck_ssh_line_composer.cpp` | `cyberdeck_ssh_line_composer` | Mantem no maximo um comando pendente e controla o separador entre eco e saida. |

O parser de alvo suporta IPv4, IPv6 entre brackets, usuario e porta. A selecao
de familia usa `inet_pton`; hostnames continuam usando `ANY`. O overlay local de
`sock_utils` fornece `getnameinfo` dual-stack e flags compativeis com libssh/lwIP.

### Wi-Fi

| Arquivo | Simbolos/contrato | Papel |
| --- | --- | --- |
| `components/cyberdeck/src/features/wifi/wifi_mgr.cpp` | API `wifi_mgr_*` | Adaptador ESP-IDF, conexao, desconexao, scan, callbacks, tokens e integracao de rede. |
| `components/cyberdeck/src/features/wifi/wifi_storage.cpp` | armazenamento de redes | Persistencia de SSID/credencial no SD; senhas nao sao exibidas nem registradas. |
| `components/cyberdeck/src/features/wifi/cyberdeck_wifi_menu.cpp` | `cyberdeck_wifi_search_menu` | Modelo/renderizacao pura do menu de busca e redes salvas. |
| `components/cyberdeck/src/features/wifi/cyberdeck_wifi_state_machine.cpp` | `state_machine` | Estados search/saved/password/connecting/forget e acoes derivadas. |
| `components/cyberdeck/src/features/wifi/cyberdeck_wifi_event_dispatch.cpp` | `event_dispatch` | Snapshots bounded de `GOT_IP`/`LOST_IP`, tokens, ack, retry e invalidacao. |
| `components/cyberdeck/src/features/wifi/cyberdeck_wifi_persistence_queue.cpp` | `persistence_queue` | Fila bounded com ownership, retry, acknowledge, wipe e teardown. |
| `components/cyberdeck/src/features/wifi/cyberdeck_wifi_persistence_coordinator.cpp` | coordinator | Worker que serializa persistencia e efeitos derivados fora de `sys_evt`. |
| `components/cyberdeck/src/platform/networking/cyberdeck_net_coordinator.cpp` | `cyberdeck_net_coordinator` | Gating de timers e teardown idempotente de Wi-Fi/SSH. |

O callback de `GOT_IP` somente publica snapshot. Trabalho pesado, SNTP,
persistencia e notificacao da UI ocorrem no contexto apropriado. Tokens
monotônicos impedem callbacks atrasados de alterar uma tentativa nova.

### Screenshot HTTP

| Arquivo | Simbolos/contrato | Papel |
| --- | --- | --- |
| `components/cyberdeck/src/features/screenshot/screenshot_server.cpp` | `screenshot_server_init`, `screenshot_server_wifi_state` | Servidor `GET /screenshot` na porta 80, iniciado/parado por estado Wi-Fi. |
| `components/cyberdeck/src/features/screenshot/screenshot_bmp.cpp` | geracao de BMP | Converte framebuffer RGB565 em BMP 24-bit bottom-up com stride/padding. |

O endpoint serializa requisicoes, usa lock de display somente durante o
snapshot e aceita apenas peers locais: loopback, RFC1918, link-local, ULA,
IPv6 local e IPv4-mapped em IPv6.

### Input, sensores e logging

| Arquivo | Simbolos/contrato | Papel |
| --- | --- | --- |
| `components/cyberdeck/src/platform/input/tab5_keyboard.cpp` | `tab5_keyboard_init`, callback e task | Driver I2C Character do Tab5; entrega eventos fora da stack da task. |
| `components/cyberdeck/src/platform/input/tab5_keyboard_event.cpp` | `tab5_char_event_parse` | Parser puro; `REG_CHAR_EVENT_LEN` inclui modificador. |
| `components/cyberdeck/src/platform/input/tab5_keyboard_keys.cpp` | mapeamento de teclas | Conversao dos codigos LVGL/Tab5 para eventos da UI. |
| `components/cyberdeck/src/platform/logging/event_log.cpp` | `event_log_init`, `event_log_write`, `event_log_latest` | Log circular e task de persistencia/consulta. |
| `components/cyberdeck/src/platform/logging/event_log_recent.cpp` | `event_log_recent_indices` | Selecao pura dos indices recentes sem copiar todos os registros na stack. |
| `components/cyberdeck/src/platform/sensors/imu_reader.cpp` | `imu_reader_start` | Inicializacao do BMI270 e leitura usada pela orientacao. |
| `components/cyberdeck/src/platform/sensors/orientation.cpp` | orientacao | Rotacao da tela baseada no sensor. |

## Dependencias e composicao

### ESP-IDF e componentes

`components/cyberdeck/CMakeLists.txt` registra todos os fontes de producao e
declara dependencias de LVGL, BSP, Wi-Fi, rede, FreeRTOS, SD/FATFS, libssh e
HTTP server. `main/idf_component.yml` declara ESP-IDF, `esp_lvgl_port`,
`esp_hosted`, `esp_wifi_remote`, libssh e o override local de `sock_utils`.

### Overlay local

`components/sock_utils/` substitui a dependencia gerenciada de mesmo nome:

- `src/getnameinfo.c`: AF_INET, AF_INET6 e IPv4-mapped.
- `include/netdb_macros.h`: flags `NI_NUMERICHOST`, `NI_NUMERICSERV` e `NI_DGRAM`.
- `test/host/main/test_getnameinfo.cpp`: validacao host dual-stack.

### Concorrencia

- UI/LVGL: atualizacoes protegidas por `bsp_display_lock`.
- Teclado fisico: fila FIFO bounded de 8 e despacho por `lv_async_call`.
- SSH: task dedicada e callbacks coordenados com a UI.
- Wi-Fi: callbacks de eventos publicam snapshots; workers/coordinators executam I/O.
- Screenshot: mutex de requisicao e lock de display apenas durante captura.
- Log: ring buffer e processamento incremental para evitar overflow de stack.

## Matriz producao -> testes host

| Producao | Testes principais |
| --- | --- |
| `tab5_keyboard_keys.cpp` | `test_keymap.cpp` |
| `tab5_keyboard_event.cpp` | `test_keyboard_event.cpp` |
| `cyberdeck_shell_utils.cpp` | `test_shell_utils.cpp` |
| `cyberdeck_history.cpp` | `test_history.cpp` |
| `cyberdeck_edit_line.cpp` | `test_edit_line.cpp`, `test_prompt_behavior.py` |
| `cyberdeck_local_shell.cpp` | `test_local_shell.cpp`, `test_prompt_behavior.py`, `test_local_prompt_contract.py` |
| `event_log_recent.cpp` | `test_event_log_recent.cpp` |
| `cyberdeck_clock.cpp` | `test_clock_formatter.cpp` |
| `cyberdeck_wifi_indicator.cpp` | `test_wifi_indicator_state.cpp` |
| `cyberdeck_wifi_icon.cpp` | `test_wifi_icon_layout.cpp` |
| `cyberdeck_ui.cpp` (roteamento Enter Wi-Fi) | `wifi_enter_dispatch_contract.py` |
| `cyberdeck_wifi_menu.cpp` | `test_wifi_menu.cpp` |
| `cyberdeck_wifi_state_machine.cpp` | `test_wifi_state_machine.cpp` |
| `cyberdeck_wifi_event_dispatch.cpp` | `test_wifi_event_dispatch.cpp`, `test_wifi_dispatch_contract.py` |
| `cyberdeck_wifi_persistence_queue.cpp` | `test_wifi_persistence_queue.cpp` |
| `cyberdeck_wifi_persistence_coordinator.cpp` | `test_wifi_persistence_coordination.cpp` |
| `cyberdeck_terminal_filter.cpp` | `test_terminal_filter.cpp` |
| `cyberdeck_ssh_echo_guard.cpp` | `test_ssh_echo_guard.cpp` |
| `cyberdeck_ssh_line_composer.cpp` | `test_ssh_line_composer.cpp` |
| `cyberdeck_net_coordinator.cpp` | `test_net_coordinator.cpp` |
| `screenshot_bmp.cpp` | `test_screenshot_bmp.cpp` |
| `cyberdeck_ui.cpp` | `test_boot_sequence.py`, `test_keyboard_input_contract.py`, `test_ui_resource_contract.py`, `test_local_prompt_contract.py`, `test_wifi_enter_routing_contract.py` |
| `main/app_main.cpp` | `test_boot_sequence.py` |

Os testes host nao substituem a validacao do hardware para LVGL, touch, I2C,
Wi-Fi real, libssh real ou endpoint HTTP. Os contratos Python inspecionam a
fonte real quando a UI nao e linkavel no host.

## Comandos de validacao

### Testes host

```bash
make -C tests/host/keymap clean test
make -C tests/host/keymap verify
```

`verify` compara os valores `LV_KEY_*` do shim com o LVGL gerenciado. Para uma
execucao completa, `test` tambem roda os contratos estruturais de boot, input,
cleanup da UI, prompts e dispatch Wi-Fi.

### Build ESP-IDF

```bash
source ~/esp/esp-idf/export.sh
idf.py set-target esp32p4
idf.py build
```

### Flash e monitor

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

### Validacao manual

O roteiro TUI esta em `tests/manual/tui-shell-validation.pt-BR.md`.

## Limites e cuidados de manutencao

- Nao executar I/O bloqueante na thread LVGL, na task `sys_evt` ou em callbacks
  de timer; use os coordinators/workers existentes.
- Callbacks SSH e Wi-Fi podem ocorrer sob locks ou atravessar geracoes; use
  snapshots, ownership explicito e tokens.
- Nao aumentar buffers de stack sem investigar a origem; `event_log_latest`
  foi deliberadamente tornado incremental.
- Ao alterar valores do LVGL usados em host, atualizar o shim somente quando a
  paridade com o LVGL gerenciado for mantida.
- O endpoint `/screenshot` e restrito a peers locais, mas nao e um mecanismo de
  autenticacao.
- `managed_components/` e artefatos `build/` sao gerados pelo ESP-IDF e nao
  fazem parte do mapa detalhado de fontes da aplicacao.

## Documentacao relacionada

- `AGENTS.md`: instrucoes obrigatorias para agentes consultarem e manterem este
  mapa, incluindo a politica de busca progressiva.
- `README.md`: recursos, uso do shell, Wi-Fi, SSH e screenshot.
- `docs/ARCHITECTURE.md`: principios, boot, concorrencia Wi-Fi, screenshot e UI.
- `tests/manual/tui-shell-validation.pt-BR.md`: validacao no dispositivo.
- `components/cyberdeck/CMakeLists.txt`: lista definitiva dos fontes compilados.
- `tests/host/keymap/Makefile`: lista definitiva dos testes e fontes puros.
