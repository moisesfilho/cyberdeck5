# cyberdeck5 Code Map

Mapa de navegacao do firmware monolitico ESP-IDF para o M5Stack Tab5
(ESP32-P4). Os caminhos abaixo sao relativos a raiz do repositorio.

## Visao geral

- Plataforma: ESP-IDF 5.5.5, LVGL 9.x, BSP `m5stack_tab5`.
- Aplicacao: uma unica tela TUI LVGL, com header, relogio, indicador Wi-Fi, bateria e terminal.
- Organizacao: `components/cyberdeck/src/features/` contem fluxos de produto;
  `components/cyberdeck/src/platform/` contem integracoes de hardware e runtime.
- A logica pura e extraida para testes host; `main/app_main.cpp` faz a composicao
  das implementacoes dependentes do ESP-IDF.
- A inicializacao da bateria e um fluxo nao fatal: o reader INA226 publica
  snapshots a partir de task dedicada e a UI apenas os consome.

## Pontos de entrada e fluxo de boot

| Ponto | Arquivo | Responsabilidade |
| --- | --- | --- |
| `app_main()` | `main/app_main.cpp` | Monta SD, valida o handle, inicia log/NVS, display/LVGL, IMU, UI, protecao de tela, adaptador de protecao de bateria (que inicializa o reader INA226 sensor-only), teclado, brilho, servidor de screenshot e Wi-Fi. |
| `cyberdeck_ui_init()` | `components/cyberdeck/src/platform/display/cyberdeck_ui.cpp` | Cria a tela TUI, header, terminal, callbacks e estado de entrada. |
| `wifi_mgr_start()` | `components/cyberdeck/src/features/wifi/wifi_mgr.cpp` | Inicia o gerenciamento de Wi-Fi e reconexao. |
| `screenshot_server_init()` | `components/cyberdeck/src/features/screenshot/screenshot_server.cpp` | Prepara o servidor HTTP; a disponibilidade depende do estado Wi-Fi. |
| `tab5_keyboard_init()` | `components/cyberdeck/src/platform/input/tab5_keyboard.cpp` | Inicia a task do teclado fisico e entrega eventos a `cyberdeck_keyboard_input`. |

Ordem relevante de inicializacao:

1. `bsp_sdcard_mount()` e validacao de `bsp_sdcard_get_handle()`.
2. `event_log_init()` e `nvs_flash_init()`.
3. `bsp_display_start()`.
4. Sob lock do display: `imu_reader_start()`, `cyberdeck_ui_init()` e `screen_off_init()`.
5. Apos liberar o display: `battery_protection_start()`; o adaptador inicializa o reader INA226 sensor-only e falha apenas gera log.
6. Callback do teclado, `tab5_keyboard_init()` e brilho.
7. `screenshot_server_init()`, callback de estado do screenshot e `wifi_mgr_start()`.

## Funcionalidades

### UI e terminal

| Arquivo | Simbolos/funcao | Papel |
| --- | --- | --- |
| `components/cyberdeck/src/platform/display/cyberdeck_ui.cpp` | `cyberdeck_ui_init`, `cyberdeck_ui_deinit`, `cyberdeck_keyboard_input`, callbacks de SSH/Wi-Fi/cat, `battery_indicator_symbol`, `refresh_battery_status` | Compoe a TUI multilinear, roteia Enter por estado, sanitiza dados de `cat` antes do LVGL, aplica limite explicito ao textarea, atualiza sob lock e integra shell, SSH, Wi-Fi, auditoria local com gate de estado no timer (sem publicar `collecting`), o hand-off não bloqueante de `wifi audit save` com ACK/path pós-publicação e o snapshot de bateria no terceiro filho do header. O <PII type="CASE_ID" id="29"> só consome `battery_protection_get_policy_snapshot`, delega o mapeamento a `cyberdeck_battery_view::resolve` e fixa o glyph semântico em `battery_indicator_symbol` (`LV_SYMBOL_CHARGE`, `LV_SYMBOL_BATTERY_FULL` constante e `LV_SYMBOL_MINUS`); não há regra de negócio, escolha de glyph por estado nem acesso a I2C/NVS/reader na camada LVGL. |
| `components/cyberdeck/include/platform/display/cyberdeck_ui.h` | API publica da UI | Contrato usado por `app_main` e pelo driver de teclado. |
| `components/cyberdeck/src/platform/display/cyberdeck_font.c` | Fonte monoespaciada | Recurso visual do terminal/header, incluindo os simbolos LVGL de Wi-Fi, menos e carga; o include LVGL permanece condicionado por `LV_LVGL_H_INCLUDE_SIMPLE` e usa `"lvgl.h"` em ambos os ramos. Os unicos codepoints FontAwesome disponiveis sao `0xF067` (mais), `0xF068` (menos), `0xF0E7` (carga), `0xF1EB` (Wi-Fi) e `0xF240..0xF244` (bateria), portanto nao ha glyph de tomada/USB/energia para o caso de alimentacao externa sem bateria. |
| `components/cyberdeck/src/platform/display/cyberdeck_clock.cpp` | `cyberdeck_clock_from_utc`, `cyberdeck_format_clock` | Conversao/formato do relogio GMT-3. |
| `components/cyberdeck/src/platform/display/cyberdeck_wifi_indicator.cpp` | `cyberdeck_wifi_indicator_is_lit` | Regra pura: claro somente com `enabled && connected && has_ip`. |
| `components/cyberdeck/src/platform/display/cyberdeck_wifi_icon.cpp` | layout, criacao, resize e cor do icone | Desenha tres arcos e ponto; recalcula posicao em resize. |
| `components/cyberdeck/src/platform/display/cyberdeck_screen_protection.cpp`, `components/cyberdeck/include/platform/display/cyberdeck_screen_protection.h` | `parse_timeout_minutes`, `state`, `persisted_timeout` | Politica pura host-testavel: timeout padrao de 2 min, faixa inclusiva 0..1440, zero desabilitando sem perder o ultimo valor positivo, restauracao dos dois campos e transicao on/off no limite exato de inatividade. |
| `components/cyberdeck/src/platform/display/cyberdeck_battery_view.cpp`, `components/cyberdeck/include/platform/display/cyberdeck_battery_view.h` | `power_glyph`, `input`, `presentation`, `clamp_percentage`, `from_snapshot`, `resolve` | View pura do indicador de energia do header, sem ESP-IDF, FreeRTOS, LVGL, I2C, NVS ou BSP. Mapeamento total de `battery_state` + `charge_signal` + disponibilidade + percentual para visivel/percentual/glyph: indisponivel ou `unknown` fica oculto, `absent` fica visivel com glyph externo e sem percentual, `charging` (por estado ou por sinal) usa glyph de carga com percentual, e `battery`/`external` usam glyph de bateria com percentual. Ausencia e resolvida antes do sinal do carregador, portanto um `CHG_STAT` preso em low nunca fabrica uma bateria. O nivel nunca vem do glyph. |
| `components/cyberdeck/src/platform/display/screen_off.cpp`, `components/cyberdeck/include/platform/display/screen_off.h` | `screen_off_init`, `screen_off_turn_on`, `screen_off_turn_off`, `screen_off_set_timeout_minutes` | Adaptador LVGL/BSP da protecao de tela: timer de 1 s, duplo toque para religar, comandos `screen on|off|timeout`, restauracao NVS antes do timer e persistencia enfileirada para uma task dedicada (fora da task LVGL) do timeout efetivo e do ultimo valor positivo, com zero pausando/desabilitando o timer. |

