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
| `components/cyberdeck/src/platform/display/cyberdeck_ui.cpp` | `cyberdeck_ui_init`, `cyberdeck_ui_deinit`, `cyberdeck_keyboard_input`, callbacks de SSH/Wi-Fi/cat | Compoe a TUI multilinear, roteia Enter por estado, sanitiza dados de `cat` antes do LVGL, aplica limite explicito ao textarea, atualiza sob lock e integra shell, SSH, Wi-Fi, auditoria local com gate de estado no timer (sem publicar `collecting`) e o hand-off não bloqueante de `wifi audit save` com ACK/path pós-publicação. |
| `components/cyberdeck/include/platform/display/cyberdeck_ui.h` | API publica da UI | Contrato usado por `app_main` e pelo driver de teclado. |
| `components/cyberdeck/src/platform/display/cyberdeck_font.c` | Fonte monoespaciada | Recurso visual do terminal/header. |
| `components/cyberdeck/src/platform/display/cyberdeck_clock.cpp` | `cyberdeck_clock_from_utc`, `cyberdeck_format_clock` | Conversao/formato do relogio GMT-3. |
| `components/cyberdeck/src/platform/display/cyberdeck_wifi_indicator.cpp` | `cyberdeck_wifi_indicator_is_lit` | Regra pura: claro somente com `enabled && connected && has_ip`. |
| `components/cyberdeck/src/platform/display/cyberdeck_wifi_icon.cpp` | layout, criacao, resize e cor do icone | Desenha tres arcos e ponto; recalcula posicao em resize. |
| `components/cyberdeck/src/platform/display/screen_off.cpp` | `screen_off_init` | Timeout de 120 s e duplo toque para religar a tela. |

O header usa grade 30/40/30 para titulo, relogio e Wi-Fi. Nao exibe SSID nem
estado SSH. Estados SSH vao para o terminal e event log; diagnostico Wi-Fi e
obtido pelo comando `wifi` e pela auditoria local.

### Shell local

| Arquivo | Simbolos/contrato | Papel |
| --- | --- | --- |
| `components/cyberdeck/src/features/shell/cyberdeck_local_shell.cpp` | classe `cyberdeck_local_shell`; `cyberdeck_local_shell_cat` | Shell confinado ao root virtual `/sdcard`; tokenizer manual byte-a-byte bounded para espacos/tabs; implementa `pwd`, `cd`, `ls`, `cat`, `touch`, `mkdir`, `rm`, `rmdir` e ajuda. A API cat-specific usa apenas strings bounded e descritores confinados, retorna output heap-backed, limita arquivos a 12288 bytes e chunks de 1024, e e usada pelo worker sem construir o shell geral. |
| `components/cyberdeck/src/features/shell/cyberdeck_cat_worker.cpp`, `components/cyberdeck/include/features/shell/cyberdeck_cat_worker.h` | `cyberdeck_cat_worker_start`, `cyberdeck_cat_worker_enqueue`, `cyberdeck_cat_worker_teardown` | Worker FreeRTOS com fila bounded para I/O de `cat`, stack explícita de 6144 bytes; o worker deve chamar uma API cat-specific heap/bounded, sem construir/usar o shell genérico, `fs::path` ou `vector` no caminho específico. O contrato estrutural permite os identificadores `cyberdeck_local_shell_*` da API dedicada e rejeita apenas a construção/uso genérico. Cada start drena a sinalização de parada e cria uma geração nova, e teardown sinaliza/aguarda o retorno do worker antes de liberar fila, root e callback, invalidando callbacks LVGL tardios. |
| `components/cyberdeck/include/features/shell/cyberdeck_local_shell.h` | API do shell local | Contrato usado pela UI e testes. |
| `components/cyberdeck/src/features/shell/cyberdeck_shell_utils.cpp`, `components/cyberdeck/include/features/shell/cyberdeck_shell_utils.h` | `cyberdeck_help_text`, `parse_ssh_target`, `cyberdeck_parse_command`, `CYBERDECK_CMD_WIFI_AUDIT_SAVE` | Ajuda comum, parser de `ssh [user@]host[:port]` e comandos `wifi`, incluindo auditoria local e `wifi audit save` explícito; a grafia de exportação legada é rejeitada. |
| `components/cyberdeck/src/features/shell/cyberdeck_history.cpp` | classe `cyberdeck_history` | Historico limitado a 64 linhas, com navegacao e duplicatas preservadas. |
| `components/cyberdeck/src/features/shell/cyberdeck_edit_line.cpp` | classe `cyberdeck_edit_line` | Linha UTF-8, cursor, backspace, Enter e comportamento por sessao. |

