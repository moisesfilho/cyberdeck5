# cyberdeck5 Code Map

Mapa de navegacao do firmware monolitico ESP-IDF para o M5Stack Tab5
(ESP32-P4). Os caminhos abaixo sao relativos a raiz do repositorio.

## Visao geral

- Plataforma: ESP-IDF 5.5.5, LVGL >=9.6,<10.0, BSP `m5stack_tab5`.
- Aplicacao: uma unica tela TUI LVGL, com header, relogio, indicador Wi-Fi, bateria e terminal.
- Organizacao: `components/cyberdeck/src/apps/` contem fluxos de produto;
  `components/cyberdeck/src/platform/` contem integracoes de hardware e runtime.
- A logica pura e extraida para testes host; `main/app_main.cpp` faz a composicao
  das implementacoes dependentes do ESP-IDF.
- A inicializacao da bateria e um fluxo nao fatal: o reader INA226 publica
  snapshots a partir de task dedicada e a UI apenas os consome.
- Artefatos de cobertura gcov comprimidos (`*.gcov.json.gz`) sao ignorados pelo
  Git para manter o repositorio livre de relatorios gerados.
- O plano de transformacao em sistema operacional embarcado fica em
  `docs/OS-TRANSFORMATION-PLAN.pt-BR.md`, com o status das fases concluidas e
  pendentes, a estrutura-alvo e os gates de validacao.

`AGENTS.md` documenta o procedimento operacional para agentes validarem o
firmware no dispositivo pela ponte USB Serial-JTAG, usando a CLI, comandos de
UI, `term.dump`, `ui.dump`, `screen.dump` e coleta de evidencias. `term.dump` é
a API preferencial para recuperar e validar texto/retorno após ações como
`ui.type`, por ser mais rápida e objetiva que screenshot/OCR; `ui.dump` fica
para estrutura/estado de widgets e `screen.dump`/screenshot para validação
visual. O roteiro executavel detalhado fica em
`tests/manual/serial-bridge-validation.pt-BR.md`.
Para tarefas que envolvam `idf.py` ou outros recursos do ESP-IDF, o arquivo
tambem orienta usar `IDF_PATH` para localizar o diretorio padrao do IDF na
maquina e carregar o ambiente quando necessario.

## Pontos de entrada e fluxo de boot

| Ponto | Arquivo | Responsabilidade |
| --- | --- | --- |
| `app_main()` | `main/app_main.cpp` | Monta SD, valida o handle, inicia log/NVS, registra system apps, display/LVGL, IMU, UI, proteção de tela, adaptador de proteção de bateria, teclado, brilho e inicia o supervisor de serviços. |
| `cyberdeck_ui_init()` | `components/cyberdeck/src/platform/display/cyberdeck_ui.cpp` | Compoe a tela TUI, registra callbacks/timers e inicializa os componentes visuais de header, terminal e teclado. |
| `wifi_mgr_start()` | `components/cyberdeck/src/apps/wifi/wifi_mgr.cpp` | Inicia o gerenciamento de Wi-Fi e reconexao. |
| `screenshot_server_init()` / `screenshot_server_start()` / `screenshot_server_stop()` | `components/cyberdeck/src/apps/screenshot/screenshot_server.cpp` | Lifecycle do servidor HTTP por task proprietária, com fila bounded de snapshots/comando, handshake de prontidão, join/quiescência antes de `httpd_stop()` e quarentena fail-safe em timeout. |
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

### Lifecycle Wi-Fi (Etapa 5)

`wifi_mgr_start()`/`wifi_mgr_stop(timeout_ms)` em
`components/cyberdeck/src/apps/wifi/wifi_mgr.cpp` usam estados explícitos e
join cooperativo/quarentena para `wifi_event_worker` e `net_worker`; o system
app usa `stop_wifi()` com orçamento de 8000 ms. O contrato estrutural fica em
`tests/host/keymap/test_wifi_mgr_lifecycle_contract.py` e é integrado ao
Makefile host.

Esta entrada supersede o recorte histórico da tabela de Wi-Fi: o stop público
e o teardown simétrico dos dois workers já estão implementados; timeout não
destrói recursos potencialmente usados.

### UI e terminal

| Arquivo | Simbolos/funcao | Papel |
| --- | --- | --- |
| `tests/host/keymap/test_shell_session.cpp` + `tests/host/keymap/shim/esp_err.h`, `shim/esp_wifi_types.h`, `shim/freertos/FreeRTOS.h` | harness comportamental da sessão | Testa `handle_key`/`execute_line`/insercao de texto com um `host` falso: edicao e historico, comandos locais, portas de tela/Wi-Fi/bateria/log/SSH, tokens de conexao, passe com varios newlines, buffer de passkey e o override temporario `log lines` (default, limites, entradas invalidas sem mutacao e reset ao recriar a sessao). Os shims sao minimos e nao espelham logica de producao. |
| `components/cyberdeck/src/platform/display/cyberdeck_ui.cpp` | `cyberdeck_ui_init`, `cyberdeck_ui_deinit`, `cyberdeck_keyboard_input`, `shell_session_host`, `compose_console_line`, `get_rendered_output`, `render_terminal`, `local_key`, callbacks de SSH/Wi-Fi/BLE/cat, `refresh_battery_status` | Fachada LVGL 9: constrói e atualiza widgets, usa as APIs dedicadas `lv_obj_set_hidden`/`lv_obj_set_scroll_chain` para visibilidade e rolagem, mantém o pump de eventos e renderiza, mas não decide o que cada tecla faz. O estado de sessão (linha/cursor, histórico, SSID pendente, chave SSH pendente, buffer de passkey) e o roteamento de teclas/comandos vivem em `cyberdeck_shell_session::session`; a UI implementa a interface `host` (output, BLE, Wi-Fi, SSH, cat, shell local) e traduz `lv_key_t` para `cyberdeck_shell_session::key`. A construção visual do header e dos widgets terminal/teclado fica nos componentes `cyberdeck_header_view` e `cyberdeck_terminal_view`; a fila de entrada física fica no dispatcher `cyberdeck_keyboard_dispatch`; o pump de eventos BLE (`on_ble_event`/`process_ble_events`/`ble_submit_actions`) permanece na UI e delega o agendamento background ao `cyberdeck_ble_background::scheduler`. Saída SSH marca repaint pendente e é coalescida pelo timer LVGL de 100 ms; caminhos interativos continuam com flush imediato. Os eventos SSH usam slots de armazenamento estático fora da stack, contadores de descarte saturantes separados para dados e estados e geração para descartar eventos stale; a geração esperada é invalidada no teardown. Callbacks SSH apenas publicam eventos e não adquirem lock direto do display. |
| `components/cyberdeck/include/apps/shell/cyberdeck_shell_session.h`, `components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp` | `cyberdeck_shell_session::session`, `cyberdeck_shell_session::host`, `session::handle_key`, `session::execute_line`, `session::insert_physical_text`, `session::insert_modified_key`, `session::insert_virtual_text`, `session::clear_ble_auth_input`, `session::invalidate_wifi_connection`, `wifi_ui_state_t` | Sessão de shell extraída, sem LVGL e sem chamadas diretas a API de plataforma: possui a linha corrente, o cursor, o histórico, o SSID Wi-Fi pendente, o `cyberdeck_edit_line` por contexto, o buffer de passkey BLE e os dois tokens de conexão Wi-Fi (o do gerenciador, que o pump casa com os callbacks, e o da tentativa, do modelo). Decide o roteamento de teclas e a execução de comandos. Todo efeito externo passa pela interface `host`, que declara portas para saída de terminal, BLE, Wi-Fi (serviço, armazenamento e auditoria), SSH, proteção de tela, bateria e log de eventos. Por isso ela linka no host contra um host falso (`test_shell_session.cpp`). `execute_line` aceita `line_already_sent`; `insert_modified_key` trata tecla modificadora como sequência de escape SSH, nunca como texto; `insert_virtual_text` interpreta byte de controle isolado como ação de edição e executa cada linha de um paste com vários newlines exatamente uma vez. O buffer de passkey é zerado em todo caminho que encerra a posse, inclusive no deinit da UI. |
| `components/cyberdeck/src/platform/display/cyberdeck_header_view.cpp`, `components/cyberdeck/include/platform/display/cyberdeck_header_view.h` | `cyberdeck_header_view::view` | Componente visual do header: cria a grade 30/40/30 e os widgets Bluetooth, Wi-Fi e bateria; usa `lv_obj_set_hidden` para ocultação e desativa rolagem; recebe somente estado/presentation já resolvidos e não conhece serviços, NVS, I2C ou modelos BLE/Wi-Fi. |
| `components/cyberdeck/src/platform/display/cyberdeck_terminal_view.cpp`, `components/cyberdeck/include/platform/display/cyberdeck_terminal_view.h` | `cyberdeck_terminal_view::view`, `cyberdeck_terminal_view::callbacks` | Componente visual do terminal e teclado virtual: cria textarea/teclado, aplica estilos, usa `lv_obj_set_hidden`/`lv_obj_set_scroll_chain`, limites e registra callbacks fornecidos pela composição da UI; não implementa shell, SSH ou roteamento de comandos. |
| `tests/host/keymap/test_display_views.cpp` + `tests/host/keymap/shim/full/lvgl.h` | `test_display_views` | Harness host compartilhado para `cyberdeck_header_view` e `cyberdeck_terminal_view`, com shim LVGL mínimo. |
| `components/cyberdeck/src/platform/input/cyberdeck_keyboard_dispatch.cpp`, `components/cyberdeck/include/platform/input/cyberdeck_keyboard_dispatch.h` | `cyberdeck_keyboard_dispatch::dispatcher` | Infraestrutura de entrada física: copia snapshots, mantém fila bounded de capacidade 8, serializa enqueue/rollback com mutex e agenda processamento no contexto LVGL; não interpreta comandos nem conhece sessões da UI. |
| `tests/host/keymap/test_keyboard_dispatch.cpp` + `tests/host/keymap/shim/full/` | `test_keyboard_dispatch` | Exercita o dispatcher host com shims completos de LVGL/FreeRTOS. |
| `components/cyberdeck/include/platform/display/cyberdeck_ui.h` | API publica da UI, `cyberdeck_ui_term_dump` | Contrato usado por `app_main`, pelo driver de teclado e pela ponte Serial-JTAG; o dump textual encapsula o lock de display bounded sem expor LVGL. |
| `components/cyberdeck/src/platform/display/cyberdeck_font.c` | Fonte monoespaciada | Recurso visual do terminal/header, incluindo os simbolos LVGL de Wi-Fi, menos, carga e Bluetooth; o include LVGL permanece condicionado por `LV_LVGL_H_INCLUDE_SIMPLE` e usa `"lvgl.h"` em ambos os ramos. Os unicos codepoints FontAwesome disponiveis sao `0xF067` (mais), `0xF068` (menos), `0xF0E7` (carga), `0xF1EB` (Wi-Fi), `0xF293` (Bluetooth) e `0xF240..0xF244` (bateria), portanto nao ha glyph de tomada/USB/energia para o caso de alimentacao externa sem bateria. |
| `components/cyberdeck/src/platform/display/cyberdeck_clock.cpp` | `cyberdeck_clock_from_utc`, `cyberdeck_format_clock` | Conversao/formato do relogio GMT-3. |
| `components/cyberdeck/src/platform/display/cyberdeck_wifi_indicator.cpp` | `cyberdeck_wifi_indicator_is_lit` | Regra pura: claro somente com `enabled && connected && has_ip`. |
| `components/cyberdeck/src/platform/display/cyberdeck_wifi_icon.cpp` | layout, criacao, resize e cor do icone | Desenha tres arcos e ponto; recalcula posicao em resize. |
| `components/cyberdeck/src/platform/display/cyberdeck_screen_protection.cpp`, `components/cyberdeck/include/platform/display/cyberdeck_screen_protection.h` | `parse_timeout_minutes`, `state`, `persisted_timeout` | Politica pura host-testavel: timeout padrao de 2 min, faixa inclusiva 0..1440, zero desabilitando sem perder o ultimo valor positivo, restauracao dos dois campos e transicao on/off no limite exato de inatividade. |
| `components/cyberdeck/src/platform/display/cyberdeck_battery_view.cpp`, `components/cyberdeck/include/platform/display/cyberdeck_battery_view.h` | `power_glyph`, `input`, `presentation`, `clamp_percentage`, `from_snapshot`, `resolve` | View pura do indicador de energia do header, sem ESP-IDF, FreeRTOS, LVGL, I2C, NVS ou BSP. Mapeamento total de `battery_state` + `charge_signal` + disponibilidade + percentual para visivel/percentual/glyph: indisponivel ou `unknown` fica oculto, `absent` fica visivel com glyph externo e sem percentual, `charging` (por estado ou por sinal) usa glyph de carga com percentual, e `battery`/`external` usam glyph de bateria com percentual. Ausencia e resolvida antes do sinal do carregador, portanto um `CHG_STAT` preso em low nunca fabrica uma bateria. O nivel nunca vem do glyph. |
| `components/cyberdeck/src/platform/display/screen_off.cpp`, `components/cyberdeck/include/platform/display/screen_off.h` | `screen_off_init`, `screen_off_turn_on`, `screen_off_turn_off`, `screen_off_set_timeout_minutes` | Adaptador LVGL/BSP da protecao de tela: timer de 1 s, duplo toque para religar, comandos `screen on|off|timeout`, restauracao NVS antes do timer e persistencia enfileirada para uma task dedicada (fora da task LVGL) do timeout efetivo e do ultimo valor positivo, com zero pausando/desabilitando o timer. |

A UI nao possui mais o console: `s_shell_app` referencia
`cyberdeck_shell_app::global_application()`, o `cyberdeck_ui_init` empresta o
host de sessao (`attach_console`) antes de qualquer entrada ou prompt, e o
teardown chama `detach_console` para que o supervisor libere o console e
apague o passkey. `compose_console_line()` e a unica
fonte de estado de superficie (SSH conectado, senha pendente, ownership de
BLE/Wi-Fi e cwd) e entrega tudo a `s_shell_app.compose_line(surface)`, que
 devolve a linha ajustada e o cursor ja clampado; `render_terminal` aplica
`view.text()` e `view.cursor_chars()` sobre o scrollback sem acrescentar LF
  artificial, e
`TERMINAL_LIMIT` deriva de `cyberdeck_shell_console::k_terminal_limit` com
`static_assert` contra `cyberdeck_edit_line::limit`, de modo que scrollback,
prompt e linha nao podem divergir do limite de 12288 bytes. As antigas
facades locais `sync_editor`/`sync_line`/`execute_line` foram removidas: entrada
e submissao passam apenas pela aplicacao.

O header usa grade direta 30/40/30 para titulo, relogio e celula direita. A
celula direita renderiza Bluetooth antes do Wi-Fi antes da bateria; o glyph
Bluetooth (`s_ble_status` dentro da celula wrapper `s_ble_cell`,
`LV_SYMBOL_BLUETOOTH`/`U+F293`) fica visivel somente enquanto
`cyberdeck_ble::state_machine::is_connected()` e verdadeiro (estado inicial
oculto, atualizado em `process_ble_events` a cada 100 ms apos `advance_time`,
sem sobreposicao pois a celula usa `LV_SIZE_CONTENT` x 42, layout none,
`flex_grow=0`, padding 0 e pad direito de 4 px com fundo transparente); o
glyph interno usa tamanho content e `lv_obj_set_y` com a constante nomeada
`CYBERDECK_BLE_HEADER_Y_OFFSET` para centralizar visualmente no eixo do
Wi-Fi (correcao do desalinhamento ~10 px do label direto de 42 px); a bateria mantem os dois labels
originais (um glyph semantico e o percentual numerico) e apenas aplica o que a
view pura `cyberdeck_battery_view` decidiu: glyph de carga em `charging`, glyph
constante de bateria mais percentual em `battery`/`external`, glyph externo sem
percentual em `absent` e grupo oculto quando a leitura e invalida ou o estado e
`unknown`. O nivel nunca e escolhido pelo percentual. O filho Wi-Fi ocupa a
largura compacta derivada de
`CYBERDECK_WIFI_ICON_RADIUS_2` e
`CYBERDECK_WIFI_ICON_DEFAULT_THICKNESS` (`2 * (raio + espessura)`, ~34 px), a
bateria usa `LV_SIZE_CONTENT` com `pad_left` de 11 px (gap visual Wi-Fi→bateria
de 15 px), ambos definem `flex_grow=0`, a celula BLE usa
`LV_SIZE_CONTENT` e a celula direita usa
`LV_FLEX_ALIGN_END` com gaps pequenos (0–4 px nos demais intervalos). Nao exibe SSID nem estado
SSH. Estados SSH vao para o terminal e event log; diagnostico Wi-Fi e obtido
pelo comando `wifi` e pela auditoria local.

### Shell local (Etapa 6 concluida)

| Arquivo | Simbolos/contrato | Papel |
| --- | --- | --- |
| `components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp` | classe `cyberdeck_local_shell`; `cyberdeck_local_shell_cat` | Shell confinado ao root virtual `/`; `host_root` continua sendo o ponto fisico do SD (montado em `/sdcard`), e `/sdcard` e descendentes sao rejeitados no namespace virtual. Tokenizer manual byte-a-byte bounded para espacos/tabs; implementa `pwd`, `cd`, `ls`, `cat`, `touch`, `mkdir`, `rm`, `rmdir` e ajuda. A ajuda e as opcoes `-h`/`--help` consomem o catalogo compartilhado sem listas literais locais. A API cat-specific usa as mesmas regras de cwd/caminho, apenas strings bounded e descritores confinados, retorna output heap-backed, limita arquivos a 12288 bytes e chunks de 1024, e e usada pelo worker sem construir o shell geral. |
| `components/cyberdeck/src/apps/shell/cyberdeck_cat_worker.cpp`, `components/cyberdeck/include/apps/shell/cyberdeck_cat_worker.h` + `tests/host/keymap/test_cat_worker.cpp` | `cyberdeck_cat_worker_process_request`, `cyberdeck_cat_worker_start`, `cyberdeck_cat_worker_enqueue`, `cyberdeck_cat_worker_teardown` | Worker FreeRTOS com fila bounded para I/O de `cat`, stack explícita de 6144 bytes; `cyberdeck_cat_worker_process_request` é o seam host que chama a implementação cat-specific real, coberto pelo binário host sem usar o stub inativo. O worker deve chamar uma API cat-specific heap/bounded, sem construir/usar o shell genérico, `fs::path` ou `vector` no caminho específico. O contrato estrutural permite os identificadores `cyberdeck_local_shell_*` da API dedicada e rejeita apenas a construção/uso genérico. Cada start drena a sinalização de parada e cria uma geração nova, e teardown sinaliza/aguarda o retorno do worker antes de liberar fila, root e callback, invalidando callbacks LVGL tardios. |
| `components/cyberdeck/include/apps/shell/cyberdeck_local_shell.h` | API do shell local | Contrato usado pela UI e testes. |
| `components/cyberdeck/include/apps/shell/cyberdeck_shell_help.h` | `cyberdeck_shell_help::kCatalog`, `cyberdeck_shell_help::text`, `cyberdeck_shell_help::command_text` | Modulo header-only puro STL com a unica tabela ordenada de 16 entradas e formatadores deterministicos; nao depende de LVGL, UI ou ESP-IDF. |
| `components/cyberdeck/include/apps/shell/cyberdeck_vfs_namespace.h`, `components/cyberdeck/src/apps/shell/cyberdeck_vfs_namespace.cpp` | `cyberdeck_vfs_namespace::at`, `resolve`, `backend_for`, `backend_kind`, `is_filesystem_backend`, `is_mutable_backend`, `is_null_device`, `path_kind` | Catalogo compilado fixo e resolver puro bounded dos cinco namespaces (`/apps`, `/data`, `/dev`, `/tmp`, `/system`). `backend_for` centraliza a classificação em `metadata`, `filesystem`, `null_device` ou `invalid`; o shell usa essa política única para separar metadados, backends físicos e a interface `/dev/null`. `/data` mapeia para `<host_root>/data` e permite apenas mutacoes confinadas do shell; `/system` permanece readonly em `<host_root>/system`; `/dev/null` e uma interface virtual vazia e bounded, sem abrir o `/dev` fisico; `/apps` e `/tmp` continuam metadata-only. A raiz `/data` nao pode ser removida. |
| `components/cyberdeck/src/apps/shell/cyberdeck_shell_utils.cpp`, `components/cyberdeck/include/apps/shell/cyberdeck_shell_utils.h` | `cyberdeck_help_text`, `cyberdeck_command_help_text`, `parse_ssh_target`, `cyberdeck_parse_command`, `CYBERDECK_CMD_WIFI_AUDIT_SAVE`, `CYBERDECK_CMD_SCREEN_ON/OFF/TIMEOUT` | Adapters publicos para o catalogo compartilhado, parser de `ssh [user@]host[:port]`, comandos `wifi` (incluindo auditoria local e `wifi audit save` explicito; a grafia de exportacao legada e rejeitada), roteamento de `screen on`, `screen off` e `screen timeout <0-1440>` para a politica pura e preservacao dos argumentos de `log` para o override volatil da sessao. |
| `tests/host/keymap/contracts/cyberdeck_help.h` | `kUnifiedHelpText` | Fixture de teste com o catalogo unificado esperado, incluindo `log [lines <1-64>]`; nao e uma implementacao de producao. |
| `tests/host/keymap/test_help_unification.cpp` | paridade de `help`, `help -h` e `help --help`; catalogo unico de 16 linhas | Teste comportamental host que liga `cyberdeck_shell_utils.cpp` e `cyberdeck_local_shell.cpp`, sem hardware. |
| `tests/host/keymap/help_unification_contract.py` | contrato estrutural do catalogo | Impede listas literais independentes em `cyberdeck_shell_utils.cpp`, `cyberdeck_local_shell.cpp` e `cyberdeck_ui.cpp`. |
| `components/cyberdeck/src/apps/shell/cyberdeck_history.cpp` | classe `cyberdeck_history` | Historico limitado a 64 linhas, com navegacao e duplicatas preservadas. |
| `components/cyberdeck/src/apps/shell/cyberdeck_edit_line.cpp` | classe `cyberdeck_edit_line` | Linha UTF-8, cursor, backspace, Enter e comportamento por sessao. |