O header usa grade direta 30/40/30 para titulo, relogio e celula direita. A
celula direita renderiza Wi-Fi antes da bateria; a bateria mantem os dois labels
originais (um glyph semantico e o percentual numerico) e apenas aplica o que a
view pura `cyberdeck_battery_view` decidiu: glyph de carga em `charging`, glyph
constante de bateria mais percentual em `battery`/`external`, glyph externo sem
percentual em `absent` e grupo oculto quando a leitura e invalida ou o estado e
`unknown`. O nivel nunca e escolhido pelo percentual. O filho Wi-Fi ocupa a
largura compacta derivada de
`CYBERDECK_WIFI_ICON_RADIUS_2` e
`CYBERDECK_WIFI_ICON_DEFAULT_THICKNESS` (`2 * (raio + espessura)`, ~34 px), a
bateria usa `LV_SIZE_CONTENT`, ambos definem `flex_grow=0` e a celula usa
`LV_FLEX_ALIGN_END` com gaps pequenos (0–4 px). Nao exibe SSID nem estado
SSH. Estados SSH vao para o terminal e event log; diagnostico Wi-Fi e obtido
pelo comando `wifi` e pela auditoria local.

### Shell local

| Arquivo | Simbolos/contrato | Papel |
| --- | --- | --- |
| `components/cyberdeck/src/features/shell/cyberdeck_local_shell.cpp` | classe `cyberdeck_local_shell`; `cyberdeck_local_shell_cat` | Shell confinado ao root virtual `/`; `host_root` continua sendo o ponto fisico do SD (montado em `/sdcard`), e `/sdcard` e descendentes sao rejeitados no namespace virtual. Tokenizer manual byte-a-byte bounded para espacos/tabs; implementa `pwd`, `cd`, `ls`, `cat`, `touch`, `mkdir`, `rm`, `rmdir` e ajuda. A ajuda e as opcoes `-h`/`--help` consomem o catalogo compartilhado sem listas literais locais. A API cat-specific usa as mesmas regras de cwd/caminho, apenas strings bounded e descritores confinados, retorna output heap-backed, limita arquivos a 12288 bytes e chunks de 1024, e e usada pelo worker sem construir o shell geral. |
| `components/cyberdeck/src/features/shell/cyberdeck_cat_worker.cpp`, `components/cyberdeck/include/features/shell/cyberdeck_cat_worker.h` | `cyberdeck_cat_worker_start`, `cyberdeck_cat_worker_enqueue`, `cyberdeck_cat_worker_teardown` | Worker FreeRTOS com fila bounded para I/O de `cat`, stack explícita de 6144 bytes; o worker deve chamar uma API cat-specific heap/bounded, sem construir/usar o shell genérico, `fs::path` ou `vector` no caminho específico. O contrato estrutural permite os identificadores `cyberdeck_local_shell_*` da API dedicada e rejeita apenas a construção/uso genérico. Cada start drena a sinalização de parada e cria uma geração nova, e teardown sinaliza/aguarda o retorno do worker antes de liberar fila, root e callback, invalidando callbacks LVGL tardios. |
| `components/cyberdeck/include/features/shell/cyberdeck_local_shell.h` | API do shell local | Contrato usado pela UI e testes. |
| `components/cyberdeck/include/features/shell/cyberdeck_shell_help.h` | `cyberdeck_shell_help::kCatalog`, `cyberdeck_shell_help::text`, `cyberdeck_shell_help::command_text` | Modulo header-only puro STL com a unica tabela ordenada de 16 entradas e formatadores deterministicos; nao depende de LVGL, UI ou ESP-IDF. |
| `components/cyberdeck/src/features/shell/cyberdeck_shell_utils.cpp`, `components/cyberdeck/include/features/shell/cyberdeck_shell_utils.h` | `cyberdeck_help_text`, `cyberdeck_command_help_text`, `parse_ssh_target`, `cyberdeck_parse_command`, `CYBERDECK_CMD_WIFI_AUDIT_SAVE`, `CYBERDECK_CMD_SCREEN_ON/OFF/TIMEOUT` | Adapters publicos para o catalogo compartilhado, parser de `ssh [user@]host[:port]`, comandos `wifi` (incluindo auditoria local e `wifi audit save` explicito; a grafia de exportacao legada e rejeitada) e roteamento de `screen on`, `screen off` e `screen timeout <0-1440>` para a politica pura. |
| `tests/host/keymap/contracts/cyberdeck_help.h` | `kUnifiedHelpText` | Fixture de teste com o catalogo unificado esperado; nao e uma implementacao de producao. |
| `tests/host/keymap/test_help_unification.cpp` | paridade de `help`, `help -h` e `help --help`; catalogo unico de 16 linhas | Teste comportamental host que liga `cyberdeck_shell_utils.cpp` e `cyberdeck_local_shell.cpp`, sem hardware. |
| `tests/host/keymap/help_unification_contract.py` | contrato estrutural do catalogo | Impede listas literais independentes em `cyberdeck_shell_utils.cpp`, `cyberdeck_local_shell.cpp` e `cyberdeck_ui.cpp`. |
| `components/cyberdeck/src/features/shell/cyberdeck_history.cpp` | classe `cyberdeck_history` | Historico limitado a 64 linhas, com navegacao e duplicatas preservadas. |
| `components/cyberdeck/src/features/shell/cyberdeck_edit_line.cpp` | classe `cyberdeck_edit_line` | Linha UTF-8, cursor, backspace, Enter e comportamento por sessao. |

O shell local aceita `..` somente em `cd`, faz clamp na raiz virtual `/`, rejeita
traversal nos demais comandos e protege contra symlinks. A montagem física
continua em `/sdcard`, mas esse nome não é alias válido no namespace virtual. `cat` abre por
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

### Bluetooth LE no ESP32-C6 hospedado (REQ-BLE-001..011)

O radio BLE nao e do ESP32-P4: e do coprocessor ESP32-C6 alcancado por
`esp_hosted` (VHCI/HCI), e a stack host roda no P4. Nenhum modulo BLE toca
`lvgl.h`, FreeRTOS, NVS ou BSP, exceto o adaptador. Bluetooth Classic (BR/EDR)
esta fora do escopo e e rejeitado por contrato.