O shell local aceita `..` somente em `cd`, faz clamp no `/sdcard`, rejeita
traversal nos demais comandos e protege contra symlinks. `cat` abre por
descritores confinados, com `O_NOFOLLOW`/`openat` quando disponíveis, `fstat` e
leitura do mesmo descritor; no ESP-IDF usa abertura direta do caminho confinado
compatível com o VFS FATFS (sem objetos symlink), preservando `O_NOFOLLOW` quando
exposto, e rejeita conservadoramente plataformas host sem essas garantias. `ls` e `ls -a` usam a
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
| `components/cyberdeck/src/features/wifi/cyberdeck_wifi_audit.cpp`, `components/cyberdeck/include/features/wifi/cyberdeck_wifi_audit.h` | `cyberdeck_wifi_audit::audit_controller` | Worker FreeRTOS com fila bounded para auditoria passiva da associação/interface local, snapshot versionado, token sob mutex, teardown por sinalização/join, leitura bounded do campo SSID sem varredura além do array do driver, saneamento central, renderização direta de `status`/`ssid`/`bssid`/`ip` com `<missing>` e payload key=value bounded determinístico em seis linhas na ordem `version`/`token`/`status`/`ssid`/`bssid`/`ip`, com cada chave exatamente uma vez, LF final e campos ausentes marcados por `<missing>`, persistência explícita por `wifi audit save`, sem I/O no comando padrão e I/O fora da UI; o resultado grande e o scratch de formatação ficam fora da stack do worker, e o resultado bem-sucedido preserva `bytes`/`data` somente após a persistência. No ESP/FATFS, o adaptador delega toda a transação ao seam injetável, sem fallback legado; o commit usa sidecars `.tmp`/`.bak` derivados por `make_transaction_paths` no diretório `/sdcard/wifi-audit/`, O_EXCL + fsync + renomeação em sequência, com nome GMT-3 `wifi-audit-YYYYMMDD-HHMMSS.txt` e colisão fail-closed e recuperação stale segura, sem prometer atomicidade POSIX de replace; se o rollback falhar, candidato e backup são preservados. Falhas de entrega por fila cheia usam um slot de fallback bounded. No firmware, a transação nativa roda em uma task curta `wifi_audit_io` com deadline de 2 s; o worker aguarda apenas esse limite, mantém ownership do request até a liberação e não compartilha adapter/sink com a UI. Uma operação VFS que não retorna fica quarentenada sem permitir um segundo acesso concorrente aos sidecars; a UI recebe `failed` em vez de permanecer pendente. |
| `components/cyberdeck/src/features/wifi/cyberdeck_wifi_audit_persistence.cpp`, `components/cyberdeck/include/features/wifi/cyberdeck_wifi_audit_persistence.h` | `cyberdeck_wifi_audit_persistence::{file_ops, completion_sink, audit_persistence}` | Seam host injetável para a transação durável da auditoria: criação do diretório `/sdcard/wifi-audit/`, `open_exclusive`/write parcial/`fsync`/`close`/rename, recovery fail-closed dos sidecars `.tmp`/`.bak`, rollback com preservação quando a restauração falha, publicação no-clobber para o destino final, fila single-shot bounded, guard de ACK e retenção de completion quando o sink rejeita a entrega. O adapter ESP/FatFs é wired pelo backend `wifi_audit_io` de `cyberdeck_wifi_audit.cpp`; o seam e o sink têm ownership exclusivo da task, enquanto o controller bounded apenas aguarda a completion. O teardown e a corrida entre `enqueue`/`pump_one`/`drain` são serializados pelo seam; o contrato host fica em `tests/host/keymap/contracts/cyberdeck_wifi_audit_persistence.h`. |
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
snapshot e aceita apenas peers locais: loopback, 10/8, link-local, ULA,
IPv6 local e IPv4-mapped em IPv6.