### Runtime de aplicações compiladas

| Arquivo | Simbolos/contrato | Papel |
| --- | --- | --- |
| `components/cyberdeck/include/apps/runtime/cyberdeck_app_logger.h`, `components/cyberdeck/include/apps/runtime/cyberdeck_app_runtime.h`, `components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp` | `cyberdeck_apps::logger`, `manifest`, `application`, `runtime`, `result`, `app_state`, `app_type` | Supervisor bounded de até 16 aplicações compiladas. O manifesto declara `api_version`, tipo, dependências, recursos, capacidades, stack, fila e comandos. Mantém estados `registered`/`starting`/`running`/`stopping`/`failed`, resolve dependências declaradas em ordem determinística, mede o tempo dos hooks, registra diagnóstico da última falha, expõe metadados em `app info` e implementa restart/stop-all. `manifest::owns_console` marca a aplicacao que possui o unico console de usuario, e `runtime::stops_console_owner(id)` percorre a arvore de dependentes com marcadores visitados (bounded contra ciclos) para recusar qualquer `app stop` cuja cascata em `stop_index` alcance o dono do console: dependentes sao parados antes do alvo, entao `app stop <dependencia do shell>` destruiria o console sem nunca nomea-lo e bloquearia toda a entrada ate um reboot. A API de lifecycle do supervisor (`stop_application`, `stop_all`, `restart_application`) nao e afetada, porque o bloqueio se aplica aos comandos que chegam pelo proprio console. O estado segue o retorno real do hook: como os hooks são síncronos e não podem ser preemptados, o estouro pós-retorno de `lifecycle_timeout_ms` é apenas diagnóstico (`start hook timeout`/`stop hook timeout`) e não troca o estado para `failed`, o que impede bloquear dependentes de um serviço lento. Os limites efetivos ficam nos joins cooperativos bounded dos serviços. `application::init()` e `application::teardown()` formam hooks completos com defaults compatíveis com `start()`/`stop()`. `runtime::set_logger()` injeta a porta de logging em cada aplicação registrada e `runtime::app_logger()` permite que compositores consumam o serviço sem incluir o backend de plataforma. O parser continua whitespace-only e limitado a 256 bytes/8 tokens. |
| `components/cyberdeck/include/apps/runtime/cyberdeck_window_manager.h`, `components/cyberdeck/src/apps/runtime/cyberdeck_window_manager.cpp` | `cyberdeck_window_manager::manager`, `view_context`, `surface_state`, `view_status` | Politica pura e bounded da Fase 8: no maximo 8 superficies e 8 notificacoes FIFO, gera contextos opacos com geracao monotona, centraliza foco, invalida/quiesce superficies em teardown (inclusive foco e notificacoes), preserva ownership sem que uma nova superficie antecipe o fallback durante teardown e rejeita handles expirados, overflow e notificacoes oversized sem depender de LVGL, ESP-IDF ou FreeRTOS. |
| `components/cyberdeck/include/apps/runtime/cyberdeck_app_facades.h`, `components/cyberdeck/src/apps/runtime/cyberdeck_app_facades.cpp` | `display_facade`, `input_facade` e facades de storage/network/BLE/Serial/Screenshot/event log/clock/bateria | Fachadas tipadas da Fase 9. Cada operacao revalida o grant; display e input usam exclusivamente `view_context` e o window manager, enquanto os demais recursos expõem somente presença autorizada, sem singleton ou handle de plataforma. |
| `components/cyberdeck/include/apps/runtime/cyberdeck_app_runtime.h`, `components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp` | `resource`, `grant`, `runtime::app_grant`, `resource_from_name` | Autoridade bounded de grants: valida recursos declarados no manifesto (desconhecidos/duplicados/overflow recusados), separa capabilities metadados, cria geração por ciclo `running` e revoga grants antes do teardown; limite de 16 aplicações preservado. |
| `components/cyberdeck/include/platform/display/cyberdeck_window_manager_adapter.h`, `components/cyberdeck/src/platform/display/cyberdeck_window_manager_adapter.cpp` | `cyberdeck_window_manager_adapter::adapter`, `global` | Unico adaptador dono da raiz LVGL composta pelo screen, barra de sistema persistente e area de conteudo; a barra e chrome preto sem borda, padding ou rolagem, com 42 px fixos, enquanto a politica pura continua separada e cada app recebe somente `view_context`. A UI existente usa a area de conteudo e registra a superficie do shell, removendo-a antes do teardown para invalidar callbacks tardios. |
| `components/cyberdeck/include/apps/demo/cyberdeck_demo_app.h`, `components/cyberdeck/src/apps/demo/cyberdeck_demo_app.cpp` | `cyberdeck_apps::demo_application` | Aplicação headless de prova, registrada junto aos system apps, com manifesto, lifecycle e comando `demo`. |
| `components/cyberdeck/include/apps/system/cyberdeck_system_apps.h`, `components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp` | `cyberdeck_system_apps_register`, `cyberdeck_system_apps_start_logging`, `cyberdeck_system_apps_start`, `service_application`, `event_log_logger`, `make_manifest` | Registro fixo dos system apps do Tab5, incluindo `cyberdeck.event_log`. O adaptador `event_log_logger` implementa a porta `cyberdeck_apps::logger` e encaminha ao backend persistente; o supervisor injeta essa porta ao registrar as aplicações. `cyberdeck_system_apps_start_logging` inicia o logger antes do restante do boot, e `cyberdeck_system_apps_start` delega a ordem dos demais apps ao supervisor via `start_all`. `make_manifest` declara tipo, dependências, recursos, capacidades, stack, fila e orçamento de lifecycle por app (Wi-Fi usa 8 s, pois `wifi_mgr_start()` leva ~2,7 s para subir o radio C6 e o storage SD). SSH, BLE, Serial e Screenshot possuem stop idempotente com joins bounded; Wi-Fi expõe `wifi_mgr_stop()` com join dos dois workers, cancelamento de timers/callbacks e quarentena fail-safe. |
| `components/cyberdeck/include/apps/system/cyberdeck_service_ports.h`, `components/cyberdeck/src/apps/system/cyberdeck_service_ports.cpp` | `cyberdeck_apps::service_ports::{wifi,ssh,ssh_generation,ble,serial_start,serial_stop}` | Gateway das portas de serviço consumidas pela composição da UI. Centraliza Wi-Fi, SSH, BLE e Serial; `cyberdeck_ui.cpp` não referencia mais diretamente `wifi_mgr_*`, `ssh_client_*`, `ble_mgr_*` ou `bridge_start`. Serial expõe stop cooperativo bounded. A porta SSH também expõe a geração monotônica aceita para filtragem de eventos stale; os callbacks continuam bounded nos contratos públicos existentes; a conversão geral para event bus permanece pendente. |
| `components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp` | `session::execute_line` | Consulta o runtime depois dos comandos locais e antes do passthrough SSH, mantendo o shell como aplicação inicial do firmware. A entrada externa SSH chega ao compositor pela fila bounded de eventos da UI, nao diretamente pela task de rede. Na Fase 7 a consulta became condicional: `cyberdeck_shell_console::dispatcher::resolve` decide o dono do primeiro token, e o runtime só recebe `app` e comandos declarados por aplicações registradas. O shell local continua sendo o primeiro dono das linhas e o parser legado permanece o único dono de `wifi`, `log`, `clear`, `screen`, `battery`, `bluetooth`, `ssh` e `help`. |
| `components/cyberdeck/include/apps/shell/cyberdeck_shell_console.h`, `components/cyberdeck/src/apps/shell/cyberdeck_shell_console.cpp` | `k_terminal_limit`, `utf8_char_count`, `utf8_valid_start_offset`, `truncate_left_utf8`, `fit_prompt_marker`, `fit_visible_line`, `session_mode`, `mode_for`, `surface_state`, `line_input`, `line_view`, `compose`, `first_token`, `is_legacy_command`, `dispatcher::resolve`, `dispatcher::collisions` | Politica pura do console do shell (Fase 7), sem LVGL, ESP-IDF, terminal ou sessao local. As helpers de UTF-8 e de ajuste de largura foram movidas do TU anonimo de `cyberdeck_ui.cpp` sem alterar comportamento, de modo que prompt, linha e scrollback tem uma unica autoria sobre o limite de 12288 bytes (`k_terminal_limit = cyberdeck_edit_line::limit`). `compose` aplica mascara na senha, suprime o prompt local quando outro dono controla a entrada ou quando SSH esta conectado, preserva a linha remota filtrada sem inferir prompt e clampa o cursor na janela visivel UTF-8. `dispatcher` resolve o primeiro token e preserva o roteamento existente. |
| `components/cyberdeck/include/apps/shell/cyberdeck_shell_app.h`, `components/cyberdeck/src/apps/shell/cyberdeck_shell_app.cpp` | `cyberdeck_shell_app::application` (`get_manifest`, `init`, `start`, `stop`, `teardown`, `running`, `execute`, `attach_console`, `detach_console`, `console`, `console_ready`, `mode`, `compose_line` e a fachada nula-segura de console), `cyberdeck_shell_app::global_application` | `cyberdeck.shell` como aplicacao de primeiro plano real (Fase 7), registrada no supervisor em vez de `service_application` com hooks stub. `start` e idempotente, falha fechado sem host de composicao e cria um `cyberdeck_shell_session::session` novo por ciclo, o que isola historico, edicao, tokens Wi-Fi e buffer de passkey; `stop` e idempotente, apaga o passkey antes de liberar o console e nunca reaproveita uma sessao descartada. `detach_console` para a aplicacao e devolve o host emprestado, e `attach_console` nao cria console: quem cria e o supervisor. A fachada e nula-segura (sem console, entrada e descartada e um chunk virtual nunca reivindica Enter), e `compose_line` sincroniza o editor antes de composing. O manifesto declara `app_type::foreground`, dependencia de `cyberdeck.event_log`, recursos/capacidades, stack 8192, fila 8 e `owns_console`, sem verbo de comando porque o console e dirigido pela entrada. `execute` permanece `not_handled` para que o supervisor nunca reivindique uma linha em nome do shell. Nao inclui LVGL nem chama backends de plataforma. |
| `tests/host/keymap/test_shell_app.cpp` + `tests/host/keymap/Makefile` | `test_shell_app` | Teste comportamental host da Fase 7 com host de sessao falso: matriz de composicao do prompt (menu, senha mascarada, SSH online sem prompt algum, entrada de outro dono que suprime o prompt por completo), helpers de UTF-8 e de modo com fallbacks, limites bounded e clamp do cursor, linha remota apenas de view (`remote_prompt_literal_contract`: ausencia de marcador fixo, de prompt remoto inferido e de prompt local no modo conectado, `text() == "pwd"` sem marcador, linha remota verbatim, cursor em codepoints sem prompt no conectado e com o prompt local no desconectado, clamp no prefixo oculto, `reserved()` igual a cauda composta sem prompt e payload/porta de senha/host key/banda do compositor sem prompt) e preservacao dos modos (`ssh_mode_preservation_contract`: senha mascarada, TOFU de host key e volta ao console local sem residuo de marcador ou prompt de senha), politica de despacho (exclusividade de `app`, legados nunca interceptados, comando declarado roteado, linha vazia/desconhecida/oversized com falha fechada, colisoes reportadas e registry cheio), manifesto e ownership do console (start/stop idempotentes, start sem host falha fechado, fachada nula-segura, console novo e isolado a cada start, dois consoles independentes, tokens Wi-Fi lidos do console possuido) e propriedade pelo supervisor (dependencia declarada ausente mantem o shell `failed` sem console, `app stop cyberdeck.shell` recusado pelo console dono, lifecycle por API ainda disponivel e `at()` bounded). |
| `tests/host/keymap/test_shell_app_contract.py` | contrato estrutural da Fase 7 | Protege as fronteiras de ownership que o binário host nao alcanca: `cyberdeck.shell` registrado pela aplicacao real (sem `start_shell`/`stop_shell`/`service_application s_shell`), manifesto com tipo/dependencia/`owns_console`, hooks de lifecycle reais, UI sem sessao propria e sem recompor marker/linha, `attach_console` antes do dispatcher de teclado, `detach_console` no teardown, ordem `app` antes da reserva legada e da lista de comandos declarados, switch legado intacto na sessao, reserva derivada do catalogo de ajuda, SSH como `session_mode` derivado de `ssh_phase()`, ausencia de LVGL/ESP-IDF na aplicacao e registro no CMake. |
| `tests/host/keymap/test_app_runtime.cpp` + `tests/host/keymap/Makefile` | `test_app_runtime` | Testa registro duplicado, manifesto expandido, `app info` com API/tipo/capacidades/stack/fila/comandos, hooks explícitos `init`/`teardown`, listagem, lifecycle, dispatch, limites de estado e passthrough de comandos desconhecidos. Cobre a regressão de lifecycle descoberta no dispositivo: `slow_app`/`slow_dependent_app` provam que um hook de start que excede o orçamento mas retorna sucesso permanece `running` e não bloqueia dependentes (orçamento é diagnóstico, não estado), enquanto `failing_app`/`failing_dependent_app` confirmam que falha real continua fail-closed e nomeia a dependência ausente. O Makefile compila o teste host diretamente com a implementação real do runtime. |
| `tests/host/keymap/test_demo_app.cpp` + `tests/host/keymap/Makefile` | `test_demo_app` | Teste host da aplicação compilada em ROM: `demo_application` (manifesto, `start`/`stop`, `running` e `execute` via `cyberdeck_apps::application`). O alvo `test_demo_app` do Makefile liga `test_demo_app.cpp` com `cyberdeck_demo_app.cpp` e `cyberdeck_app_runtime.cpp`, no mesmo padrão de `test_app_runtime`, e entra em `BINS` e no agregado `test`. `components/cyberdeck/src/apps/demo/cyberdeck_demo_app.cpp` passa a ser coberto pelo gcovr e portanto NÃO aparece na allowlist do guard. |
| `tests/host/keymap/test_window_manager.cpp` + `tests/host/keymap/Makefile` | `test_window_manager` | Teste comportamental host da Fase 8 (TEST-WM-01..07 + TEST-REG-8-BAR + TEST-REG-8-CHROME) que liga os TUs reais de produção contra o shim LVGL completo, sem stub: superficie principal por app com limite bounded de 8 e reuso de vaga, foco centralizado com no maximo um dono do input, teardown que expira handles tardios e sobrevive a `reset`, fila FIFO de 8 notificacoes com payload no limite exato e oversized fail-closed, e o adaptador como unico dono da raiz composta (system_bar 42 px + content em coluna, exatamente dois filhos do root, init idempotente, deinit que expira as capacidades). `bar_layout_scenario` cobre a regressão BUG-8-WM-BAR: `system_bar` com 42 px fixos como primeiro item da coluna, `content` sem altura explícita (`height != LV_PCT(100)` e `height == 0`, ou seja, entregue ao flex grow), ordem de composicao barra -> content, e um modelo de coluna flex que prova `content` iniciando em `bar.bottom`, sem intersecao e terminando exatamente em `root.height`; o contrafato pré-correção (content forçado a 100% da raiz) é rejeitado pelo mesmo modelo, então as checagens não são vacuosas. `bar_chrome_scenario` (TEST-REG-8-CHROME) cobre o cromo da barra: fundo preto, `border_width`/padding 0 nos quatro lados, `pad_row`/`pad_column` 0, rolagem desligada (`LV_DIR_NONE`, sem cadeia, scrollbar `OFF`), `LV_PCT(100)` x 42 com `flex_grow` 0 declarado, padding externo de 12 px preservado na raiz, `content` ainda abaixo da barra, e o TU real do header (`cyberdeck_header_view`, o mesmo que a UI anexa a `system_bar()`) ocupando a area inteira sem deslocamento interno — a titulo, relogio e celula direita nascendo na origem da linha. Um controle com qualquer inset diferente de zero rejeita o mesmo modelo, então as checagens de "cromo zero" não são vacuosas. `static_assert` fixam a API opaca: contexto default-constructible e copiavel, mas nao forjavel. O alvo do Makefile compila com `-std=c++17 -Wall -Wextra -Werror -O0 --coverage -Ishim/full`, liga também o header view e suas dependências (`WIFI_ICON_SRC`, `WIFI_INDICATOR_SRC`, `BATTERY_VIEW_SRC`), entra em `BINS` e no agregado `test`. |
| `tests/host/keymap/test_window_manager_contract.py` | contrato estrutural da Fase 8 (TEST-WM-03/05/06/07 + TEST-REG-8-BAR + TEST-REG-8-CHROME) | Fecha as fronteiras que o binario host nao alcanca: policy pura sem LVGL/ESP-IDF/FreeRTOS (includes apenas da biblioteca padrao, sem container que possa crescer, registries em `std::array` fixos e sentinela `focused_slot_ = k_max_surfaces`), `view_context` opaca (ctor de minting privado e befriended apenas ao manager, sem `slot_` publico e sem getter), guardas de limite e ausencia de handles obsoletos (`find()` com range-check e comparacao de geracao, `create()` limpando `out` antes de qualquer decisao e nunca gerando geracao zero, `remove()` exigindo teardown, `copy_text` com clamp e terminador, `notify` recusando oversized antes do indice do anel e contabilizando drop, `reset` invalidando capacidades), LVGL confinado ao adaptador (`lv_scr_act()` em um unico TU, nenhum TU de app com API de criacao/destruicao/estilo da arvore, somente headers de `include/platform/` incluem `lvgl.h`, UI nao compoe raiz), `system_bar`/`content` vindos do adaptador com a UI anexando o header uma unica vez, ordem teardown -> remocao -> limpeza da capacidade -> `deinit` na UI, a regressão de layout BUG-8-WM-BAR (`check_bar_row_layout`: raiz com `LV_LAYOUT_FLEX` + `LV_FLEX_FLOW_COLUMN` escolhidos antes de compor as áreas, barra com 42 px e `flex_grow` 0, content com `flex_grow` 1 e nenhuma altura explícita sob qualquer grafia, teclado virtual criado no root com `LV_OBJ_FLAG_IGNORE_LAYOUT` e oculto por padrão, revelado só por `lv_obj_align` e sem reestilizar `s_menu`/`s_screen`), e a API
exclusao de layout do teclado exige a grafia LVGL 9 `lv_obj_set_ignore_layout(s_keyboard, true)`
(declarada em `lv_obj.h`; `lv_obj_add_flag(..., LV_OBJ_FLAG_IGNORE_LAYOUT)` é
`LV_DEPRECATED` em 9.x) de forma incondicional, sem guarda de preprocessador que
a compilasse fora do build host; o shim `shim/full/lvgl.h` espelha
`LVGL_VERSION_MAJOR`, `lv_obj_set_ignore_layout` e `lv_obj_is_ignore_layout` para
que o harness host compile e observe a mesma garantia, e `test_display_views.cpp`
fixa a asserção comportamental do overlay (teclado no root, `ignore_layout`
verdadeiro, oculto e sem tamanho), a regressão de cromo TEST-REG-8-CHROME
(`check_bar_chrome`: fundo preto explícito com `lv_color_hex(0x000000)`,
`border_width`/`pad_all`/`pad_row`/`pad_column` 0 e nenhum padding não-zero na
barra, `disable_scrolling` exigindo `LV_DIR_NONE` + cadeia `false` + scrollbar
`OFF` sem reabilitar `lv_obj_set_scrollable`, `LV_PCT(100)` x 42 com
`flex_grow` 0, padding externo de 12 px na raiz, header real com
`lv_obj_set_size(header, lv_pct(100), 42)` e cromo próprio zero, e o shim
`shim/full/lvgl.h` registrando o cromo aplicado com sentinela de "não aplicado"
mais os read-backs `lv_obj_get_scroll_dir`, `lv_obj_get_scrollbar_mode`,
`lv_obj_get_content_width`/`height`, sem os quais toda assert de cromo zero
passaria num default), e wiring CMake/Makefile com os alvos phony, `BINS`,
agregado `test` e as TUs fora da allowlist do guard de cobertura. O alvo do
Makefile declara todos os arquivos inspecionados como pre-requisitos, incluindo
`cyberdeck_terminal_view.cpp` e `cyberdeck_header_view.cpp`. |
| `tests/host/keymap/test_system_apps_contract.py` + `tests/host/keymap/test_screenshot_lifecycle_contract.py` | contratos dos system apps e Screenshot | Confirma manifests, ordem de startup, uso do runtime global pela UI e ausência de inicialização direta dos serviços em `app_main`; verifica também callback Wi-Fi bounded, unregister sincronizado, task/fila, handshake, stop/join fail-safe e lifecycle idempotente do Screenshot; tambem exige a criacao da task SSH por `xTaskCreateWithCaps(ssh_client_task, "ssh_client", 24576` (regex tolerante a whitespace/quebra de linha) e ausencia de `xTaskCreate(` cru. |