| Arquivo | Simbolos/contrato | Papel |
| --- | --- | --- |
| `components/cyberdeck/include/features/bluetooth/cyberdeck_ble_types.h`, `components/cyberdeck/src/features/bluetooth/cyberdeck_ble_types.cpp` | `k_max_name_bytes`, `k_max_address_bytes`, `k_max_devices`, `k_min_rssi`/`k_max_rssi`, `k_passkey_digits`/`k_passkey_modulus`, `k_unnamed_placeholder`, constantes de appearance do Bluetooth SIG, `device_kind`, `device`, `kind_from_appearance`, `kind_label`, `normalize_address`, `sanitize_name`, `display_name`, `clamp_rssi`, `parse_passkey`, `format_passkey`, `mask_passkey`, `device_list` | Tipos puros e modelo de listabounded. Classificacao por `appearance` apenas quando provavel (teclado `0x03C1`, mouse `0x03C2`, fone de ouvido `0x0401`/`0x0408`/`0x0418`/`0x0419`/`0x041A`/`0x041B`); `0x03C0` (HID generico), joystick, gamepad, `0x0400` e qualquer outro valor viram `unknown` em vez de adivinharem. `sanitize_name` converte C0/C1, `U+007F`, `U+2028`/`U+2029` e byte UTF-8 invalido em um unico espaco, limita a 32 bytes e nunca corta uma sequencia UTF-8. `device` nao possui campo de chave, ligacao ou IRK, portanto listagem, snapshot, log e store nao conseguem vazar segredo por construcao. `device_list` deduplica por endereco (RSSI mais forte, primeiro nome nao vazio, `paired`/`connectable` monotonicos), limita a 32 entradas e renderiza `Nome (Tipo, -55 dBm)` de forma deterministica. |
| `components/cyberdeck/include/features/bluetooth/cyberdeck_ble_state_machine.h`, `components/cyberdeck/src/features/bluetooth/cyberdeck_ble_state_machine.cpp` | `k_scan_timeout_ms` (10000), `k_pair_timeout_ms`/`k_auth_timeout_ms` (30000), `k_connect_timeout_ms` (20000), `k_max_reconnect_attempts` (3), `screen`, `key`, `notice`, `auth_request_kind`, `pair_outcome`, `action_kind`, `action`, mensagens `k_msg_*`/`k_status_*`, `state_machine` | Modelo de tela voltado ao usuario, sem I/O e sem chamada de stack, portanto nao pode bloquear a task LVGL. Conclusoes de conexao usam token + marcador in-flight mesmo quando uma busca simultanea muda a tela; callbacks terminais nao geram cancelamentos duplicados, enquanto deadlines geram exatamente a acao de cancelamento correspondente; cancelamento e disconnect preservam endereco/token e notices distinguem origem de pareamento, conexao e reconexao. `status_line()` usa `display_name()` bounded/sanitizado e o teto de reconexao nao emite nova action. |
| `components/cyberdeck/include/features/bluetooth/cyberdeck_ble_event_dispatch.h`, `components/cyberdeck/src/features/bluetooth/cyberdeck_ble_event_dispatch.cpp` | `k_max_pending_events` (8), `ble_event_kind`, `ble_event`, `event_dispatch`, `begin_scan`/`begin_pairing`/`begin_connection` (incluindo ativacao com token externo), `publish_*`, `drain`, `drop_stale`, `reset`, `event_summary` | Seam entre o callback da stack e o modelo de tela. Publica somente snapshots bounded, com fila limitada e overflow fail-closed; o adaptador pode ativar cada geracao com o token emitido pelo modelo, eliminando dominios shadow. Tokens monotônicos compartilhados impedem que callback obsoleto ou de outra geração seja aplicado. `drop_stale` conserva a geracao ativa e a imediatamente anterior por tipo, sem que pairing invalide scan; conexoes copiam endereco e marcador automatico para o snapshot. `event_summary` inclui nome sanitizado/bounded e nunca contem passkey, PIN, link key ou IRK. |
| `components/cyberdeck/include/features/bluetooth/cyberdeck_ble_store.h`, `components/cyberdeck/src/features/bluetooth/cyberdeck_ble_store.cpp` | `k_max_bonds` (16), `k_max_record_bytes` (96), `k_max_store_bytes` (2048), `k_bond_magic` (`CDB1`), `bond_record`, `store_result`, `encode_bond`, `decode_bond`, `bond_store` (`add`/`update`/`remove`/`find`/`snapshot`/`serialize`/`deserialize`/`clear`) | Persistencia logica de bonds, sem ESP-IDF, NVS ou FATFS; o mapeamento bytes-armazenamento e do adaptador. O registro e `addr` + `name` + `kind` + `last`, nessa ordem fixa; nao existe campo para chave, IRK, LTK ou passkey, e um payload com campo desconhecido e rejeitado fail-closed em vez de ser aceito e repersistido. `deserialize` e atômico: payload malformado deixa o conteudo anterior intacto. |
| `components/cyberdeck/include/features/bluetooth/ble_mgr.h`, `components/cyberdeck/src/features/bluetooth/ble_mgr.cpp` | `ble_mgr_start`, `ble_mgr_stop` | Adaptador ESP-IDF, unico modulo autorizado a falar com a stack BLE do C6 via `esp_hosted`. Possui task FreeRTOS dedicada e fila bounded; a UI apenas enfileira. Pair/connect usam o endereco e token da acao, a seguranca inicia por `ble_gap_security_initiate` e so callbacks reais publicam conclusoes; cancelamento/desconexao usam terminacao bounded, OOB e passkeys invalidos falham fechados. O ciclo de vida separa o cancelamento de pairing da conexao autenticada promovida apos o bond: disconnect fisico publica DISCONNECTED real e permite nova conexao. Callbacks GAP e comandos de cancelamento/autenticacao validam a geracao ativa; callbacks stale nao alteram estado nem publicam. Bonds existentes sao atualizados, nao duplicados. O adaptador drena o `event_dispatch` para os observers, preservando um unico dominio de tokens. Falha de inicializacao e nao fatal no boot. |
| `components/cyberdeck/include/features/shell/cyberdeck_shell_utils.h`, `components/cyberdeck/src/features/shell/cyberdeck_shell_utils.cpp` | `CYBERDECK_CMD_BLUETOOTH_SEARCH`, `CYBERDECK_CMD_BLUETOOTH_PAIRED`, `"bluetooth search"`, `"bluetooth paired"` | Parser roteia exatamente os dois subcomandos aprovados; o verbo nu e qualquer operando extra continuam `CYBERDECK_CMD_UNKNOWN`. Nao ha subcomando para Bluetooth Classic. |
| `components/cyberdeck/include/features/shell/cyberdeck_shell_help.h` | linha `bluetooth [search|paired]` | Catalogo unico de ajuda (16 entradas), com a linha bluetooth entre `battery` e `ssh`; o `static_assert` de descricoes em `cyberdeck_shell_utils.cpp` acompanha a ordem. |
| `tests/host/keymap/contracts/cyberdeck_ble_types.h` | ABI de `cyberdeck_ble_types.h` | Fixture de teste, nao e implementacao de producao. |
| `tests/host/keymap/contracts/cyberdeck_ble_state_machine.h` | ABI de `cyberdeck_ble_state_machine.h` + mensagens `k_msg_*`/`k_status_*` | Fixture de teste; fonte unica das mensagens de terminal e dos deadlines. |
| `tests/host/keymap/contracts/cyberdeck_ble_event_dispatch.h` | ABI de `cyberdeck_ble_event_dispatch.h` | Fixture de teste. |
| `tests/host/keymap/contracts/cyberdeck_ble_store.h` | ABI de `cyberdeck_ble_store.h` | Fixture de teste. |
| `tests/host/keymap/contracts/cyberdeck_help.h` | `kUnifiedHelpText` | Fixture compartilhada; agora inclui a linha `bluetooth` aprovada. E a oracle byte a byte de `test_shell_utils.cpp` e `test_help_unification.cpp`, incluindo a linha `screen [on|off|timeout <0-1440>]`. |