### Ponte manual USB Serial-JTAG NDJSON (REQ-002/003/005/006/007/008/009)

| Arquivo | Simbolos/contrato | Papel |
| --- | --- | --- |
| `components/cyberdeck/include/features/serial/cyberdeck_serial_bridge.h` | `cyberdeck_serial::k_max_ndjson_line=4096`, `k_max_rid_len=64`, `k_screen_chunk_bytes=1024`, `k_fs_write_max_bytes=2048`, `dispatch_error`, `request/response/dispatch_result`, `SysInfo/WifiNet`, `LineAssembler`, `crc32/screen_bmp_size/screen_chunk_bounds/screen_dump_*`, `sys_info_* / wifi_scan_* / handle_* / dispatch_one / build_envelope`, `bridge_start` (`#ifdef ESP_PLATFORM`) | Contrato NDJSON bounded: parse limitado, envelopes com `rid` ecoado, erros tipados, comandos UI/sys/wifi e chunk/CRC da captura; `fs.write` seguro com path confinado, 2048 decoded, Base64 canonico, commit por temp unico no mesmo diretorio, criado com O_CREAT|O_EXCL e removido apenas apos create bem-sucedido, +rename (replace atomico no host; no-clobber no ESP/FATFS) e NDJSON errors. Header puro (sem `esp_err.h`). |
| `components/cyberdeck/src/features/serial/cyberdeck_serial_bridge.cpp` | parser JSON proprio (`parse_value` classifica e decodifica cada valor numa unica passada: string vira `kind::string` com `s` decodido e `raw` literal com aspas), dispatch, sessao BMP/CRC, LineAssembler; `fs.write` (campos `path/data_b64/size` no `k_request_fields`, tipo conhecido `fs.write`, validacao rid/type/path/data_b64/size, path safety traversal/symlink/directory/parent, 2048, Base64 strict canonico `b64_decode_canonical`, temp unico `.fswrite.<sequencia>.tmp` no mesmo diretorio, criado com O_CREAT|O_EXCL e removido apenas apos create bem-sucedido, +rename sem unlink do destino, CRC; no ESP exige `O_NOFOLLOW` e limita `/sdcard` ao VFS FATFS sem symlink/openat, valida parents e o descriptor temporario, rejeita overwrite existente porque nao ha garantia de replace atomico, e preserva o destino em falha de rename; no host a raiz logica `/sdcard` e reancorada em sandbox `/tmp/cyberdeck5_sd` via `CYBERDECK_SD_ROOT`); device: task `serial_brg`, driver USB Serial-JTAG, `esp_log_set_vprintf` com mutex, hooks UI/wifi/screen/fs | Dispatch NDJSON sem alocacao >4096, validacao UTF-8/rid/type, `screen.dump` byte-identical em chunks 1024 com CRC IEEE e flag `end`, feeder tolerante a logs/fragmentacao, `sys.info`/`wifi`; `fs.write` com validacao completa, commit seguro e erros tipados `invalid_path`/`invalid_payload`/`io_error`. Autocontido em relacao a `screenshot_bmp_*` fora de `ESP_PLATFORM`: os tres testes que nao linkam `screenshot_bmp.cpp` nao veem undefined de funcoes externas (stride/size/header/conv sao espelhados localmente com formulas identicas). |
| `tools/cyberdeck_cli.py` | `CyberdeckSession`, `open_session/connect`, `exchange`, comandos `ping/sys.info/wifi.status/wifi.scan/ui.click/ui.tap/ui.type/ui.dump/ui.clear/screen.shot/screen.dump/fs.write`, `build_request` com `--data/--data-b64/--input/--stdin/--size/stdin`, `validate_fs_write_path`, `decode_b64_canonical`, `read_fs_write_input`, `FS_WRITE_MAX_BYTES=2048`, encoding Base64 canonico | Cliente NDJSON host: correlacao por `rid` (sem campo `action`), descarte de logs/frames nao-JSON, validacao de dump (contiguidade, Base64 canonico, `size`/`chunks`/`crc32` do `start`); `fs.write` com CLI seguro (build_request, input/stdin, 2048, strict canonical, recusa eager de path invalido); pyserial opcional com mensagem clara. |
| `main/app_main.cpp` | `cyberdeck_serial::bridge_start()` | Inicia a ponte ao fim do boot, apos `wifi_mgr_start()`; falha loga aviso sem abortar o boot (ordem do `test_boot_sequence.py` preservada). |
| `components/cyberdeck/CMakeLists.txt` | SRCS + REQUIRES `esp_driver_usb_serial_jtag`, `esp_app_format`, `esp_timer`, `esp_system` | Composicao do componente. |