O runtime inicial é deliberadamente compilado no firmware: não há loader ELF,
instalação dinâmica, scripts ou execução de aplicações do SD nesta fase. O SD
continua sendo armazenamento de dados e a expansão futura deve adicionar um
host de aplicação controlado, sem permitir acesso direto a LVGL ou aos serviços.

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

Recorte da Etapa 4: `cyberdeck_service_ports::ssh_disconnect_and_wait` e a
porta usada pelo teardown da UI antes da destruicao de `s_ssh_event_queue`.
Callbacks SSH mantem payload bounded ao maior chunk do transporte (1023 bytes,
pois o cliente SSH le ate 1023 bytes por vez); chunks acima desse contrato
sao descartados sem copia e avancam o epoch de ressincronizacao. Dados
descartados por overflow sao contados em contador saturante. Eventos de estado removem dados antigos para
reservar espaco quando necessario; quando a fila cheia contem apenas estados,
o novo estado e descartado e contabilizado separadamente para diagnostico.

| Arquivo | Simbolos/contrato | Papel |
| --- | --- | --- |
| `components/cyberdeck/src/apps/ssh/ssh_client.cpp` | `ssh_client_connect`, `ssh_client_generation`, `ssh_client_send_data`, `ssh_client_disconnect`, `ssh_client_disconnect_and_wait` | Cliente libssh assincrono, task FreeRTOS, PTY, senha, host key e callbacks de estado/dados; cada conexão aceita recebe geração monotônica `uint64_t`, publicada pela task/callback; callbacks não tocam LVGL nem adquirem `bsp_display_lock`; teardown cooperativo com espera bounded via `vTaskDelay` ate `s_task_handle` ficar nulo. A task e criada com `xTaskCreateWithCaps(ssh_client_task, "ssh_client", 24576, ..., 5, &task, MALLOC_CAP_SPIRAM)`, portanto o TCB permanece interno e a stack de 24576 bytes vem da PSRAM (a heap interna não comporta a stack); o par de APIs é obrigatório e todos os 12 pontos de autoexclusão usam `vTaskDeleteWithCaps(NULL)` para liberar a stack externa, sob pena de vazar memória da PSRAM. `ssh_client_connect` registra `ESP_LOGE` e devolve `ESP_FAIL` se a criação falhar, mantendo `s_task_handle` nulo. Eventos de sessão sao publicados pela API leve do Wi-Fi e consumidos pela `net_worker`; o cliente nao inclui nem chama o backend Wi-Fi diretamente e nao remonta o storage. |
| `components/cyberdeck/include/apps/ssh/ssh_client.h` | `ssh_client_generation_t`, `ssh_client_state_t` e API SSH | Contrato entre UI e cliente, incluindo callbacks com geração, consulta da geração e a API de desconexao com espera bounded. |
| `tests/host/keymap/test_ssh_task_alloc_contract.py` | TEST-SSH-001/002/003/008 | Contrato estrutural host da alocacao da task SSH (nao linkavel no host por acoplar libssh/FreeRTOS): exige `xTaskCreateWithCaps(..., MALLOC_CAP_SPIRAM)` com 24576 e ausencia de `xTaskCreate(` cru, exatamente 12 `vTaskDeleteWithCaps(NULL)` e nenhum `vTaskDelete(NULL)`, ramo de falha com `ESP_LOGE`/`s_task_handle` nulo/give unico do mutex/`ESP_FAIL`, e documentacao da stack externa no `code-map.md`; as comparacoes de chamada usam visao normalizada (whitespace/quebra de linha colapsados), entao nao acoplam ao layout do formatador sem perder forca; executado no alvo agregado `make test`. Cobertura de runtime real (alocacao sob heap interna fragmentada, ausencia de leak e conexao) permanece validacao de hardware via ponte Serial-JTAG (TEST-SSH-004/005/006). |
| `components/cyberdeck/src/apps/shell/cyberdeck_terminal_filter.cpp` + `tests/host/keymap/test_terminal_filter.cpp` | classe/funcoes do filtro incremental | Remove ANSI/CSI/OSC, normaliza CR/LF, descarta C0/DEL e preserva UTF-8; o teste host cobre fragmentacao, particionamento e bytes UTF-8. `test_remote_prompt_is_preserved_literally` fixa que o prompt remoto e a unica fonte de prompt: variantes de prompt, prompt sob ANSI, UTF-8 e a antiga grafia `ssh> ` sao preservados byte a byte, CR/CRLF viram um unico LF e o filtro nunca acrescenta quebra que nao veio; `test_remote_prompt_survives_partition_invariance` fixa o mesmo texto em qualquer fatiamento, inclusive na fronteira ANSI/prompt, dentro do CRLF e com CR isolado no flush (flush idempotente). A UI propaga um epoch monotônico de descarte dos eventos SSH (incluindo payload acima do contrato de transporte) até o próximo evento da mesma geração e ressincroniza o filtro descartando somente sua cauda ANSI incerta, sem reconstruir bytes. Após gap explícito, `resync_after_gap()` descarta somente uma cauda plausível `[0-9;]*m`, com orçamento de 16 bytes de parâmetro (`k_gap_parameter_limit`); o primeiro byte fora desse padrão é reprocessado como texto, evitando consumo indefinido. `test_gap_resync_discards_only_the_bounded_ansi_tail` fixa esse contrato (cauda com e sem terminador, orçamento e sua fronteira, byte incompatível reprocessado, gaps repetidos, `flush` desarmando e CR antes/depois do gap). |
| `components/cyberdeck/src/apps/shell/cyberdeck_ssh_echo_guard.cpp` | `cyberdeck_ssh_echo_guard` | Suprime somente o eco remoto que corresponde ao payload enviado. |
| `components/cyberdeck/src/apps/shell/cyberdeck_ssh_line_composer.cpp` + `tests/host/keymap/test_ssh_line_composer.cpp` | `cyberdeck_ssh_line_composer` | Mantem no maximo um comando pendente e suprime somente o eco byte a byte do payload. `test_remote_prompt_never_matches_the_echo_guard` fixa que o compositor nao conhece nenhum prompt: um prompt remoto diverge do payload em k == 0 e e preservado integralmente, tanto na grafia tipica quanto na antiga grafia `ssh> `, tambem fragmentado byte a byte; um comando cujo prefixo coincide com o prompt continua com eco suprimido. Saida remota e LF recebido sao preservados verbatim; o compositor e `get_rendered_output()` nao fabricam LF nem separador. |

| `tests/host/keymap/test_terminal_output_contract.py` | T-COAL-01/02, T-BOUND-01/02, T-CTX-01/02, T-FLUSH-01/02, T-FILTER-01, T-REG-01, T-REG-02, T-REG-03, T-GAP-01/02/03/04 | Contrato estrutural host da UI nao linkavel: verifica coalescencia por dirty/timer, limite 12288 e poda UTF-8-safe, contexto LVGL sem lock reentrante, flush imediato, ordem filtro/compositor e preservacao da edicao. T-COAL tambem fixa o payload do evento SSH de forma **relacional ao transporte**: `k_ssh_event_data_limit` tem de ser igual a `sizeof(rx_buffer) - 1`, derivado de `ssh_client.cpp` (nunca um literal arbitrario como o antigo 256, que truncaria silenciosamente todo chunk cheio legitimo); o elemento do evento so pode dimensionar arrays pelas constantes nomeadas; e a rejeicao fail-closed de payload acima do contrato tem de ocorrer antes de qualquer `memcpy`, sem `static_cast` de truncamento e sem carimbar epoch em um evento que nao existe. T-REG-02 fixa a ausencia de marcador fixo e de inferencia: `k_ssh_marker` e a grafia `ssh> ` estao ausentes do header, do TU do console, da UI, da sessao, do filtro e do compositor, o console so sintetiza os dois prompts legitimos, e `state.ssh_connected` decide a `visible_line` e suprime o prompt local com `else if (!state.ssh_connected && !state.input_owned_elsewhere)` (e exige os cenarios `test_remote_prompt_is_preserved_literally`, `test_remote_prompt_survives_partition_invariance` e `test_remote_prompt_never_matches_the_echo_guard`); T-REG-03 fixa que o scrollback e aparado por `view.reserved()` e que o LF visual e unico, condicional a saida normalizada nao terminar em LF, orcado em `available - 1` e nunca escrito de volta no scrollback retido; tambem exige os cenarios comportamentais `test_prompt_matrix_executes_production_composition` e `test_visual_separator_executes_production_statements`; T-GAP-01/02/03 fixa o seam de gap (campo `discard_epoch` no elemento da fila, `s_ssh_discard_epoch`/`s_ssh_applied_discard_epoch`, os tres sítios de descarte de dados e os dois de estado passando por `mark_ssh_event_discarded`, payload acima do contrato de transporte rejeitado em fail-closed antes da copia, evicção re-selada com o epoch do descarte, filtro de geração stale **antes** da guarda de epoch, `reset_ssh_output_filter()` chamada uma unica vez e sem tocar o compositor, e o sync do epoch apenas em `ssh_connect` com `ESP_OK`), exige a ausencia de `0x9B`/`0x9D` no filtro e a presenca dos cenarios de `test_terminal_filter.cpp` e de `test_ssh_output_gap.py`; T-GAP-04 exige `s_ssh_output_filter.resync_after_gap()` exatamente uma vez e depois da guarda de epoch no filtro incremental (o reset sozinho devolveria `34m` para a tela), mais o contrato do header (`void resync_after_gap();`, `k_gap_parameter_limit = 16`, o limiar no `.cpp`) — os quatro cenarios e as duas dimensoes do header tambem ficam listados por nome, logo nao podem ser removidos em silencio; executado isoladamente e no alvo agregado `make test`. |
| `tests/host/keymap/test_ssh_output_gap.py` + `tests/host/keymap/Makefile` | `test_ssh_output_gap` | Harness comportamental host do seam de gap da saida SSH (REQ-01/02/03/04). A UI nao e linkavel, entao o harness extrai *verbatim* da `cyberdeck_ui.cpp` o `enum`/`struct` do evento, as tres constantes, os dez globals, os nove corpos (`append_output`, `reset_ssh_output_filter`, `mark_ssh_event_discarded`, `current_ssh_discard_epoch`, `discard_ssh_line_composer`, `process_ssh_data`, `on_ssh_data`, `on_ssh_state`, `process_ssh_events`) e o bloco `if (result == ESP_OK)` de `shell_session_host::ssh_connect`, compilando-os contra os TUs reais `cyberdeck_terminal_filter.cpp`, `cyberdeck_ssh_line_composer.cpp`, `cyberdeck_shell_console.cpp` e `cyberdeck_app_runtime.cpp`. Apenas a fila bounded (FIFO, capacidade de producao, `pdFALSE` em overflow/underflow) e os efeitos de LVGL (`render_terminal`, a linha de status de `process_ssh_state`) sao duplos do harness, no mesmo padrao do host falso de `test_shell_session.cpp`. Cobre: prompt `ESC[01;34m~$ ESC[00m` sem perda em todos os fatiamentos; gap sinalizado antes de `34m~$ ` sem vazamento de ESC nem replay; guarda aplicada uma vez por avanco de epoch; evicção por evento de estado; fila so de estados com contadores separados; geração stale descartada antes da guarda; limite de payload derivado do transporte e rejeicao fail-closed do chunk acima dele (TEST-SSH-02/03); scrollback de 12288 sob chunks de transporte cheios (TEST-SSH-04); sync do epoch no connect (TEST-SSH-05); e invariantes de payload/eco com o reset do gap tocando apenas o filtro ANSI. O limite nao e literal: `transport_chunk_limit()` deriva `sizeof(rx_buffer) - 1` de `ssh_client.cpp` e um `static_assert` compila a relacao no harness, de modo que reduzir o slot ou mover o buffer de recepcao sem mover o outro lado falha o build do teste. |

O parser de alvo suporta IPv4, IPv6 entre brackets, usuario e porta. A selecao
de familia usa `inet_pton`; hostnames continuam usando `ANY`. O overlay local de
`sock_utils` fornece `getnameinfo` dual-stack e flags compativeis com libssh/lwIP.

Com SSH conectado, a view da linha nao fabrica marcador nem tenta classificar ou
detectar o prompt remoto. A saida recebida continua passando pelo filtro e pelo
compositor existentes, preservando seus bytes normalizados; a UI e
`get_rendered_output()` nao acrescentam LF entre a saida e a linha
editada. O cursor e `reserved()` continuam
relativos apenas ao texto efetivamente visivel, e payload, echo, protocolo e
estados SSH permanecem inalterados. O compositor SSH preserva a saida remota e
LF recebido verbatim, sem inserir LF ao resolver eco, divergencia ou
desconexao.

Rastreabilidade da composicao visual (host, sem hardware):

| REQ/AC | TEST | Evidencia |
| --- | --- | --- |
| REQ-1/AC-1 prompt remoto literal, sem marcador fixo nem inferencia | `test_prompt_behavior.py` (`test_prompt_matrix_executes_production_composition`) | Harness compila o TU real `cyberdeck_shell_console.cpp`; varredura exaustiva de 8 superficies (`ssh_connected`/`password_pending`/`input_owned_elsewhere`) x 4 `cwd`s afirma que `marker` so pode ser `""`, `"Password: "` ou terminar em `"$ "`, nunca conter `ssh> `, `ssh`, `root@` ou `@host`; conectado devolve `marker == ""` e a entrada editada sem marcador local; `visible_line`/`fitted_line` verbatim |
| REQ-1/AC-1 (fronteira nao linkavel) | `local_prompt_contract.py`, `test_terminal_output_contract.py` | `k_ssh_marker` ausente do header e do TU do console; literais `ssh> `, `root@`, `%h`, `%n`, `@%` ausentes; exatamente 2 pontos de atribuicao (`view.marker =`); `state.ssh_connected` aparece duas vezes, no ternario de `visible_line` e na guarda que suprime o prompt local (`else if (!state.ssh_connected && !state.input_owned_elsewhere)`); `ssh>` ausente na UI, na sessao, no filtro e no compositor |
| REQ-1/AC-1 (prompt remoto pelo fluxo remoto) | `test_terminal_filter.cpp` (`test_remote_prompt_is_preserved_literally`) | Prompt tipico, variantes (`root@host:/etc#`, `[user@host ~]$`, `bash-5.2$`), prompt sob ANSI reduzido aos seus bytes, UTF-8 (`usuário@host:~$ café`) e a antiga grafia `ssh> ` treated como texto remoto ordinario, tudo preservado byte a byte |
| REQ-1/AC-1 (fragmentacao) | `test_terminal_filter.cpp` (`test_remote_prompt_survives_partition_invariance`), `test_ssh_line_composer.cpp` (`test_remote_prompt_never_matches_the_echo_guard`) | Prompt remoto fatiado de 1 a `raw.size()` bytes produz sempre o mesmo texto; corte na fronteira ANSI/prompt e dentro do CRLF; no compositor, prompt remoto diverge do payload em `k == 0`, e preservado sem LF fabricado e nao e engolido pela regra de eco; comando cujo prefixo coincide com o prompt continua com eco suprimido |
| REQ-SSH-02/AC-2 saida remota e LF preservados | `test_prompt_behavior.py` (`test_visual_separator_executes_production_statements`) | `get_rendered_output()` preserva a saida recebida e nao fabrica LF; saida ausente ou ja terminada em LF nao recebe byte adicional, e saida sem LF permanece sem LF. `available == 0` nao escreve nada; a matriz tambem cobre a cauda conectada (`ssh_connected`, sem prompt), onde `reserved()` desconta zero byte |
| REQ-2/AC-2 (CR/CRLF normalizados decidem a regra) | `test_prompt_behavior.py`, `test_terminal_filter.cpp` | `root@host:~$ ls\r\n` e `root@host:~$ ls\r` normalizam em um unico LF e a regra visual nao acrescenta nada; `\r` isolado no fim so vira LF no `flush()`, que e idempotente; saida sem LF continua sem LF apos o filtro |
| REQ-3/AC-3 payload, eco e protocolo inalterados | `test_shell_app.cpp` (`remote_prompt_literal_contract`, `ssh_mode_preservation_contract`) | Portas SSH gravadas pelo host falso: `ssh_sent == "pwd\n"` sem prompt nem `ssh> `, banda local `written == "pwd"`, nenhum eco local, `ssh_password` vazio e `host_key_accepted == 0` num comando conectado, `reset_ssh_filter()` disparado, compositor armado com o payload cru (feed do eco exato nao devolve LF e desarma), `composer_discards == 0`, linha vazia envia so `\n`, senha por `ssh_send_password("hunter2")` sem tocar o PTY, TOFU por `ssh_accept_host_key()`; prompt remoto que volta pelo fluxo diverge do payload e nao retorna ao payload |
| REQ-SSH-02/AC-2 (LF fora do scrollback) | `local_prompt_contract.py` (`visual_separator_contract`), `test_terminal_output_contract.py` | `s_output.push_back` inexistente na UI; `s_output` so sofre `append`/`erase` em `append_output`, e `get_rendered_output()` nao escreve LF fabricado no scrollback |
| AC-4 cursor, reserva e scrollback | `test_shell_app.cpp`, `test_prompt_behavior.py`, `test_terminal_output_contract.py` | `cursor_chars() == utf8_char_count(marker) + <codepoints visiveis>` para ASCII e UTF-8 (`p\xC3\xA9\xE2\x82\xAC`, paradas 0/1/3/6) nas duas superficies, com o prompt local quando desconectado e prompt zero quando conectado, cursor em casa, clamp no prefixo oculto, `reserved() == text().size() == marker + fitted_line`, `fitted_line.size() <= 12288 - marker.size()` (com `marker` vazio no modo conectado, a linha ajustada ocupa o limite cheio); no seam do dispositivo, `get_rendered_output()` apara por `view.reserved()` e `render_terminal()` posiciona por `utf8_char_count(output) + view.cursor_chars()`; o harness do separador afirma `scrollback + text().size() <= 12288` e que o cursor relativo continua dentro da area renderizada |