A listagem mostra nome e tipo (`Keyboard`, `Headset`, `Mouse`, `Unknown`), em
ingles como todas as demais strings de terminal do firmware; nome ausente vira
`(unnamed)` e dois perifericos com o mesmo nome so sao distinguidos pelo
endereco. A navegacao usa Cima, Baixo, Enter e Escape. Mensagens de terminal
sao exatamente uma por classe: lista vazia, falha, timeout e cancelamento, para
scan, pareamento e conexao. O scan e assincrono e toda espera e bounded por
`advance_time`; nenhum modulo BLE puro importa ESP-IDF, FreeRTOS, LVGL, NVS ou
FATFS, e o log nunca recebe passkey ou material de chave.

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
| `components/cyberdeck/src/platform/sensors/battery_status.cpp`, `components/cyberdeck/include/platform/sensors/battery_status.h` | `percentage_from_bus_voltage_mv`, `classify_current_ma`, `classify_sample` | Logica pura recebe `bus_voltage_mv`, satura a janela validada `6000..8230` mV em `0..100` e classifica corrente positiva como descarregando, negativa como carregando e zero/indeterminada como neutral; ausencia so ocorre com presenca explicita e leitura invalida fica indisponivel sem fabricar percentual. |
| `components/cyberdeck/src/platform/sensors/ina226_reader.cpp`, `components/cyberdeck/include/platform/sensors/ina226_reader.h` | `ina226_reader_start`, `ina226_reader_get_snapshot` | Reader INA226 no barramento BSP e endereco I2C `0x41`: registra de tensao `0x02` convertido em mV, configuracao `0x4527`, calibracao `0x0D55`, identificacao, tarefa dedicada a cada 1 s e snapshot protegido por mutex; falhas de leitura apos startup sao publicadas como indisponiveis. Nao usa `CHG_EN`, `CHG_STAT`, NVS ou politica de protecao; o startup e nao fatal. |
| `components/cyberdeck/src/platform/sensors/cyberdeck_battery_protection.cpp`, `components/cyberdeck/include/platform/sensors/cyberdeck_battery_protection.h` | `decode_chg_stat`, `state::observe`, `state::snapshot`, `state::last_safe_snapshot`, `state::set_protection_enabled`, `charger_enabled`, `current_uncertainty_ma`, `external_voltage_mv`, `absent_voltage_mv`, `state_vote_count`, `protection_enter_percentage`, `protection_enter_voltage_mv`, `protection_exit_percentage`, `default_protection_enabled` | Politica pura host-testavel: estados battery/external/charging/absent/unknown, threshold corrente ±15 mA, external >=7900 mV, absent >=8330 mV com 5 votos, protecao so em charging + percentual >=90 + tensao >=8200, histerese retoma <=85, opcao enabled persistida no NVS (default true), falhas nao desligam CHG_EN nem perdem ultimo estado seguro. O snapshot tambem publica o sinal `charge` decodificado. Leitura INA valida atualiza tensao/corrente/percentual/disponibilidade mesmo com `CHG_STAT` invalido (sinal `unknown`, nunca `not_charging`), e somente leitura INA invalida preserva o ultimo estado seguro sem fabricar percentual. A precedencia de ausencia/presenca (>=8330 mV voteado) e resolvida antes do sinal do carregador e da corrente, de modo que um `CHG_STAT` preso em low nao fabrica uma bateria carregando. Nao usa ESP-IDF, FreeRTOS, I2C, NVS, LVGL ou BSP. |
| `components/cyberdeck/src/platform/sensors/battery_protection.cpp`, `components/cyberdeck/include/platform/sensors/battery_protection.h` | `battery_protection_init`, `battery_protection_start`, `battery_protection_get_snapshot`, `battery_protection_get_policy_snapshot`, `battery_protection_set_enabled`, `battery_protection_is_enabled`, `battery_protection_is_active`, `battery_protection_charger_enabled`, `sample_and_publish` | Adaptador exclusivo para hardware: Expander B via `bsp_io_expander1_init()` (0x44), CHG_STAT pin 6 active-low input/pull-up, CHG_EN pin 7 output push-pull, CHG_EN=1 por default. Integra reader INA226 (sensor-only), politica pura, NVS para opcao enabled, timer UI 1 s. Um unico `sample_and_publish` alimenta a politica e publica as duas projecoes: `cyberdeck_battery::snapshot` (charge_class, usada pelo shell/status) e o snapshot puro da politica (`battery_protection_get_policy_snapshot`), que e a unica entrada de bateria consumida pela view do header. Falhas de I2C/NVS/CHG logadas sem desligar CHG_EN nem perder estado seguro. |
| `components/cyberdeck/src/platform/display/cyberdeck_ui.cpp` | `battery_indicator_symbol`, `refresh_battery_status`, `process_battery_protection`, `execute_line` (battery protection on/off/status), timer LVGL 1 s | Mantem a grade direta 30/40/30, Wi-Fi antes da bateria dentro da celula direita e exatamente dois labels no grupo da bateria (um glyph semantico e o percentual numerico). O <PII type="CASE_ID" id="30"> nao classifica estado: ele le `battery_protection_get_policy_snapshot`, aplica `cyberdeck_battery_view::resolve` e fixa o glyph por `battery_indicator_symbol` (`LV_SYMBOL_CHARGE` em carga, `LV_SYMBOL_BATTERY_FULL` constante em bateria presente, `LV_SYMBOL_MINUS` em alimentacao externa sem bateria, vazio quando invisivel); nao ha glyph de nivel nem seletor por porcentagem, e o grupo e ocultado quando a view decide `visible == false` (leitura indisponivel ou estado `unknown`). O percentual e mostrado quando a view expoe `show_percentage` (carga, bateria e external) e omitido em `absent`. Timer LVGL 1 s consome snapshot do adaptador; comandos shell `battery protection on/off/status` roteados para adaptador; `ui.type` transita via bridge serial. |
| `components/cyberdeck/src/platform/display/cyberdeck_ui.cpp` | `execute_line`, `local_key`, `process_ble_events`, `on_ble_event` | Integra a UI BLE sem chamadas diretas à stack: comandos `bluetooth search/paired`, teclas Up/Down/Enter/Escape, fila bounded de eventos e observer `ble_mgr`; `cyberdeck_ble::device_list` agrega snapshots de scan e `cyberdeck_ble::state_machine` permanece a fonte da seleção/estado. Todas as ações atravessam `ble_mgr_enqueue_cmd`, e deadlines são avançados no timer LVGL sem chamadas de stack. |

## Dependencias e composicao

### ESP-IDF e componentes

`components/cyberdeck/CMakeLists.txt` registra todos os fontes de producao e
declara dependencias de LVGL, BSP, I2C master, Wi-Fi, rede, FreeRTOS, SD/FATFS,
libssh e HTTP server. `main/idf_component.yml` declara ESP-IDF, `esp_lvgl_port`,
`esp_hosted`, `esp_wifi_remote`, libssh e o override local de `sock_utils`. O
componente tambem expoe o include de configuracao do port NimBLE usado pelo
adaptador. `sdkconfig.defaults` habilita `CONFIG_FATFS_FS_LOCK=5` (protege os cinco VFS FAT slots contra rename/unlink de <PII type="CASE_ID" id="198"/> abertos) e `CONFIG_FATFS_TIMEOUT_MS=1000`; a task `wifi_audit_io` ainda impõe deadline próprio de 2 s para chamadas SD/VFS.

### Overlay local

`components/sock_utils/` substitui a dependencia gerenciada de mesmo nome:

- `src/getnameinfo.c`: AF_INET, AF_INET6 e IPv4-mapped.
- `include/netdb_macros.h`: flags `NI_NUMERICHOST`, `NI_NUMERICSERV` e `NI_DGRAM`.
- `test/host/main/test_getnameinfo.cpp`: validacao host dual-stack.

### Concorrencia