Os quatro alvos `test_serial_*` + `test_fs_write_*` do `tests/host/keymap/Makefile` cobrem a
camada host-testavel: `screen_bmp_size` tem paridade com
`screenshot_bmp_calc_size`; `crc32` IEEE `0xEDB88320`; chunks sem
gaps/sobreposicao com `is_last` no fim e reconstituicao byte-identical ao
BMP da `screenshot_bmp` pura; `LineAssembler` entrega 5 linhas do stream
intercalado e descarta linha >4096 sem ficar presa entre feeds; `fs.write` cobre protocolo rid/type/path/data_b64/size, limite 2048, path safety, strict canonical base64, atomic unique-temp/O_EXCL+rename e CRC response. A parte
device exclusiva de `#ifdef ESP_PLATFORM` (task/driver/LVGL/Wi-Fi real) nao
e coberta pelos testes host: validar com `idf.py build` e o roteiro
`tests/manual/serial-bridge-validation.pt-BR.md`.

### Input, sensores e logging

| Arquivo | Simbolos/contrato | Papel |
| --- | --- | --- |
| `components/cyberdeck/src/platform/input/tab5_keyboard.cpp` | `tab5_keyboard_init`, callback e task | Driver I2C Character do Tab5; entrega eventos fora da stack da task. |
| `components/cyberdeck/src/platform/input/tab5_keyboard_event.cpp` | `tab5_char_event_parse` | Parser puro; `REG_CHAR_EVENT_LEN` inclui modificador. |
| `components/cyberdeck/src/platform/input/tab5_keyboard_keys.cpp` | mapeamento de teclas | Conversao dos codigos LVGL/Tab5 para eventos da UI. |
| `components/cyberdeck/src/platform/logging/event_log.cpp` | `event_log_init`, `event_log_write`, `event_log_latest` | Log circular e task de persistencia/consulta. |
| `components/cyberdeck/src/platform/logging/event_log_recent.cpp` | `event_log_recent_indices` | Selecao pura dos indices recentes sem copiar todos os registros na stack. |
| `components/cyberdeck/src/platform/sensors/imu_reader.cpp` | `imu_reader_start`, `sensor_handle_t`, `bsp_sensor_init` | Inicializacao do BMI270 via Sensor Hub; `sensor_handle_t` e o handle obrigatorio usado por `bsp_sensor_init`; amostra inicial limitada por timeout, aplicacao da orientacao antes da UI e leitura posterior para rotacao. O contrato proibe somente a leitura direta `imu_acquire_acce(`, preservando o fluxo do Sensor Hub. |
| `components/cyberdeck/src/platform/sensors/orientation.cpp` | `orientation_from_accel`, `orientation_update` | Conversao da aceleracao em rotacao e debounce da orientacao posterior. |

## Dependencias e composicao

### ESP-IDF e componentes

`components/cyberdeck/CMakeLists.txt` registra todos os fontes de producao e
declara dependencias de LVGL, BSP, Wi-Fi, rede, FreeRTOS, SD/FATFS, libssh e
HTTP server. `main/idf_component.yml` declara ESP-IDF, `esp_lvgl_port`,
`esp_hosted`, `esp_wifi_remote`, libssh e o override local de `sock_utils`. `sdkconfig.defaults` habilita `CONFIG_FATFS_FS_LOCK=5` (protege os cinco VFS FAT slots contra rename/unlink de <PII type="CASE_ID" id="198"/> abertos) e `CONFIG_FATFS_TIMEOUT_MS=1000`; a task `wifi_audit_io` ainda impõe deadline próprio de 2 s para chamadas SD/VFS.

### Overlay local

`components/sock_utils/` substitui a dependencia gerenciada de mesmo nome:

- `src/getnameinfo.c`: AF_INET, AF_INET6 e IPv4-mapped.
- `include/netdb_macros.h`: flags `NI_NUMERICHOST`, `NI_NUMERICSERV` e `NI_DGRAM`.
- `test/host/main/test_getnameinfo.cpp`: validacao host dual-stack.