### Gap da fila de saida SSH (REQ-01..REQ-04 / AC-01..AC-04)

Um descarte na fila de eventos deixa o filtro ANSI com uma sequencia pela
metade (`ESC[01;` pendente). Sem sinal, os bytes de parametro seguintes
(`34m`) eram absorvidos silenciosamente; com sinal, sao lidos como texto. A UI
porta um **epoch monotono de descarte**: `mark_ssh_event_discarded()` incrementa
o contador saturante do descarte *e* avanca `s_ssh_discard_epoch`; todo evento
enfileirado recebe o epoch corrente em `discard_epoch`; e `process_ssh_events()`
ressincroniza o filtro com `reset_ssh_output_filter()` quando encontra um epoch
acima de `s_ssh_applied_discard_epoch`, descartando somente a cauda ANSI incerta
sem reconstruir nem sintetizar bytes. Tres perdas publicam epoch novo: overflow
do produtor (`uxQueueMessagesWaiting >= capacity - 1`), payload acima do
contrato de transporte (`k_ssh_event_data_limit` = 1023) e a eviccao de um
evento de dados feita para
dar lugar a um evento de estado (que e re-selada com o epoch do descarte). A
filtro de geracao stale roda **antes** da guarda, entao um evento obsoleto nao
consome o epoch. `ssh_connect` adota o epoch corrente apenas em `ESP_OK`, para
que uma sessao nova nao herde um reset pendente. O reset toca **apenas** o
filtro: o compositor de linha permanece armado, preservando a regra de eco. A
producao nao trata CSI/OSC de 8 bits (`0x9B`/`0x9D` nao tem estado na maquina
de sequencias), portanto `C2 9B` e o byte cru `9B` permanecem texto opaco,
preservados byte a byte; a remocao de CSI 8-bit so pode ser exigida depois que
a producao a tratar.

A ressincronizacao vai alem de devolver o filtro a GROUND: `resync_after_gap()`
arma o descarte de uma cauda SGR plausivel `[0-9;]*m` com orcamento de 16 bytes
de parametro, para que a parte do prompt que o gap deixou pela metade (`34m`)
nao vaze como texto. O primeiro byte fora da classe e reprocessado em GROUND, o
que torna a heuristica one-shot e fail-safe (nada e reconstruido e o consumo
nunca e indefinido); `flush()` desarma a cauda, entao o reset puro do consumer
nao basta e a UI precisa chamar `resync_after_gap()` explicitamente.

Rastreabilidade do seam de gap (host, sem hardware):

| REQ/AC | TEST | Evidencia |
| --- | --- | --- |
| REQ-01/AC-01 prompt ANSI sem perda, fluxo inteiro | `test_ssh_output_gap.py` (`req01_ansi_prompt_is_lossless`) | Sentencas de `on_ssh_data`/`process_ssh_events`/`process_ssh_data` compiladas verbatim contra o filtro real: `ESC[01;34m~$ ESC[00m` (16 bytes) rende exatamente `~$ `, sem `\x1B` e sem `01;34`/`34m` como texto, com `drop_data == 0`, `drop_state == 0` e `applied_discard_epoch == 0`; o pump nunca repinta de forma sincrona (marca `s_terminal_output_dirty` e deixa o timer coalescer) e re-drenar a fila nao duplica byte |
| REQ-01/AC-01 fragmentacao | `test_ssh_output_gap.py` (`req01_ansi_prompt_is_lossless`), `test_terminal_filter.cpp` (`test_ansi_prompt_is_lossless_and_partition_invariant`) | Toda biparticao do prompt (0..16) e toda tripartition (pares de fronteira) produzem `~$ `; entrega byte a byte com drain por byte tambem; o filtro puro confirma o mesmo texto para `step` de 1 a 16 e para o fluxo real `user@host:~$ ls -la` seguido de um segundo prompt |
| REQ-02/AC-02 gap sinalizado e cauda ambigua descartada | `test_ssh_output_gap.py` (`req02_gap_before_parameter_tail_is_signalled`, `req02_gap_discards_the_uncertain_ansi_tail`) | Controle sem sinal: `ESC[01;` + `34m~$ ESC[00m` engole `34m` e devolve `~$ ` (o reset nao aconteceu). Com sinal: `drop_data` incrementa, `epoch` avanca, o evento seguinte recebe o epoch e o reset real ocorre — a cauda incerta e descartada e o prompt chega limpo (`~$ `, nunca `34m~$ `), nenhum `\x1B` vaza, nem `01;` nem `34m` aparecem como texto e drenar a fila vazia nao muda nada; um segundo gap rearma o mesmo comportamento |
| REQ-02/AC-02 cauda bounded `[0-9;]*m` | `test_ssh_output_gap.py` (`req02_gap_discards_the_bounded_parameter_tail`), `test_terminal_filter.cpp` (`test_gap_resync_discards_only_the_bounded_ansi_tail`) | No seam: gap + `34m~$ \x1B[00m` rende `~$ `; cauda sem `m` e fragmentada (`01;` + `34` + `~$ `) tambem e descartada; 18 digitos perdem exatamente os 16 primeiros e deixam `77`; um fluxo que nao comeca por parametro (`user@host:~$ 34m\r\n`) e entregue inteiro; SGR completo depois do gap e SGR fragmentado depois do gap continuam removidos. No filtro puro: `user@host:\x1B[01;` + gap + `34m~$ \x1B[00m` = `user@host:~$ ` sem `34m`/`01;`; fronteira do orcamento (`15` parametros + `m` = cauda, `16` parametros + `m` deixa o `m` como texto), `;` na mesma classe; ESC, byte alto e C0 cancelam a cauda e sao reprocessados (`\xC3\xA9 caf\xC3\xA9` intacto, `\x07abc` = `abc`); CR pendente antes do gap nao vira LF fantasma; `flush` desarma a cauda (`34` + flush + `m~$ ` = `m~$ `) e sem gap `34` continua texto |
| REQ-02/AC-02 gaps repetidos rearmam | `test_ssh_output_gap.py` (`req02_repeated_gaps_rearm_the_tail_discard`), `test_terminal_filter.cpp` (`test_gap_resync_discards_only_the_bounded_ansi_tail`) | Duas perdas seguidas sem byte entre elas descartam **uma** cauda (`34m34m~$ ` seria o sintoma de um rearma duplo) e um terceiro gap rearma de novo; tres rodadas com CSI pendente cortado pela perda mostram o prompt uma vez e a cauda nunca (`~$ ~$ ~$ ~$ ~$ `), com `applied_discard_epoch` avancando uma vez por perda; no filtro puro, tres `resync_after_gap()` seguidos rendem o mesmo texto incremental |
| REQ-02/AC-02 guarda uma vez por epoch | `test_ssh_output_gap.py` (`req02_gap_guard_fires_once_per_epoch`) | Dois eventos com o mesmo epoch: o primeiro resincroniza (o CSI pendente passa aGROUND) e o segundo nao — prova observavel, porque um segundo reset transformaria os parametros engolidos em texto vazado; `applied_discard_epoch` avanca uma unica vez e um terceiro par com o mesmo epoch continua engolido |
| REQ-02/AC-02 evicção por evento de estado | `test_ssh_output_gap.py` (`req02_state_eviction_is_a_signalled_gap`) | Com a fila no teto de transporte, a transicao seguinte e a ultima a ser observada; `drop_data == 1`, `drop_state == 0`, `epoch == 1`, e o `discard_epoch` re-selado faz o consumidor ressincronizar, com a cauda `34m` descartada e o prompt exibido como `~$ ` — o contraste com o controle da mesma funcao prova que o reset veio do epoch, nao de acaso |
| REQ-03/AC-03 UTF-8 preservado (incluindo `C2 9B`) | `test_ssh_output_gap.py` (`req03_utf8_and_eight_bit_csi_are_opaque_text`), `test_terminal_filter.cpp` (`test_eight_bit_csi_bytes_stay_opaque_text`) | Nove fluxos (2, 3 e 4 bytes, `usuário@host:~$ café`, `C2 9B`, `9B`, `C2 9D`, `9D` junto do prompt) conferidos byte a byte em **toda** biparticao e byte a byte com drain por byte; `C2` e `9B` em chunks distintos devolvem `C2 9B`; codepoint partido pela fronteira de evento (`caf\xC3` + `\xA9 ...`) e pela fronteira de gap sao preservados sem re-encode — inclusive quando a cauda `34m` e o primeiro byte da cauda depois do gap (`34m\xA9 ok` = `\xA9 ok`), e quando um codepoint de 2, 3 ou 4 bytes comeca no primeiro byte apos o gap e por isso cancela o descarte da cauda; byte 8-bit no meio de um CSI aborta a sequencia e vira texto, sem engolir o vizinho |
| REQ-03/AC-03 CSI 8-bit so se a producao tratar | `test_terminal_output_contract.py`, `test_terminal_filter.cpp` | `0x9B` e `0x9D` ausentes do filtro (fonte e header); `C2 9B0;t\x07x` devolve `C2 9D0;tx` (o byte OSC de 8 bits **nao** abre varredura OSC, so o BEL e descartado), e `9B 34m` devolve `9B 34m` como texto — a evidencia e transparencia, nunca remocao |
| REQ-04/AC-04 fila cheia e contrato de transporte | `test_ssh_output_gap.py` (`req02_gap_before_parameter_tail_is_signalled`, `req04_transport_chunks_up_to_the_limit_arrive_intact`/TEST-SSH-02, `req04_oversized_payload_is_rejected_fail_closed_and_signalled`/TEST-SSH-03) | Overflow no teto do produtor (`capacity - 1`) e payload acima de `k_ssh_event_data_limit` (derivado de `sizeof(rx_buffer) - 1` = 1023 bytes) contam como perda de dados e publicam epoch; um chunk de exatamente 1023 bytes e um chunk valido e chega integral, sem truncamento, sem drop e sem epoch; 1024 bytes ja estao fora do contrato e sao rejeitados sem copiar nada (`queued() == 0`), e o mesmo vale com a fila cheia (uma unica perda, nao duas); a rejeicao nao enfileira evento, logo o epoch fica pendente e o proximo chunk valido ressincroniza o filtro (prompt como `~$ `, nunca `34m~$ `); `data == nullptr` e `length == 0` sao inertes |
| REQ-04/AC-04 geracao stale | `test_ssh_output_gap.py` (`req04_stale_generation_is_skipped_before_the_gap_guard`) | Eventos da geracao 6 (dados e estado) com epoch 1 sao descartados sem tocar o scrollback, sem chamar `process_ssh_state` e **sem** arrastar `applied_discard_epoch` (fica 0); a geracao corrente consome o epoch pendente e resincroniza |
| REQ-04/AC-04 fila so de estados e contadores separados | `test_ssh_output_gap.py` (`req04_state_only_queue_keeps_counters_separate`) | Com a fila cheia so de estados, `drop_state == 1` e `drop_data == 0`, o epoch avanca (a evicção e sinalizada) e nenhuma transicao e trocada por outra: as enfileiradas aparecem uma vez cada em ordem FIFO e a perdida esta ausente |
| REQ-04/AC-04 sync do epoch no connect | `test_ssh_output_gap.py` (`req04_connect_adopts_the_current_discard_epoch`) | `ESP_FAIL` nao adota nada (geracao e `applied_discard_epoch` intactos); `ESP_OK` adota o epoch corrente e a nova geracao; o primeiro evento da sessao nova roda sem reset herdado (o CSI continua pendente, logo `34m` continua engolido) |
| REQ-04/AC-04 payload e eco invariantes | `test_ssh_output_gap.py` (`req04_gap_reset_preserves_the_payload_and_echo_invariants`, `req04_only_the_explicit_flush_disarms_the_composer`), `test_terminal_output_contract.py` (T-GAP-03) | Com o compositor armado e o eco partido (`pw`), um gap sinalizado no meio **nao** desarma o casamento: o eco completo nao devolve LF fabricado, o comando aparece uma vez na banda, e a divergencia pos-gap libera prefixo + byte divergente + resto uma vez cada (`ls -laX\n`); um segundo `begin()` enquanto ha pendencia e recusado; so `discard_ssh_line_composer()` (flush explicito) desarma; `process_ssh_events` referencia `reset_ssh_output_filter()` uma unica vez e nao menciona o compositor |
| REQ-01/03 e REQ-04/AC-04 scrollback sob chunks de transporte | `test_ssh_output_gap.py` (`req04_full_chunks_respect_the_scrollback_budget`/TEST-SSH-04) | Doze chunks de 1023 bytes (12276) cabem no scrollback de 12288 e o decimo terceiro e aparado em vez de crescer o buffer; com o excesso de 4 bytes caindo no byte de continuacao de um codepoint, a poda remove os 5 bytes do codepoint inteiro e so sobra ASCII valido; `drop_data`, `drop_state` e `epoch` permanecem em zero durante a sonda, que e de orcamento e nao de overflow |
| Fronteira nao linkavel do gap | `test_terminal_output_contract.py` (T-GAP-01/02/03/04) | `discard_epoch` no elemento, `s_ssh_discard_epoch`/`s_ssh_applied_discard_epoch` com CAS saturante, 3 + 2 sítios de `mark_ssh_event_discarded` com contadores exatos, ausencia de `compare_exchange_weak` nos dois callbacks, ordem `continue` de geracao antes da guarda, e `s_ssh_applied_discard_epoch = current_ssh_discard_epoch();` uma unica vez, dentro de `if (result == ESP_OK)`; T-GAP-04 fixa `s_ssh_output_filter.resync_after_gap()` uma unica vez e depois da guarda de epoch, alem do contrato do header (`void resync_after_gap();` e `k_gap_parameter_limit = 16`) |

Riscos residuais deste seam (nao fixados como invariante, apenas documentados):
um overflow de fila so de estados tambem remove o estado mais antigo que a
eviccao tentava substituir, portanto custa duas transicoes e contabiliza uma;
e `s_ssh_discard_epoch` satura em `UINT32_MAX`, caso em que o sinal deixa de
avancar (inviavel em host sem custo proibitivo de tempo). A cauda descartada tem
orcamento de 16 bytes de parametro e o terminador fecha a cauda **dentro** desse
orcamento, portanto um SGR com exatamente 16 parametros deixa o `m` como texto
(no maximo 1 byte visivel, nunca laco) — fronteira fixada por
`test_gap_resync_discards_only_the_bounded_ansi_tail`, porque um SGR real com 16
parametros e practicamente inexistente. Cobertura de runtime
real — fila saturada sob carga SSH de verdade, eco partido no dispositivo —
permanece validacao de hardware pela ponte Serial-JTAG.

### Wi-Fi

| Arquivo | Simbolos/contrato | Papel |
| --- | --- | --- |
| `components/cyberdeck/src/apps/wifi/wifi_mgr.cpp` | API `wifi_mgr_*`, `net_session_event`, `wifi_event_worker`, `net_worker`, `rollback_start` | Adaptador ESP-IDF, conexao, desconexao, scan, callbacks, tokens e integracao de rede. Eventos leves de sessao SSH usam fila FIFO bounded de capacidade 4, com descarte contado sem bloqueio; somente `net_worker` chama o coordinator sob `net_lock`, e a fila participa do rollback/teardown transacional. Callbacks dos timers apenas publicam flags atomicas coalescidas e acordam o worker; o `net_worker` drena retry, scan e timeout sob `net_lock`. Stop/rollback sinalizam os workers antes de destruir mutexes e limpam as flags para tornar callbacks tardios inofensivos. |
| `components/cyberdeck/src/apps/wifi/wifi_storage.cpp` | armazenamento de redes | Persistencia de SSID/credencial no SD; senhas nao sao exibidas nem registradas. |
| `components/cyberdeck/src/apps/wifi/cyberdeck_wifi_menu.cpp` | `cyberdeck_wifi_search_menu` | Modelo/renderizacao pura do menu de busca e redes salvas. |
| `components/cyberdeck/src/apps/wifi/cyberdeck_wifi_state_machine.cpp` | `state_machine` | Estados search/saved/password/connecting/forget e acoes derivadas. |
| `components/cyberdeck/src/apps/wifi/cyberdeck_wifi_event_dispatch.cpp` | `event_dispatch` | Snapshots bounded de `GOT_IP`/`LOST_IP`, tokens, ack, retry e invalidacao. |
| `components/cyberdeck/src/apps/wifi/cyberdeck_wifi_persistence_queue.cpp` | `persistence_queue` | Fila bounded com ownership, retry, acknowledge, wipe e teardown. |
| `components/cyberdeck/src/apps/wifi/cyberdeck_wifi_persistence_coordinator.cpp` | coordinator | Worker que serializa persistencia e efeitos derivados fora de `sys_evt`. |
| `components/cyberdeck/src/apps/wifi/cyberdeck_wifi_audit.cpp`, `components/cyberdeck/include/apps/wifi/cyberdeck_wifi_audit.h` | `cyberdeck_wifi_audit::audit_controller` | Worker FreeRTOS com fila bounded para auditoria passiva da associação/interface local, snapshot versionado, token sob mutex, teardown por sinalização/join, leitura bounded do campo SSID sem varredura além do array do driver, saneamento central, renderização direta de `status`/`ssid`/`bssid`/`ip` com `<missing>` e payload key=value bounded determinístico em seis linhas na ordem `version`/`token`/`status`/`ssid`/`bssid`/`ip`, com cada chave exatamente uma vez, LF final e campos ausentes marcados por `<missing>`, persistência explícita por `wifi audit save`, sem I/O no comando padrão e I/O fora da UI; o resultado grande e o scratch de formatação ficam fora da stack do worker, e o resultado bem-sucedido preserva `bytes`/`data` somente após a persistência. No ESP/FATFS, o adaptador delega toda a transação ao seam injetável, sem fallback legado; o commit usa sidecars `.tmp`/`.bak` derivados por `make_transaction_paths` no diretório `/sdcard/wifi-audit/`, O_EXCL + fsync + renomeação em sequência, com nome GMT-3 `wifi-audit-YYYYMMDD-HHMMSS.txt` e colisão fail-closed e recuperação stale segura, sem prometer atomicidade POSIX de replace; se o rollback falhar, candidato e backup são preservados. Falhas de entrega por fila cheia usam um slot de fallback bounded. No firmware, a transação nativa roda em uma task curta `wifi_audit_io` com deadline de 2 s; o worker aguarda apenas esse limite, mantém ownership do request até a liberação e não compartilha adapter/sink com a UI. Uma operação VFS que não retorna fica quarentenada sem permitir um segundo acesso concorrente aos sidecars; a UI recebe `failed` em vez de permanecer pendente. |
| `components/cyberdeck/src/apps/wifi/cyberdeck_wifi_audit_persistence.cpp`, `components/cyberdeck/include/apps/wifi/cyberdeck_wifi_audit_persistence.h` | `cyberdeck_wifi_audit_persistence::{file_ops, completion_sink, audit_persistence}` | Seam host injetável para a transação durável da auditoria: criação do diretório `/sdcard/wifi-audit/`, `open_exclusive`/write parcial/`fsync`/`close`/rename, recovery fail-closed dos sidecars `.tmp`/`.bak`, rollback com preservação quando a restauração falha, publicação no-clobber para o destino final, fila single-shot bounded, guard de ACK e retenção de completion quando o sink rejeita a entrega. O adapter ESP/FatFs é wired pelo backend `wifi_audit_io` de `cyberdeck_wifi_audit.cpp`; o seam e o sink têm ownership exclusivo da task, enquanto o controller bounded apenas aguarda a completion. O teardown e a corrida entre `enqueue`/`pump_one`/`drain` são serializados pelo seam; o contrato host fica em `tests/host/keymap/contracts/cyberdeck_wifi_audit_persistence.h`. |
| `components/cyberdeck/src/platform/networking/cyberdeck_net_coordinator.cpp` | `cyberdeck_net_coordinator` | Gating de timers e teardown idempotente de Wi-Fi/SSH. |
| `tests/host/keymap/test_net_session_ipc_contract.py` | contrato estrutural IPC SSH/Wi-Fi | Verifica eventos tipados bounded, fila de quatro itens, descarte nao bloqueante, consumo no `net_worker` e ausencia de chamadas/includes diretos ou montagem de storage no cliente SSH; incluido no agregado host. |
| `tests/host/keymap/test_wifi_evt_worker_lifecycle_contract.py` + `tests/host/keymap/Makefile` | contrato estrutural do rollback do `wifi_evt_worker` | Confirma criação dos sinais de stop/quiescencia, wake, join antes de destruir recursos e retenção/quarentena em timeout. Cobre somente o sub-recorte seguro do rollback; não cria stop público nem altera `net_worker`/Screenshot. |