- UI/LVGL: atualizacoes protegidas por `bsp_display_lock`.
- Teclado fisico: fila FIFO bounded de 8 e despacho por `lv_async_call`.
- SSH: task dedicada e callbacks coordenados com a UI.
- Bateria: task `ina226` faz I2C a cada 1 s e publica sob mutex; a UI apenas copia o snapshot e altera LVGL no contexto do display.
- Protecao de tela: a restauracao ocorre no boot antes do timer; a UI apenas enfileira snapshots e a task `screen_nvs` executa a escrita NVS fora da task LVGL.
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
| `components/cyberdeck/include/features/shell/cyberdeck_shell_help.h` + `cyberdeck_shell_utils.cpp` | `test_shell_utils.cpp` (inclui os parsers de `wifi audit`/`wifi audit save` e `screen on|off|timeout <0-1440>`; estrutura de 16 linhas e 16 linhas nao vazias, presenca das 16 linhas do catalogo com guarda de fronteira de linha e contagem de completude, igualdade exata com a fixture e determinismo) |
| `components/cyberdeck/include/features/shell/cyberdeck_shell_help.h` + `cyberdeck_shell_utils.cpp` + `cyberdeck_local_shell.cpp` + `cyberdeck_ui.cpp` | `test_help_unification.cpp` (igualdade exata de `cyberdeck_help_text()` e `execute("help")`/`help -h`/`help --help`; 16 linhas com comandos comuns e locais uma vez cada) e `help_unification_contract.py` (fonte unica e ausencia de listas literais independentes) |
| `cyberdeck_screen_protection.cpp` + contrato puro `contracts/cyberdeck_screen_protection.h` | `test_screen_protection.cpp` (default 2 min, limites 0/1/2/120/1440, parsing decimal estrito, zero desabilitando com preservacao do ultimo positivo, reject 1441 sem mutacao, restore/snapshot NVS, transicoes exatas do timer e independencia de power on/off) |
| `screen_off.cpp` + `cyberdeck_ui.cpp` + `cyberdeck_shell_utils.cpp` + `cyberdeck_serial_bridge.cpp` + `app_main.cpp` + `cyberdeck_shell_help.h` | `test_screen_protection_contract.py` (rotas de comando, literal `screen [on|off|timeout <0-1440>]` na linha unica do catalogo compartilhado com delegacao `cyberdeck_help_text()` -> `cyberdeck_shell_help::text()`, fixture `contracts/cyberdeck_help.h` e os dois testes comportamentais que a comparam, timer LVGL, persistencia/restauracao NVS, default seguro, init NVS antes do adapter, duplo toque e caminho transitivo `ui.type` -> `inject_text_segmented`/`inject_enter` -> `cyberdeck_keyboard_input`; o alvo do Makefile declara todos esses arquivos como pre-requisitos) |
| `cyberdeck_history.cpp` | `test_history.cpp` |
| `cyberdeck_edit_line.cpp` | `test_edit_line.cpp`, `test_prompt_behavior.py` |
| `cyberdeck_local_shell.cpp`, `cyberdeck_cat_worker.cpp` | `test_local_shell.cpp` (raiz virtual `/`, caminhos relativos/absolutos, rejeição do alias `/sdcard`, confinamento, operações e regressões de segurança), `test_cat_multiline.cpp` (API dedicada com cwd `/` e alias físico rejeitado, LF/CRLF/tabs/UTF-8, marcador final, bytes inválidos, limite exato de 12288, NUL embutido e sufixo após newline preservados para sanitização), `cat_contract.py`, `test_cat_multiline_contract.py` (contrato estrutural de ponteiro+tamanho explícito, textarea multiline/max-length, limite UTF-8 e payload completo até append), `test_cat_lifecycle_sanitization_contract.py` (ramos sem task vs. com task, drain/join/ack, reset sincronizado, callback/generation e sanitizacao), `test_cat_start_teardown_start_contract.py` (restart e invalidacao stale), `test_cat_stack_footprint_contract.py` (regressao do stack minimo do worker), `test_cat_worker_path_safety_contract.py` (proibe o caminho worker->shell/resolve/path/vector e exige API cat-specific heap/bounded), `local_shell_security_contract.py`, `local_shell_tokenizer_contract.py` (tokenização e contrato estrutural do root `/`), `test_prompt_behavior.py`, `test_local_prompt_contract.py` |
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
| `main/app_main.cpp`, `cyberdeck_ui.cpp` | `test_boot_sequence.py` (ordem SD/UI, montagem física `/sdcard` e root virtual `/`) |
| `imu_reader.cpp` | `imu_sensor_contract.py` (callback Sensor Hub, `sensor_handle_t` obrigatorio para `bsp_sensor_init`, proibicao especifica da leitura direta `imu_acquire_acce(`, timeout/fallback seguro e continuidade da rotacao) |
| `battery_status.cpp` + `ina226_reader.cpp` + `battery_protection.cpp` + contrato puro `contracts/cyberdeck_battery.h` | `test_battery_contract.cpp` (REQ-BAT-001/002: tensao `bus_voltage_mv`, janela `6000..8230`, saturacao, estados charging/discharging/neutral, zero/indeterminado e ausencia explicita), `test_battery_reader_contract.py` (REQ-BAT-001/002/004/005/006: `0x41`, `0x4527`/`0x0D55`, conversao do registro de tensao, task 1 s, mutex/snapshot, exclusoes, sensor-only, composicao pelo adaptador e nao-fatal; a UI consome a entrada de bateria apenas pelo seam de snapshot do adaptador, `battery_protection_get_policy_snapshot` ou `battery_protection_get_snapshot`, sempre declarado no header do adaptador, e nao pode conter nenhuma referencia direta ao reader INA226, seja chamada ou include), `test_battery_protection.cpp` + `test_battery_protection_contract.py` (politica e adaptador reais, sem hardware) |
| `cyberdeck_battery_view.cpp` + contrato puro `contracts/cyberdeck_battery_view.h` | `test_battery_view.cpp` (REQ-BAT-UI-001..005 / AC-BAT-UI-001..006: `resolve` sobre a matriz total `battery_state` x `charge_signal` x disponibilidade x percentual, `absent` somente com glyph externo e sem percentual em qualquer sinal, bateria presente sem carga com percentual e glyph de bateria, `charging` por estado ou por sinal com glyph de carga, `external` com bateria presente sem glyph de carga, `invalid`/`unknown` totalmente oculto, `clamp_percentage` 0..100, `from_snapshot` sem inventar entrada, glyph nunca escolhido pelo nivel em varredura 0..100, `decode` do sinal `unknown` e regressao de `CHG_STAT` preso em low apos os 5 votos) e `test_battery_view_contract.py` (view pura sem ESP-IDF/FreeRTOS/LVGL/I2C/NVS/BSP e registrada no CMake, LVGL aplicando `from_snapshot`/`resolve` sem `charge_class::`/`battery_state::`/`charge_signal::`, tabela de glyph fixo, percentual vazio quando `show_percentage` e falso, grupo oculto quando `visible` e falso, sem I2C/NVS/reader diretos). O alvo liga a view com a politica pura de `cyberdeck_battery_protection.cpp`, sem ESP-IDF, LVGL, I2C, NVS, simulador, Serial Automation Bridge ou hardware. |
| `cyberdeck_ui.cpp` + `cyberdeck_wifi_icon.cpp` + `cyberdeck_battery_view.cpp` + `battery_protection.cpp` + `app_main.cpp` | `test_battery_ui_contract.py` (REQ-BAT-002/003/004/006/010 + REQ-BAT-UI-004/005: grade direta 30/40/30, ordem Wi-Fi/bateria, layout compacto, percentual numerico, exatamente dois labels, ausencia de selecao de icone por nivel e de ramo `charge_class::` no refresh, ocultacao em falha, timer/snapshot do adaptador sem dependencia direta do reader, continuacao do boot e rastreabilidade). A selecao de icone foi transferida para a view pura de REQ-BAT-UI: o `refresh_battery_status` consome `battery_protection_get_policy_snapshot` e delega a `cyberdeck_battery_view::resolve`, enquanto `battery_indicator_symbol` fixa o glyph semantico. |
| `cyberdeck_serial_bridge.cpp` (REQ-002/003/005/006/007/008/009) — ponte NDJSON bounded, rid/envelopes, UI/sys/wifi, screen.dump chunks/CRC/end, feeder tolerante a logs/fragmentacao; `cyberdeck_cli.py` | `test_serial_ndjson_dispatch.cpp` (bounded/erros/rid/envelopes/UI, inclusive `ui.type` para `screen on|off|timeout`), `test_serial_screen_dump.cpp` (byte-identical chunks/CRC/end), `test_serial_cli_tolerance.cpp` (logs/leitura fragmentada), `test_serial_sysinfo_wifi.cpp` (sys.info/wifi contratos) — todos host-only, sem pyserial/hardware; GREEN com a producao criada |
| `cyberdeck_serial_bridge.cpp` `fs.write` (REQ-001..REQ-011) — protocolo rid/type/path/data_b64/size, limite 2048, path safety, strict canonical base64, commit por temp unico/O_EXCL+rename (substituicao atomica no host/no-clobber no ESP/FATFS; nunca remove candidato preexistente), CRC response, NDJSON errors; `cyberdeck_cli.py` `fs.write` (`build_request`, encoding, input/stdin, 2048, strict canonical) | `test_fs_write_dispatch.cpp` (dispatch/validacao/path/size/base64/CRC/preservacao; colisao de temp preexistente/no-clobber), `test_fs_write_cli.py` (parser/build_request/encoding/stdin/limite), `test_fs_write_contract.py` (estrutural: disco/path/atomic/CRC/CLI/Makefile/code-map) — todos host-only, RED antes da producao |
| `cyberdeck_ble_types.cpp` + contrato puro `contracts/cyberdeck_ble_types.h` | `test_ble_types.cpp` (classificacao por `appearance` provavel vs. `unknown`, normalizacao estrita de endereco, sanitizador bounded/UTF-8-safe sem C0/C1, clamp de RSSI, passkey estrito + mascara constante, dedup por endereco com RSSI mais forte e primeiro nome, capacidade 32, selecao com clamp, render deterministico, nomes duplicados, beacon nao conectavel) |
| `cyberdeck_ble_state_machine.cpp` + contrato puro `contracts/cyberdeck_ble_state_machine.h` | `test_ble_state_machine.cpp` (scan assincrono com um unico `start_scan`, deadlines exatos, quatro classes de mensagem distintas, rejeicao de token obsoleto/zero/duplicado, dispositivo desaparecido nao pareia, passkey exibido so em `status_line` e zerado apos submissao, passkey errado nao encaminha, desfechos bonded/rejected/cancelled/timed_out/failed, conectar/escape/desconectar, orcamento de reconexao com rearme manual, concorrencia scan x reconexao) |
| `cyberdeck_ble_event_dispatch.cpp` + contrato puro `contracts/cyberdeck_ble_event_dispatch.h` | `test_ble_event_dispatch.cpp` (tokens monotonicos compartilhados, publicacao obsoleta descartada sem enfileirar, fila bounded 8 com overflow fail-closed, ordem e propriedade no `drain`, `drop_stale`, geracao manual x automatica, `event_summary` sem digitos do passkey) |
| `cyberdeck_ble_store.cpp` + contrato puro `contracts/cyberdeck_ble_store.h` | `test_ble_store.cpp` (encode determinista com ordem de campos fixa, decode estrito rejeitando campo duplicado/reordenacao/lixo, campo desconhecido rejeitado impedindo contrabandear `ltk`/`irk`/`passkey`, capacidade 16, serialize bounded, deserialize atomico fail-closed, round trip de reboot) |
| `cyberdeck_shell_utils.h/.cpp` + `cyberdeck_shell_help.h` (roteamento de `bluetooth search`/`bluetooth paired` e a 16a linha do catalogo) | `test_ble_command_parse.cpp` (comportamental: os dois subcomandos, o verbo nu e 22 grafias proximas — inclusive `bluetooth classic`/`spp`/`a2dp` — como `CYBERDECK_CMD_UNKNOWN`, Help com 16 linhas) + `test_help_unification.cpp`/`help_unification_contract.py` (fonte unica do catalogo) |
| `ble_mgr.cpp` + `cyberdeck_ui.cpp` + `app_main.cpp` + `components/cyberdeck/CMakeLists.txt` + `sdkconfig.defaults` + `main/idf_component.yml` (REQ-BLE-001/003/008/010/011) | `test_ble_integration_contract.py` (modulos puros livres de stack/RTOS/UI, ABI de producao igual aos contratos, roteamento de shell e UI com as quatro teclas, task FreeRTOS dedicada e `ble_mgr_start` nao fatal, ausencia de log com passkey/chave e de campo secreto em `bond_record`, registro em CMake/sdconfig/idf_component.yml, exclusao de Bluetooth Classic, rastreabilidade REQ/AC em Makefile/.gitignore/code-map/docs/READMEs) |