### Concorrencia

- UI/LVGL: atualizacoes protegidas por `bsp_display_lock`.
- Teclado fisico: fila FIFO bounded de 8 e despacho por `lv_async_call`.
- SSH: task dedicada e callbacks coordenados com a UI.
- Wi-Fi: callbacks de eventos publicam snapshots; workers/coordinators executam I/O. A auditoria usa `wifi_audit` para snapshot/hand-off e `wifi_audit_io` para a transação FatFs com timeout bounded e ownership do adapter/sink.
- Screenshot: mutex de requisicao e lock de display apenas durante captura.
- Ponte serial: task `serial_brg` fora da stack do LVGL; frames e logs
  compartilham mutex de escrita no driver USB Serial-JTAG; `ui.*` usa
  `bsp_display_lock` + `lv_async_call`, e a injecao de teclado respeita a fila
  bounded de 8 com delay entre eventos.
- Log: ring buffer e processamento incremental para evitar overflow de stack.

## Matriz producao -> testes host

| Producao | Testes principais |
| --- | --- |
| `tab5_keyboard_keys.cpp` | `test_keymap.cpp` |
| `tab5_keyboard_event.cpp` | `test_keyboard_event.cpp` |
| `cyberdeck_shell_utils.cpp` | `test_shell_utils.cpp` (inclui o parser de `wifi audit` e `wifi audit save`) |
| `cyberdeck_history.cpp` | `test_history.cpp` |
| `cyberdeck_edit_line.cpp` | `test_edit_line.cpp`, `test_prompt_behavior.py` |
| `cyberdeck_local_shell.cpp`, `cyberdeck_cat_worker.cpp` | `test_local_shell.cpp`, `test_cat_multiline.cpp` (API dedicada: LF/CRLF/tabs/UTF-8, marcador final, bytes inválidos, limite exato de 12288, NUL embutido e sufixo após newline preservados para sanitização), `cat_contract.py`, `test_cat_multiline_contract.py` (contrato estrutural de ponteiro+tamanho explícito, textarea multiline/max-length, limite UTF-8 e payload completo até append), `test_cat_lifecycle_sanitization_contract.py` (ramos sem task vs. com task, drain/join/ack, reset sincronizado, callback/generation e sanitizacao), `test_cat_start_teardown_start_contract.py` (restart e invalidacao stale), `test_cat_stack_footprint_contract.py` (regressao do stack minimo do worker), `test_cat_worker_path_safety_contract.py` (proibe o caminho worker->shell/resolve/path/vector e exige API cat-specific heap/bounded), `local_shell_security_contract.py`, `local_shell_tokenizer_contract.py`, `test_prompt_behavior.py`, `test_local_prompt_contract.py` |
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
| `cyberdeck_wifi_audit.cpp` (worker passivo/local, snapshot versionado, SSID do driver limitado ao array, scratch/resultado grandes fora da stack e persistência save confinada; separação de operações, gate lifecycle/token antes de hardware, guard single-shot até drain e descarte de stale/cancelado/teardown; adapter ESP/FatFs delega a transação a `cyberdeck_wifi_audit_persistence.cpp` com O_EXCL, fsync, sequência `.tmp`/`.bak` e recuperação/rollback fail-closed, sem atomicidade POSIX prometida; backend `wifi_audit_io` com deadline de 2 s e sem I/O na UI) | `test_wifi_audit.cpp`, `test_wifi_audit_contract.py`, `test_wifi_audit_save.cpp` (host exige conteúdo SSID/BSSID/IP, token/version/status, campos ausentes com `<missing>`, formato determinístico de seis chaves em ordem e LF final, escaping bounded, destino timestampado GMT-3, diretório/colisão, payload/bytes e guard; contrato estrutural valida a implementação real do seam `cyberdeck_wifi_audit_persistence.cpp`/header, exigindo write/fsync/close/rename antes do ACK, recuperação stale fail-closed, ordenações backup/rollback, sidecars preservados em falha, guard armado após queue success e liberado em drain/falha/stale/teardown, cópia do payload após a publicação e I/O fora da UI; o mesmo contrato exige task `wifi_audit_io`, deadline 2000 ms, ownership por semáforos até o release, quarentena `export_backend_busy`, retorno false/cleanup seguro sem ACK de sucesso no timeout, e defaults FATFS `FS_LOCK=5`/`TIMEOUT_MS=1000`) |
| `cyberdeck_wifi_audit_persistence.cpp` (seam de filesystem injetável em produção; adapter nativo e coordinator de completion) | `test_wifi_audit_persistence.cpp` (sucesso com payload real e ACK após publicação; write/fsync/close/rename; destino timestampado em `/sdcard/wifi-audit/`; sidecars `.tmp`/`.bak` derivados de `make_transaction_paths`; destino existente; recovery; rollback e preservação; single-shot/fila cheia; ACK retido; concorrência enqueue/publish e teardown concorrente com pump_one/enqueue/drain), `test_wifi_audit_persistence_contract.py` + `contracts/cyberdeck_wifi_audit_persistence.h` (Makefile/CMake/code-map; ambos os fontes de teste têm exceção explícita no `.gitignore`) |
| `cyberdeck_ui.cpp` + `cyberdeck_shell_utils.cpp` + `cyberdeck_wifi_audit.cpp` + `cyberdeck_wifi_audit_persistence.cpp` (contrato TDD do fluxo `wifi audit`/`wifi audit save`) | `test_wifi_audit_save.cpp` (comportamental host: renderização silenciosa em `collecting`, linha terminal exata em `ready`/`error`, `<missing>` somente no snapshot final, diretório, timestamp, colisão, sidecars, falhas, rollback e ACK; exercita a implementação de produção) e `test_wifi_audit_save_contract.py` (contrato estrutural de parser/UI, gate de `process_wifi_audit`, renderização once-only, GMT-3 e compatibilidade serial transitiva `exec_ui_type` -> `inject_text_segmented`/`inject_enter` -> `cyberdeck_keyboard_input`, sem hardware) |
| `screenshot_bmp.cpp` | `test_screenshot_bmp.cpp` |
| `cyberdeck_ui.cpp` | `test_boot_sequence.py`, `test_keyboard_input_contract.py`, `test_ui_resource_contract.py`, `test_local_prompt_contract.py`, `test_wifi_enter_routing_contract.py` |
| `main/app_main.cpp`, `cyberdeck_ui.cpp` | `test_boot_sequence.py` (ordem SD/UI e shell local `/sdcard`) |
| `imu_reader.cpp` | `imu_sensor_contract.py` (callback Sensor Hub, `sensor_handle_t` obrigatorio para `bsp_sensor_init`, proibicao especifica da leitura direta `imu_acquire_acce(`, timeout/fallback seguro e continuidade da rotacao) |
| `cyberdeck_serial_bridge.cpp` (REQ-002/003/005/006/007/008/009) — ponte NDJSON bounded, rid/envelopes, UI/sys/wifi, screen.dump chunks/CRC/end, feeder tolerante a logs/fragmentacao; `cyberdeck_cli.py` | `test_serial_ndjson_dispatch.cpp` (bounded/erros/rid/envelopes/UI), `test_serial_screen_dump.cpp` (byte-identical chunks/CRC/end), `test_serial_cli_tolerance.cpp` (logs/leitura fragmentada), `test_serial_sysinfo_wifi.cpp` (sys.info/wifi contratos) — todos host-only, sem pyserial/hardware; GREEN com a producao criada |
| `cyberdeck_serial_bridge.cpp` `fs.write` (REQ-001..REQ-011) — protocolo rid/type/path/data_b64/size, limite 2048, path safety, strict canonical base64, commit por temp unico/O_EXCL+rename (substituicao atomica no host/no-clobber no ESP/FATFS; nunca remove candidato preexistente), CRC response, NDJSON errors; `cyberdeck_cli.py` `fs.write` (`build_request`, encoding, input/stdin, 2048, strict canonical) | `test_fs_write_dispatch.cpp` (dispatch/validacao/path/size/base64/CRC/preservacao; colisao de temp preexistente/no-clobber), `test_fs_write_cli.py` (parser/build_request/encoding/stdin/limite), `test_fs_write_contract.py` (estrutural: disco/path/atomic/CRC/CLI/Makefile/code-map) — todos host-only, RED antes da producao |