O callback de `GOT_IP` somente publica snapshot. Trabalho pesado, SNTP,
persistencia e notificacao da UI ocorrem no contexto apropriado. Tokens
monotônicos impedem callbacks atrasados de alterar uma tentativa nova.

### Bluetooth LE no ESP32-C6 hospedado (REQ-BLE-001..014)

Implementacao revisada: `address_type` em `cyberdeck_ble_types`/acoes/eventos/store

O lifecycle do `ble_mgr` agora possui uma barreira de quiescencia separada do
ACK de STOP: a task só sinaliza após sua última operação em filas, mutexes e
estado de lifecycle, e `ble_mgr_stop` só executa NimBLE teardown e destrói os
recursos depois dessa confirmação. Em timeout, os handles são preservados.
preserva public, random static, RPA e NRPA. `ble_mgr` usa scan NimBLE de 5000 ms
com parametros zero e filtro de duplicatas, parse bounded por `ble_hs_adv_parse_fields` e
agregacao ADV/SCAN_RSP por endereco + tipo; somente o anuncio primario define
`connectable`.

O radio BLE nao e do ESP32-P4: e do coprocessor ESP32-C6 alcancado por
`esp_hosted` (VHCI/HCI), e a stack host roda no P4. Nenhum modulo BLE toca
`lvgl.h`, FreeRTOS, NVS ou BSP, exceto o adaptador. Bluetooth Classic (BR/EDR)
esta fora do escopo e e rejeitado por contrato.

| Arquivo | Simbolos/contrato | Papel |
| --- | --- | --- |
| `components/cyberdeck/include/apps/bluetooth/cyberdeck_ble_types.h`, `components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_types.cpp` | `k_max_name_bytes`, `k_max_address_bytes`, `k_max_devices`, `k_min_rssi`/`k_max_rssi`, `k_passkey_digits`/`k_passkey_modulus`, `k_unnamed_placeholder`, constantes de appearance do Bluetooth SIG, `device_kind`, `device`, `kind_from_appearance`, `kind_label`, `normalize_address`, `sanitize_name`, `display_name`, `clamp_rssi`, `parse_passkey`, `format_passkey`, `mask_passkey`, `device_list` | Tipos puros e modelo de listabounded. Classificacao por `appearance` apenas quando provavel (teclado `0x03C1`, mouse `0x03C2`, fone de ouvido `0x0401`/`0x0408`/`0x0418`/`0x0419`/`0x041A`/`0x041B`); `0x03C0` (HID generico), joystick, gamepad, `0x0400` e qualquer outro valor viram `unknown` em vez de adivinharem. `sanitize_name` converte C0/C1, `U+007F`, `U+2028`/`U+2029` e byte UTF-8 invalido em um unico espaco, limita a 32 bytes e nunca corta uma sequencia UTF-8. `device` nao possui campo de chave, ligacao ou IRK, portanto listagem, snapshot, log e store nao conseguem vazar segredo por construcao. `device_list` deduplica por endereco (RSSI mais forte, primeiro nome nao vazio, `paired`/`connectable` monotonicos), limita a 32 entradas e renderiza `Nome (Tipo, -55 dBm)` de forma deterministica. |
| `components/cyberdeck/include/apps/bluetooth/cyberdeck_ble_state_machine.h`, `components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_state_machine.cpp` | `k_scan_timeout_ms` (10000), `k_pair_timeout_ms`/`k_auth_timeout_ms` (30000), `k_connect_timeout_ms` (20000), `k_max_reconnect_attempts` (3), `screen`, `key`, `notice`, `auth_request_kind`, `pair_outcome`, `action_kind`, `action`, mensagens `k_msg_*`/`k_status_*`, `state_machine`, `owns_input()`, `is_connected()`, `arm_background_reconnect()`, `block_background_reconnect()`, `background_reconnect_armed()`, `has_background_target()`, `background_target()`, `consume_background_attempt()`, `reset_background_cycle()` | Modelo de tela voltado ao usuario, sem I/O e sem chamada de stack, portanto nao pode bloquear a task LVGL. Conclusoes de conexao usam token + marcador in-flight mesmo quando uma busca simultanea muda a tela; apos `connection_finished(token, true)`, a tela volta a `idle` e libera o prompt sem emitir disconnect, enquanto o manager mantem o link, contexto HID e bond; o callback fisico posterior de desconexao continua aceito apenas para o token estabelecido. Sucesso de conexao arma o ciclo background para o peer (`background_armed`, alvo e ciclo zerados); perda espontanea mantem o ciclo armado sem emitir notice; Escape em `connected` ou em `idle` com link ativo (desconexao manual) bloqueia o ciclo ate Enter explicito; `local_key` roteia esse Escape mesmo sem ownership (fora de modais Wi-Fi/SSH/senha) para que a acao `disconnect` alcance o manager; Enter em `paired` re-arma para o bond escolhido. `arm_background_reconnect` (boot/perda espontanea) registra o bond sem tocar tela/tokens/notices; `consume_background_attempt` aplica o teto de 3 por ciclo sem desarmar ao esgotar (novo anuncio via `reset_background_cycle` reabre sem re-arme manual); `block_background_reconnect` mantem bloqueio manual, que so Enter explicito limpa. Callbacks terminais nao geram cancelamentos duplicados, enquanto deadlines geram exatamente a acao de cancelamento correspondente; cancelamento e disconnect preservam endereco/token e `addr_type` exatos (inclusive random/private), inclusive apos `schedule_reconnect`, que copia o tipo para o campo ativo usado por `cancel_connect`; e notices distinguem origem de pareamento, conexao, reconexao e selecao nao conectavel. ENTER em dispositivo nao conectavel mantem results, nao emite acao e publica uma notice fixa/bounded uma unica vez pela deduplicacao existente da UI. `status_line()` usa `display_name()` bounded/sanitizado e o teto de reconexao nao emite nova action. `owns_input()` e o predicado puro de ownership; results/paired vazios liberam o terminal apos a mensagem final. `is_connected()` e o predicado publico somente-leitura de link estabelecido (reflete `connection_established`, sem mutacao), consumido apenas por `refresh_ble_status()` no contexto LVGL para mostrar/ocultar o glyph `LV_SYMBOL_BLUETOOTH` do header. |
| `components/cyberdeck/include/apps/bluetooth/cyberdeck_ble_event_dispatch.h`, `components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_event_dispatch.cpp` | `k_max_pending_events` (8), `ble_event_kind`, `ble_event`, `event_dispatch`, `begin_scan`/`begin_pairing`/`begin_connection` (incluindo ativacao com token externo), `publish_*`, `drain`, `drop_stale`, `reset`, `event_summary` | Seam entre o callback da stack e o modelo de tela. Publica somente snapshots bounded, com fila limitada e overflow fail-closed; o adaptador pode ativar cada geracao com o token emitido pelo modelo, eliminando dominios shadow. Tokens monotônicos compartilhados impedem que callback obsoleto ou de outra geração seja aplicado. `drop_stale` conserva a geracao ativa e a imediatamente anterior por tipo, sem que pairing invalide scan; conexoes copiam endereco e marcador automatico para o snapshot. `event_summary` inclui nome sanitizado/bounded e nunca contem passkey, PIN, link key ou IRK. |
| `components/cyberdeck/include/apps/bluetooth/cyberdeck_ble_background.h`, `components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_background.cpp` + `tests/host/keymap/test_ble_background.cpp` | `cyberdeck_ble_background::scheduler`, `k_window_interval_ms` (10000), `k_restore_retry_limit` (3), `k_token_base` (1<<63) | Observador background de reconexao: restaura bonds NVS uma vez no boot (somente o `last_connected` arma o primeiro ciclo), observa anuncios em janelas de 10 s, conecta apenas peers conhecidos, limita a 3 tentativas por ciclo e reabre com novo anuncio. Opera sobre `cyberdeck_ble::state_machine&` e enfileira comandos `ble_mgr` (scan/cancel); nunca toca LVGL, terminal ou tela. A UI mantem o pump (`on_ble_event`/`process_ble_events`/`ble_submit_actions`) e apenas roteia eventos e preempcao manual para o scheduler; `test_ble_background` cobre o scheduler host. |
| `components/cyberdeck/include/apps/bluetooth/cyberdeck_ble_store.h`, `components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_store.cpp` | `k_max_bonds` (16), `k_max_record_bytes` (96), `k_max_store_bytes` (2048), `k_bond_magic` (`CDB1`), `bond_record`, `store_result`, `encode_bond`, `decode_bond`, `bond_store` (`add`/`update`/`remove`/`find`/`snapshot`/`serialize`/`deserialize`/`clear`) | Persistencia logica de bonds, sem ESP-IDF, NVS ou FATFS; o registro e `addr` + `addr_type` + `name` + `kind` + `last`, nessa ordem fixa. O tipo preserva public/random static/RPA/NRPA; nao existe campo para chave, IRK, LTK ou passkey, e payload com campo desconhecido e rejeitado fail-closed. `deserialize` e atomico. |
| `components/cyberdeck/include/apps/bluetooth/ble_mgr.h`, `components/cyberdeck/src/apps/bluetooth/ble_mgr.cpp` | `ble_mgr_start`, `ble_mgr_stop`, `ble_bond_snapshot_t`, `ble_bonds_copy` | Adaptador ESP-IDF, unico modulo autorizado a falar com a stack BLE do C6 via `esp_hosted`. `ble_mgr_start` executa estritamente `esp_hosted_connect_to_slave()` -> `esp_hosted_bt_controller_init()` -> `esp_hosted_bt_controller_enable()` -> `nimble_port_init()`, registra cada retorno e mantém o boot não fatal; após habilitar o controlador consulta e registra `esp_hosted_get_coprocessor_fwversion()` quando disponível. Falha de transporte/controlador encerra a tentativa sem iniciar NimBLE, sem retry/reset loop. Possui task FreeRTOS dedicada e fila bounded; a UI apenas enfileira. Pair/connect usam o endereco e token da acao, a seguranca inicia por `ble_gap_security_initiate` e so callbacks reais publicam conclusoes; cancelamento/desconexao usam terminacao bounded, OOB e passkeys invalidos falham fechados. O ciclo de vida separa o cancelamento de pairing da conexao autenticada promovida apos o bond: disconnect fisico publica DISCONNECTED real e permite nova conexao. Callbacks GAP e comandos de cancelamento/autenticacao validam a geracao ativa; callbacks stale nao alteram estado nem publicam. Bonds existentes sao atualizados, nao duplicados. `ble_bonds_copy` expõe snapshot bounded de identidade (addr+tipo+nome/kind/last, sem segredo) sob `s_dispatch_mutex` com espera curta e fail-closed, para restauracao NVS no boot pelo observador da UI. O `event_dispatch` fica protegido por mutex entre a task `ble_mgr` e callbacks GAP; a task host nomeada `ble_host` usa stack de 8192 bytes e o drain bounded continua fora da UI. Para o scan, o adaptador infere o tipo de endereco com `ble_hs_id_infer_auto(0, ...)` em vez de fixar `BLE_OWN_ADDR_PUBLIC`, registra inicio/fim e mantem contadores bounded de GAP DISC, cinco tipos aceitos, ignorados, stale/mutex, publicacoes, descartes de dispatch e malformed sem payloads irrestritos. Relatorios `BLE_GAP_EVENT_DISC` dos cinco tipos de anuncio LE (incluindo respostas de scan) sao encaminhados a `scan_report_adv`; tipos desconhecidos sao ignorados. Falha de inicializacao e nao fatal no boot. O teardown `nimble_port_freertos` permanece como follow-up fora desta tarefa. |
| `components/cyberdeck/include/apps/shell/cyberdeck_shell_utils.h`, `components/cyberdeck/src/apps/shell/cyberdeck_shell_utils.cpp` | `CYBERDECK_CMD_BLUETOOTH_SEARCH`, `CYBERDECK_CMD_BLUETOOTH_PAIRED`, `CYBERDECK_CMD_LOG`, `"bluetooth search"`, `"bluetooth paired"` | Parser roteia exatamente os dois subcomandos aprovados; o verbo nu e qualquer operando extra continuam `CYBERDECK_CMD_UNKNOWN`. O comando `log` preserva os argumentos para o override de linhas por sessao. Nao ha subcomando para Bluetooth Classic. |
| `components/cyberdeck/include/apps/shell/cyberdeck_shell_help.h` | linha `bluetooth [search|paired]` | Catalogo unico de ajuda (16 entradas), com a linha bluetooth entre `battery` e `ssh`; o `static_assert` de descricoes em `cyberdeck_shell_utils.cpp` acompanha a ordem. |
| `tests/host/keymap/contracts/cyberdeck_ble_types.h` | ABI de `cyberdeck_ble_types.h` | Fixture de teste, nao e implementacao de producao. |
| `tests/host/keymap/contracts/cyberdeck_ble_state_machine.h` | ABI de `cyberdeck_ble_state_machine.h` + mensagens `k_msg_*`/`k_status_*` | Fixture de teste; fonte unica das mensagens de terminal e dos deadlines. |
| `tests/host/keymap/contracts/cyberdeck_ble_event_dispatch.h` | ABI de `cyberdeck_ble_event_dispatch.h` | Fixture de teste. |
| `tests/host/keymap/contracts/cyberdeck_ble_store.h` | ABI de `cyberdeck_ble_store.h` | Fixture de teste. |
| `tests/host/keymap/contracts/cyberdeck_help.h` | `kUnifiedHelpText` | Fixture compartilhada; agora inclui a linha `bluetooth` aprovada. E a oracle byte a byte de `test_shell_utils.cpp` e `test_help_unification.cpp`, incluindo a linha `screen [on|off|timeout <0-1440>]`. |
| `components/cyberdeck/src/platform/display/cyberdeck_ui.cpp` + `components/cyberdeck/src/apps/bluetooth/ble_mgr.cpp` | `test_ble_ui_timeout_contract.py`, `test_ble_adv_types_contract.py` | Contratos estruturais de regressao para o gate de timeout/renderizacao da UI BLE e para o scan: `advance_time(100)` ocorre exatamente uma vez, incondicionalmente em todo tick antes da fila, enquanto o snapshot de ownership continua governando apenas renderizacao; tambem cobre inferencia do endereco local antes de `ble_gap_disc`, cinco tipos de anuncio LE, anuncios sem nome/malformed bounded e diagnostico fixo sem payload/segredos; ambos sao executados pelo alvo agregado `make -C tests/host/keymap test`. |
| `components/cyberdeck/src/apps/bluetooth/ble_mgr.cpp` + modelos BLE puros | `test_ble_scan_contract.py`, `test_ble_review_regressions.py`, `test_ble_types.cpp`, `test_ble_event_dispatch.cpp`, `test_ble_store.cpp` | Contrato host dos parametros BLE (5000 ms, interval/window zero e filter_duplicates=1), parser oficial bounded, filas/snapshots GAP bounded com `DISC_COMPLETE` terminal não descartável, agregacao ADV/SCAN_RSP sem promover connectable, identidade por endereco+addr_type (public/static/RPA/NRPA), propagacao em eventos/conexao, resultado terminal preservado quando a fila visual excede 8, os tres `ble_gap_connect` usando tipo local inferido, teardown cooperativo, lookup tipado e política `bonds_v2`/legado. O alvo estrutural e os binarios relacionados sao executados isoladamente por `make -C tests/host/keymap test_ble_review_regressions test_ble_scan_contract test_ble_types test_ble_event_dispatch test_ble_store`. |
| `components/cyberdeck/src/apps/bluetooth/ble_mgr.cpp` + `cyberdeck_ui.cpp` + modelos BLE puros | `test_ble_auth_contract.py`, `test_ble_state_machine.cpp`, `test_ble_event_dispatch.cpp` | Contrato host da Fase 1 de autenticacao: valida `DISP`/`INPUT`/`NUMCMP` conforme NimBLE 5.5.5, com decisão booleana por `numcmp_accept` e número NUMCMP somente para exibição, sem inventar `BLE_SM_IOACT_CONFIRM` nem exigir que o host determine a passkey correta; valida gate `host_synced`, formato/comprimento da entrada, stale/duplicado, OOB fail-closed, token/conn_handle/addr_type e consumo one-shot, limpeza, sigilo e deadlines. A Fase 2 HID é coberta pelo contrato dedicado. Executado isoladamente por `make -C tests/host/keymap test_ble_auth_contract test_ble_state_machine test_ble_event_dispatch`. |
| `components/cyberdeck/src/apps/bluetooth/ble_mgr.cpp` + `cyberdeck_ble_event_dispatch.cpp` | `test_ble_hid_contract.py`, `test_ble_event_dispatch.cpp` | Contrato host da descoberta GATT HID: serviço `0x1812`, características relevantes e limites de 8 characteristics/4 CCCDs; contexto `conn_handle` + token/geração com callbacks stale-safe; o recorte do switch usa o `SCAN_START` real da task (não o helper de rejeição pre-sync); desconexão/STOP invalidam a fase; `CONNECTED` é publicado antes do evento adicional `HID_DISCOVERY`; snapshots e diagnósticos não carregam payload, segredo ou valores de atributos; não há parser de report-map, notificações ou injeção de teclas. Executado isoladamente por `make -C tests/host/keymap test_ble_hid_contract test_ble_event_dispatch` e no agregado `make -C tests/host/keymap test`. |
| `components/cyberdeck/src/apps/bluetooth/ble_mgr.cpp` + `cyberdeck_ui.cpp` | `test_ble_runtime_regressions.py` | Contratos host mínimos da regressão de scan preso: preempção só cancela scan ativo, `DISC_COMPLETE` mantém término e recupera slot do dispatch, `BLE_HS_EALREADY` no cancelamento publica término cancelado porque não haverá callback, erro real permanece falha sem liberar reconnect, `advance_time(100)` é chamado incondicionalmente em todo tick antes de observer/fila (com snapshot de ownership anterior ao avanço), resultado não vazio renderiza a lista pelo repaint comum mesmo com `notice_text()` vazio, resultados vazios/timeout preservam o aviso e o prompt volta após liberar ownership; observer é copiado sob mutex e chamado fora da seção crítica; logs de cancelamento/término/observer permanecem fixos, bounded e sem payload/segredo. Executado isoladamente pelo alvo `make -C tests/host/keymap test_ble_runtime_regressions`. |
| `cyberdeck_ble_state_machine.cpp` + `ble_mgr.cpp` + `cyberdeck_ui.cpp` | `test_ble_connection_lifecycle_contract.py`, `test_ble_state_machine.cpp` | Regressão do ciclo pós-conexão: `connection_finished(true)` libera tela/ownership sem ação `disconnect`, o manager retém geração, endereço e `addr_type`, desconexão real/stale não contamina nova geração, reconnect preserva identidade, prompt `/$`/shell/Wi-Fi/SSH permanece disponível e a descoberta HID segue independente. Executado isoladamente por `make -C tests/host/keymap test_ble_connection_lifecycle_contract test_ble_state_machine`. |
| `components/cyberdeck/src/platform/display/cyberdeck_ui.cpp` + `cyberdeck_ble_types.cpp` + `cyberdeck_ble_state_machine.cpp` + `cyberdeck_ble_event_dispatch.cpp` | `test_ble_ui_render_contract.py`, `test_ble_types.cpp` | Contrato host da lista BLE transitória e do status: renderização consulta `state_machine::devices()` atual, marcador deriva da seleção do `device_list`, notice é acrescentado uma única vez por mudança, `advance_time(100)` permanece antes do processamento e setas continuam roteadas, consolidação no histórico ocorre uma única vez ao liberar ownership, ENTER continua roteado ao modelo apenas no path genérico results/paired (o early-return legítimo de auth INPUT/BACKSPACE/ENTER permanece coberto separadamente por `test_ble_auth_contract.py`), prompt local reaparece e Wi-Fi/shell/SSH permanecem fora do ownership BLE; pairing/connecting/connected usam `status_line()` apenas no repaint, auth mostra somente máscara temporária de INPUT, requests não entram em notice/output/history e o resumo de log mascara a passkey. O teste comportamental adiciona 18 itens e verifica seleção/marker nos limites. Executado isoladamente por `make -C tests/host/keymap test_ble_ui_render_contract test_ble_types` e no agregado `make -C tests/host/keymap test`. |