### Bluetooth LE: rastreabilidade REQ/AC -> TEST

| Requisito | Criterio de aceite | Testes host/contratos |
| --- | --- | --- |
| `REQ-BLE-001` — BLE somente no ESP32-C6 hospedado pelo ESP32-P4 | `AC-BLE-001` | `test_ble_integration_contract.py` (adaptador como unico modulo da stack, host NimBLE com `CONFIG_BT_NIMBLE_ENABLED=y` + VHCI `CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI=y` e `CONFIG_BT_BLUEDROID_ENABLED=n`, `esp_hosted` em `main/idf_component.yml`, `CONFIG_BT_ENABLED=y` em `sdkconfig.defaults`, tokens Bluedroid `esp_ble_gap`/`esp_ble_gattc`/`esp_gap_ble` rejeitados, modulos puros sem `esp_bt`/`nimble`/`esp_hosted`) |
| `REQ-BLE-002` — shell `bluetooth search` e `bluetooth paired` | `AC-BLE-002` | `test_ble_command_parse.cpp`, `test_ble_integration_contract.py`, `test_help_unification.cpp`, `help_unification_contract.py` |
| `REQ-BLE-003` — scan assincrono e nao bloqueante | `AC-BLE-003` | `test_ble_state_machine.cpp`, `test_ble_event_dispatch.cpp`, `test_ble_integration_contract.py` (UI nao chama API de stack NimBLE; UI roteia so pela fila publica limitada `ble_mgr_enqueue_cmd` com `TickType_t` e pelo observador `ble_mgr_register_observer`; qualquer outro simbolo `ble_mgr_*` na UI e rejeitado por allow-list, sem depender de nomes privados) |
| `REQ-BLE-004` — listagem com nome e tipo (teclado, fone de ouvido, mouse, desconhecido) | `AC-BLE-004` | `test_ble_types.cpp` (`kind_from_appearance`, `kind_label`, `render`, `(unnamed)`, duplicatas por endereco) |
| `REQ-BLE-005` — navegacao Cima/Baixo, Enter e Escape | `AC-BLE-005` | `test_ble_state_machine.cpp`, `test_ble_integration_contract.py` (`cyberdeck_ble::key::up/down/enter/escape` na UI) |
| `REQ-BLE-006` — pareamento com autenticacao interativa quando necessaria | `AC-BLE-006` | `test_ble_state_machine.cpp` (passkey exibido, conferido, zerado apos submissao; confirm e numeric comparison sem segredo), `test_ble_types.cpp` (`parse_passkey`/`format_passkey`/`mask_passkey`) |
| `REQ-BLE-007` — persistencia de bonds | `AC-BLE-007` | `test_ble_store.cpp`, `test_ble_integration_contract.py` (adapter responsavel por bytes para NVS/FATFS) |
| `REQ-BLE-008` — reconexao automatica | `AC-BLE-008` | `test_ble_state_machine.cpp` (orcamento de 3 tentativas e rearme manual), `test_ble_event_dispatch.cpp` (geracao `automatic`) |
| `REQ-BLE-009` — mensagens de lista vazia, falha, timeout e cancelamento | `AC-BLE-009` | `test_ble_state_machine.cpp` (constantes `k_msg_*` como fonte unica, um texto exato por classe) |
| `REQ-BLE-010` — sem bloquear a UI e sem expor segredos | `AC-BLE-010` | `test_ble_types.cpp`, `test_ble_state_machine.cpp`, `test_ble_event_dispatch.cpp`, `test_ble_store.cpp`, `test_ble_integration_contract.py` (nenhum `ESP_LOG` com passkey/chave, `bond_record` sem campo secreto) |
| `REQ-BLE-011` — Bluetooth Classic fora do escopo | `AC-BLE-011` | `test_ble_integration_contract.py` (lista de tokens BR/EDR rejeitada em todo o diretorio da feature) |

