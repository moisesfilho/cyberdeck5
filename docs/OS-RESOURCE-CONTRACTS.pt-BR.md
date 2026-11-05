# Fase 2 — contratos e donos de recursos do Tab5

Este inventário audita as fronteiras e os recursos que participam de boot,
console, persistência e serviços iniciados pelo supervisor. É uma descrição da
implementação, não uma nova camada de APIs nem um certificado de execução física.
Números abaixo são **referências à fonte**; os testes devem verificar a
implementação e derivar os limites do código, não copiar valores desta tabela.

## Fronteiras de acesso

| Caminho | Classificação | Motivo e limite | Prova existente |
| --- | --- | --- | --- |
| `apps/runtime/cyberdeck_app_{runtime,facades,storage,logger}.*`, `apps/runtime/cyberdeck_window_manager.*` | Porta pública compilada para apps e política do supervisor | Grants são cooperativos e verificados a cada operação; não são isolamento de memória nem habilitam apps não confiáveis. Facade de storage é bounded; view/input não entregam handles LVGL | `test_app_runtime`, `test_runtime_sdk`, `test_storage_facade`, `test_window_manager` |
| `apps/editor/cyberdeck_editor_app.cpp` | Consumidora da porta pública | Usa facade de storage e lifecycle de foreground; acesso ao SD é interno ao adapter da facade | `test_editor_runtime_contract`, `test_storage_facade` |
| `apps/music/cyberdeck_music_app.cpp`, `apps/music/cyberdeck_music_player.cpp` | Exceção justificada: app compilada e backend concreto de áudio/SD | Enumeração de arquivos usa confinamento próprio; player precisa de FreeRTOS, memória SPIRAM, tempo e porta do codec, sem expor esses handles pelo SDK. Não chamar esta exceção de sandbox | `test_music_app`, `test_music_player`, `test_music_codec_i2s_vendor` |
| `apps/network/cyberdeck_netdiag_{app,dns_adapter,tcp_adapter,https_adapter}.*` | Exceção justificada: app compilada + adapters concretos de rede | A app lê status Wi-Fi/ESP-IDF e usa task; adapters possuem lwIP/HTTP. Controller e parsing são puros, com alvo explícito e deadlines próprios | `test_netdiag_app`, `test_netdiag_controller`, testes de adapters |
| `apps/serial/cyberdeck_serial_bridge.cpp`, `apps/screenshot/screenshot_server.cpp` | Implementação de plataforma de serviços | USB Serial-JTAG, locks LVGL/BSP, HTTP e captura exigem drivers concretos; não são portas para apps arbitrárias | `test_serial_ndjson_dispatch`, `test_serial_lifecycle_contract`, `test_screenshot_lifecycle_contract` |
| `apps/wifi/wifi_mgr.cpp`, `apps/bluetooth/ble_mgr.cpp`, `apps/ssh/ssh_client.cpp`, `apps/system/cyberdeck_time_sync.cpp` | Implementação de plataforma de serviços de conectividade | Donos de ESP-Hosted/Wi-Fi, NimBLE, socket/libssh e HTTP/TLS; hooks do supervisor apenas iniciam/param e reportam estado. Cada worker responde por seu cancel/join | testes Wi-Fi, BLE, SSH e `test_time_sync`/contrato |
| `apps/shell/cyberdeck_local_shell.cpp`, `apps/shell/cyberdeck_cat_worker.cpp`, `platform/storage/cyberdeck_sd_io_coordinator.cpp` | Implementação de plataforma do VFS e política de lease | `/` virtual confinado a `/sdcard`; worker não executa I/O na task LVGL; lease não é substituto de `fsync` nem lock geral de todo o FatFs | `test_local_shell`, `test_cat_worker`, `test_sd_io_coordinator` |
| `main/app_main.cpp`, `apps/system/cyberdeck_system_apps.cpp`, `platform/{display,input,sensors,power,logging}/` | Composição de boot, hooks concretos e adapters de hardware | A composição decide ordem/gates; BSP/ESP-IDF executam mount, display e drivers. Inicialização de bateria não é gate; SD mount e teclado são. Nenhum serviço reordena SDMMC/Hosted por otimização | `test_boot_sequence`, `test_system_apps_contract`, contratos de bateria/poweroff |

Uma dependência `#include "platform/..."` isolada não prova violação: o produtor
concreto pode legitimamente possuir hardware. A exceção material é app compilada
que ainda usa API direta (music/netdiag), não permissão para apps instaláveis.
Migrar essas chamadas para facades requer consumidor/benefício demonstrado,
semântica de cancelamento preservada e teste comportamental; não há proibição
global de includes nem tabela universal de operações.

## Inventário dos contratos críticos

`k_*`, `CONFIG_*` e os argumentos às funções FreeRTOS citados abaixo ficam na
fonte de produção. Quando o módulo não implementa um deadline global, “sem
deadline global” é um fato operacional, não autorização para esperar para sempre
no contexto de UI. Os testes indicados são host/contratos, não medição de stack
ou latência no Tab5.