Os testes host nao substituem a validacao do hardware para LVGL, touch, I2C,
Wi-Fi real, libssh real ou endpoint HTTP. Os contratos Python inspecionam a
fonte real quando a UI nao e linkavel no host.

## Comandos de validacao

### Testes host

```bash
make -C tests/host/keymap clean test
make -C tests/host/keymap verify
make -C tests/host/keymap test_wifi_audit test_wifi_audit_contract imu_sensor_contract
make -C tests/host/keymap test_wifi_audit_save test_wifi_audit_save_contract test_shell_utils
make -C tests/host/keymap test_wifi_audit_persistence test_wifi_audit_persistence_contract
make -C tests/host/keymap test_wifi_enter_routing_contract
make -C tests/host/keymap test_cat_contract
make -C tests/host/keymap test_cat_multiline test_cat_multiline_contract
make -C tests/host/keymap test_cat_lifecycle_sanitization_contract
make -C tests/host/keymap test_cat_start_teardown_start_contract
make -C tests/host/keymap test_cat_stack_footprint_contract
make -C tests/host/keymap test_cat_worker_path_safety_contract
make -C tests/host/keymap local_shell_tokenizer_contract
make -C tests/host/keymap test_serial_ndjson_dispatch test_serial_screen_dump test_serial_cli_tolerance test_serial_sysinfo_wifi
make -C tests/host/keymap test_fs_write_dispatch test_fs_write_cli test_fs_write_contract
```