Os seis alvos BLE exercitam agora a implementacao de producao: os modulos puros
(`cyberdeck_ble_types.cpp`, `cyberdeck_ble_state_machine.cpp`,
`cyberdeck_ble_event_dispatch.cpp`, `cyberdeck_ble_store.cpp`), o adaptador
`ble_mgr.cpp`, o roteamento de shell/UI e a linha do catalogo de ajuda foram
criados. Os contratos de `contracts/cyberdeck_ble_*.h` sao fixtures de teste:
definem a ABI e re-incluem o header de producao assim que ele existir, portanto
nenhum teste inventa API de hardware. `CONFIG_BT_ENABLED=y` em
`sdkconfig.defaults` e o registro de `ble_mgr.cpp` em
`components/cyberdeck/CMakeLists.txt` fazem parte da implementacao.

`test_ble_integration_contract.py` fixa a stack aprovada: o host BLE e NimBLE
(`nimble_port_*`, `ble_hs_cfg`, `ble_gap_disc`, `ble_gap_connect`) com HCI
transportado pelo ESP-Hosted VHCI ate o controlador do ESP32-C6, e
`CONFIG_BT_BLUEDROID_ENABLED=n` mantem o Bluedroid fora do escopo, entao as
APIs exclusivas de Bluedroid (`esp_ble_gap`, `esp_ble_gattc`, `esp_gap_ble`) sao
rejeitadas em todo o diretorio da feature. O adaptador e o unico caller da
stack: ele cria a tarefa FreeRTOS (com nome explicito em toda criacao) e uma
fila de comandos limitada por constante de compilacao. A composicao com a UI e
descrita pela superficie publica limitada — `ble_mgr_start`/`ble_mgr_stop`,
`ble_mgr_enqueue_cmd` (com `TickType_t`, para nunca bloquear o LVGL) e
`ble_mgr_register_observer`/`ble_mgr_unregister_observer` — e nao por nomes de
helpers privados, entao a regra nao fixa a implementacao interna do adaptador.

### Bateria: rastreabilidade REQ -> TEST

| Requisito | Testes host/contratos |
| --- | --- |
| `REQ-BAT-001` — percentual por tensao real `bus_voltage_mv`, janela `6000..8230` mV saturado | `test_battery_contract.cpp`, `test_battery_reader_contract.py` |
| `REQ-BAT-002` — estados charging/discharging/neutral; corrente zero/indeterminada e neutral, nunca absent; ausencia somente por presenca explicita | `test_battery_contract.cpp`, `test_battery_reader_contract.py`, `test_battery_ui_contract.py` |
| `REQ-BAT-003` — neutral mantem percentual sem glyph/texto de estado; estados ativos usam um icone semantico, sem icone de nivel | `test_battery_ui_contract.py` |
| `REQ-BAT-004` — boot nao fatal e nenhum controle de carregador inventado | `test_battery_reader_contract.py`, `test_battery_ui_contract.py` |
| `REQ-BAT-005` — INA226 `0x41` e integracao task dedicada, mutex e cadencia de 1 s preservados | `test_battery_reader_contract.py` |
| `REQ-BAT-006` — documentacao e `code-map.md` rastreiam os seis requisitos e os contratos host | `test_battery_reader_contract.py`, `test_battery_ui_contract.py`, `docs/ARCHITECTURE.md`, `docs/ARCHITECTURE.pt-BR.md` |
| `REQ-BAT-007` — expander-B pin6 CHG_STAT active-low e pin7 CHG_EN | `test_battery_protection.cpp`, `test_battery_protection_contract.py` |
| `REQ-BAT-008` — battery/external/charging/absent/unknown thresholds e votos | `test_battery_protection.cpp`, `test_battery_protection_contract.py` |
| `REQ-BAT-009` — 90/85 protection hysteresis e fail-safe state | `test_battery_protection.cpp`, `test_battery_protection_contract.py` |
| `REQ-BAT-010` — NVS default/option, UI timer/snapshot, shell e ui.type | `test_battery_protection_contract.py` |

Os cinco alvos de bateria agora exercitam a implementacao de producao: o reader
continua sendo o unico proprietario da aquisicao I2C, mas app_main inicia o
adaptador de protecao e a UI consome exclusivamente o snapshot copiado desse
adaptador. O contrato puro exige neutral para corrente zero/indeterminada e
preserva percentual sem estado visual; os contratos estruturais verificam a
integracao INA226, a composicao adapter->UI sem dependencia direta do reader,
o boot nao fatal, a ausencia de controle de carregador no reader e a
rastreabilidade em documentacao/mapa. A protecao de carregamento adiciona
politica pura host-testavel, adaptador expander-B/INA226/NVS, timer UI 1 s,
comandos shell `battery protection on/off/status` e compatibilidade serial
transitiva via `ui.type`. A validacao final dos alvos fica a cargo
do reviewer.

### Indicador de energia removivel: rastreabilidade REQ/AC -> implementacao

| Requisito | Criterio de aceite | Implementacao de producao |
| --- | --- | --- |
| `REQ-BAT-UI-001` — falha de leitura de `CHG_STAT` nao congela o snapshot | `AC-BAT-UI-001` — leitura INA valida atualiza tensao, corrente, percentual e disponibilidade | `cyberdeck_battery_protection.cpp`: `state::observe` so preserva o ultimo estado seguro quando `ina_valid == false`; `snapshot.charge` publica `unknown` para leitura CHG_STAT falha |
| `REQ-BAT-UI-001` — leitura INA invalida continua sem percentual | `AC-BAT-UI-002` — nenhuma percentual e fabricado a partir de leitura invalida | `cyberdeck_battery_protection.cpp` (`state::observe`, `state::last_safe_snapshot`), `battery_protection.cpp` (`sample_and_publish`) |
| `REQ-BAT-UI-002` — precedencia de ausencia/presenca | `AC-BAT-UI-003` — `CHG_STAT` preso em low nao fabrica bateria carregando; estados e thresholds aprovados preservados | `cyberdeck_battery_protection.cpp`: `classify_state` decide `absent` (>= 8330 mV, 5 votos) antes do sinal do carregador e da corrente; `cyberdeck_battery_view.cpp`: `resolve` decide ausencia antes do sinal |
| `REQ-BAT-UI-003` — camada pura de apresentacao sem ESP/LVGL/FreeRTOS | `AC-BAT-UI-004` — mapeamento total de estado/sinal/disponibilidade/percentual para visivel, percentual e glyph semantico | `cyberdeck_battery_view.h`/`.cpp`: `power_glyph`, `input`, `presentation`, `clamp_percentage`, `from_snapshot`, `resolve` |
| `REQ-BAT-UI-004` — integracao da UI sem regra de negocio no LVGL | `AC-BAT-UI-005` — dois labels, grade 30/40/30, sem acesso direto a I2C/NVS/reader | `cyberdeck_ui.cpp`: `battery_indicator_symbol` fixa o glyph, `refresh_battery_status` consome `battery_protection_get_policy_snapshot` e aplica `resolve`; `battery_protection.cpp`/`battery_protection.h` publicam o snapshot puro |
| `REQ-BAT-UI-005` — sem percentual/`CHG_STAT` e sem glyph de nivel | `AC-BAT-UI-006` — `absent` mostra apenas o glyph externo e o glyph nunca e escolhido pelo percentual | `cyberdeck_battery_view.cpp` (`show_percentage` falso em `absent`), `cyberdeck_ui.cpp` (percentual vazio quando `show_percentage == false`) |