| Recurso e dono | Capacidade e espera na fonte | Overflow, cancelamento, teardown e evidência |
| --- | --- | --- |
| Supervisor, `apps/runtime/cyberdeck_app_runtime.{h,cpp}` | `k_max_applications`, dependências/recursos/comandos e quotas do manifesto; `lifecycle_timeout_ms` é medido **depois** do hook síncrono | Registro excedente/duplicado é recusado; `activate_foreground()` preserva owner de console; `stop_index()` para dependentes antes do alvo e só revoga grant/quota depois de teardown bem-sucedido. Falha preserva foreground e grant para retry. `test_app_runtime`, `test_shell_app`, `test_music_app` |
| Ordem de boot, `main/app_main.cpp`/`system_apps.cpp` | NVS/recovery → Hosted no modo normal → mount SDMMC/SD → sidecars com lease → apps/log → display/IMU/UI → bateria não fatal → teclado/brilho → serviços → checkpoint. Wait Wi-Fi para BLE vem de `k_ble_wifi_ready_timeout_ms` | Hosted falho tenta mount comum; mount falho passa por `ESP_ERROR_CHECK` e não inicia console. Shell + serial são gates do checkpoint, serviços opcionais só degradam. Não paralelizar SD/C6. `test_boot_sequence`, `test_system_apps_contract`, `test_phase10_contract` |
| Leases SD, `platform/storage/cyberdeck_sd_io_coordinator.cpp` | `acquire` sem espera, `drain` usa deadline monotônico do chamador; mutex/contador e owner do fechamento | OPEN→CLOSING recusa novos leases; timeout de drain conserva CLOSING e leases vivos, sem unmount; só owner pode reabrir. Não é lock exclusivo de leitores versus escritores nem cobre automaticamente todos os caminhos legados. `test_sd_io_coordinator`, `test_sd_io_integration_contract` |
| Cat, `apps/shell/cyberdeck_cat_worker.cpp` | `k_cat_work_queue_capacity`, `k_cat_field_bytes`, `k_cat_worker_stack_size`; receive periódico; enqueue não bloqueia com fila cheia | Recusa request novo, valida campos, sinaliza shutdown, invalida callback e aguarda worker pelo orçamento de `stop_cat_worker` antes de deletar fila. Timeout preserva propriedade para retry. `test_cat_worker`, `test_cat_worker_runtime` |
| Console LVGL, `platform/display/cyberdeck_ui.cpp` e dispatch | Filas Wi-Fi, BLE e SSH criadas em `cyberdeck_ui_init`; capacidade e prioridade de terminais nas rotinas de enqueue; lock/display budget em `cyberdeck_lvgl_dispatch.h` | Eventos stale descartados por token/generation; callbacks não podem executar I/O bloqueante sob lock LVGL. SSH terminal e Wi-Fi state não são equivalentes a FIFO de logs. `test_ui_resource_contract`, testes BLE/Wi-Fi/UI, `test_lvgl_dispatch_state` |
| Serial, `apps/serial/cyberdeck_serial_bridge.cpp` | Task `serial_brg`, buffer/frame NDJSON bounded e semáforos estáticos; leitura USB tem timeout e escrita usa deadline por chunk | Admissão e `rid` diferenciam request de logs; stop possui timeout do chamador e join do worker antes de liberar estado; timeout não prova quiescência. `test_serial_lifecycle_contract`, `test_serial_ndjson_dispatch`, `test_serial_cli_tolerance` |
| Event log, `platform/logging/event_log.cpp` | `QUEUE_LENGTH`, `DURABLE_QUEUE_LENGTH`, `RECORD_SIZE`, limite da projeção textual e tasks `event_log`/`event_text` | Caminho binário autoritativo, projeção textual best-effort; fila durable mantém request/ownership até ACK e rejeita overflow sem falso sucesso. Teardown depende do quiesce e semáforos. `test_event_log_runtime`, contratos de log/durable |
| Wi-Fi, `apps/wifi/wifi_mgr.cpp` e coordinators | Fila state coalescida; filas de eventos de rede com capacidades de headers/constantes, workers de evento e rede; stop recebe timeout do chamador | Eventos críticos têm reserva/geração, callbacks apenas sinalizam; join antes de liberar estado, late/stale ignorados; callbacks externos não rodam sob lock de estado. `test_wifi_persistence_coordination`, `test_wifi_event_dispatch`, `test_wifi_persistence_queue`, `test_net_coordinator` |
| SSH, `apps/ssh/ssh_client.cpp` e UI | Filas de input/password/host-key no client e `k_ssh_event_queue_capacity` na UI; task SSH usa stack declarada no source | Entrada rejeitada quando fila não comporta payload, estado desconectado/timeout cancela e join deve preservar ownership; output/terminal não bloqueia produtor sob lock LVGL. Testes SSH/compositor/lifecycle |
| BLE, `apps/bluetooth/ble_mgr.cpp` | `BLE_MGR_QUEUE_SIZE`, `BLE_MGR_GAP_EVENT_QUEUE_SIZE`, fila terminal separada `BLE_MGR_GAP_TERMINAL_QUEUE_SIZE`, task BLE e task NimBLE configurada | Overflow de anúncios pode descartar não terminais, conclusão não pode desaparecer; tokens/generation e scan cancelado rejeitam callback stale; falha parcial não autoriza retry init no mesmo boot. Testes dispatch/state machine e contratos scan/review/background |
| HTTP screenshot, `apps/screenshot/screenshot_server.cpp` | `k_control_queue_depth`, `k_task_stack_bytes`; request mutex, wait de admissão e stop por timeout do chamador | Stop flag encerra na próxima recepção mesmo se fila cheia; request mutex é barreira de quiescência. Falha no join põe serviço em quarentena, não deleta task viva. `test_screenshot_lifecycle_contract` |
| Time sync, `apps/system/cyberdeck_time_sync.cpp` | `kBodyLimit`, `kHttpTimeoutMs`, `kMaxAttemptsPerToken`, `kTaskStackBytes`; semáforos binários wake/quiesced | Token limita tentativas; resposta oversized rejeitada sem ajustar hora; stop espera quiesce com timeout recebido, sem liberar task viva na falha. `test_time_sync`, `test_time_sync_contract` |
| Netdiag, `apps/network/cyberdeck_netdiag_{app,controller}.*` | Worker único `k_task_stack_bytes`, `k_stage_timeout_ms`, `k_stop_timeout_ms`; adapters DNS/TCP/HTTPS possuem deadlines por estágio | Slot ocupado recusa, cancelamento cooperativo; stop que não faz reap conserva worker/foreground. Testes controller/adapters/app |
| Music, `apps/music/cyberdeck_music_{app,player}.*` | `k_queue_depth`, `k_worker_stack` no player; scan separado da app com semaphore/task e timeout de teardown | Stop descarta requests pendentes, enfileira stop, aguarda `finished` e `worker_exited`, depois deleta task/fila; falha não revoga grant nem finge app parada. `test_music_app`, `test_music_player` |
| Bateria/proteção, `platform/sensors/` | INA226 em task de polling; política pura tem votos/tempo de repouso e limites próprios nos headers, snapshot sob mutex | Leitura inválida oculta telemetria e preserva último estado seguro; não usar zero como leitura, nem elevar corte para otimizar carga. Testes battery/SoC/protection/HTTP |
| Poweroff, `platform/power/cyberdeck_poweroff.cpp` | Controller one-shot, watchdog e worker com orçamento total e deadline remanescente por estágio | Quiesce serviços, gate SD, fsync/unmount e pulso só após sucesso; timeout conserva recursos e recusa pulso/retry. Testes poweroff e roteiro físico próprio (não executar poweroff como gate documental) |