| `components/cyberdeck/src/platform/display/cyberdeck_ui.cpp` + `cyberdeck_font.c` + `cyberdeck_ble_state_machine.cpp` | `test_ble_header_contract.py`, `test_ble_state_machine.cpp::test_is_connected_is_false_until_matching_connected_event` | Contrato host do indicador Bluetooth no header: celula wrapper `s_ble_cell` (`LV_SIZE_CONTENT` x 42, layout none, padding 0, `flex_grow=0`, fundo transparente, pad direito 4 px) com label interno `s_ble_status` (`LV_SIZE_CONTENT` content, `LV_SYMBOL_BLUETOOTH`/U+F293, `lv_obj_set_y` com `CYBERDECK_BLE_HEADER_Y_OFFSET` para o centro visual do Wi-Fi), nasce oculta junto do label, fica antes do Wi-Fi e da bateria sem crescimento, a bateria fixa `pad_left=11` para preservar o gap Wi-Fi→bateria de 15 px, e a visibilidade deriva exclusivamente de `is_connected()` aplicada a celula e ao label. Rastreabilidade da regressao: BUG_REPORT (Bluetooth y=14..32 vs Wi-Fi y=25..41) -> BUG_EVIDENCE (desalinhamento visual medido) -> REQ (centralizar o glyph sem alterar a logica BLE) -> correcao (wrapper content-sized/layout none + offset nomeado) -> TEST (`test_ble_header_contract.py`). O timer BLE avança o modelo a cada 100 ms, processa CONNECTED/DISCONNECTED e atualiza o indicador sem depender de ownership; o teste puro cobre estado inicial oculto, callback válido, callbacks stale e desconexão. Executado isoladamente pelos alvos `test_ble_header_contract` e `test_ble_state_machine`, e integrado ao gate agregado `make -C tests/host/keymap test` após o contrato de renderização BLE e antes dos contratos de autenticação/HID. |
| `components/cyberdeck/src/platform/display/cyberdeck_ui.cpp` + `ble_mgr.cpp` + `cyberdeck_ble_state_machine.cpp` | `test_ble_background_contract.py`, `test_ble_state_machine.cpp::test_background_bond_reconnect_cycles_are_bounded_and_explicit` | Testes de restauração de bonds no boot e reconexao background: NVS `bonds_v2`, alvo somente do ultimo bond conectado, janelas periódicas com 10 s entre fechamentos, scan sem pareamento/sem conexao automatica, matching por endereco+`addr_type` apenas de bonds conhecidos, perda espontanea mantendo o ciclo, teto de tres tentativas por anuncio, novo anuncio reabrindo o ciclo, bloqueio apos desconexao manual ate Enter explicito e rejeicao de callbacks/token stale. A conexão automática cancela discovery antes de conectar e falhas retornam ao observador sem notice interativa. Executado isoladamente por `make -C tests/host/keymap test_ble_background_contract test_ble_state_machine`; nao acessa hardware, radio, simulador ou ponte serial. |
| `tests/host/keymap/test_ble_background_contract.py` + `tests/host/keymap/test_ble_state_machine.cpp` | ciclo de refinamento 2 da reconexao background BLE | O contrato estrutural confirma o intervalo periodico de 10 s sem spin, rearmamento apos `SCAN_FINISHED`, isolamento/cancelamento antes de reconnect, descarte de callbacks stale, nao interferencia de scan/pair/connect manual, matching por endereco+`addr_type`, tokens e gate agregado; o teste comportamental consome exatamente tres tentativas, reabre somente por novo anuncio/ciclo do mesmo peer, preserva `addr_type` e tokens, rejeita callback stale e exige Enter para rearmar apos desconexao manual. |
| `tests/host/keymap/test_ble_bond_name_contract.py` + alvo `test_ble_bond_reconnect` | cobertura focada de bonds/name BLE | Contrato estrutural para restaurar todos os bonds com `last_connected` como primeiro alvo, percorrer os demais em janelas de 10 s, matching por identidade/RPA + `addr_type` sem duplicar, tres tentativas por anuncio/ciclo e bloqueio manual; verifica ausencia de novo PAIR, fila/callbacks/tokens/generation bounded, sanitizacao ADV/SCAN_RSP antes/durante pairing, persistencia NVS, restauracao e exibicao em `bluetooth paired`, mantendo nome vazio como `unnamed`. Os dois alvos sao executados no agregado `make -C tests/host/keymap test`, sem remover os alvos individuais. |

A listagem mostra nome e tipo (`Keyboard`, `Headset`, `Mouse`, `Unknown`), em
ingles como todas as demais strings de terminal do firmware; nome ausente vira
`(unnamed)` e dois perifericos com o mesmo nome so sao distinguidos pelo
endereco. A navegacao usa Cima, Baixo, Enter e Escape. Mensagens de terminal
sao exatamente uma por classe: lista vazia, falha, timeout e cancelamento, para
scan, pareamento e conexao. O scan e assincrono e toda espera e bounded por
`advance_time`; nenhum modulo BLE puro importa ESP-IDF, FreeRTOS, LVGL, NVS ou
FATFS, e o log nunca recebe passkey ou material de chave.

`state_machine::connection_finished()` captures the background-attempt marker
before clearing in-flight flags; a background failure returns silently to
`idle`, without interactive results or an `owns_input()` dependency, leaving
the bounded retry/rearm cycle available.

### BLE scan serialization

`ble_mgr.cpp` mantém `host_synced` explícito: o callback de sync garante a
identidade local, infere e armazena uma única vez o `own_addr_type` real (com
fallback publico deterministico somente quando a inferencia falha) e só então
libera GAP. `ble_hs_util_ensure_addr` e `ble_hs_id_infer_auto` existem somente
nesse callback; scan e connect reutilizam `s_own_addr_type`. O dispatcher da
task aplica um único gate radio-ready a todos os comandos GAP (scan,
cancelamento, pair, connect, reconnect, disconnect, `conn_cancel`,
`PASSKEY_REPLY` e respostas de segurança); antes do sync cada comando é
rejeitado com evento terminal bounded/fail-closed, sem fila genérica pré-sync e
sem tocar NimBLE. STOP/lifecycle permanece executável nesse estado, e o gate é
resetado no inicio e no stop.

Callbacks GAP copiam snapshots bounded para filas separadas de reports e eventos
terminais; não esperam `s_dispatch_mutex` nem chamam dispatch/GAP. A task
`ble_mgr` processa primeiro os reports e depois a fila terminal, preservando
`DISC_COMPLETE` sob burst e mantendo todas as chamadas GAP fora do mutex.

`ble_mgr.cpp` serializa preempcao de discovery como `SCAN_CANCEL -> DISC_COMPLETE
-> SCAN_START`: enquanto o cancelamento GAP esta pendente, uma nova geracao nao
inicia scan; `DISC_COMPLETE` valida o token capturado e a task libera
explicitamente a proxima geracao. Se `ble_gap_disc` retornar `EALREADY`, o
adaptador tenta cancelar e consulta `ble_gap_disc_active()` antes de publicar o
termino de falha do token atual. Cancelamento de token stale e ignorado porque o
terminal da geracao anterior ja foi publicado; nenhum evento e emitido com token
obsoleto e a geracao ativa nao e afetada.
O caminho preserva observer de 10 s, bonds/RPA/`addr_type`, nomes persistidos,
tres tentativas, novo anuncio, bloqueio manual, filas bounded e callbacks.

Os contratos `test_ble_scan_contract.py` e `test_ble_runtime_regressions.py`
cobrem a inferencia segura de `own_addr_type` no sync (fora do helper de start),
a rejeicao terminal pre-sync sem GAP e a liberacao de uma unica geracao, alem dessa
ordem, do bloqueio durante cancelamento, `EALREADY` somente sem scan ativo,
terminal legitimo com token/generation atual e o fluxo interativo de anuncios
seguido de `DISC_COMPLETE`; `test_ble_background_contract.py` e
`test_ble_bond_name_contract.py` cobrem as janelas de 10 s, RPA/addr_type,
nomes/bonds, tres tentativas e bloqueio manual.

`test_ble_pre_sync_contract.py` cobre especificamente PAIR, CONNECT,
RECONNECT, scan/cancel, `PASSKEY_REPLY`, pair cancel e disconnect/`conn_cancel`
antes de `host_synced`: comandos são rejeitados terminalmente pelo gate central,
sem fila genérica e sem tocar GAP; sync libera uma única geração, e callbacks
stale/eventos terminais continuam bounded e fail-closed. Os contratos
`test_ble_scan_contract.py`, `test_ble_pre_sync_contract.py`,
`test_ble_auth_contract.py` e `test_ble_hid_contract.py` acompanham a
implementação simplificada e permanecem no agregado do Makefile. Executado
isoladamente por
`make -C tests/host/keymap test_ble_pre_sync_contract` e no alvo agregado
`make -C tests/host/keymap test`.

### Screenshot HTTP

### BLE authentication and HID follow-up

Delta de produção desta implementação: `ble_mgr.cpp` usa janelas NimBLE de
10 s, resolve RPA com `ble_gap_rpa_resolve` quando a identidade está disponível,
mantém tokens/gerações e agrega o nome sanitizado de ADV/SCAN_RSP no bond antes
de `save_bonds_to_nvs`. O observador de background percorre os bonds restaurados
em janelas bounded, começando pelo `last_connected`, sem iniciar novo pairing;
desconexão manual continua bloqueando o ciclo.

O scheduler background BLE mantém a janela periódica de 10 s também quando
`SCAN_START` não pode ser enfileirado, e só agenda reconnect após o término
confirmado do scan cancelado; falhas de cancelamento não iniciam conexão nem
entram em spin. A restauração de bonds retrya de forma bounded quando o snapshot
sofre contenção, e uma conexão normaliza a persistência para um único
`last_connected`, preservando o `addr_type`.

No término de um scan background preemptado manualmente, o callback também limpa
explicitamente o pending de reconnect e o dispositivo reservado antes de liberar
o marcador de preempção, impedindo que um término posterior reutilize um anúncio
antigo.

The authentication handoff preserves only the NimBLE IO actions (`DISP`,
`INPUT`, and `NUMCMP`) through the transient event and command rather than
inventing a confirmation action or inferring the action from a passkey. The UI
uses a bounded six-digit input only for `INPUT`; display and numeric comparison
challenges are accepted with explicit ENTER. NUMCMP carries its displayed
number separately from `numcmp_accept`; the number is never injected as a
passkey. Tokens, address type, connection handle, challenge action and
one-shot consumption are checked by `ble_mgr`; transient passkey buffers are
wiped on completion, cancellation, timeout, or failure and are not persisted
or logged. OOB and malformed/stale/duplicate replies fail closed, while the
actual authentication result remains NimBLE's decision.
GATT HID service discovery after authentication remains pending for a separate
bounded asynchronous phase; connection readiness is intentionally not delayed.
Fase 2 agora inicia em `ble_mgr.cpp` após a conexão promovida: descoberta
assíncrona do serviço HID 0x1812, características relevantes e até quatro
CCCDs, com contexto `conn_handle` + token/geração, deadline de 10 s e callbacks
stale-safe. Os callbacks GATT aceitam `BLE_HS_EDONE` com objeto NULL como
término nominal e serializam o contexto HID/dispatch com mutex curto; também o
poll de deadline usa o mesmo lock, mantendo falha fechada para demais erros.
Desconexão/stop invalidam a geração e publicam um evento adicional de
readiness/indisponibilidade sem atrasar ou invalidar CONNECTED. Não há leitura
de report map, notificações ou injeção de teclas.

| Arquivo | Simbolos/contrato | Papel |
| --- | --- | --- |
| `components/cyberdeck/src/apps/screenshot/screenshot_server.cpp` | `screenshot_server_init`, `screenshot_server_set_display_port`, `screenshot_server_wifi_state`, `screenshot_frame_t` | Servidor `GET /screenshot` na porta 80, iniciado/parado por estado Wi-Fi. Serializa BMP a partir de um frame RGB565 bounded recebido pela porta de display; nao inclui LVGL/BSP nem captura a tela diretamente. |
| `components/cyberdeck/include/platform/display/cyberdeck_display_port.h`, `components/cyberdeck/src/platform/display/cyberdeck_display_port.cpp` | `cyberdeck_display_capture`, `cyberdeck_display_release` | Adapter exclusivo de LVGL/BSP para a porta de display do Screenshot; faz lock, snapshot, copia bounded do frame e libera recursos. |
| `components/cyberdeck/src/apps/screenshot/screenshot_bmp.cpp` | geracao de BMP | Converte framebuffer RGB565 em BMP 24-bit bottom-up com stride/padding. |

O endpoint serializa requisicoes, usa lock de display somente durante o
snapshot e aceita apenas peers locais: loopback, 10/8, link-local, ULA,
IPv6 local e IPv4-mapped em IPv6.

### Ponte manual USB Serial-JTAG NDJSON (REQ-002/003/005/006/007/008/009)