Os alvos host desses requisitos sao criados pelo `tester` depois desta
implementacao; ate la, o contrato existente `test_battery_ui_contract.py` ainda
descreve o contrato anterior de selecao de icone dentro de
`refresh_battery_status`, e `test_battery_protection.cpp` ainda fixa o
comportamento antigo de congelamento no caminho `chg_valid == false`.
A execucao e a regressao da suite ficam a cargo do `reviewer`.

Limitacoes de hardware e de recurso que restringem o desenho acima:

- A ausencia e detectada por tensao fixa do barramento (>= 8330 mV, 5 votos),
  nao por presenca eletrica dedicada. Um `CHG_STAT` preso em low abaixo dessa
  janela ainda podeappear como carga ate a votacao de ausencia confirmar.
- `cyberdeck_font.c` embarca somente os codepoints FontAwesome `0xF067` (mais),
  `0xF068` (menos), `0xF0E7` (carga), `0xF1EB` (Wi-Fi) e `0xF240..0xF244`
  (bateria). Nao existe glyph de tomada, USB ou energia na fonte compilada, e
  regenerar a fonte esta fora do escopo: o caso de alimentacao externa sem
  bateria usa `LV_SYMBOL_MINUS` como marcador de "sem bateria".
- O pictograma de bateria e um marcador constante (`LV_SYMBOL_BATTERY_FULL`): o
  nivel pertence ao percentual numerico vizinho e nunca e derivado do glyph.

Os testes host nao substituem a validacao do hardware para LVGL, touch, I2C,
Wi-Fi real, libssh real ou endpoint HTTP. Os contratos Python inspecionam a
fonte real quando a UI nao e linkavel no host. O alvo puro de bateria linka
`battery_status.cpp` com o contrato de tensao real, enquanto os contratos
estruturais verificam o registro INA226, os estados, a integracao LVGL sem
glyph de nivel, a composicao do adaptador de protecao, o boot nao fatal e a
ausencia de controle de carregador no reader. O Makefile mantem os cinco
alvos registrados e separa o executavel `test_battery_protection_bin` do
alvo publico `test_battery_protection`; a execucao fica a cargo do reviewer.

## Comandos de validacao

### Testes host

```bash
make -C tests/host/keymap clean test
make -C tests/host/keymap verify
make -C tests/host/keymap test_wifi_audit test_wifi_audit_contract imu_sensor_contract
make -C tests/host/keymap test_wifi_audit_save test_wifi_audit_save_contract test_shell_utils
make -C tests/host/keymap test_help_unification help_unification_contract
make -C tests/host/keymap test_screen_protection test_screen_protection_contract test_shell_utils test_local_shell test_serial_ndjson_dispatch
make -C tests/host/keymap test_wifi_audit_persistence test_wifi_audit_persistence_contract
make -C tests/host/keymap test_wifi_enter_routing_contract
make -C tests/host/keymap test_cat_contract
make -C tests/host/keymap test_cat_multiline test_cat_multiline_contract
make -C tests/host/keymap test_cat_lifecycle_sanitization_contract
make -C tests/host/keymap test_cat_start_teardown_start_contract
make -C tests/host/keymap test_cat_stack_footprint_contract
make -C tests/host/keymap test_cat_worker_path_safety_contract
make -C tests/host/keymap local_shell_tokenizer_contract
make -C tests/host/keymap test_local_shell test_cat_multiline test_prompt_behavior test_boot_sequence
make -C tests/host/keymap test_serial_ndjson_dispatch test_serial_screen_dump test_serial_cli_tolerance test_serial_sysinfo_wifi
make -C tests/host/keymap test_fs_write_dispatch test_fs_write_cli test_fs_write_contract
make -C tests/host/keymap test_battery_contract test_battery_reader_contract test_battery_ui_contract test_battery_protection test_battery_protection_contract
make -C tests/host/keymap test_ble_types test_ble_state_machine test_ble_event_dispatch test_ble_store
make -C tests/host/keymap test_ble_command_parse test_help_unification help_unification_contract
make -C tests/host/keymap test_ble_integration_contract
```

`verify` compara os valores `LV_KEY_*` do shim com o LVGL gerenciado. Os
`test_battery_*` sao os contratos desta correcao: o alvo puro exercita a
percentual por tensao e os estados, enquanto os dois alvos Python inspecionam
a fonte real para leitura INA226, icone semantico unico, boot nao fatal e
ausencia de controle de carregador. `test_battery_protection` e
`test_battery_protection_contract` exercitam a politica pura de estados e
protecao de carregamento, o adaptador expander-B/INA226/NVS, o timer UI 1 s,
os comandos shell e a compatibilidade serial transitiva. Os targets
`test_wifi_audit_contract`, `test_wifi_audit_persistence` e
`test_wifi_audit_persistence_contract` inspecionam/exercitam a implementação real
da seam injetável e permanecem registrados no Makefile; `test` executa o binário
comportamental (não apenas a compilação). O contrato
IMU continua como alvo separado e, quando falha, é identificado no final como
falha preexistente isolada, sem misturá-la com os testes desta transação.

Os seis alvos BLE exercitam agora a implementacao de producao: os modulos puros
(`cyberdeck_ble_types.cpp`, `cyberdeck_ble_state_machine.cpp`,
`cyberdeck_ble_event_dispatch.cpp`, `cyberdeck_ble_store.cpp`), o adaptador
`ble_mgr.cpp`, o roteamento de shell/UI e a linha do catalogo de ajuda foram
criados. A validacao final dos alvos fica a cargo do reviewer.

`test_wifi_audit_save` e um contrato comportamental do fluxo novo: ele
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

`test_screen_protection` é o contrato comportamental do seam puro: ele fixa
default/range, zero desabilitando com retenção do último positivo, round-trip
dos dois campos NVS e o limite exato do timer. `test_screen_protection_contract`
inspeciona o adaptador LVGL/BSP/NVS, o roteamento da UI, a ordem de boot e a
compatibilidade transitiva via `ui.type`, sem abrir hardware. Como a ajuda deixou
de ser uma lista literal em `cyberdeck_shell_utils.cpp`, o contrato também fixa a
linha `screen [on|off|timeout <0-1440>]` na tabela compartilhada de 16 entradas,
exige a delegação `cyberdeck_help_text()` -> `cyberdeck_shell_help::text()`, a
presença exata da linha na fixture e o uso dessa fixture pelos dois testes
comportamentais, e exige que o alvo do Makefile declare catálogo, fixture e testes
como pré-requisitos para que nenhum deles possa ficar obsoleto em silêncio. A
implementação de produção fornece o seam puro, o adaptador LVGL/NVS e o
roteamento shell; esses contratos aguardam apenas a validação do reviewer.

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
- `README.md` e `README.pt-BR.md`: recursos, uso do shell, Wi-Fi, SSH, screenshot e ponte serial.
- `docs/ARCHITECTURE.md` e `docs/ARCHITECTURE.pt-BR.md`: principios, boot, concorrencia Wi-Fi, screenshot,
  ponte serial, bateria e UI.
- `tests/manual/tui-shell-validation.pt-BR.md`: validacao no dispositivo.
- `docs/WIFI.md`: fluxos Wi-Fi, incluindo auditoria local e salvamento explícito
  da rede conectada.
- Bluetooth LE: `bluetooth search` e `bluetooth paired` sao as duas entradas de
  terminal; o catalogo de ajuda e a linha 16 de
  `components/cyberdeck/include/features/shell/cyberdeck_shell_help.h`.
- `tests/manual/serial-bridge-validation.pt-BR.md`: validacao da ponte
  USB Serial-JTAG no dispositivo.
- `tools/cyberdeck_cli.py`: cliente NDJSON host da ponte serial.
- `components/cyberdeck/CMakeLists.txt`: lista definitiva dos fontes compilados.
- `tests/host/keymap/Makefile`: lista definitiva dos testes e fontes puros.