`verify` compara os valores `LV_KEY_*` do shim com o LVGL gerenciado. Os targets
`test_wifi_audit_contract`, `test_wifi_audit_persistence` e
`test_wifi_audit_persistence_contract` inspecionam/exercitam a implementação real
da seam injetável e permanecem registrados no Makefile; `test` executa o binário
comportamental (não apenas a compilação). O contrato
IMU continua como alvo separado e, quando falha, é identificado no final como
falha preexistente isolada, sem misturá-la com os testes desta transação.

`test_wifi_audit_save` é um contrato comportamental do fluxo novo: ele
exercita os seams reais de auditoria/persistência e o parser/renderizador
host-testáveis, sem ESP-IDF ou hardware. A regressão de estado exige que
`collecting` não produza snapshot/campos, que `ready` seja renderizado uma
única vez e que `error` produza somente a linha de status; o contrato
estrutural confirma que o gate está em `process_wifi_audit` e que o comando
somente inicia a auditoria. `test_wifi_audit_save_contract` é o contrato
estrutural complementar para a parte não-linkável (comando da UI, gate de
estado/renderização, criação do diretório, GMT-3, ACK e compatibilidade do
bridge serial). `test_shell_utils` agora chama explicitamente a asserção do
parser `wifi audit save`; sua execução é parte dos testes diretamente
relacionados.

### Build ESP-IDF

```bash
source ~/esp/esp-idf/export.sh
idf.py set-target esp32p4
idf.py build
```

A validação focada do backend real também pode usar `ninja -C build cyberdeck5.elf` após `source`; o binário final é regenerado por `idf.py build`/`flash`.

### Flash e monitor

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

### Validacao manual

O roteiro TUI esta em `tests/manual/tui-shell-validation.pt-BR.md`.
O roteiro da ponte serial esta em
`tests/manual/serial-bridge-validation.pt-BR.md`.

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
- `README.md`: recursos, uso do shell, Wi-Fi, SSH, screenshot e ponte serial.
- `docs/ARCHITECTURE.md`: principios, boot, concorrencia Wi-Fi, screenshot,
  ponte serial e UI.
- `tests/manual/tui-shell-validation.pt-BR.md`: validacao no dispositivo.
- `docs/WIFI.md`: fluxos Wi-Fi, incluindo auditoria local e salvamento explícito
  da rede conectada.
- `tests/manual/serial-bridge-validation.pt-BR.md`: validacao da ponte
  USB Serial-JTAG no dispositivo.
- `tools/cyberdeck_cli.py`: cliente NDJSON host da ponte serial.
- `components/cyberdeck/CMakeLists.txt`: lista definitiva dos fontes compilados.
- `tests/host/keymap/Makefile`: lista definitiva dos testes e fontes puros.