| Arquivo | Simbolos/contrato | Papel |
| --- | --- | --- |
| `components/cyberdeck/include/apps/serial/cyberdeck_serial_bridge.h` | `cyberdeck_serial::k_max_ndjson_line=4096`, `k_max_rid_len=64`, `k_screen_chunk_bytes=1024`, `k_fs_write_max_bytes=2048`, `dispatch_error`, `request/response/dispatch_result`, `SysInfo/WifiNet`, `LineAssembler`, `crc32/screen_bmp_size/screen_chunk_bounds/screen_dump_*`, `sys_info_* / wifi_scan_* / handle_* / dispatch_one / build_envelope`, `bridge_start`/`bridge_stop` (`#ifdef ESP_PLATFORM`) | Contrato NDJSON bounded e lifecycle da task `serial_brg`: start aguarda prontidão, stop solicita saída e faz join bounded sem desmontar driver, hook global ou sincronização compartilhada. `fs.write` seguro com path confinado, 2048 decoded, Base64 canonico, commit por temp unico no mesmo diretorio, criado com O_CREAT|O_EXCL e removido apenas apos create bem-sucedido, +rename (replace atomico no host; no-clobber no ESP/FATFS) e NDJSON errors. Header puro (sem `esp_err.h`). |
| `components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp` | parser JSON proprio (`parse_value` classifica e decodifica cada valor numa unica passada: string vira `kind::string` com `s` decodido e `raw` literal com aspas), dispatch, sessao BMP/CRC, LineAssembler; `term.dump` reutiliza a API textual bounded da UI, com JSON escaping e envelope <=3500; `fs.write` (campos `path/data_b64/size` no `k_request_fields`, tipo conhecido `fs.write`, validacao rid/type/path/data_b64/size, path safety traversal/symlink/directory/parent, 2048, Base64 strict canonico `b64_decode_canonical`, temp unico `.fswrite.<sequencia>.tmp` no mesmo diretorio, criado com O_CREAT|O_EXCL e removido apenas apos create bem-sucedido, +rename sem unlink do destino, CRC; no ESP exige `O_NOFOLLOW` e limita `/sdcard` ao VFS FATFS sem symlink/openat, valida parents e o descriptor temporario, rejeita overwrite existente porque nao ha garantia de replace atomico, e preserva o destino em falha de rename; no host a raiz logica `/sdcard` e reancorada em sandbox `/tmp/cyberdeck5_sd` via `CYBERDECK_SD_ROOT`); device: task `serial_brg` com handle real, handshake de prontidao, stop cooperativo/join bounded e estado fail-safe, driver USB Serial-JTAG, `esp_log_set_vprintf` com mutex, hooks UI/wifi/screen/fs | Dispatch NDJSON sem alocacao >4096, validacao UTF-8/rid/type, `screen.dump` byte-identical em chunks 1024 com CRC IEEE e flag `end`, feeder tolerante a logs/fragmentacao, `sys.info`/`wifi` e `term.dump`; `fs.write` com validacao completa, commit seguro e erros tipados `invalid_path`/`invalid_payload`/`io_error`. Autocontido em relacao a `screenshot_bmp_*` fora de `ESP_PLATFORM`: os tres testes que nao linkam `screenshot_bmp.cpp` nao veem undefined de funcoes externas (stride/size/header/conv sao espelhados localmente com formulas identicas). |
| `tools/cyberdeck_cli.py` | `CyberdeckSession`, `open_session/connect`, `exchange`, comandos `ping/sys.info/wifi.status/wifi.scan/ui.click/ui.tap/ui.type/ui.dump/term.dump/ui.clear/screen.shot/screen.dump/fs.write`, `build_request` com `--data/--data-b64/--input/--stdin/--size/stdin`, `validate_fs_write_path`, `decode_b64_canonical`, `read_fs_write_input`, `FS_WRITE_MAX_BYTES=2048`, encoding Base64 canonico | Cliente NDJSON host: correlacao por `rid` (sem campo `action`), descarte de logs/frames nao-JSON, `term.dump` como resposta unica com snapshot textual bounded, validacao de dump (contiguidade, Base64 canonico, `size`/`chunks`/`crc32` do `start`); `fs.write` com CLI seguro (build_request, input/stdin, 2048, strict canonical, recusa eager de path invalido); pyserial opcional com mensagem clara. |
| `components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp` | O helper de truncamento UTF-8 de `term.dump` pertence ao bloco `ESP_PLATFORM`, pois somente o caminho do dispositivo o utiliza. |
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
| `components/cyberdeck/Kconfig`, `components/cyberdeck/src/platform/logging/event_log.cpp`, `components/cyberdeck/include/platform/logging/event_log_recent.h` | `CYBERDECK_LOG_LINES`, `event_log_init`, `event_log_write`, `event_log_latest`, `rebuild_state`, `insert_recent`, `write_record` | Configura em build-time 1..64 linhas do comando `log` (default 20); o override da sessão pode solicitar até 64. O anel recente mantém capacidade máxima fixa de 64, sem copiar registros para a stack, e a saída seleciona somente a janela solicitada em ordem cronológica. A reconstrução preserva sequência, próximo slot e janela persistida a partir dos registros válidos; cada slot é persistido com `fflush` + `fsync` antes de avançar o estado. A saída converte o epoch UTC para o GMT-3 fixo do header sem depender de `TZ` e usa uptime quando o relógio é inválido; I/O permanece na task do log. |
| `tests/host/keymap/test_event_log_contract.py`, `tests/host/keymap/test_event_log_config_contract.py` | contratos estruturais do event log | Testes host focados (sem link ESP-IDF) para layout/checksum de 256 B, recuperação bounded de slots válidos, janela recente de 20 registros com sequência/slot wrap, capacidade máxima estrutural de 64, ordem `fflush`/`fsync` antes do avanço, formatação UTC em GMT-3, Kconfig default 20/faixa 1..64 e solicitação configurável pelo comando `log`. Executados isoladamente pelos alvos `test_event_log_contract` e `test_event_log_config_contract`, e também pelo agregado `make -C tests/host/keymap test`. |
| `components/cyberdeck/src/platform/logging/event_log_recent.cpp` | `event_log_recent_indices` | Selecao pura dos indices recentes sem copiar todos os registros na stack. |
| `components/cyberdeck/src/platform/sensors/imu_reader.cpp` | `imu_reader_start`, `sensor_handle_t`, `bsp_sensor_init` | Inicializacao do BMI270 via Sensor Hub; `sensor_handle_t` e o handle obrigatorio usado por `bsp_sensor_init`; amostra inicial limitada por timeout, aplicacao da orientacao antes da UI e leitura posterior para rotacao. O contrato proibe somente a leitura direta `imu_acquire_acce(`, preservando o fluxo do Sensor Hub. |
| `components/cyberdeck/src/platform/sensors/orientation.cpp` | `orientation_from_accel`, `orientation_update` | Conversao da aceleracao em rotacao e debounce da orientacao posterior. |
| `components/cyberdeck/src/platform/sensors/battery_status.cpp`, `components/cyberdeck/include/platform/sensors/battery_status.h` | `percentage_from_bus_voltage_mv`, `classify_current_ma`, `classify_sample` | Logica pura recebe `bus_voltage_mv`, satura a janela validada `6000..8230` mV em `0..100` e classifica corrente positiva como descarregando, negativa como carregando e zero/indeterminada como neutral; ausencia so ocorre com presenca explicita e leitura invalida fica indisponivel sem fabricar percentual. |
| `components/cyberdeck/src/platform/sensors/ina226_reader.cpp`, `components/cyberdeck/include/platform/sensors/ina226_reader.h` | `ina226_reader_start`, `ina226_reader_get_snapshot` | Reader INA226 no barramento BSP e endereco I2C `0x41`: registra de tensao `0x02` convertido em mV, configuracao `0x4527`, calibracao `0x0D55`, identificacao, tarefa dedicada a cada 1 s e snapshot protegido por mutex; falhas de leitura apos startup sao publicadas como indisponiveis. Nao usa `CHG_EN`, `CHG_STAT`, NVS ou politica de protecao; o startup e nao fatal. |
| `components/cyberdeck/src/platform/sensors/cyberdeck_battery_protection.cpp`, `components/cyberdeck/include/platform/sensors/cyberdeck_battery_protection.h` | `decode_chg_stat`, `state::observe`, `state::snapshot`, `state::last_safe_snapshot`, `state::set_protection_enabled`, `charger_enabled`, `current_uncertainty_ma`, `external_voltage_mv`, `absent_voltage_mv`, `state_vote_count`, `protection_enter_percentage`, `protection_enter_voltage_mv`, `protection_exit_percentage`, `default_protection_enabled` | Politica pura host-testavel: estados battery/external/charging/absent/unknown, threshold corrente ±15 mA, external >=7900 mV, absent >=8330 mV com 5 votos, e tensao abaixo de 6000 mV tratada como ausencia, todos com 5 votos; protecao so em charging + percentual >=90 + tensao >=8200, histerese retoma <=85, opcao enabled persistida no NVS (default true), falhas nao desligam CHG_EN nem perdem ultimo estado seguro. O snapshot tambem publica o sinal `charge` decodificado. Leitura INA valida atualiza tensao/corrente/percentual/disponibilidade mesmo com `CHG_STAT` invalido (sinal `unknown`, nunca `not_charging`); leitura INA invalida preserva o estado seguro para a protecao, mas publica `available=false` para ocultar a UI ate uma nova leitura valida. A precedencia de ausencia/presenca e resolvida antes do sinal do carregador e da corrente, de modo que um CHG_STAT preso em low nao fabrica uma bateria carregando. Nao usa ESP-IDF, FreeRTOS, I2C, NVS, LVGL ou BSP. |
| `components/cyberdeck/src/platform/sensors/battery_protection.cpp`, `components/cyberdeck/include/platform/sensors/battery_protection.h` | `battery_protection_init`, `battery_protection_start`, `battery_protection_get_snapshot`, `battery_protection_get_policy_snapshot`, `battery_protection_set_enabled`, `battery_protection_is_enabled`, `battery_protection_is_active`, `battery_protection_charger_enabled`, `sample_and_publish` | Adaptador exclusivo para hardware: Expander B via `bsp_io_expander1_init()` (0x44), CHG_STAT pin 6 active-low input/pull-up, CHG_EN pin 7 output push-pull, CHG_EN=1 por default. Integra reader INA226 (sensor-only), politica pura, NVS para opcao enabled, timer UI 1 s. Um unico `sample_and_publish` alimenta a politica e publica as duas projecoes: `cyberdeck_battery::snapshot` (charge_class, usada pelo shell/status) e o snapshot puro da politica (`battery_protection_get_policy_snapshot`), que e a unica entrada de bateria consumida pela view do header. Falhas de I2C/NVS/CHG logadas sem desligar CHG_EN nem perder estado seguro. |
| `components/cyberdeck/src/platform/display/cyberdeck_ui.cpp` | `battery_indicator_symbol`, `refresh_battery_status`, `refresh_ble_status`, `process_battery_protection`, `execute_line` (battery protection on/off/status), timer LVGL 1 s | Mantem a grade direta 30/40/30, Bluetooth antes do Wi-Fi antes da bateria dentro da celula direita e exatamente dois labels no grupo da bateria (um glyph semantico e o percentual numerico). O <PII type="CASE_ID" id="30"> nao classifica estado: ele le `battery_protection_get_policy_snapshot`, aplica `cyberdeck_battery_view::resolve` e fixa o glyph por `battery_indicator_symbol` (`LV_SYMBOL_CHARGE` em carga, `LV_SYMBOL_BATTERY_FULL` constante em bateria presente, `LV_SYMBOL_MINUS` em alimentacao externa sem bateria, vazio quando invisivel); nao ha glyph de nivel nem seletor por porcentagem, e o grupo e ocultado quando a view decide `visible == false` (leitura indisponivel ou estado `unknown`). A celula wrapper Bluetooth (`s_ble_cell`, `LV_SIZE_CONTENT` x 42, layout none, padding 0, `flex_grow=0`, fundo transparente) nasce oculta junto do label interno (`s_ble_status`, `LV_SIZE_CONTENT` content, `CYBERDECK_BLE_HEADER_Y_OFFSET` via `lv_obj_set_y`); o glyph padrao `LV_SYMBOL_BLUETOOTH` usa `style_base(BLACK, WHITE)` e `refresh_ble_status()` apenas aplica `s_ble_model.is_connected()` na celula e no label, sem classificar estado. O percentual e mostrado quando a view expoe `show_percentage` (carga, bateria e external) e omitido em `absent`. Timer LVGL 1 s consome snapshot do adaptador; comandos shell `battery protection on/off/status` roteados para adaptador; `ui.type` transita via bridge serial. |
| `components/cyberdeck/src/platform/display/cyberdeck_ui.cpp` | `execute_line`, `local_key`, `process_ble_events`, `refresh_ble_status`, `on_ble_event`, `get_rendered_output`, `render_terminal`, `sync_ble_transient_block`, `ble_restore_background_bonds_once`, `ble_background_maybe_reconnect`, `ble_background_on_scan_result`, `ble_background_on_scan_finished`, `ble_background_note_advertisement`, `ble_background_address_known` | Integra a UI BLE sem chamadas diretas à stack: comandos `bluetooth search/paired`, teclas Up/Down/Enter/Escape, fila bounded de eventos e observer `ble_mgr`; `cyberdeck_ble::device_list` agrega snapshots de scan e `cyberdeck_ble::state_machine` permanece a fonte da seleção/estado. Todas as ações atravessam `ble_mgr_enqueue_cmd`, deadlines são avançados no timer LVGL sem chamadas de stack e as teclas consultam `owns_input()` sem flag sombra. Enquanto results/paired possuem entrada, a lista fica em bloco transitório fora de `s_output` e é renderizada dinamicamente a cada repaint, mantendo o marcador `> ` sincronizado com a seleção; a última representação é consolidada uma única vez ao sair desse fluxo. Durante `pairing`, `auth`, `connecting` e `connected`, `status_line()` bounded é renderizada no terminal sem ser anexada ao histórico; somente `auth` acrescenta o prompt de entrada com máscara. O prompt local `/$` continua suprimido durante ownership e reaparece depois, sem alterar Wi-Fi, shell, SSH ou tokens tardios. `process_ble_events` (100 ms) aplica `refresh_ble_status()` apos `advance_time`, antes do bloco `changed`, de modo que `BLE_MGR_EVT_CONNECTED`/`DISCONNECTED` atravessam a fila e atualizam o header sem gating de ownership. No mesmo tick, o observador background restaura bonds NVS uma unica vez (`ble_bonds_copy`, somente `last_connected` arma a primeira reconexao), inicia scan background apos perda espontanea quando `background_reconnect_armed()` e ocioso, consome no maximo 3 `reconnect` por ciclo de anuncio via `schedule_reconnect`/`consume_background_attempt` somente para bonds conhecidos conectaveis, aguarda novo anuncio apos esgotar (`reset_background_cycle`), ignora relatorios background na lista interativa e mantem desconexao manual bloqueada ate Enter em `paired`/`results`; `bluetooth paired` re-semeia a lista via `ble_bonds_copy` sem tocar na stack. |

## Dependencias e composicao

### Correcoes BLE de producao

- Os tres caminhos de `PASSKEY_ACTION` copiam a identidade do peer de
  `ble_gap_conn_desc.peer_id_addr`, campo disponível no NimBLE do ESP-IDF
  5.5.5, preservando `addr_type` e token da autenticação.
- `ble_mgr.cpp` drena resultados incrementalmente antes de publicar o terminal
  do scan; a fila bounded da UI reserva/recupera um slot para `scan_finished`,
  inclusive quando chegam 8 ou mais anuncios.
- O tipo de endereco local e inferido no sync e reutilizado em pair/connect/
  reconnect. O teardown usa comando de parada e espera bounded, sem delete
  forcado da task.
- A persistencia usa `bonds_v2`, preserva blobs novos validos no legado e
  descarta apenas blobs antigos de quatro campos sem `addr_type`, com erase
  fail-closed e log acionavel exigindo novo pareamento.
- `cyberdeck_ui.cpp` cruza endereco e `addr_type` em lookup tipado e preenche
  cada membro da union de comando explicitamente, sem aliasing entre membros.

Os testes `tests/host/keymap/test_prompt_behavior.py` e
`tests/host/keymap/local_prompt_contract.py` cobrem as duas rotas de
renderizacao do prompt local: o prompt desaparece enquanto
`cyberdeck_ble::state_machine::owns_input()` e verdadeiro e reaparece apos a
liberacao. `test_ble_state_machine.cpp` cobre as transicoes de busca ativa,
resultado vazio e resultado nao vazio, incluindo a preservacao da navegacao.

### Status textual da bateria

### Regressoes BLE de runtime

`ble_mgr.cpp` confirma `ble_gap_disc_active()` antes de cancelar uma busca
anterior, registra de forma bounded o callback terminal `DISC_COMPLETE` (token,
arg, reason e estado ativo) e recupera um slot do dispatch antes de desistir de
publicar `scan_finished`. `cyberdeck_ui.cpp` avanca o deadline puro da busca
em todo tick do timer LVGL, antes de depender da fila/observer, mantendo o
timeout como fallback quando o callback terminal da stack nao chega, inclusive
para buscas iniciadas pelo shell.

O processamento da fila BLE marca explicitamente a chegada do terminal para
renderizar a lista de resultados mesmo quando uma busca bem-sucedida nao gera
notice textual; isso evita deixar apenas a mensagem `Bluetooth search started.`
no terminal. `ble_mgr` copia observers sob mutex e invoca callbacks fora da
secao critica, alem de registrar publicacoes sem observer ou com timeout de
mutex em mensagens bounded sem payload de dispositivo.

`components/cyberdeck/include/platform/sensors/cyberdeck_battery_protection.h`
declara `state_name`, `charge_signal_name` e `format_status_line`; a
implementação permanece pura em
`components/cyberdeck/src/platform/sensors/cyberdeck_battery_protection.cpp`.
`format_status_line` recebe um único `snapshot` e produz, sem alocação ou
ESP-IDF, o formato exato
`battery: state=%s charge=%s available=%s voltage_mv=%ld current_ma=%ld percentage=%ld protection=%s charger=%s\n`,
com buffer bounded, terminação NUL quando há capacidade e `capacity == 0`
seguro. `cyberdeck_ui.cpp` usa o formatter no comando `battery protection
status`, sem tabelas locais nem getters live adicionais.

### ESP-IDF e componentes