## Riscos de concorrência e limites da evidência

- Ordem de locks: código de UI só toca LVGL sob display lock; callbacks de
  Wi-Fi/BLE/SSH enfileiram snapshots, não fazem render diretamente. Na captura,
  request mutex serializa HTTP e o display lock dura apenas a cópia; no SD,
  `begin_close` fecha admissão antes de `drain`. O runtime não mantém
  `grant_mutex_` durante hooks, logger ou quota; stop não revoga grant antes
  do join. Essas propriedades têm testes de fluxo/host, mas não são prova de
  ausência universal de deadlock no ESP32-P4.
- `portMAX_DELAY` existe em mutexes internos/receive de workers: o stop
  cooperativo precisa sinalizar o worker e acordá-lo; **não** reinterpretar
  `lifecycle_timeout_ms` como preempção. O timeout de fila/lock interno não
  cria deadline universal de SD, decoder ou HTTP externo.
- Gate SD Stage 1 cobre shell síncrono, cat, editor e recuperação de sidecars;
  event log, Wi-Fi/audit, Serial global, SSH e outros writers assíncronos não
  ficam automaticamente cercados por essa lease. Poweroff usa quiesce dedicado;
  a quiescência física global e falhas reais de VFS ainda são gates da Fase 3.
- O manifesto declara recursos/grants, stack e fila para supervisão, mas não
  aplica quota de heap/CPU por app. Para algumas apps o manifesto descreve o
  serviço enquanto adapters internos possuem mais de uma fila/task; este
  inventário usa os **locais reais de criação**, não trata metadata como
  contagem global de recursos.
- Testes host cobrem os ciclos/rejeições conhecidos. Stress físico, high-water
  mark, latência e falhas de energia/SD não foram medidos nesta fase e ficam
  pendentes em OS-TAB5-04/05/07/08.