`components/cyberdeck/CMakeLists.txt` registra todos os fontes de producao e
declara dependencias de LVGL, BSP, I2C master, Wi-Fi, rede, FreeRTOS, SD/FATFS,
libssh e HTTP server. `main/idf_component.yml` declara ESP-IDF, `esp_lvgl_port`,
LVGL diretamente na faixa `>=9.6.0,<10.0.0`, `esp_hosted`, `esp_wifi_remote`,
libssh e o override local de `sock_utils`. O
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
| `components/cyberdeck/include/apps/shell/cyberdeck_shell_help.h` + `cyberdeck_shell_utils.cpp` | `test_shell_utils.cpp` (inclui os parsers de `wifi audit`/`wifi audit save` e `screen on|off|timeout <0-1440>`; estrutura de 16 linhas e 16 linhas nao vazias, presenca das 16 linhas do catalogo com guarda de fronteira de linha e contagem de completude, igualdade exata com a fixture e determinismo) |
| `components/cyberdeck/include/apps/shell/cyberdeck_shell_help.h` + `cyberdeck_shell_utils.cpp` + `cyberdeck_local_shell.cpp` + `cyberdeck_ui.cpp` | `test_help_unification.cpp` (igualdade exata de `cyberdeck_help_text()` e `execute("help")`/`help -h`/`help --help`; 16 linhas com comandos comuns e locais uma vez cada) e `help_unification_contract.py` (fonte unica e ausencia de listas literais independentes) |
| `cyberdeck_screen_protection.cpp` + contrato puro `contracts/cyberdeck_screen_protection.h` | `test_screen_protection.cpp` (default 2 min, limites 0/1/2/120/1440, parsing decimal estrito, zero desabilitando com preservacao do ultimo positivo, reject 1441 sem mutacao, restore/snapshot NVS, transicoes exatas do timer e independencia de power on/off) |
| `screen_off.cpp` + `cyberdeck_ui.cpp` + `cyberdeck_shell_utils.cpp` + `cyberdeck_serial_bridge.cpp` + `app_main.cpp` + `cyberdeck_shell_help.h` | `test_screen_protection_contract.py` (rotas de comando, literal `screen [on|off|timeout <0-1440>]` na linha unica do catalogo compartilhado com delegacao `cyberdeck_help_text()` -> `cyberdeck_shell_help::text()`, fixture `contracts/cyberdeck_help.h` e os dois testes comportamentais que a comparam, timer LVGL, persistencia/restauracao NVS, default seguro, init NVS antes do adapter, duplo toque e caminho transitivo `ui.type` -> `inject_text_segmented`/`inject_enter` -> `cyberdeck_keyboard_input`; o alvo do Makefile declara todos esses arquivos como pre-requisitos) |
| `cyberdeck_history.cpp` | `test_history.cpp` |
| `cyberdeck_edit_line.cpp` | `test_edit_line.cpp`, `test_prompt_behavior.py` |
| `cyberdeck_local_shell.cpp`, `cyberdeck_cat_worker.cpp` | `test_local_shell.cpp` (raiz virtual `/`, caminhos relativos/absolutos, rejeição do alias `/sdcard`, confinamento, operações e regressões de segurança), `test_cat_multiline.cpp` (API dedicada com cwd `/` e alias físico rejeitado, LF/CRLF/tabs/UTF-8, marcador final, bytes inválidos, limite exato de 12288, NUL embutido e sufixo após newline preservados para sanitização), `cat_contract.py`, `test_cat_multiline_contract.py` (contrato estrutural de ponteiro+tamanho explícito, textarea multiline/max-length, limite UTF-8 e payload completo até append), `test_cat_lifecycle_sanitization_contract.py` (ramos sem task vs. com task, drain/join/ack, reset sincronizado, callback/generation e sanitizacao), `test_cat_start_teardown_start_contract.py` (restart e invalidacao stale), `test_cat_stack_footprint_contract.py` (regressao do stack minimo do worker), `test_cat_worker_path_safety_contract.py` (proibe o caminho worker->shell/resolve/path/vector e exige API cat-specific heap/bounded), `local_shell_security_contract.py`, `local_shell_tokenizer_contract.py` (tokenização e contrato estrutural do root `/`), `test_prompt_behavior.py`, `test_local_prompt_contract.py` |
| `event_log_recent.cpp` + Kconfig/log shell | `test_event_log_recent.cpp` (default 20 preservado, capacidade 64, faixa 1..64, wrap/clamp/ordem e canário), `test_event_log_contract.py`, `test_event_log_config_contract.py` (contrato estrutural do anel e binding Kconfig), `test_shell_session.cpp` (parser/sessão e `log lines 40` com 40 eventos) |
| `cyberdeck_clock.cpp` | `test_clock_formatter.cpp` |
| `cyberdeck_wifi_indicator.cpp` | `test_wifi_indicator_state.cpp` |
| `cyberdeck_wifi_icon.cpp` | `test_wifi_icon_layout.cpp` |
| `cyberdeck_ui.cpp` (roteamento Enter Wi-Fi) | `wifi_enter_dispatch_contract.py` |
| `cyberdeck_wifi_menu.cpp` | `test_wifi_menu.cpp` |
| `cyberdeck_wifi_state_machine.cpp` | `test_wifi_state_machine.cpp` |
| `cyberdeck_wifi_event_dispatch.cpp` | `test_wifi_event_dispatch.cpp`, `test_wifi_dispatch_contract.py` |
| `cyberdeck_wifi_persistence_queue.cpp` | `test_wifi_persistence_queue.cpp` |
| `cyberdeck_wifi_persistence_coordinator.cpp` | `test_wifi_persistence_coordination.cpp` |
| `cyberdeck_terminal_filter.cpp` | `test_terminal_filter.cpp` (inclui `test_remote_prompt_is_preserved_literally`: prompt tipico/variantes/ANSI/UTF-8/`ssh> ` preservados byte a byte, CR e CRLF normalizados em um LF, filtro sem acrescentar quebra; e `test_remote_prompt_survives_partition_invariance`: mesmo texto em qualquer fatiamento, com CR isolado resolvido no flush idempotente) |
| `cyberdeck_ssh_echo_guard.cpp` | `test_ssh_echo_guard.cpp` |
| `cyberdeck_ssh_line_composer.cpp` | `test_ssh_line_composer.cpp` |
| `cyberdeck_net_coordinator.cpp` | `test_net_coordinator.cpp` |
| `cyberdeck_wifi_audit.cpp` (worker passivo/local, snapshot versionado, SSID do driver limitado ao array, scratch/resultado grandes fora da stack e persistência save confinada; separação de operações, gate lifecycle/token antes de hardware, guard single-shot até drain e descarte de stale/cancelado/teardown; adapter ESP/FatFs delega a transação a `cyberdeck_wifi_audit_persistence.cpp` com O_EXCL, fsync, sequência `.tmp`/`.bak` e recuperação/rollback fail-closed, sem atomicidade POSIX prometida; backend `wifi_audit_io` com deadline de 2 s e sem I/O na UI) | `test_wifi_audit.cpp`, `test_wifi_audit_contract.py`, `test_wifi_audit_save.cpp` (host exige conteúdo SSID/BSSID/IP, token/version/status, campos ausentes com `<missing>`, formato determinístico de seis chaves em ordem e LF final, escaping bounded, destino timestampado GMT-3, diretório/colisão, payload/bytes e guard; contrato estrutural valida a implementação real do seam `cyberdeck_wifi_audit_persistence.cpp`/header, exigindo write/fsync/close/rename antes do ACK, recuperação stale fail-closed, ordenações backup/rollback, sidecars preservados em falha, guard armado após queue success e liberado em drain/falha/stale/teardown, cópia do payload após a publicação e I/O fora da UI; o mesmo contrato exige task `wifi_audit_io`, deadline 2000 ms, ownership por semáforos até o release, quarentena `export_backend_busy`, retorno false/cleanup seguro sem ACK de sucesso no timeout, e defaults FATFS `FS_LOCK=5`/`TIMEOUT_MS=1000`) |
| `cyberdeck_wifi_audit_persistence.cpp` (seam de filesystem injetável em produção; adapter nativo e coordinator de completion) | `test_wifi_audit_persistence.cpp` (sucesso com payload real e ACK após publicação; write/fsync/close/rename; destino timestampado em `/sdcard/wifi-audit/`; sidecars `.tmp`/`.bak` derivados de `make_transaction_paths`; destino existente; recovery; rollback e preservação; single-shot/fila cheia; ACK retido; concorrência enqueue/publish e teardown concorrente com pump_one/enqueue/drain), `test_wifi_audit_persistence_contract.py` + `contracts/cyberdeck_wifi_audit_persistence.h` (Makefile/CMake/code-map; ambos os fontes de teste têm exceção explícita no `.gitignore`) |
| `cyberdeck_ui.cpp` + `cyberdeck_shell_utils.cpp` + `cyberdeck_wifi_audit.cpp` + `cyberdeck_wifi_audit_persistence.cpp` (contrato TDD do fluxo `wifi audit`/`wifi audit save`) | `test_wifi_audit_save.cpp` (comportamental host: renderização silenciosa em `collecting`, linha terminal exata em `ready`/`error`, `<missing>` somente no snapshot final, diretório, timestamp, colisão, sidecars, falhas, rollback e ACK; exercita a implementação de produção) e `test_wifi_audit_save_contract.py` (contrato estrutural de parser/UI, gate de `process_wifi_audit`, renderização once-only, GMT-3 e compatibilidade serial transitiva `exec_ui_type` -> `inject_text_segmented`/`inject_enter` -> `cyberdeck_keyboard_input`, sem hardware) |
| `screenshot_bmp.cpp` | `test_screenshot_bmp.cpp` |
| `cyberdeck_window_manager.cpp` + `cyberdeck_window_manager_adapter.cpp` + `cyberdeck_terminal_view.cpp` + `cyberdeck_ui.cpp` + `components/cyberdeck/CMakeLists.txt` + `tests/host/keymap/Makefile` (REQ/AC-8.1..8.7 / TEST-WM-01..07 + TEST-REG-8-BAR, Fase 8) | `test_window_manager.cpp` (comportamental host: superficies, foco, teardown, notificacoes, adaptador contra o shim LVGL completo e `bar_layout_scenario` para a regressão BUG-8-WM-BAR) e `test_window_manager_contract.py` (contrato estrutural: pureza dos headers, `view_context` opaca, LVGL somente no adaptador, `system_bar`/`content` pela raiz composta, linha fixa da barra e coluna flex sem altura explícita no content, teclado virtual em overlay, ordem teardown antes de `deinit`, guardas de limite/handles obsoletos e wiring CMake/Makefile/guard de cobertura) |
| `cyberdeck_ui.cpp` | `test_boot_sequence.py`, `test_keyboard_input_contract.py`, `test_ui_resource_contract.py` (contrato estrutural: `destroy_ui_resource_handles()` entra pelo SSH antes de deletar a fila, guarda e zera cada handle e para o dispatcher; em `cyberdeck_ui_init()` enumera por nome e em ordem de aquisição as 8 fases fail-closed — dispatcher de teclado, filas Wi-Fi (estado, scan + mutex), filas BLE e SSH, raiz composta do window manager, registro da superfície do shell e timer de saída do terminal — exigindo `ESP_ERR_NO_MEM` em cada uma, `destroy_ui_resource_handles()` em toda fase posterior à primeira, ordem estrita das fases, um único exit por fase e teardown da superfície antes do `deinit()` da raiz), `test_local_prompt_contract.py`, `test_wifi_enter_routing_contract.py` |
| `main/app_main.cpp`, `cyberdeck_ui.cpp` | `test_boot_sequence.py` (ordem SD/UI, montagem física `/sdcard` e root virtual `/`) |
| `imu_reader.cpp` | `imu_sensor_contract.py` (callback Sensor Hub, `sensor_handle_t` obrigatorio para `bsp_sensor_init`, proibicao especifica da leitura direta `imu_acquire_acce(`, timeout/fallback seguro e continuidade da rotacao) |
| `battery_status.cpp` + `ina226_reader.cpp` + `battery_protection.cpp` + contrato puro `contracts/cyberdeck_battery.h` | `test_battery_contract.cpp` (REQ-BAT-001/002: tensao `bus_voltage_mv`, janela `6000..8230`, saturacao, estados charging/discharging/neutral, zero/indeterminado e ausencia explicita), `test_battery_reader_contract.py` (REQ-BAT-001/002/004/005/006: `0x41`, `0x4527`/`0x0D55`, conversao do registro de tensao, task 1 s, mutex/snapshot, exclusoes, sensor-only, composicao pelo adaptador e nao-fatal; a UI consome a entrada de bateria apenas pelo seam de snapshot do adaptador, `battery_protection_get_policy_snapshot` ou `battery_protection_get_snapshot`, sempre declarado no header do adaptador, e nao pode conter nenhuma referencia direta ao reader INA226, seja chamada ou include), `test_battery_protection.cpp` + `test_battery_protection_contract.py` (politica e adaptador reais, sem hardware) |
| `cyberdeck_battery_view.cpp` + contrato puro `contracts/cyberdeck_battery_view.h` | `test_battery_view.cpp` (REQ-BAT-UI-001..005 / AC-BAT-UI-001..006: `resolve` sobre a matriz total `battery_state` x `charge_signal` x disponibilidade x percentual, `absent` somente com glyph externo e sem percentual em qualquer sinal, bateria presente sem carga com percentual e glyph de bateria, `charging` por estado ou por sinal com glyph de carga, `external` com bateria presente sem glyph de carga, `invalid`/`unknown` totalmente oculto, `clamp_percentage` 0..100, `from_snapshot` sem inventar entrada, glyph nunca escolhido pelo nivel em varredura 0..100, `decode` do sinal `unknown` e regressao de `CHG_STAT` preso em low apos os 5 votos) e `test_battery_view_contract.py` (view pura sem ESP-IDF/FreeRTOS/LVGL/I2C/NVS/BSP e registrada no CMake, LVGL aplicando `from_snapshot`/`resolve` sem `charge_class::`/`battery_state::`/`charge_signal::`, tabela de glyph fixo, percentual vazio quando `show_percentage` e falso, grupo oculto quando `visible` e falso, sem I2C/NVS/reader diretos). O alvo liga a view com a politica pura de `cyberdeck_battery_protection.cpp`, sem ESP-IDF, LVGL, I2C, NVS, simulador, Serial Automation Bridge ou hardware. |
| `cyberdeck_ui.cpp` + `cyberdeck_wifi_icon.cpp` + `cyberdeck_battery_view.cpp` + `battery_protection.cpp` + `app_main.cpp` | `test_battery_ui_contract.py` (REQ-BAT-002/003/004/006/010 + REQ-BAT-UI-004/005: grade direta 30/40/30, ordem Wi-Fi/bateria, layout compacto, percentual numerico, exatamente dois labels, ausencia de selecao de icone por nivel e de ramo `charge_class::` no refresh, ocultacao em falha, timer/snapshot do adaptador sem dependencia direta do reader, continuacao do boot e rastreabilidade). A selecao de icone foi transferida para a view pura de REQ-BAT-UI: o `refresh_battery_status` consome `battery_protection_get_policy_snapshot` e delega a `cyberdeck_battery_view::resolve`, enquanto `battery_indicator_symbol` fixa o glyph semantico. |
| `cyberdeck_serial_bridge.cpp` (REQ-002/003/005/006/007/008/009) — ponte NDJSON bounded, rid/envelopes, UI/sys/wifi, screen.dump chunks/CRC/end, feeder tolerante a logs/fragmentacao; `cyberdeck_cli.py` | `test_serial_ndjson_dispatch.cpp` (bounded/erros/rid/envelopes/UI, inclusive `ui.type` para `screen on|off|timeout`), `test_serial_screen_dump.cpp` (byte-identical chunks/CRC/end), `test_serial_cli_tolerance.cpp` (logs/leitura fragmentada), `test_serial_sysinfo_wifi.cpp` (sys.info/wifi contratos) — todos host-only, sem pyserial/hardware; GREEN com a producao criada |
| `cyberdeck_serial_bridge.cpp` `fs.write` (REQ-001..REQ-011) — protocolo rid/type/path/data_b64/size, limite 2048, path safety, strict canonical base64, commit por temp unico/O_EXCL+rename (substituicao atomica no host/no-clobber no ESP/FATFS; nunca remove candidato preexistente), CRC response, NDJSON errors; `cyberdeck_cli.py` `fs.write` (`build_request`, encoding, input/stdin, 2048, strict canonical) | `test_fs_write_dispatch.cpp` (dispatch/validacao/path/size/base64/CRC/preservacao; colisao de temp preexistente/no-clobber), `test_fs_write_cli.py` (parser/build_request/encoding/stdin/limite), `test_fs_write_contract.py` (estrutural: disco/path/atomic/CRC/CLI/Makefile/code-map) — todos host-only, RED antes da producao |
| `cyberdeck_ble_types.cpp` + contrato puro `contracts/cyberdeck_ble_types.h` | `test_ble_types.cpp` (classificacao por `appearance` provavel vs. `unknown`, normalizacao estrita de endereco, sanitizador bounded/UTF-8-safe sem C0/C1, clamp de RSSI, passkey estrito + mascara constante, dedup por endereco com RSSI mais forte e primeiro nome, capacidade 32, selecao com clamp, render deterministico, nomes duplicados, beacon nao conectavel) |
| `cyberdeck_ble_state_machine.cpp` + contrato puro `contracts/cyberdeck_ble_state_machine.h` | `test_ble_state_machine.cpp` (scan assincrono com um unico `start_scan`, deadlines exatos, ownership derivado do estado visivel e liberado silenciosamente apos lista vazia/falha/timeout, quatro classes de mensagem distintas, rejeicao de token obsoleto/zero/duplicado, dispositivo desaparecido nao pareia, passkey exibido so em `status_line` e zerado apos submissao, passkey errado nao encaminha, desfechos bonded/rejected/cancelled/timed_out/failed, conectar/escape/desconectar, orcamento de reconexao com rearme manual, concorrencia scan x reconexao) |
| `cyberdeck_ble_event_dispatch.cpp` + contrato puro `contracts/cyberdeck_ble_event_dispatch.h` | `test_ble_event_dispatch.cpp` (tokens monotonicos compartilhados, publicacao obsoleta descartada sem enfileirar, fila bounded 8 com overflow fail-closed, ordem e propriedade no `drain`, `drop_stale`, geracao manual x automatica, `event_summary` sem digitos do passkey) |
| `cyberdeck_ble_store.cpp` + contrato puro `contracts/cyberdeck_ble_store.h` | `test_ble_store.cpp` (encode determinista com ordem de campos fixa, decode estrito rejeitando campo duplicado/reordenacao/lixo, campo desconhecido rejeitado impedindo contrabandear `ltk`/`irk`/`passkey`, capacidade 16, serialize bounded, deserialize atomico fail-closed, round trip de reboot) |
| `cyberdeck_shell_utils.h/.cpp` + `cyberdeck_shell_help.h` (roteamento de `bluetooth search`/`bluetooth paired` e a 16a linha do catalogo) | `test_ble_command_parse.cpp` (comportamental: os dois subcomandos, o verbo nu e 22 grafias proximas — inclusive `bluetooth classic`/`spp`/`a2dp` — como `CYBERDECK_CMD_UNKNOWN`, Help com 16 linhas e a linha configuravel `log [lines <1-64>]`) + `test_help_unification.cpp`/`help_unification_contract.py` (fonte unica do catalogo) |
| `ble_mgr.cpp` + `cyberdeck_ui.cpp` + `app_main.cpp` + `components/cyberdeck/CMakeLists.txt` + `sdkconfig.defaults` + `main/idf_component.yml` (REQ-BLE-001/003/008/010/011) | `test_ble_integration_contract.py` (modulos puros livres de stack/RTOS/UI, ABI de producao igual aos contratos, roteamento de shell e UI com as quatro teclas, ownership sem flag sombra e derivado por `state_machine::owns_input()`, notice final append antes da liberacao, task FreeRTOS dedicada e `ble_mgr_start` nao fatal, ordem regressiva connect -> controller init -> controller enable -> NimBLE, ausencia de `abort`/`ESP_ERROR_CHECK`, mutex do dispatch, task `ble_host` com stack 8192, ausencia de log com passkey/chave e de campo secreto em `bond_record`, registro em CMake/sdconfig/idf_component.yml, dependencias do alvo Makefile sobre `ble_mgr.cpp`/header e state machine, exclusao de Bluetooth Classic, rastreabilidade REQ/AC em Makefile/.gitignore/code-map/docs/READMEs) |

### Bluetooth LE: regressões entregues e rastreabilidade REQ/AC -> TEST

As regressões pós-implementação abaixo são contratos estruturais host-only. Não
abrem hardware, rádio, simulador ou Serial Automation Bridge: `test_ble_ui_timeout_contract.py`
inspeciona `process_ble_events` para garantir que `changed` seja capturado antes
de `advance_time(100)` e permaneça ativo até `render_terminal()` após o drain;
`test_ble_adv_types_contract.py` inspeciona o callback GAP e exige os cinco
tipos LE encaminhados a `scan_report_adv`, com default fail-closed. Ambos são
alvos independentes no `tests/host/keymap/Makefile` e estão explicitamente
preservados no `.gitignore`.

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

### CI e cobertura host mensuravel

| Arquivo | Simbolos/contrato | Papel |
| --- | --- | --- |
| `.clang-format`, `.github/workflows/quality-gate.yml`, `tools/coverage_scope_guard.py`, `tests/host/keymap/test_coverage_scope_guard.py` | `quality`, guard de escopo gcovr | Gate determinístico de formatação C++ (clang-format 18.1.8, ranges alterados em `ble_mgr.cpp` e no seam de `cyberdeck_cat_worker`), lint, smells, Bandit, agregado host com `-O0 --coverage` e build ESP-IDF 5.5.5 depois dos checks host. O agregado executa `test_serial_ndjson_dispatch`, `test_serial_screen_dump`, `test_serial_cli_tolerance`, `test_serial_sysinfo_wifi` e `test_fs_write_dispatch`, além dos binários host dos seams UI/worker. A cobertura global bloqueia em >=80% de linhas, >=94% de funções e >=59% de branches em `components/cyberdeck/src/**/*.cpp`; uma única execução gcovr usa `--txt-metric branch`, `--fail-under-line 80`, `--fail-under-function 94` e `--fail-under-branch 59`. O JSON temporário fica em `$RUNNER_TEMP/cyberdeck-coverage.json`. O guard compara fail-closed as TUs de produção ao JSON do gcovr e aceita somente uma allowlist explícita de adaptadores hardware/ESP-IDF; `test_coverage_scope_guard.py` cobre os casos de escopo e allowlist. `cyberdeck_ui.cpp` é allowlisted especificamente por ser composição LVGL/BSP/FreeRTOS, enquanto o comportamento foi deslocado para modelos host-testáveis. `ssh_client.cpp` é allowlisted por acoplar libssh a BSP/FreeRTOS. A allowlist tem 15 entradas e ganhou mais três adaptadores: `cyberdeck_display_port.cpp` (captura BSP/LVGL com `bsp_display_lock` e `lv_snapshot_take`, sem build host), `cyberdeck_service_ports.cpp` (encaminhamento fino para os adaptadores `wifi_mgr`/`ssh_client`/`ble_mgr`/serial bridge) e `cyberdeck_system_apps.cpp` (composição e lifecycle dos system apps acoplada a serviços de hardware, coberta por `test_system_apps_contract.py`, `test_shell_app_contract.py` e `test_serial_lifecycle_contract.py`). `apps/demo/cyberdeck_demo_app.cpp` não é allowlisted: passa a ser coberta pelo binário host `test_demo_app`. TUs cobertas não podem permanecer allowlisted, e `cyberdeck_cat_worker.cpp`, `cyberdeck_keyboard_dispatch.cpp`, `cyberdeck_header_view.cpp`, `cyberdeck_terminal_view.cpp` e `cyberdeck_ble_background.cpp` são proibidas na allowlist. O gate BLE isolado foi removido. As actions são pinadas por SHA exato: checkout v4.2.2 em `11bd71901bbe5b1630ceea73d27597364c9af683` e Espressif branch v1 tip em `8fc05d1470d5591417e7a3707a1f2bec178db4ae`. |

Declaracao de validacao fisica reportada pelo usuario (REQ-HW-01): foram testados
no firmware gravado `0202b2b` os cenarios de conexao Bluetooth, scan,
pareamento/reconexao e recuperacao `EALREADY`. A data e o dispositivo nao foram
registrados na declaracao; nao ha logs seriais, comandos ou capturas de tela
registrados dessa acao do usuario. Esta declaracao nao substitui a validacao
reproduzivel pelo roteiro `tests/manual/serial-bridge-validation.pt-BR.md`.

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
  `components/cyberdeck/include/apps/shell/cyberdeck_shell_help.h`.
- `tests/manual/serial-bridge-validation.pt-BR.md`: validacao da ponte
  USB Serial-JTAG no dispositivo.
- `tools/cyberdeck_cli.py`: cliente NDJSON host da ponte serial.
- `components/cyberdeck/CMakeLists.txt`: lista definitiva dos fontes compilados.
- `tests/host/keymap/Makefile`: lista definitiva dos testes e fontes puros.
