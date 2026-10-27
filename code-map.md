# cyberdeck5 Code Map

## Validacao de firmware

Como o projeto produz firmware para um dispositivo específico, implementações
devem ser validadas no dispositivo de teste conectado, com flash do firmware
compilado quando disponível. Consulte `AGENTS.md` para o procedimento Serial-JTAG
e registre a validação física como pendente quando o dispositivo não estiver
disponível.

## Testes host

`tests/host/keymap/Makefile` mantém os alvos focados e o agregado `test`.
`BINS` representa artefatos de build; `test-build` constrói essa lista e então
`test` executa os cenários/gates ordenadamente. Exceção: `test_event_log_shim`
compila e executa seu binário na própria receita de build. Use
`make -C tests/host/keymap <alvo>` para
execução focada; para paralelizar somente a construção do agregado, passe
`-jN` ao make (por exemplo, `make -C tests/host/keymap -j4 test`). Sem essa
opção, a construção é serial. O Makefile não sobrescreve jobserver/jobs do
chamador. Os cenários e gates continuam ordenados. A CI exige gcovr mínimo de
80% de linhas, 94% de funções e 59% de branches, além do scope guard de TUs;
alvos focados não executam esses gates globais. Executáveis `<alvo>_bin` ficam
na raiz de `tests/host/keymap/`. Alvos incrementais por TU armazenam objetos,
depfiles `.o.d` e sidecars gcov em diretórios `test_<alvo>/`; alvos monolíticos
não têm diretório por alvo e gravam sidecars gcov na raiz com nomes como
`<alvo>_bin-<tu>.gcda`. Para coverage, não execute instâncias concorrentes no
mesmo diretório; `clean` remove executáveis listados em `BINS`, nomes legados
listados em `LEGACY_TEST_ARTIFACTS`, diretórios de objetos/depfiles e sidecars
gcov da raiz (`*.gcda`, `*.gcno`, `*.gcov`). Deve ocorrer fora de uma execução.
Consulte `AGENTS.md` para os comandos operacionais detalhados.

Os oito alvos `test_save_sidecar_recovery`,
`test_wifi_survey_export_controller`, `test_netdiag_controller`,
`test_netdiag_tcp_adapter`, `test_event_log_runtime`,
`test_sd_io_coordinator`, `test_battery_contract` e `test_battery_view` usam
objetos/depfiles incrementais em diretórios isolados por alvo, com depfiles
`.o.d` e regra vazia explícita para evitar que Make tente construir `.d.o`.
Seus binários `_bin` pertencem a `BINS`; `test-build` compila artefatos, sem
executar cenários nesses alvos (exceto a receita histórica de `test_event_log_shim`,
que compila e executa no build). Os binários dependem dos objetos incrementais, das fontes
e do `Makefile`, para mudanças transitivas de headers e receitas/flags propagarem
até relink. Os wrappers `.PHONY` focados sempre executam o(s) cenário(s)
após garantir o binário atual. Os cenários estão no agregado uma única vez e
mantêm a ordem existente; `test_event_log_runtime` permanece após os quatro
cenários inicialmente migrados. Objetos dependem explicitamente do `Makefile`
para invalidar quando flags/receitas mudam. `clean` remove novos e antigos
binários, diretórios de objetos/depfiles e gcov correspondente.

Os alvos puros `test_ble_types`, `test_ble_state_machine`,
`test_ble_event_dispatch`, `test_ble_store` e `test_ble_command_parse` também
usam artefatos `_bin` e objetos/depfiles incrementais isolados por alvo. Os
includes e flags originais são preservados (`-Icontracts`, `-Ishim` quando
aplicável); `test-build` compila esses artefatos, enquanto wrappers `.PHONY` focados
sempre executam o binário atual. No agregado, os cinco cenários mantêm sua ordem
e executam uma vez cada. `clean` remove binários legados e diretórios de objetos/
  depfiles; o padrão existente de `.gitignore` cobre esses artefatos.

Os alvos incrementais por TU estão declarados no `Makefile`: objetos próprios
por alvo, depfiles `.o.d` em `MAKE_DEPFILES`, artefatos `_bin` em `BINS`, wrappers
`.PHONY` para executar cenários e limpeza dos diretórios. A ordem de TUs/link,
flags e includes originais é preservada. O contrato estrutural fixa a lista
dos alvos cobertos e verifica seus objetos, flags e dependências. Os pilotos
`test_screen_protection` e `test_wifi_audit` também têm contratos próprios,
`test_screen_protection_contract.py` e `test_wifi_audit_contract.py`, que cobrem
suas respectivas migrações.

`tests/host/keymap/test_incremental_batch_contract.py` cobre 30 alvos: os onze
alvos migrados anteriormente, os seis Tier-A de Wi-Fi event dispatch,
persistence queue/coordination, clock formatter, terminal filter e edit line,
os três alvos de indicador Wi-Fi, layout do ícone e menu, e os quatro alvos de
histórico, evento do teclado, log recente e máquina de estados Wi-Fi, além do
coordenador de rede, guarda de eco SSH, compositor de linhas SSH, mapa de teclas,
BMP e orientação. Verifica paridade
de TUs/flags contra o Makefile baseline,
dependências diretas/transitivas, controle negativo e mutações. O alvo
`test_incremental_batch_contract` executa esse contrato uma vez no agregado;
ele não é artefato de `BINS`.

Os 27 candidatos finais (shell/runtime, editor, diagnóstico de rede, bateria,
cat, BLE, display, known hosts e poweroff) usam objetos e depfiles próprios
`*_MIG_OBJS` no `Makefile`, mantendo a ordem e as flags de link de cada cenário.
`tests/host/keymap/test_incremental_final_contract.py` compara os 27 conjuntos
de TUs/flags e ordem de link à fixture imutável
`tests/host/keymap/incremental_final_baseline.json` extraída das receitas no
commit `59f4758`, verifica 176 depfiles, `BINS`, wrappers, agregado e limpeza;
o alvo `test_incremental_final_contract` roda no agregado após o build.
Os contratos `test_shell_help_contract.py`, `test_window_manager_contract.py`
e `test_coverage_scope_guard.py` verificam as ligações por objeto/depfile após
a migração, sem exigir a receita monolítica antiga.

O progresso é de 59/59 candidatos originalmente monolíticos elegíveis;
nenhum permanece pendente.
Os 13 alvos per-TU que já eram incrementais antes desta tarefa não integram esse
denominador. Também ficam excluídos os alvos inelegíveis `test_event_log_shim`,
`test_cat_worker_runtime` e `test_app_runtime_tsan`, e os objetos
`test_cat_runtime_*.o`. O inventário de alvos e objetos permanece no Makefile.

Os alvos excluídos `test_event_log_shim`, `test_cat_worker_runtime` e
`test_app_runtime_tsan` continuam com suas regras próprias; os objetos
`test_cat_runtime_*.o` não foram migrados. O alvo composto
`test_ble_bond_reconnect` depende e executa os
artefatos `_bin` de state machine, event dispatch e store, além dos dois
contratos, sem duplicar os cenários no agregado.

## Planos de aplicativos e recursos

Os planos numerados em `docs/` organizam a evolução proposta; o plano 01 foi
finalizado no escopo aprovado de DNS/TCP/HTTPS explícitos. A ordem é
intencional: diagnóstico de rede, levantamento Wi-Fi, player, inspeção BLE, IA
e então recursos transversais.

| Plano | Escopo |
| --- | --- |
| `docs/01-network-diagnostics-plan.pt-BR.md` | Plano finalizado para DNS/TCP/HTTPS explícitos; registra decisões, limites de timeout/cancelamento, evidência e lacunas do roteiro manual integral. `docs/NETDIAG.md` detalha o contrato; `README.md` e `README.pt-BR.md` ensinam a usar os comandos. |
| `docs/02-wifi-survey-plan.pt-BR.md` | Levantamento passivo de APs e tendências de sinal. |
| `docs/03-audio-player-plan.pt-BR.md` | Player local no SD via codec/saída de áudio Tab5. |
| `docs/04-ble-inspector-plan.pt-BR.md` | Inventário BLE e inspeção GATT explícita, sem coleta de segredos. |
| `docs/05-lan-ai-assistant-plan.pt-BR.md` | Assistente de texto via servidor de IA confiável na LAN. |
| `docs/06-investigation-notebook-plan.pt-BR.md` | Sessões locais de notas e relatórios sanitizados. |
| `docs/07-environment-comparison-plan.pt-BR.md` | Comparação consentida de inventários e medições salvos. |
| `docs/08-guided-diagnostics-plan.pt-BR.md` | Fluxos determinísticos de diagnóstico por camadas. |
| `docs/09-voice-notes-plan.pt-BR.md` | Gravação e reprodução local de notas de voz. |
| `docs/10-mission-profiles-plan.pt-BR.md` | Perfis de escopo, limites e retenção por contexto de uso. |

Os planos consideram o runtime atual de aplicativos compilados, as portas e
grants do SDK, os serviços existentes de Wi-Fi/BLE/SD e os testes host. Eles
devem ser revisitados contra o código vigente antes de cada implementação.

## Plano de poweroff

- Testes reais em 13/out/2026: após correções de stack PSRAM e autoexclusão BLE,
  stop BLE ocorreu sem panic, seguido de BOD/USB disconnect. Usuário confirmou
  desligamento físico e pós-relight íntegro; `sys.info`/`ping` passaram e
  `cat /.poweroff-sync` retornou `1`. Usuário confirma cenários bateria-only,
  bateria+USB e cabo externo; canário/hash verificados nos dois primeiros.
  Fault/timeout têm cobertura host. Não há implementação pendente conhecida; ver
  `poweroff-implementation-plan.md` para o caveat de CI remoto do delta local.
- Rodada com canário: cancelamentos `Y`/`yes` passaram; arquivo descartável de
  1272 bytes foi gravado e lido antes do shutdown (hash esperado
  `860367d95a4c84b3790ddf5306fc5d2807cfbce37e2c1e9128dfa4b18c96cc14`). Após
  uma confirmação `y`, usuário confirmou corte físico. Pós-relight `sys.info`/
  `ping` passaram e os 24 registros foram lidos exatamente; hash SHA-256 confere.
  Matriz de alimentação e falhas/timeout ainda pendente.
- Rechecagem posterior: agregado `make -C tests/host/keymap test` e os três alvos
  poweroff focados (`test_poweroff`, contrato e linker guard) passaram. Isso valida
  os cenários host/estruturais, não fault injection física no Tab5.
- Tentativa bateria + USB: boot/ponte/canário conferidos, uma confirmação admitida,
  USB desconectou às 20:30. Usuário confirmou corte físico; pós-relight `sys.info`/
  `ping` passaram e o canário permaneceu integral, com SHA-256 conferido. Cenário
  bateria+USB PASS.
- Cenário bateria-only executado pelo usuário e confirmado fisicamente; pós-relight
  `sys.info`/`ping` e readback/hash do mesmo canário passaram. Bateria-only e
  bateria+USB agora têm evidência de ciclo e persistência.
- Usuário confirmou ainda teste somente com cabo externo e que todas as formas de
  alimentação suportadas foram testadas. Não há hash pós-relight independente
  registrado para cabo-only; não falta outro ciclo de alimentação conhecido.

- Comando `poweroff` documentado em `README.md`/`README.pt-BR.md`; catálogo
  legado compartilhado passa a 18 entradas `[terminal]`, mantendo ordem das 17
  anteriores. Comandos dinâmicos de apps registrados são linhas `[app]` extras;
  contratos de paridade/help/BLE são atualizados com a nova entrada, não relaxados.
- Validação local: agregado PASS, gcovr 86.9% linhas / 95.5% funções / 59.3%
  branches (gates 80/94/59); guard 61 TUs cobertas e 16 hardware allowlisted,
  build ESP-IDF/linker PASS. Quality Gate remoto PASS no commit b0ce47f.
  Stack PSRAM foi admitida, BLE avançou sem assert, e o usuário confirmou
  desligamento físico e pós-relight correto. A barreira `.poweroff-sync` persistiu.
  A evidência local deste snapshot não conclui o gate de CI remoto; aguardar o
  resultado do Quality Gate para o delta atual.

- `tools/poweroff_link_guard.py` verifica no cross-reference map GNU que
  `ff.c.obj` realmente chama os hooks `__wrap_ff_disk_write/ioctl` do poweroff,
  não apenas que existem no ELF. CMake intercede os símbolos públicos `ff_disk_*`;
  erros, inclusive FSInfo que FatFS R0.15 mascara, são sticky e impedem unmount/
  pulso. CI roda o guard após build; `test_poweroff_link_guard.py` valida fixtures
  positivas e ausência/owner incorreto/hooks não referenciados. Não acessa drive.

- `components/cyberdeck/include/apps/ssh/cyberdeck_known_hosts_file.h` e
  `components/cyberdeck/src/apps/ssh/cyberdeck_known_hosts_file.cpp` controlam READ/WRITE, leitura
  bounded de known_hosts (128 KiB/linha 4096), write parcial/EINTR, fsync e
  fechamento verificado. `ssh_client` usa export/parse/comparação de chaves da
  libssh (incluindo matching de host/porta/hash), mas não seu I/O com fclose
  ignorado. `test_known_hosts_file.cpp` cobre o helper real com wrappers de
  falhas e gate; symlinks/arquivo oversized/erro de fechamento falham fechado.

- `components/cyberdeck/include/platform/power/cyberdeck_poweroff.h` e
  `components/cyberdeck/src/platform/power/cyberdeck_poweroff.cpp` implementam controller one-shot,
  ordem de quiescência com deadline global de 45 s, watchdog que mantém recursos
  vivos em timeout, barreira de metadata SD e pulso de fábrica B/P4. A task não
  faz teardown sob lock LVGL; UI apenas agenda e fica bloqueada/pausada. A admissão
  retorna causa tipada para coordinator ausente, one-shot consumido, fence ou
  alocação de watchdog/task; task principal mantém stack de 8192 bytes explicitamente
  alocada na PSRAM (`xTaskCreateWithCaps`/`MALLOC_CAP_SPIRAM`), habilitada por
  `CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM` em `sdkconfig.defaults`, preservando stack
  mesmo com heap interno fragmentado; falha na criação registra heap livre e
  maior bloco MALLOC_CAP_8BIT e INTERNAL|8BIT uma única vez via seam
  host-testável. Snapshots instantâneos auxiliam diagnóstico, mas não provam
  causa única. A
  shell distingue admissão da conclusão assíncrona.
- `cyberdeck_shell_session` exige linhas exatas `poweroff` e `y`, cancela qualquer
  outra confirmação ou Escape e impede retry. Cat tem quiesce permanente bounded;
  auditoria mantém recursos em timeout e recusa corte com export pendente.
- A instância única de SD em `app_main` também é publicada por `bind_global`:
  Wi-Fi config, backend de audit, Serial fs.write e known_hosts SSH usam leases;
  logger usa fence/leases próprios e é drenado após seus clientes. Falhas de
  fechamento/persistência são sticky e bloqueiam sync/unmount/corte.
- `screen_off_quiesce` drena FIFO de NVS por sentinel/ACK; BLE para antes de
  Wi-Fi; HTTP e Serial reportam falhas sem liberar recursos vivos. O BSP faz
  unmount VFS antes de retirar o LDO SD. `.poweroff-sync` é barreira FatFS de
  metadata, não sync global nem substituto do fsync/close por proprietário.
- `test_poweroff.cpp` liga o controller, metadata barrier e pulso reais com
  ports/wrappers de falha; `test_poweroff_contract.py` confirma wiring dos fences
  e BSP. `test_cat_worker_runtime.cpp` liga o worker ESP real com adapters em
  `cat_runtime_shim/`, shims compartilhados do logger e barreira em read: timeout
  preserva fila/lease, reject de start/enqueue, completion/join antes de cleanup
  único, no-reopen. Novos alvos entram no agregado/coverage. Teste no Tab5
  confirmou admissão com stack PSRAM, stop BLE sem panic e ciclo físico básico
  confirmado pelo usuário; pós-relight, `sys.info`/`ping` e marcador `.poweroff-sync`
  passaram. A matriz HW integral segue pendente no plano.

- `poweroff-implementation-plan.md` registra aprovações, implementação global,
  estratégia de sync/FatFS/BSP, matriz REQ/evidência/estado e gates restantes.
  É controle de progresso, não prova de corte físico.
  `tests/host/keymap/poweroff_traceability.md` mapeia os contratos host para
  REQ-POWER/AC-POWER e evidencia observador de falha de criação da task.
  `tests/manual/poweroff-validation.pt-BR.md` orienta flash pelo usuário,
  confirmação/cancelamento, rail/alimentação e persistência após religar.
  As notas históricas Stage 1 abaixo não delimitam o poweroff global atual.

## Atualizacao de recuperacao de eventos

- `test_event_log_runtime.cpp` acrescenta `partial-init`: falha injetada de
  criação do worker binário após iniciar `event_text`, restauração do hook
  serial, rejeição de novas operações, preservação dos handles de fila e
  quiescência/idempotência sem reopen. O shim oferece falha one-shot por nome
  e associa handle ao evento de startup para completion/join bounded.
  Esta cobertura não substitui o fence global de SD nem testes físicos.
- Validação local em 13/out/2026: agregado com coverage e gates gcovr
  80/94/59 passam (86.9% linhas, 95.3% funções, 61.0% branches); guard confirma
  59 TUs cobertas e 16 hardware allowlisted. Build ESP-IDF passa. O plano
  separa essa evidência local da aceitação global/CI remota e do corte físico.
- `text_log_task` aguarda completion do proprietário binário antes de parar e
  reconsulta a fila após observar completion, evitando perder publicação que
  correu com o timeout de receive. `quiesce-race` força um receive textual
  expirado enquanto o binário está retido em fsync e exige projeção única após
  a liberação. `partial-init` e `quiesce-race` entram nos cenários padrão do
  Makefile; permanecem lacunas de drain multi-item e outros pontos de init.

- Milestone 2 adiciona stop cooperativo dos workers proprietários após fence:
  `event_log_quiesce` aguarda leases e completion lógico, cada worker drena sua
  fila e fecha o próprio arquivo, e falhas binárias permanecem sticky até reboot.
  Timeout mantém `CLOSING`; uma nova chamada usa orçamento próprio. `CLOSED` é
  publicado somente após drain/completion sem falha. Projeção textual segue
  best-effort; recursos estáticos são preservados. Esta implementação requer
  revisão e os cenários M2 do tester antes de ser considerada validada.
  Revisão deste batch: a falha de init após iniciar apenas o worker textual
  mantém `CLOSING` e seus recursos; quiesce sinaliza stop e espera seu drain.
  Chamadores concorrentes reavaliam as condições atômicas de lease/completion
  após todo wake (o semáforo é apenas uma notificação), com deadline monotônico
  local; nenhum defeito adicional foi confirmado nesta inspeção.
- Histórico do milestone intermediário de lifecycle:
  `event_log_init` usa estados atômicos e publica `OPEN` somente após criar
  filas, semáforos, hook e workers; as APIs de I/O usam admission leases
  contadas até o fim da operação, incluindo o fechamento dos snapshots textuais.
  `event_log_quiesce(timeout_ms)` fecha o fence e aguarda leases, mas retorna
  explicitamente `ESP_ERR_TIMEOUT` mesmo após drenagem: este milestone não para
  workers, fecha arquivos/filas nem publica `CLOSED`, e não deve ser aceito ou
  liberado como quiescência concluída. Falha de init depois de criar qualquer
  worker mantém `CLOSING` e todos os handles/recursos estáticos preservados até
  reboot; init subsequente falha fechado. Falha anterior a workers faz rollback
  e pode retornar a `UNINITIALIZED`. `event_log_latest` permanece leitura da
  memória recente sob mutex e não participa do fence SD. Stop/completion e
  cleanup controlado pertencem ao milestone 2.

- `components/cyberdeck/src/platform/logging/event_log.cpp` mantem
  `events.log` como fonte binaria e projeta best-effort em `events.txt`, com
  rotacao `.1`..`.7` limitada a 1 MiB por arquivo; estado recente e coordenacao
  da projecao usam locks separados, e a leitura textual abre um snapshot
  bounded de descritores antes de liberar a coordenacao para I/O SD. O task de
  projecao tem rollback explicito no init; `event_log_text_size` e
  `event_log_text_read` fornecem leitura somente leitura bounded a 1024 bytes,
  com linhas normalizadas e timeout de mutex preservado.
  O task de log e acordado explicitamente após enfileirar eventos normais ou
  duráveis, mantendo uma espera de fallback bounded; a fila durável continua
   sendo sondada antes da espera e o ACK só ocorre após persistência/fsync; a
   API `event_log_write_durable` usa fila durável bounded prioritária, retorna
   `ESP_ERR_NO_MEM` somente quando nenhum enqueue foi aceito (retry seguro) e
   `ESP_ERR_TIMEOUT` quando o ACK de uma requisição já aceita expirou (sem
   retry, evitando duplicação por token de time_sync). A fila normal é drenada
   com `xQueueReceive(s_queue, &record, ...)` somente após a fila durável e
   antes do fallback de notify; binary e projection continuam separados.
- `components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp` adiciona
  `events.read`, stream NDJSON em chunks Base64 de no maximo 1024 bytes.
- `components/cyberdeck/src/apps/screenshot/screenshot_server.cpp` expoe o
  endpoint local fixo `GET /events.txt`, sem aceitar caminho arbitrario, junto
  do servidor HTTP existente.
- `tests/host/keymap/test_event_projection_contract.py` cobre
  TEST-EVENT-BOOT/APPEND/ROTATE/READ/SECURITY/REGRESSION: inicializacao e
  append best-effort, mutex/I-O desacoplado, timeout tipado, rollback de
  init/task, snapshot paginado estável sob rotação, normalização CR/LF,
  rotacao bounded, leitura somente leitura/EOF, stream Serial-JTAG
   `events.read`, separacao entre append duravel e I/O da projection, consumo estrutural da fila normal antes do fallback de notify,
   ACK/timeout durável sem duplicação por token, endpoint HTTP fixo e preservacao dos contratos Serial-JTAG
  existentes. `event_projection_traceability.md` mantém a matriz REQ/AC ->
  TEST; o alvo isolado `make test_event_projection_contract` também entra no
  agregado host `make test`. Como a implementação depende de ESP-IDF/SD, os
   cenários de I/O são contratos estruturais e modelos bounded determinísticos.
 - `tests/host/keymap/test_event_log_runtime.cpp` liga o TU real do event log
   com shims host dedicados (`event_log_shim/`) para filas, semáforos, tasks,
    BSP, timer e ESP logging, diretório SD temporário e `--wrap=fsync`. O runtime
     caracteriza init, append normal, ACK durable após fsync e falhas de
    write/fsync, além de timeout/lifetime determinístico do caller e backpressure
     da fila durável no TU real. Os cenários isolados `preinit`/`quiesce-fence`
     comprovam INVALID_STATE pré-init, timeout sticky e rejeição de writers/
     readers posteriores; `TEST-EVENT-VPRINTF-FENCE` observa forwarding serial
     sem persistência e `TEST-EVENT-QUIESCE-FENCE` bloqueia `fread(events.txt)`
     para provar lease ativa, timeout e retry sticky.
     O milestone não prova shutdown nem quiescência final. A matriz
       `tests/host/keymap/event_log_shim/event_log_shim.{h,cpp}` extrai os adapters
        host e oferece introspecção passiva de snapshots imutáveis da fila (handle,
        capacidade, tamanho de item e depth sob mutex), enumeração dos handles válidos
        registrados por `xQueueCreateStatic` sem lock aninhado, e espera bounded por depth;
       `test_event_log_shim.cpp` valida fila bounded FIFO/cópia e backpressure
       comportamental em fila de 4 ponteiros (rejeição cheia, sender bloqueado,
       wake no receive, payload/order preservados), semáforos e
      espera por deadline/wake com ticks falsos. A barreira de registro do timed wait
      e os wakes do fake clock sincronizam notificacoes sob o mutex de cada wait,
      evitando lost wakes sem confundir avanço de ticks com wake antecipado. O shim oferece barreiras de
      registro imediatamente antes de waits por notificação e timed waits; os testes separam
     wake unitário, contagem acumulada gated (`clear=false`/`clear=true`) e
      timeout fake-clock após registro do timed wait, aguardando conclusão das tasks. Não há
       `test_event_log_runtime.cpp` também caracteriza timeout/lifetime no TU real:
       após reter o fsync binário, avança o fake clock por 250 ms e exige
       `ESP_ERR_TIMEOUT` do caller; após liberar o fsync, o token aceito persiste
       exatamente uma vez. Em backpressure, mantém o worker retido no fsync:
       um request in-flight e quatro requests em fila são aceitos e expiram como
       `ESP_ERR_TIMEOUT`; após a liberação, cada um dos cinco tokens aceitos
       persiste exatamente uma vez. O sexto pedido retorna `ESP_ERR_NO_MEM` e
       seu token não aparece no log. Esses cenários caracterizam o logger atual,
       não implementam nem provam quiesce/fence. `make test_event_log_shim`
      é alvo isolado. O shim mantém handles/task threads joinable: `wait_for_test_task`
      observa completion com timeout, `join_test_task` só aceita task concluída,
      e cleanup libera handles já joined. `vTaskDelete(NULL)` só marca self-delete
      solicitado para retorno natural do callback; casos TEST-SHIM-TASK-COMPLETION-JOIN
      e TEST-SHIM-TASK-TIMEOUT provam completion/join e timeout sem falso completion.
      O runtime event-log não termina workers nesta etapa e preserva `_exit` para
      seus loops infinitos; nenhum join é feito em worker vivo.
        `test_event_log_runtime.cpp` inclui os casos M2 `quiesce-fence` (reader
        lease bloqueada, timeout/retry, close do owner, idempotência e no-reopen)
         e `sticky-close`, `sticky-write`, `sticky-fsync` e `worker-timeout-retry`.
         Os wrappers witness falhas binárias write/fsync após a API void e quiesce
         sticky; o worker real retido no fsync comprova timeout e retry com
         orçamento novo, persistindo o registro uma vez. O wrapper de
        fclose só injeta falha no descritor canônico `events.log`. A matriz
          `event_log_traceability.md` registra os cenários M2 já comprovados,
           incluindo falhas separadas de fwrite/fsync/fclose da projection durante
           quiesce, preservação do sucesso binário e corrida de caller durable timeout
              versus quiesce; `TEST-QUIESCE-M2-WORKER-COMPLETION` enfileira evento
                identificável, bloqueia fwrite em `events.txt`, inicia quiesce
                concorrentemente e prova que não teve sucesso antes da liberação e
                 drain. Identifica `s_quiesce_changed` pela ordem de criação,
                correlaciona nomes de startup a `std::thread::id` diretos, exige
                fechamento por worker antes do give e nenhuma operação de arquivo/
                fila depois. Prova drain de uma projection aceita, não de fila
                 múltipla; init parcial permanece gap explícito. `quiesce-race`
                 retém o worker binário em fsync, registra dois callers com budgets
                 independentes, avança além do deadline curto e exige timeout
                 somente de A antes de liberar o worker; B conclui após persistência/
                 drain e o close ocorre uma vez. O alvo runtime aceita `SCENARIOS=`
                 para executar somente cenários selecionados.
         `tests/host/keymap/event_log_traceability.md` separa essa evidência runtime
     dos contratos estruturais existentes; `make test_event_log_runtime` compila
      e executa o TU real no agregado host e no alvo isolado, usando root
      temporário por execução e mantendo os perfis gcov no diretório de teste
      para o gcovr/scope guard da mesma CI. `make clean` remove binários e
      sidecars gcov; o TU deixa a allowlist do coverage scope guard.
      Somente esse alvo define `CYBERDECK_EVENT_LOG_TEST`; o observer de release
      é implementado no TU de teste e sua execução após ACK durable é aguardada
       com watchdog bounded. O wrapper fsync identifica por caminho canonicalizado
       o descritor exato `<EVENT_LOG_TEST_ROOT>/logs/events.log`: somente esse
       descritor pode consumir a barreira binária ou a falha injetada; fsync de
       `events.txt` delega imediatamente ao host. O runtime comprova projeção
       fsync enquanto o fsync binário está retido. Todos os waits de sincronização do harness runtime
       e shim expiram em cinco segundos e falham explicitamente.

Mapa de navegacao do OS embarcado simplificado ESP-IDF para o unico alvo
M5Stack Tab5 (ESP32-P4). Os caminhos abaixo sao relativos a raiz do repositorio.

## Visao geral

### Editor de texto (`edit`)

- `components/cyberdeck/include/apps/editor/cyberdeck_editor_model.h` e
  `src/apps/editor/cyberdeck_editor_model.cpp` formam o modelo host-testavel:
  buffer de 12000 bytes, historico undo/redo heap-backed bounded por 8 snapshots
  e 32768 bytes por trilha, edicao UTF-8 em fronteiras bounded de codepoint,
  busca, teclas direcionais,
  gesto e conversao UTF-8/UTF-16 LE/BE/Windows-1252. Arquivos novos sao UTF-8
  sem BOM/LF; arquivos existentes preservam codec, BOM e EOL.
- `include/apps/editor/cyberdeck_editor_app.h` e `src/apps/editor/cyberdeck_editor_app.cpp`
  registram `cyberdeck.editor` como foreground compilado e implementam `edit`,
  parsing bounded de argumentos para `edit <arquivo>`, confirmacao de save-as sem mutacao antecipada,
  entrada validada pelo window manager e save atomico via SDK. O catalogo declara
  `edit` uma unica vez no campo de comandos.
 - `cyberdeck_app_storage.{h,cpp}` expoe leitura bounded e a transacao de escrita
  temporario -> flush/fsync_file -> commit sem overwrite no mesmo diretorio; se o
  destino existir, o backend serializa a reserva do sidecar exclusivo `.rollback`,
  move o original para backup e tenta restaurar em falha. O resultado distingue
  stage/errno primarios de rollback_stage/rollback_errno e rollback_attempted;
   `fsync_directory` e best-effort. A facade resolve caminhos relativos com
   cwd virtual `/data`, aceitando alvos absolutos apenas quando a resolucao
   permanece no namespace mutavel; recusas de politica nao reutilizam errno
   stale. Cada chamada revalida grant e
  lifecycle; o backend permanece confinado e nao entrega
  VFS/descritor ao app. A leitura do editor usa o `cat_bounded`, que preserva
  ENOENT somente para ausencia real, com rejeicao por tamanho antes da abertura
  e nunca trunca a resposta; EACCES/ELOOP/EMFILE e falha de secure-open
  permanecem erros. O grant de input e revalidado no execute/handle_key apos o
  ciclo running. fsync usa somente descritor de escrita. O app e registrado
   em `cyberdeck_system_apps.cpp`.
- `include/platform/display/cyberdeck_editor_view.h` e
  `src/platform/display/cyberdeck_editor_view.cpp` formam a superfície LVGL
  própria do editor: slots fixos para linhas, documento/cursor e uma única linha
   bounded de rodapé para os atalhos `Ctrl+F Buscar`, `Ctrl+S Salvar` e
   `Ctrl+Q Fechar`, além da sigla do encoding, com capacidade calculada pela
    altura útil atual.
   Falhas de Ctrl+S e Ctrl+Q+Enter mantêm `save_diagnostic` no status/footer
   bounded visível do editor, além do event log/scrollback; atalhos diferentes
   de Ctrl+S limpam o diagnóstico anterior antes de executar, enquanto Ctrl+S
   limpa no início de `save_current` e preserva/repopula o diagnóstico em falha;
   fechar sem salvar também limpa o estado. `render_terminal` alimenta
   o footer sem ocultar a mensagem pelo estado de atalhos/diálogo/encoding.
  `set_bottom_inset` reduz a superfície quando o teclado virtual ocupa a base;
  eventos de resize recalculam footer, slots e limites de scroll/cursor para
  retrato/paisagem. O render permanece bounded e corta somente em fronteiras de
  codepoint UTF-8, com toque/gesto preservado. A UI a cria após o contexto do
  window manager e a destrói no teardown; a view não acessa VFS nem serviços.
- A superfície e o roteamento cobrem `TEST-EDIT-UI/INPUT/COMMANDS/DIRTY/NEW`,
  `TEST-EDIT-SAVE-FAILURE/LIFECYCLE/SERIAL`: atalhos e confirmação permanecem
  na app, enquanto a view só renderiza e traduz scroll bounded. Ctrl+C/V/X são
  rejeitados explicitamente (clipboard ainda não faz parte da UX), e o teclado
  virtual roteia caracteres, Enter, backspace e setas para o mesmo modelo do
  teclado físico.
- `tests/host/keymap/test_editor_model.cpp` cobre limites, UTF-8, UTF-16 LE/BE,
  Windows-1252, rejeicao de binarios/bytes invalidos, EOL e undo/redo;
   `test_editor_runtime_contract.py` verifica SDK/VFS, facade de storage unica,
   dispatch de `edit`, ENOENT de arquivo ausente, documentacao, limite e save
   atomico/fsync, trim/save-as, wiring de input, rejeicao oversized antes de
      abrir e CR isolado. `test_editor_save_contract.py` executa um fake backend
      host comportamental (lambdas de falha) e fixa resultados estruturados de
       storage (status, stage, errno e campos de rollback), ordem write-temp -> flush/fsync -> rename,
      fsync de diretorio best-effort, preservacao do original/dirty/dialog em
      falhas, orphan `.tmp`, revalidacao de grant/lifecycle, Ctrl+S,
      Ctrl+Q+Enter, D, ENOENT versus EACCES/ELOOP/EMFILE e orphan temporario.
      `test_storage_facade.cpp` liga os TUs reais de storage facade e shell
      local; um seam de linker redireciona `/sdcard` para um diretório
      temporário confinado e cobre a transação real write-temp -> flush/fsync
       -> rename -> fsync-directory, destino existente/ausente, colisão `.rollback`
        sem overwrite, preservação do original e sidecars, leitura, falhas de I/O e grant revogado.
       O wrapper `link` falha somente publicação e restauração identificadas pelos paths completos;
       o cenário confirma errno/stages estruturados e destino ausente com backup/temp íntegros.
      `test_editor_surface_contract.py` cobre a superfície
      e2e, slots bounded, footer de uma linha bounded com atalhos e UTF-8/UTF-16LE/
      UTF-16BE/Windows-1252, truncamento com sigla preservada, altura dinâmica,
      inset de teclado virtual, retrato/paisagem/resize, cursor/scroll clampados, cursor UTF-8,
     scroll/gesto, atalhos/diálogo, arquivo novo, falhas atômicas,
      grants/teardown e integração no Makefile. `test_ui_resource_contract.py` contabiliza a superficie
   do editor e seu teardown. O harness de capabilities inclui explicitamente a
    storage facade para manter o agregado host compilavel. Os contratos de runtime, save e superfície são executados por `make test`; o app
  estruturalmente coberto permanece na allowlist explicita do scope guard.
  A matriz formal `tests/host/keymap/editor_traceability.md`
    rastreia `REQ-EDIT-01..19`/`AC-EDIT-01..19` para
  `TEST-EDIT-RUNTIME`, `TEST-EDIT-VFS`, `TEST-EDIT-CODEC`, `TEST-EDIT-MODEL`,
  `TEST-EDIT-SAVE`, `TEST-EDIT-INPUT` e `TEST-EDIT-DOC`, incluindo cenarios
  positivos, negativos, de limite e de falha.

- Plataforma: ESP-IDF 6.1, LVGL >=9.6,<10.0, BSP `m5stack_tab5` 1.3.2.
- `version.txt` na raiz define `PROJECT_VER` pelo mecanismo nativo do ESP-IDF
  (prioridade sobre argumentos VERSION e `git describe`); use inteiro positivo
  como identificador independente de CI para builds locais/físicos e incremente-o
  para a próxima versão de firmware. O valor é exposto como versão do app/firmware.
- Aplicacao: uma unica tela TUI LVGL, com header, relogio, indicador Wi-Fi, bateria e terminal.
- Organizacao: `components/cyberdeck/src/apps/` contem fluxos de produto;
  `components/cyberdeck/src/platform/` contem integracoes de hardware e runtime.
- A logica pura e extraida para testes host; `main/app_main.cpp` faz a composicao
  das implementacoes dependentes do ESP-IDF.
- A inicializacao da bateria e um fluxo nao fatal: o reader INA226 publica
  snapshots a partir de task dedicada e a UI apenas os consome.
- Artefatos de cobertura gcov comprimidos (`*.gcov.json.gz`) sao ignorados pelo
  Git para manter o repositorio livre de relatorios gerados.
- A arquitetura e o plano de transformacao ficam em `docs/ARCHITECTURE*.md` e
  `docs/OS-TRANSFORMATION-PLAN.pt-BR.md`, com as camadas platform/hardware,
  kernel/runtime, SDK e apps, status das Fases 1-10 e backlog aprovado.
- `components/m5stack_tab5/` é o BSP vendored do único alvo; sua API é
  documentação upstream/gerada e não o SDK de aplicações.

`AGENTS.md` documenta o procedimento operacional para agentes validarem o
firmware no dispositivo pela ponte USB Serial-JTAG, usando a CLI, comandos de
UI, `term.dump`, `ui.dump`, `screen.dump` e coleta de evidencias. `term.dump` é
a API preferencial para recuperar e validar texto/retorno após ações como
`ui.type`, por ser mais rápida e objetiva que screenshot/OCR; `ui.dump` fica
para estrutura/estado de widgets e `screen.dump`/screenshot para validação
visual. O roteiro executavel detalhado fica em
`tests/manual/serial-bridge-validation.pt-BR.md`.

## Boot do terminal

O export worker da pesquisa Wi-Fi em `cyberdeck_ui.cpp` é explicitamente
app-lifetime: filas bounded de capacidade 1 e task persistem entre
`cyberdeck_ui_deinit()`/`init()`, e a task não mantém ponteiros de UI. Um job só
é liberado ao worker após commit; o worker executa a persistência com stack de
8192 bytes, limpa payload/path antes de publicar completion e mantém o resultado
na fila bounded até o pump da UI voltar. O gate busy impede aceitar outro job
enquanto a completion aguarda consumo; resultados são aplicados ao controller
por token, inclusive após reinit. Falhas defensivas de gate/queue usam um token
de falha bounded, e o pump encerra a admissão via controller antes de liberar busy;
apenas tokens atuais geram mensagem visível. Confirmação repetida enquanto há
export ativo é rejeitada sem substituir a confirmação existente. Falha de criação da task deixa as filas
process-lifetime reutilizáveis e rejeita o export como indisponível; não reduz a
stack para contornar falta de heap. Strings temporárias do port são zeradas após
admissão, inclusive falhas, e buffers do job são zerados pelo worker.

`components/cyberdeck/src/platform/display/cyberdeck_ui.cpp` limpa o scrollback
durante a inicialização sem acrescentar banner; o prompt e as demais saídas
continuam sendo renderizados pelos fluxos existentes.

## Coordenacao de I/O SD (Stage 1)

- `test_wifi_audit_persistence.cpp` mantém o rename retido até observar a
  conclusão do enqueue concorrente (watchdog de 5 s), exigindo
  `rejected_queue_full`. Evita confundir startup da thread com execução do
  enqueue, fonte de flakiness no agregado com coverage; produção Wi-Fi intacta.
  Também cobre erros de inspeção dos sidecars e falha de unlink durante recovery,
  exigindo fail-closed sem escrita; `test_wifi_survey.cpp` cobre desempates de
  ordenação por BSSID/canal e lote de callback nulo sem alterar amostras.
  Resultados focados atuais e limites globais constam em
  `tests/host/keymap/wifi_survey_traceability.md`.

- As fixtures de `test_shell_session.cpp` e `test_shell_app.cpp` vinculam um
  coordenador OPEN por host. Regressões comportamentais exigem rejeição de
  `cd` em CLOSING e `touch` em CLOSED, cwd preservado e zero leases; o teste
  da aplicação também exige que stop/start do console não reabra o gate.
  `cat_io_traceability.md` registra TEST-FIXTURES-04/05. Isto não comprova
  quiescência global nem autoriza corte físico.

- `components/cyberdeck/include/platform/storage/cyberdeck_sd_io_coordinator.h`
  e `src/platform/storage/cyberdeck_sd_io_coordinator.cpp` fornecem leases
  move-only RAII READ/WRITE e gate OPEN/CLOSING/CLOSED. `begin_close` bloqueia
  novas admissoes; `drain` espera pelo zero de leases ate deadline monotonico
  fora da secao critica; timeout mantem o gate em CLOSING e as leases ativas.
  O dono pode reabrir somente enquanto CLOSING.
- `main/app_main.cpp` possui a unica instancia do coordinator durante a vida do
  processo e a injeta na recuperacao de sidecars, na UI, no shell local e no
  runtime de apps/editor. O editor adquire READ para open e uma unica WRITE para
  toda a transacao de save; a recuperacao de sidecars usa WRITE durante toda a
  operacao. Todas as operacoes sincronas do shell local que acessam caminhos SD
  (`ls`, `cd`, `touch`, `mkdir`, `rm`, `rmdir`) adquirem leases antes de
  qualquer I/O e as mantêm durante a operacao; `cat` e executado pelo worker
  assincrono e usa READ lease por request quando o coordinator compartilhado e
  injetado pela UI. Stage 1 inclui as operacoes sincronas locais do shell (nao
  cat), a transacao de leitura/save do editor e a recuperacao de boot. A
  integracao READ do cat tem evidencia estrutural parcial; lifecycle/fence
  global permanece aberto.
- `tests/host/keymap/test_sd_io_coordinator.cpp` cobre o contrato de leases/gate;
  `test_sd_io_integration_contract.py` verifica a integracao e injecao. Os casos
  de gate fechado para `ls`, `cd` e `cat` ficam em `test_local_shell.cpp`.
  `test_ssh_output_gap.py` e `test_prompt_behavior.py` incluem ajustes de link
  do harness para o coordinator.
- Exclusoes explicitas do Stage 1: escritores async globais, event log,
  WiFi/audit, Serial/global `vprintf`, SSH, leitores (incluindo
  screenshot) e outros caminhos assincronos. Sync global, unmount e poweroff
  tambem estao fora; o coordinator nao e um guard global.

Para tarefas que envolvam `idf.py` ou outros recursos do ESP-IDF, o arquivo
`AGENTS.md` tambem orienta usar `IDF_PATH` para localizar o diretorio padrao
do IDF na maquina e carregar o ambiente quando necessario.

## Pontos de entrada e fluxo de boot

| Ponto | Arquivo | Responsabilidade |
| --- | --- | --- |
| `app_main()` | `main/app_main.cpp` | Inicializa NVS/recovery antes do SD para conhecer safe mode; no boot normal alimenta e conecta ESP-Hosted antes de montar SD, usando mount BSP com init/deinit no-op para slot 0 enquanto Hosted possui SDMMC slot 1. Em safe mode pula Hosted. `HOSTED_DIAG` registra timestamps, resultado e duração da preparação Hosted, mount SD e inicialização da proteção de bateria, sem dados de credenciais. Depois recupera sidecars, inicia log, display/LVGL, IMU/UI, proteção de tela, bateria, teclado e supervisor. A admissão de comandos do runtime fica fechada antes da exposição da UI e abre após startup/checkpoint. Falhas da plataforma ou da superfície essencial preservam boot pendente; falhas apenas em serviços opcionais são registradas, mas não bloqueiam checkpoint. |
| `cyberdeck_system_apps_start()` | `components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp` | Tenta iniciar todos os apps; recovery considera pronto quando `cyberdeck.shell` e `cyberdeck.serial` estão `running`. Falhas de Wi-Fi/Bluetooth/time sync/screenshot/editor/netdiag/music são persistidas/logadas e deixam estado degradado sem impedir checkpoint, desde que shell/Serial-JTAG estejam operacionais. O serviço BLE não é idempotente: se `nimble_port_init()` falhar parcialmente, o runtime não repete a inicialização no mesmo boot. Falha de init BLE registra heap interno livre/bloco maior e retorna erro, sem assert/retry automático. Dependências ainda podem fazer um app essencial falhar e manter boot pendente. Contratos: `test_phase10_contract.py::test_optional_service_failures_do_not_block_essential_checkpoint`, `test_system_apps_contract.py` e `test_ble_integration_contract.py`. |
| `cyberdeck_ui_init()` | `components/cyberdeck/src/platform/display/cyberdeck_ui.cpp` | Compoe a tela TUI, registra callbacks/timers e inicializa os componentes visuais de header, terminal e teclado. |
| `runtime::execute_line()` startup fence | `components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp` | Rejeita dispatch com `system startup in progress` enquanto `app_main` inicia apps. O flag atomic é desabilitado antes de UI/teclado e reabilitado depois do checkpoint, evitando que `app start`/comandos de apps concorram com `runtime::start_all()`. Cobertura comportamental em `test_app_runtime.cpp` e estrutural em `test_phase10_contract.py`. |
| `wifi_mgr_hosted_transport_prepare()` / `wifi_mgr_start()` | `components/cyberdeck/src/apps/wifi/wifi_mgr.cpp` | `hosted_transport_prepare` alimenta o C6, aguarda 500 ms e inicializa/conecta Hosted antes do SD; `wifi_mgr_start` reutiliza esse transporte e depois inicia Wi-Fi/reconexão. `HOSTED_DIAG` registra fases de rail, `esp_hosted_init`, `connect_to_slave`, `esp_wifi_init/start` e duração/resultado de `esp_wifi_set_config`, sem SSID/senha. `CONFIG_ESP_HOSTED_AUTO_CALL_INIT_BEFORE_APP_MAIN=n` evita init prematuro no constructor. |
| `bsp_display_start()` / `bsp_display_lcd_init()` | `components/m5stack_tab5/src/bsp_display.c` | Inicializa painel DSI/LVGL e touch. Em SoCs com `SOC_PSRAM_DMA_CAPABLE`, buffers de desenho e rotação SW usam PSRAM com capability DMA; demais SoCs preservam o allocator DMA padrão. Telemetria registra memória livre/maior bloco compatível, DMA e interna. Falha ao criar display limpa painel/PHY e encerra antes do touch; falha no touch remove display, limpa painel e desinicializa LVGL. Regressão estrutural no `test_ble_integration_contract.py`. |
| Overlay `esp_lvgl_port` 2.9.0 | `components/esp_lvgl_port/src/lvgl9/esp_lvgl_port_disp.c` | `main/idf_component.yml` fixa a versão de registry `^2` mas substitui o componente gerenciado via `override_path`. O cleanup do `lvgl_port_add_disp_priv` marca explicitamente falhas de alocação, tolera contexto nulo, remove display parcial e libera buffers/semaphore antes de retornar `NULL`; evita retorno ambíguo/vazamento no erro de rotação. O build do IDF seleciona o arquivo local (ver `build/build.ninja`). |
| `screenshot_server_init()` / `screenshot_server_start()` / `screenshot_server_stop()` | `components/cyberdeck/src/apps/screenshot/screenshot_server.cpp` | Lifecycle do servidor HTTP por task proprietária, com fila bounded de snapshots/comando, handshake de prontidão, join/quiescência antes de `httpd_stop()` e quarentena fail-safe em timeout. |
| `tab5_keyboard_init()` | `components/cyberdeck/src/platform/input/tab5_keyboard.cpp` | Inicia a task do teclado fisico e entrega eventos a `cyberdeck_keyboard_input`. |

### Persistência do recovery

`cyberdeck_recovery.cpp` serializa init, checkpoint, clear e gravação de erro
com um mutex de estado. Transições são preparadas numa cópia e só publicadas em
RAM após `nvs_commit`; falha de persistência mantém o estado anterior e marca a
persistência indisponível. `current()` retorna snapshot por valor. Falha de
leitura/abertura NVS seleciona safe mode. `clear_safe_mode` remove latch e
contador sem cancelar `boot_pending`, permitindo que um clear recebido durante
startup não invalide o `commit_ready` subsequente. `test_recovery_runtime.cpp`
liga o adapter real a um fake NVS e cobre indisponibilidade fail-closed,
commit falho, concorrência clear/checkpoint e sobrevivência a reinit. O TU agora
é executado e coberto no host, removido da allowlist; a policy pura permanece em
`test_recovery_policy.cpp`.

The same adapter keeps a separate 2 KiB rolling diagnostic journal in the
`cyberdeck_rec/diag` NVS key (1900 bytes, below the NVS single-blob limit), independent from SD/event-log availability. It
records the loaded blob and before/after `boot_begin`, `clear`, and `checkpoint`
state plus persistence results; `events.read` exports it as a distinct bounded
`recovery_log` payload. `test_recovery_runtime.cpp` covers NVS journal reinit and
bounded reads, while `test_term_dump_cli.py` covers reconstruction of the serial
frames. Use `python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 events.read` to
capture both the SD text log and the NVS recovery journal without opening a
second serial reader. The journal is best-effort and never changes the recovery
state or blocks boot if its own NVS write fails; journal commit errors are logged
to UART. The CLI writes decoded payloads
to `/tmp/opencode/cyberdeck-sd-event-log.txt` and
`/tmp/opencode/cyberdeck-recovery-journal.txt` for inspection.
Physical capture after flashing hash
`32ff8e16224d63e74f4eef3f7e899e1d667d5b817ddb8ca4a63b0399cbb4d802` showed two
boots with prior `safe=1,pending=0,interrupted=0`, followed by `boot_begin` and
`checkpoint`; no `clear` transition was recorded by the instrumented image.
`sys.info` remained safe mode with rising uptime. This proves the latch was
already present before those boots, but not which earlier actor set it. No
further clear/reset was sent; Hosted/app validation remains blocked. Code search
shows only `begin_boot` (sets after the three-interruption threshold) and
`clear_safe_mode` (explicit clear) mutate the latch; `commit_ready` does not.
The SD text log contains concatenated historical boots, so it cannot attribute
the current persisted value to a specific event.
The later clear command did return `cleared=true` and immediate `sys.info` showed
`safe_mode=false`, but its transition journal was not captured before reboot.
The next captured boot read `prior_safe=1,pending=1,interrupted=3` and completed
in safe mode. Therefore persistence/reversion remains unresolved; no further
clear or reset is authorized. `tools/cyberdeck_cli.py sys.safe_mode.clear` now
exposes the bridge's existing explicit command and has a host request-shape
assertion in `test_term_dump_cli.py`; running that CLI command changes device
NVS and requires explicit operator authorization.

Validação de 2026-10-23: `cyberdeck_system_apps_start()` agora separa sucesso da
superfície essencial (shell + Serial-JTAG) de falha em serviços opcionais;
todos os apps continuam sendo tentados e falhas permanecem registradas. Host
completo e cobertura CI passam (87.0% linhas / 94.8% funções / 59.0% branches;
73 cobertos / 15 allowlisted), build ESP-IDF 6.1 e poweroff link guard passam.
O flash de validação respondeu `safe_mode=true`; após um clear e reset
controlados, o boot normal alcançou checkpoint com shell/Serial-JTAG prontos,
apesar de Wi-Fi/Bluetooth degradados. Uma tentativa anterior emitiu comandos
antes do checkpoint do novo boot e a captura registrou HP WDT. Saved PCs apontam
para o handler de panic/stack guard do Core1, mas não identificam a task/origem.
O gate atômico em `runtime::execute_line` foi flashado e validado: aos 5 s,
comandos de app são rejeitados; após checkpoint explícito, `safe_mode=false`,
`interrupted_boots=0` e uptime crescente. `cyberdeck.music` iniciou e listou a
faixa esperada. Um único `music play 1` foi aceito pela ponte; a escrita serial
da consulta seguinte expirou, então o cenário foi interrompido sem repetir play.
Nenhum WDT/panic posterior consta na captura, mas o status final do player e
áudio audível não foram confirmados. Não atribuir a falha de serial ao player;
Hosted RPC TX falhava e Wi-Fi/Bluetooth estavam degradados. Host/TSan/coverage/
build passam. Ver capturas e hashes em `docs/03-audio-player-plan.pt-BR.md`.

Ordem relevante de inicializacao:

1. `nvs_flash_init()` e `cyberdeck_recovery::init()` para decidir safe mode.
2. Fora do safe mode, `wifi_mgr_hosted_transport_prepare()` inicializa Hosted/C6.
3. Montagem SD: `bsp_sdcard_mount_hosted()` com Hosted ativo; mount comum no safe mode.
4. `bsp_sdcard_get_handle()` e recuperacao de sidecars; entao `event_log_init()`.
5. `bsp_display_start()`.
6. Sob lock do display: `imu_reader_start()`, `cyberdeck_ui_init()` e `screen_off_init()`.
7. Apos liberar o display: `battery_protection_start()`; o adaptador inicializa o reader INA226 sensor-only e falha apenas gera log.
8. Callback do teclado, `tab5_keyboard_init()` e brilho.
9. `screenshot_server_init()`, callback de estado do screenshot e `wifi_mgr_start()`.
10. O recovery confirma prontidao somente apos toda a ordem acima; em safe mode
   apenas event log, shell e Serial-JTAG sao iniciados.

## Funcionalidades

### Lifecycle Wi-Fi (Etapa 5)

`wifi_mgr_start()`/`wifi_mgr_stop(timeout_ms)` em
`components/cyberdeck/src/apps/wifi/wifi_mgr.cpp` usam estados explícitos e
join cooperativo/quarentena para `wifi_event_worker` e `net_worker`; o system
app usa `stop_wifi()` com orçamento de 8000 ms. O contrato estrutural fica em
`tests/host/keymap/test_wifi_mgr_lifecycle_contract.py` e é integrado ao
Makefile host. Seus cenários `TEST-DIAG-WIFI-*` verificam que logs de
instrumentação cercam as chamadas existentes de Hosted/Wi-Fi, preservam
resultado/duração, não duplicam chamadas e não mencionam credenciais.
`tests/host/keymap/test_boot_sequence.py` cobre `TEST-DIAG-BOOT-*`: ordem dos
marcadores de boot em torno de Hosted, mount SD e bateria, incluindo a política
não fatal já existente para falha de inicialização da proteção.

Esta entrada supersede o recorte histórico da tabela de Wi-Fi: o stop público
e o teardown simétrico dos dois workers já estão implementados; timeout não
destrói recursos potencialmente usados.

### UI e terminal

### Diagnóstico de rede (controller, adapters e app)

- `components/cyberdeck/include/apps/network/cyberdeck_netdiag_controller.h` e
  `src/apps/network/cyberdeck_netdiag_controller.cpp` definem o controller puro
  de orquestração `cyberdeck_netdiag::controller`: discriminant seleciona uma
   operação explícita DNS/TCP/HTTPS, valida apenas seus campos (inclusive URL
   HTTPS somente com autoridade, sem caminho) e despacha somente
  essa operação (TCP pode resolver o host como requisito interno; HTTPS não
  inicia DNS/TCP explícitos). Mantém token, admissão/rate limit, cancelamento
  lógico, grant revalidado por etapa, deadline por operação e publicação stale-safe.
  As portas de DNS/TCP/HTTPS/clock
  são injetadas. `cyberdeck_netdiag_dns_adapter.{h,cpp}` implementa a porta
  `resolve_ipv4` com uma única slot estática; submissão e resolução ocorrem no
  tcpip_thread, resultado inline/assíncrono é suportado, e timeout deixa a slot
  ocupada até callback tardio (ou reboot), sem liberar contexto ainda referenciado.
  O adapter aceita apenas IPv4 e não inicia operação HTTP/TCP. O
  `cyberdeck_netdiag_tcp_adapter.{h,cpp}` implementa `tcp_connect` com IPv4
  explícito, socket TCP não bloqueante, deadline monotônico, espera writable em
  fatias bounded, validação de `SO_ERROR` e fechamento local do descritor em
   todos os caminhos proprietários; não envia payload nem inicia HTTP/TLS. Ambos
   são usados pela integração runtime/app. REQ-ND-01..08.
   `tests/host/keymap/test_netdiag_controller.cpp` liga o TU real com portas
   determinísticas e ASan/UBSan para admissão, validação, rate limit, estágios,
   falhas, grant, cancelamento e snapshot; TEST-ND-01..04 verificam
   dispatch isolado por operação, sem tráfego de etapas implícitas.
   `test_netdiag_dns_adapter.cpp` liga o TU real do DNS
  com shims determinísticos apenas para tcpip/DNS/FreeRTOS/clock; cobre resultado
  inline/cache/async, callback inválido, enqueue failure, slot ocupado, timeout,
  retenção do contexto e callback tardio/race no limite via alvo focado
   `make test_netdiag_dns_adapter`, também executado no agregado.
   `test_netdiag_tcp_adapter.cpp` liga o TU real do adapter TCP e shima somente
   socket/fcntl/select/getsockopt/close/clock; cobre conexão imediata e
   não bloqueante, recusa, SO_ERROR, deadline, falhas de select, entrada inválida
    e ownership de close. Não prova rede real nem integração runtime.
    `cyberdeck_netdiag_https_adapter.{h,cpp}` implementa `https_head` com
    `esp_http_client` assíncrono sequencial; após cada `ESP_ERR_HTTP_EAGAIN`,
    cede uma vez ao scheduler via `vTaskDelay(1)` antes do próximo `perform`,
    sem chamadas concorrentes no mesmo handle. Mantém HEAD sem payload, CRT bundle,
   redirecionamento desabilitado, rejeição de corpo e deadline monotônico; não
   cancela concorrente nem faz fallback GET. Metadata de headers do ESP-IDF
    não tem limite rígido garantido. O adapter está integrado ao app e ao harness
     host. Os alvos focados de controller/DNS/TCP/HTTPS compilam os TUs reais com
     `--coverage` além de ASan/UBSan, para que os caminhos exercitados integrem
     o mesmo relatório gcovr do agregado. `test_netdiag_https_adapter.cpp` liga
     o TU real com shims determinísticos
    somente para HTTP-client/CRT-bundle/clock/FreeRTOS delay; cobre HEAD, URL/hostname,
    EAGAIN e delay único entre retries, EAGAIN para sucesso/deadline sem perform
    concorrente, redirect, 405 sem GET, body reject, erros e cleanup sob ASan/UBSan.
    `netdiag_traceability.md` rastreia TEST-ND-04/05 e registra os limites de
    cobertura/aceitação. `test_netdiag_app` executa harness comportamental real
    da app/runtime/controller/adapters (47 checks), incluindo timeout/reap e
    quarentena de restart; ainda não prova integração de rede/TLS real nem todos
    os caminhos de falha do runtime de plataforma. Scope guard exige app e os
    quatro TUs controller/adapters cobertos, sem allowlist; fixture negativa
    comprova falha quando falta o TU da app. O agregado executa o app harness
     antes de gcovr/guard. Com GCC/gcov 13.4 local, o agregado passa o mínimo
     de 59% branches (59,3%); o toolchain 15.2 mede branches de modo diferente.
      Os commits `dbe3a94` (`38916411814`) e `8e41338` (tentativa 2 de
      `38918012957`) passaram no Quality Gate remoto; commits posteriores
      ainda dependem de CI no novo estado. `netdiag_traceability.md` registra
       a validação Serial-JTAG direcionada no Tab5 (status, DNS, TCP, HTTPS 200,
       405 final, `/head` funcional, redirect rejeitado, rate limit e
       cancelamento); uma captura serial posterior confirmou `Failed to verify
       certificate`/falha de handshake TLS e `https_failed` para certificado
       autoassinado. O certificado expirado continua inconclusivo (timeout);
       não houve captura de pacotes nem prova de todas as corridas de teardown.
    Metadata de headers não possui limite rígido garantido.
    `cyberdeck_netdiag_app.{h,cpp}` compõe o controller real com os três adapters
   e um worker por token. O comando conserva `app_grant()` opaco para revalidar
   autorização em cada etapa; o construtor aceita `application_platform` para
   substituir clock/adapters/task/wait sem duplicar a lógica de aplicação.
    Stop cancela logicamente e aguarda com timeout; timeout mantém worker,
    controller e contexto vivos, e bloqueia start/restart/operações até completion.
    Completion só libera o handle via `wait_worker` (join/reap) uma vez; stop
    pendente é finalizado antes de novo start, sem reset/restart inseguro.
     Registro permanece em `cyberdeck_system_apps.cpp`; manifest e comando
     `netdiag` são preservados.

## Player de áudio local (`music`)

- `components/cyberdeck/include/apps/music/cyberdeck_music_model.h` e
  `src/apps/music/cyberdeck_music_model.cpp` definem os limites bounded da fila
  (64 faixas, caminhos/títulos limitados, MP3 até 32 MiB), estados, seleção,
  progresso, seek, volume, shuffle/repeat e parser ID3v2.2–2.4 para título e
  artista. O app `cyberdeck.music` em `cyberdeck_music_app.{h,cpp}` registra o
  comando de terminal `music` como app `background` (não participa da exclusividade
  de foreground): `queue /diretorio` inicia scan assíncrono,
  `list`, `play [n]`, `pause`, `resume`, `stop`, `next`, `prev`, `seek
  <segundos>`, `volume`, `shuffle`, `repeat`, `status` e `help`. O scan limita a
  64 MP3 regulares e consulta o secure-open do shell; mantém READ lease durante
  enumeração e só publica uma fila completa após validar grant/lifecycle. Uma
  trava curta serializa a publicação final com stop/poweroff: quiesce fecha a
  admissão do scanner e marca cancelamento antes de aguardar o worker; uma
  operação de secure-open já em voo não pode publicar resultado stale após esse
  fence. Novos scans também são recusados quando o coordinator SD não está OPEN.
  A enumeração e os waits continuam fora da UI.
   - `cyberdeck_music_player.{h,cpp}` é o serviço FreeRTOS de playback em
    background: fila bounded de requisições e stack de worker em PSRAM byte-addressable
     (`xQueueCreateWithCaps`/`xTaskCreateStatic`); o TCB estático é alocado separadamente
     em RAM interna, exigência de `portVALID_TCB_MEM` no ESP-IDF. A stack é alocada
   explicitamente em PSRAM; o chamador aguarda completion, chama `vTaskDelete` e
   libera a stack após a saída do worker. Mantém READ lease longa até fechar o FD,
  stream de blocos bounded, decode minimp3, estados/progresso, pausa/seek e
  geração monotônica por comando para descartar plays obsoletos e impedir que
  um dequeue posterior limpe cancelamento de stop/halt;
    quiesce/join antes do drain SD. Repetição/auto-avanço por EOF continua no
    mesmo ciclo do worker, reutiliza o request atual e mantém exatamente uma READ
    lease; não re-enfileira requests que deixem leases presas. A app depende de `player_interface` injetável;
   o player implementa essa porta sem alterar o comportamento do singleton. O
   comando `prev` sincroniza seleção e posição atuais do player antes de aplicar
   a regra de reiniciar a faixa após 3 s; EOF atualiza seleção publicada junto
   ao índice interno antes de enfileirar a próxima faixa, sob o mesmo lock e
   guard de geração.
   worker publica completion como último acesso aos handles e se autoexclui; stop
  descarta requests pendentes, enfileira wake sentinel e aguarda completion mesmo
   quando a fila estava cheia. Tags ID3 maiores que o limite de extração de
   metadados continuam sendo puladas sem alocar seu conteúdo integral; tamanhos
   syncsafe são validados contra o tamanho do arquivo. O snapshot registra
   estágio de falha bounded (`open`, `id3`, `decode`, `codec_start`,
   `codec_write`, `read`, `io`); `music status` mostra `error_stage` apenas em
    estado error. Publicação é protegida pelo mutex e geração do playback. Logs
    operacionais marcam worker/file-open, início e fim do codec, primeira escrita
    PCM e falha final (stage/generation), sem registrar o caminho do arquivo nem
     emitir log por frame. Após um `music play 1` manual sem serial aberta
   travar/reiniciar o Tab5, `-fstack-usage` do compilador ESP32-P4 mediu 17.200 B
   no `mp3dec_decode_frame` original versus stack de 8.192 B da task. Correção
   candidata: scratch no heap uma vez por faixa, com erro `decode` se indisponível;
   `mp3dec_decode_frame_with_scratch` local mede 976 B, worker 1.152 B. O wrapper
    upstream permanece disponível; adaptação em `components/cyberdeck/THIRD_PARTY.md`.
     Falhas do lifecycle inicial agora registram estágio específico (`storage_grant`,
     `player`, queue, completion semaphore, worker task ou scan completion semaphore)
     sem SSID/caminhos; `app info` continua expondo o resultado genérico do supervisor.
      O payload da fila bounded do player é alocado com `xQueueCreateWithCaps` em
      PSRAM byte-addressable (`MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`), mantendo o
       control block FreeRTOS em memória interna; rollback/stop usam
       `vQueueDeleteWithCaps`. O worker usa stack em PSRAM e TCB estático em alocação
       interna dedicada (rollback se faltar RAM); `test_music_player` cobre as
       capabilities/erro de TCB e lifecycle host.
   liberação após completion. Boot normal de 2026-10-27 confirmou LVGL/touch usando
   buffers DMA PSRAM após Hosted/Wi-Fi. O mesmo boot revelou assert posterior em
   `xTaskCreateStaticPinnedToCore` (`portVALID_TCB_MEM`) porque o `StaticTask_t`
   estava dentro do `impl` alocado na PSRAM; agora o TCB é alocação interna dedicada,
   coberta por `test_music_player` e pelo contrato de integração. Essa variante já
   foi buildada, mas ainda não foi flashada/validada fisicamente. Validação física do
   build `3c21fe901d934f36583efb02959f8540c55c9fb0302a3c5016895b61341d00cd`
      não alcançou o firmware: após flash verificado, o ESP32-P4 panicou no boot em
      `PC=0x4fc0424c`, `MTVAL=0x50091000`, antes de qualquer log Hosted/app; causa
      ainda não simbolizada. O sistema não deve declarar esta variante validada.
      A validação física anterior identificou
     `xQueueCreate` falhando com depth 8/item 268 B junto de falha na task BLE e
     socket HTTP; o motivo do esgotamento de memória interna permanece sistêmico e
     não é resolvido integralmente por esta realocação.
   `test_music_decoder_stack_contract.py` verifica ownership/stack;
   `test_music_decoder_scratch.cpp` compara duas frames decodificadas com o
   wrapper upstream e scratch externo (PCM/status iguais, ASan/UBSan limpos).
   Ambos estão no agregado e `BINS` inclui o novo binário; o contrato incremental
    final espera 80 bins únicos. Host/build passaram; firmware corrigido SHA-256
    `0bcb094320d7563f2d6b8d590a5220f417ba1b25f7ac3ff76550107b324c116d`
    gravado e verificado em `/dev/ttyACM0`. Boot com checkpoint, uptime 48→53 s,
    safe_mode=false, interrupted_boots=0, Wi-Fi com IP e sem erros RPC/RX/panic
    na captura `/tmp/opencode/music-scratch-boot.raw`. A abertura serial ainda
    causou reset USB. Teste manual posterior do usuário voltou a travar
    e reiniciar ao tocar: a correção de stack foi insuficiente. Coredump está
    desabilitado (`sdkconfig`); sem log contemporâneo não há backtrace para
    localizar a falha restante. Nenhum novo play/flash após esse relato.
   `cyberdeck_audio_output.{h,cpp}` integra
  `bsp_audio_codec_speaker_init`, codec ES8388, volume limitado a 50 e comuta
  amplificador do speaker conforme detecção do jack; o jack recebe saída do
  codec. A tabela `backend` é seam aditiva e instalável antes da primeira
  inicialização, permitindo exercitar codec/roteamento com callbacks determinísticos.
  Falha ao desligar o amplificador mantém estado de rota em falha, força retry e
  bloqueia start/write até a rota segura ser confirmada.
  `components/m5stack_tab5/CMakeLists.txt` compila `bsp_audio.c` e declara
  `esp_codec_dev`/expander como dependências.
- O código upstream minimp3 revision
  `ea99364f61c14656440e8d77e9c233ccf3124633` está em
  `components/cyberdeck/third_party/minimp3/minimp3.h`, com licença CC0 em
  `third_party/minimp3/LICENSE`; `components/cyberdeck/THIRD_PARTY.md` registra
  origem/revisão. `components/cyberdeck/CMakeLists.txt` compila model, app,
  player e adapter de áudio. `cyberdeck_system_apps.cpp` registra o app e seu
  comando.
- `cyberdeck_app_storage.{h,cpp}` expõe API aditiva de abertura read-only
  bounded; `cyberdeck_local_shell.{h,cpp}` resolve caminho virtual e retorna FD
  regular sem symlink/traversal, propriedade transferida ao player, que mantém
  a lease do coordinator até fechar. `cyberdeck_local_shell_opendir` entrega um
  `DIR*` oriundo de walk openat/O_NOFOLLOW em hosts; no ESP-IDF valida namespace,
  confinamento e tipo por `stat`, então enumera diretamente com `opendir` no VFS
  FATFS (que não possui symlinks e não implementa fdopendir nem abertura de
  diretórios via `open(O_DIRECTORY)`). A abertura do arquivo de cada entrada
  continua sendo revalidada pelo secure-open.
  Poweroff inclui estágio `music` após
  fechar admissão SD e antes do drain; `cyberdeck_music_poweroff_quiesce` cancela
  e aguarda scan e player.
- Os testes adicionados ao Makefile incluem `test_music_model.cpp` para a lógica
  pura, `test_music_app.cpp` para comandos/scan com portas injetadas,
  `test_music_player.cpp` para fila, cancelamento, leases e lifecycle worker, e
  `test_music_audio_output.cpp` para volume, mute, falhas e roteamento via backend
  codec injetável. `music_traceability.md` relaciona esses cenários a
  REQ-MUSIC/AC-MUSIC/TEST-AUDIO; os casos de secure-open usam a produção real do
  shell em `test_local_shell.cpp` e exercitam recusa de symlink/traversal sem
  enfraquecer os flags `O_NOFOLLOW`.
- `cyberdeck_header_view.{h,cpp}` adiciona botão global acionável enquanto há
  playback; callback da TUI em `cyberdeck_ui.cpp` abre um painel no terminal e
  Escape retorna ao conteúdo anterior. O painel é acrescentado após o scrollback
  para permanecer visível no bottom-anchor sem ser gravado no histórico. Timer atualiza progresso/presença do
  indicador e integra-se ao teardown/freeze da UI. Os comandos do shell seguem
  o catálogo dinâmico de manifests de apps; não há view separada nem task de UI
  dedicada.
- Validação local do snapshot integrado: agregado host com flags coverage PASS;
  gcovr 87.0% linhas / 94.8% funções / 59.0% branches; scope guard 73 TUs
   cobertas e 15 allowlisted; runtime recovery 23 checks; build ESP-IDF 6.1 PASS.
   A validação mais recente gravou o build com logs diagnósticos: boot/checkpoint,
   `sys.info` e start/status do app passaram, sem WDT/panic; Hosted RPC segue
   falhando (`get_mac`/`set_config` timeout, depois TX SDIO/BLE CP not ready),
   então nenhum scan/play foi enviado nesta sessão. SDIO enumera e negocia RPC V2
   + SW_AGGR; host 3.0.10/CP 3.0.9 é marcado compatível. A/B temporário com
    streaming legado reproduziu a falha, descartando SW_AGGR como causa provável.
    A/B com alocação RX alinhada/PSRAM eliminou `mempool OOM` RX, mas RPC 284
    passou a falhar imediatamente no TX e o BLE continuou CP-not-ready; falta de
     DMA RX é fator, porém insuficiente. Trace posterior comprovou heap DMA interno
     esgotado/fragmentado: TX do RPC 387 falha para 38 B com 703 B livres/bloco
     112; na variante RX-PSRAM, TX do RPC 284 falha para 66 B com 867 B livres/
     bloco 200, antes de esperar resposta. A alocação RX PSRAM remove OOM, mas não
     resolve TX. Heap DMA é causa confirmada da falha TX sob carga; o CP ainda não
      foi validado isoladamente. Fontes Hosted de diagnóstico foram restauradas;
     em seguida, variante CPU-staging foi gravada (SHA-256 do binário
     `e7ee8ddce54e2b27eaa4f711ec0ddc87ea1e68ed6affc515e06fc21e983857d4`). Primeiro
     boot passou checkpoint, conectou Wi-Fi e completou fila de 1 faixa sem
     playback; reabertura serial causou reset USB `CHIP_USB_UART_RESET`. Segundo
     boot ainda não validado além de uptime 5→10 s; Wi-Fi desconectado na janela
     inicial. A variante candidata permanece ativa em dois arquivos ignorados de
     `managed_components` (`eh_host_bus_sdio.c` e `eh_host_mcu_transport_channels.c`);
     sua reprodução explícita está versionada em `tools/esp-hosted-cpu-staging.patch`
     e `tools/prepare_esp_hosted_cpu_staging.py` (aplicar após resolver componentes,
     verificar revisão/hash antes do build; restore reversível). `CMakeLists.txt`
     executa verify após project() e antes do link do ELF em cada build:
     reconfigure e build incremental sem patch falham fechado. O teste
     `tests/host/keymap/test_esp_hosted_cpu_staging.py` (alvo focado e agregado)
     verifica round-trip isolado, recusa de drift e gate CMake; suíte host integral
     e reconfigure/build positivos passaram, e reconfigure sem patch falhou como
     esperado. Binário local após reconfigure é `50792ce1e712d1c5cd2336822087b80dc9572f1f176dac986a65da5d8410af34`,
     distinto do binário gravado; não houve novo flash. Não é correção física
     aceita nem aplicada automaticamente. Os marcos de
   codec ainda não foram exercitados no hardware; cobertura host não prova
   I2S/FATFS/codec. Playback, jack e controles permanecem pendentes.
   `tests/manual/music-player-validation.pt-BR.md`
  registra a ordem do gate recovery/Hosted antes do app e dos testes speaker/jack;
  `version.txt=3` identifica o próximo firmware integrado.

| Arquivo | Simbolos/funcao | Papel |
| --- | --- | --- |
| `tests/host/keymap/test_shell_session.cpp` + `tests/host/keymap/test_shell_help.cpp` + `tests/host/keymap/test_shell_help_contract.py` + `tests/host/keymap/help_traceability.md` + `tests/host/keymap/shim/esp_err.h`, `shim/esp_wifi_types.h`, `shim/freertos/FreeRTOS.h` | `test_shell_help`, `test_shell_help_contract`; REQ-HELP-01..05 | Testa `handle_key`/`execute_line`/insercao de texto com um `host` falso: edicao e historico, comandos locais, portas de tela/Wi-Fi/bateria/log/SSH, tokens de conexao, passe com varios newlines, buffer de passkey e `help` dinamico via runtime real (eco `/$ help`, saida completa exata e ordenacao legacy/app, tags, manifests registrados incluindo lifecycle parado, omissao sem command, deduplicacao, precedencia legacy e ordem deterministica). `test_shell_help.cpp` cobre `<comando> help` para comandos legacy e manifests de app, incluindo desconhecido, sem execução/despacho, preservação da ajuda geral e flags; rastreia `REQ-HELP-DETAILS`. `test_shell_help_contract.py` reconcilia a registry real de 11 apps e manifests reais com fixtures/goldens comportamentais: três apps com commands, sete sem commands, wiring e uso declarado; é inspeção estrutural, não execução do firmware. `help_traceability.md` mapeia REQ-HELP-01..05 para cenários e evidências. Ambos os alvos estão no agregado `test`; os shims sao minimos e nao espelham logica de producao. |
| `components/cyberdeck/src/platform/display/cyberdeck_ui.cpp` | `cyberdeck_ui_init`, `cyberdeck_ui_deinit`, `cyberdeck_keyboard_input`, `shell_session_host`, `compose_console_line`, `get_rendered_output`, `render_terminal`, `local_key`, callbacks de SSH/Wi-Fi/BLE/cat, `refresh_battery_status` | Fachada LVGL 9: constrói e atualiza widgets, usa as APIs dedicadas `lv_obj_set_hidden`/`lv_obj_set_scroll_chain` para visibilidade e rolagem, mantém o pump de eventos e renderiza, mas não decide o que cada tecla faz. O estado de sessão (linha/cursor, histórico, SSID pendente, chave SSH pendente, buffer de passkey) e o roteamento de teclas/comandos vivem em `cyberdeck_shell_session::session`; a UI implementa a interface `host` (output, BLE, Wi-Fi, SSH, cat, shell local) e traduz `lv_key_t` para `cyberdeck_shell_session::key`. A construção visual do header e do terminal contínuo/teclado fica nos componentes `cyberdeck_header_view` e `cyberdeck_terminal_view`; a fila de entrada física fica no dispatcher `cyberdeck_keyboard_dispatch`; o pump de eventos BLE (`on_ble_event`/`process_ble_events`/`ble_submit_actions`) permanece na UI e delega o agendamento background ao `cyberdeck_ble_background::scheduler`. O scrollback fica no modelo UTF-8 bounded `cyberdeck_terminal_scrollback::model`; a view envia somente uma janela limitada aos slots de linha reutilizáveis e compõe scrollback, prompt/editor e cursor na mesma superfície. O textarea permanece oculto como alvo do teclado virtual e não duplica estado visual. Saída SSH marca repaint pendente e é coalescida pelo timer LVGL de 100 ms; caminhos interativos continuam com flush imediato. Os eventos SSH usam slots de armazenamento estático fora da stack, contadores de descarte saturantes separados para dados e estados e geração para descartar eventos stale; a geração esperada é invalidada no teardown. Callbacks SSH apenas publicam eventos e não adquirem lock direto do display. Init possui 13 fases de aquisição fail-closed, rollback em falhas parciais e 9 timers LVGL cancelados no teardown antes da árvore; o init é idempotente. O worker de exportação do survey e suas filas bounded de capacidade 1 têm lifecycle app-lifetime e são preservados no teardown/reinit; a task usa stack 8192 e não retém ponteiros de UI/controller. Completion fica na fila até o pump LVGL consumi-la e é aplicada pelo controller somente ao token correspondente; busy mantém nova admissão fechada enquanto a completion não for consumida. Payload/path são limpos após uso ou falha. |
| `components/cyberdeck/include/apps/shell/cyberdeck_shell_session.h`, `components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp` | `cyberdeck_shell_session::session`, `cyberdeck_shell_session::host`, `session::execute_line`, `CYBERDECK_CMD_WIFI_SURVEY*`; REQ-SURVEY-01..02,06,08 / AC-01..02,06,08 | Sessão de shell sem LVGL decide comandos de survey: duração 10–120 s (default 30), início exclusivo, cancelamento, bloqueio de `wifi search` durante amostragem, aviso de impacto potencial na conexão e exportação com confirmação separada para IDs masked (default) ou unmasked. Efeitos passam pelas portas `host` da UI. O controller rejeita solicitação nova enquanto um job está ativo sem substituir a confirmação existente; falhas de job admitido encerram estado somente para o token atual. O catálogo puro `cyberdeck_shell_help.h` formata comandos legacy em ordem com tags `[terminal]`, usage/descrição e parâmetros explicados em linhas próprias; entradas dinâmicas `[app]` seguem ao fim sem duplicar comandos legacy e usam cabeçalho próprio com descrição indentada na linha seguinte. `help` sem argumento mantém esse catálogo; `<comando> help` resolve primeiro o catálogo legacy e depois comandos declarados em manifests registrados, mostrando apenas metadados disponíveis (nome, ID, versão, descrição) sem iniciar app nem despachar comando; não resolvido gera erro explícito. `-h`/`--help` permanecem no dispatch existente. `test_shell_session.cpp` cobre parsing/fluxo shell e permanece no alvo `test_shell_session` e agregado `test`. |
| `components/cyberdeck/src/platform/display/cyberdeck_header_view.cpp`, `components/cyberdeck/include/platform/display/cyberdeck_header_view.h` | `cyberdeck_header_view::view` | Componente visual do header: cria a grade 30/40/30 e os widgets Bluetooth, Wi-Fi e bateria; aplica exatamente `cyberdeck_font` monoespaciada aos labels de título, relógio, Bluetooth e bateria, mantendo a métrica/line-height compartilhada. A bateria possui três labels — ícone, percentual base e overlay do percentual — e o overlay usa a mesma fonte, é ignorado pelo flex layout e deslocado 1 px para emulação real de negrito; acompanha texto/visibilidade sem alterar fonte, estados, ordem semântica ou geometria; usa `lv_obj_set_hidden` para ocultação e desativa rolagem; recebe somente estado/presentation já resolvidos e não conhece serviços, NVS, I2C ou modelos BLE/Wi-Fi. Rastreia `REQ-HEADER-FONT-01..03`/`AC-HEADER-FONT-01`. |
  | `components/cyberdeck/src/platform/display/cyberdeck_terminal_view.cpp`, `components/cyberdeck/include/platform/display/cyberdeck_terminal_view.h` | `cyberdeck_terminal_view::view`, `cyberdeck_terminal_view::callbacks`, `view::render`, `view::toggle_cursor`, `view::deinit`, `view::begin_touch`, `view::scroll_from_touch`, `view::viewport_capacity` | Componente visual do terminal e teclado virtual: cria uma superfície contínua com labels reutilizáveis, calcula reflow UTF-8 bounded, quebra palavras longas somente em fronteiras de codepoint e mantém cada label dentro da largura disponível, além de manter offset de scroll por linhas com clamp ao viewport. `render(text,cursor_byte)` posiciona um overlay de bloco piscante pelo avanço da fonte `cyberdeck_font`, seguindo as mesmas métricas de reflow e fronteiras UTF-8; sobre glyph inverte as cores e no fim ocupa o espaço seguinte. O cursor só pisca quando sua posição está dentro da janela visível; ao navegar scrollback é ocultado e o repaint preserva seu byte para reposicioná-lo ao voltar ao bottom. `deinit` encerra o timer de blink antes da destruição da view. Gesto vertical de pointer/touch na superfície navega o offset; o estado inicia e permanece ancorado no fim quando já estava no fim, e nova saída afastada preserva a posição até o clamp seguro. O textarea fica oculto e serve apenas como alvo LVGL do teclado virtual; toque no terminal não escreve nem altera o foco de edição. `term.dump` não passa por esta janela visual. `test_display_views.cpp` identifica o overlay por estilo e a linha editável por texto explícito (sem auto-comparação do cursor), cobrindo scroll/blink, fonte `cyberdeck_font`, retorno à posição correta, UTF-8/reflow e remoção do timer; `TEST-CURSOR-05` rastreia `REQ-LAT-04` (`TEST-LAT04-08/09`). `test_lat03_contract.py` e `test_lat04_contract.py` mantêm os contratos estruturais da superfície e do cursor. |
| `components/cyberdeck/src/platform/display/cyberdeck_terminal_scrollback.cpp`, `components/cyberdeck/include/platform/display/cyberdeck_terminal_scrollback.h` | `cyberdeck_terminal_scrollback::model` | Modelo puro de scrollback bounded em 12288 bytes, com poda em fronteira UTF-8. Mantém a saída completa para `term.dump`; a view navega esse snapshot por touch em uma janela de labels reutilizáveis, sem alterar prompt/editor/cursor. |
| `tests/host/keymap/test_terminal_scrollback.cpp` + `tests/host/keymap/test_lat03_contract.py` | TEST-LAT03-01..11 | Teste host do modelo real para limites 12287/12288/12289, clear, append fragmentado/corte UTF-8 e viewport 4096 com ASan/UBSan; contrato estrutural fixa textarea somente para prompt/editor, menus/ANSI/SSH, `term.dump`, cursor/senha/Enter e a regressão FIFO/wake. Os harnesses LAT04 de prompt e gap SSH também extraem a UI contra o modelo `cyberdeck_terminal_scrollback::model`, cobrindo append, `text()` completo e `viewport()` sem perda. |
| `tests/host/keymap/test_ssh_output_gap.py` | TEST-SSH-01..05 | Fixture host que extrai a costura SSH da UI e a compila com os TUs reais de filtro/compositor/console/scrollback e todas as dependências SDK do runtime: catálogo, pacote SD, quota, storage e logger. |
| `tests/host/keymap/test_display_views.cpp` + `tests/host/keymap/shim/full/lvgl.h` | `test_display_views` | Harness host compartilhado para `cyberdeck_header_view`, `cyberdeck_terminal_view` e `cyberdeck_editor_view`, com shim LVGL completo. O TU real do editor é compilado com cobertura e permanece elegível no scope guard; o cenário verifica criação, render bounded/UTF-8, cursor, gesto e teardown. `TEST-CURSOR-01..04` cobre bloco visível/invertido, blink manual/timer e restauração de visibilidade após ocultação por blink, offsets início/meio/fim, UTF-8 com posição após codepoint, newline, wrap de margem e posição inválida/ausente; shim expõe cores e timers determinísticos sem reimplementar o algoritmo de produção. Rastreabilidade de `REQ-HEADER-FONT-01..03`/`AC-HEADER-FONT-01`: verifica `cyberdeck_font` compartilhada e line-height relacional, modo clip do relógio, os três labels da bateria (ícone, percentual base e overlay), overlay de negrito do percentual em 1 px ignorado pelo layout e ausência de API de outline no shim. Também verifica texto, visibilidade, ordem e geometria exercitados pelo cenário do header. Cobre `REQ-LAYOUT-01/AC-LAYOUT-01/02` com `border_width=0` somente na superfície visível e preservação do cromo do textarea oculto. Simula ponteiro LVGL para `TEST-TERM-SCROLL-01..06`: gesto vertical invertido (baixo -> linhas antigas, cima -> bottom anchor), clamp nos extremos, UTF-8, input prompt/editor oculto e nova saída/resize. |
| `tests/host/keymap/test_lat04_contract.py` | TEST-LAT04-01..09 | Contrato estrutural da superfície contínua: slots fixos bounded, bottom-anchor/overflow via `test_display_views`, textarea oculto, sincronização de cursor/editor, modos de sessão, resize/clear/`term.dump`, ownership de teclado e regressões LAT03. O alvo é executado no agregado `make test` imediatamente após `test_lat03_contract`. |
| `components/cyberdeck/src/platform/input/cyberdeck_keyboard_dispatch.cpp`, `components/cyberdeck/include/platform/input/cyberdeck_keyboard_dispatch.h` | `cyberdeck_keyboard_dispatch::dispatcher` | Infraestrutura de entrada física: copia snapshots, mantém fila bounded de capacidade 8, serializa enqueue/rollback com mutex, agenda processamento no contexto LVGL e desperta explicitamente a task do port após o agendamento; não interpreta comandos nem conhece sessões da UI. |
| `tests/host/keymap/test_keyboard_dispatch.cpp` + `tests/host/keymap/shim/full/` | `test_keyboard_dispatch` | Exercita o dispatcher host com shims completos de LVGL/FreeRTOS: callbacks ficam agendados ate o pump, wake explicito e FIFO sao observados, a capacidade 8 descarta o nono evento sem duplicacao, rollback de falha do `lv_async_call` preserva sobreviventes e `stop` libera pendentes sem double-free (ASan/UBSan). |
| `components/cyberdeck/include/platform/display/cyberdeck_ui.h` | API publica da UI, `cyberdeck_ui_term_dump` | Contrato usado por `app_main`, pelo driver de teclado e pela ponte Serial-JTAG; o dump textual encapsula o lock de display bounded sem expor LVGL. |
| `components/cyberdeck/include/apps/wifi/cyberdeck_wifi_survey_export_controller.h`, `src/apps/wifi/cyberdeck_wifi_survey_export_controller.cpp` | `cyberdeck_wifi_survey_export::controller`, `port`; REQ-SURVEY-06/07, AC-SURVEY-06/07 | Controller de produção é fonte das transições de confirmação, admissão, token/job, start único e completion/publication; a porta estreita admite e libera o gate após aceitação. Falhas de admissão preservam confirmação; expiry a consome. O worker e fila FreeRTOS reais são adapter em `cyberdeck_ui.cpp`; persistência permanece no backend existente. |
| `components/cyberdeck/src/platform/display/cyberdeck_ui.cpp` | `shell_session_host::wifi_survey_export`, `survey_export_queue_port`, `wifi_survey_export_worker`, `process_wifi_survey` | UI delega confirmação/admission/job ownership/start/completion ao controller; adapter só enfileira job real no worker persistente bounded cap 1 e dá start gate após controller aceitar. Worker de app-lifetime não acessa controller nem UI, executa persistência com stack 8192 e publica completion bounded até o pump após reinit; token protege a aplicação do resultado. Filas/worker são explicitamente preservados no teardown; busy mantém admissão fechada até completion ser consumida. Payload/path do port, job e resultado são zerados após uso ou falha. Criação da task sem memória rejeita export sem usar stack menor. I/O fica no worker e backend. |
| `components/cyberdeck/src/platform/display/cyberdeck_font.c` | Fonte monoespaciada | Recurso visual do terminal/header, incluindo os simbolos LVGL de Wi-Fi, menos, carga e Bluetooth; o include LVGL permanece condicionado por `LV_LVGL_H_INCLUDE_SIMPLE` e usa `"lvgl.h"` em ambos os ramos. Os unicos codepoints FontAwesome disponiveis sao `0xF067` (mais), `0xF068` (menos), `0xF0E7` (carga), `0xF1EB` (Wi-Fi), `0xF293` (Bluetooth) e `0xF240..0xF244` (bateria), portanto nao ha glyph de tomada/USB/energia para o caso de alimentacao externa sem bateria. |
| `components/cyberdeck/src/platform/display/cyberdeck_clock.cpp` | `cyberdeck_clock_from_utc`, `cyberdeck_format_clock` | Conversao/formato do relogio GMT-3. |
| `components/cyberdeck/include/apps/system/cyberdeck_time_sync.h`, `components/cyberdeck/src/apps/system/cyberdeck_time_sync.cpp` + `tests/host/keymap/test_time_sync.cpp`, `test_time_sync_contract.py`, `time_sync_traceability.md`, `time_sync_shim/freertos/task.h` | `TEST-TIME-01..17`, `TEST-TIME-STACK-01`, `TEST-TIMELOG-01..08` | Harness host determinístico com seams fake de HTTP/TLS/clock/Wi‑Fi/FreeRTOS e logger/runtime: TimeAPI válida sem fallback, fallback WorldTimeAPI válido, falha dupla, rejeição de schemas/timezone/data/payload oversized sem mutação parcial, teardown entre primária/fallback com novo start e concorrência idempotente; o shim observa `vTaskDeleteWithCaps(NULL)` e os cenários 07/08/17 garantem um teardown por worker, sem vazamento lógico ou double teardown. A cobertura de logging fixa fila durable bounded, máximo de três tentativas por token, ausência de falso sucesso em write/fsync, retenção do evento quando a projection está cheia e logger runtime ausente sem impedir o adaptador aprovado. O contrato de stack exige 16384 bytes e rejeita o legado 6144. Contrato verifica wiring, ambos endpoints e orçamento de lifecycle documentado de 9000 ms. Não acessa rede pública. TEST-TIME-10 permanece manual. |
| `components/cyberdeck/src/platform/display/cyberdeck_wifi_indicator.cpp` | `cyberdeck_wifi_indicator_is_lit` | Regra pura: claro somente com `enabled && connected && has_ip`. |
| `components/cyberdeck/src/platform/display/cyberdeck_wifi_icon.cpp` | layout, criacao, resize e cor do icone | Desenha tres arcos e ponto; recalcula posicao em resize. |
| `components/cyberdeck/src/platform/display/cyberdeck_screen_protection.cpp`, `components/cyberdeck/include/platform/display/cyberdeck_screen_protection.h` | `parse_timeout_minutes`, `state`, `persisted_timeout` | Politica pura host-testavel: timeout padrao de 2 min, faixa inclusiva 0..1440, zero desabilitando sem perder o ultimo valor positivo, restauracao dos dois campos e transicao on/off no limite exato de inatividade. |
| `components/cyberdeck/src/platform/display/cyberdeck_battery_view.cpp`, `components/cyberdeck/include/platform/display/cyberdeck_battery_view.h` | `power_glyph`, `input`, `presentation`, `clamp_percentage`, `from_snapshot`, `resolve` | View pura do indicador de energia do header, sem ESP-IDF, FreeRTOS, LVGL, I2C, NVS ou BSP. Mapeamento de estado de política + disponibilidade + corrente INA + percentual para visivel/percentual/glyph: indisponivel ou `unknown` fica oculto, `absent` fica visivel com glyph externo e sem percentual, e somente corrente INA abaixo de -`current_uncertainty_ma` autoriza glyph de carga; fora disso usa glyph de bateria. `state`/`CHG_STAT` continuam evidência da política de proteção, mas não determinam o glyph visual. Ausencia precede a apresentação e o nível nunca vem do glyph. |
| `components/cyberdeck/src/platform/display/screen_off.cpp`, `components/cyberdeck/include/platform/display/screen_off.h` | `screen_off_init`, `screen_off_turn_on`, `screen_off_turn_off`, `screen_off_is_active`, `screen_off_set_timeout_minutes` | Adaptador LVGL/BSP da protecao de tela: timer de 1 s, duplo toque para religar, consulta do estado apagado para a UI consumir a primeira tecla física como wake, comandos `screen on|off|timeout`, restauracao NVS antes do timer e persistencia enfileirada para uma task dedicada (fora da task LVGL) do timeout efetivo e do ultimo valor positivo, com zero pausando/desabilitando o timer. |
| `tests/host/keymap/test_wake_key_contract.py` | REQ-WAKE-KEY-01/02 / AC-WAKE-KEY-01/02/03 | Contrato estrutural do guard de wake: extrai o corpo real de `on_keyboard_event` da UI de produção e fixa que `screen_off_is_active()` é a primeira ação, `screen_off_turn_on()` é chamado quando a tela está apagada, o `return` precede qualquer dispatch para `local_key`/`insert_physical_text` (tecla não executa ação normal) e o caminho normal permanece após o guard quando a tela está ativa. A UI não é linkável no host (LVGL/BSP/NVS/FreeRTOS); o contrato inspeciona a fonte de produção. Alvo `test_wake_key_contract` no agregado `make test`. |

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
Wi-Fi (correcao do desalinhamento ~10 px do label direto de 42 px); a bateria mantem tres labels:
icone, percentual base e overlay bold. O grupo apenas aplica o que a
  view pura `cyberdeck_battery_view` decidiu: glyph de carga somente quando
  `current_ma < -current_uncertainty_ma`, glyph constante de bateria mais
  percentual nas demais fontes com pack (`battery`/`external`), glyph externo sem
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
| `components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp` | classe `cyberdeck_local_shell`; `cyberdeck_local_shell_cat`; `recover_save_sidecars` | Shell confinado ao root virtual `/`; `host_root` continua sendo o ponto fisico do SD (montado em `/sdcard`), e `/sdcard` e descendentes sao rejeitados no namespace virtual. Tokenizer manual byte-a-byte bounded para espacos/tabs; implementa `pwd`, `cd`, `ls`, `cat`, `touch`, `mkdir`, `rm`, `rmdir` e ajuda. A ajuda e as opcoes `-h`/`--help` consomem o catalogo compartilhado sem listas literais locais. A API cat-specific usa as mesmas regras de cwd/caminho, strings bounded e descritores confinados, sem `fs::path`, retorna output heap-backed, limita arquivos a 12288 bytes e chunks de 1024, e e usada pelo worker sem construir o shell geral. `recover_save_sidecars` enumera somente `<root>/data`, valida destinos confinados em `/data`, restaura `.rollback` somente quando o destino esta ausente, preserva como ambiguous qualquer rollback com destino existente, nunca publica `.tmp` e mantem diagnostico/idempotencia. |
| `components/cyberdeck/src/apps/shell/cyberdeck_cat_worker.cpp`, `components/cyberdeck/include/apps/shell/cyberdeck_cat_worker.h` + `tests/host/keymap/test_cat_worker.cpp` | `cyberdeck_cat_worker_process_request`, `cyberdeck_cat_worker_bind_sd_io`, `cyberdeck_cat_worker_start`, `cyberdeck_cat_worker_enqueue`, `cyberdeck_cat_worker_teardown`, `cyberdeck_cat_worker::with_read_lease` | Worker FreeRTOS com fila bounded para I/O de `cat`, stack explícita de 6144 bytes; `with_read_lease` é um seam host-testável tipado que adquire READ e mantém lease RAII ao executar o callable, falhando fechado sem invocá-lo quando a admissão não ocorre; worker ESP delega o `cyberdeck_local_shell_cat` real através de `cyberdeck_cat_worker_process_request`. A UI injeta o coordinator compartilhado. O worker chama uma API cat-specific heap/bounded, sem construir/usar o shell genérico, `fs::path` ou `vector` no caminho específico. O contrato estrutural permite os identificadores `cyberdeck_local_shell_*` da API dedicada e rejeita apenas a construção/uso genérico. Cada start drena a sinalização de parada e cria uma geração nova, e teardown sinaliza/aguarda o retorno do worker antes de liberar fila, root e callback, invalidando callbacks LVGL tardios; resultados aceitos acordam explicitamente a task do port após o enfileiramento. |
| `tests/host/keymap/cat_io_traceability.md` | TEST-FIXTURES-01..03, TEST-CAT-01..05 | Matriz TEST → AC-FIXTURES/AC-CAT para fixtures com coordinator, incluindo local shell com gate CLOSED fail-closed, lease callback/drain/gate/falha e delegação ESP → helper → cat real. Os testes do worker cobrem também cat vazio e rejeição oversized (12289 bytes), além da saída normal e confinamento. |
| `components/cyberdeck/include/apps/shell/cyberdeck_local_shell.h` | API do shell local | Contrato usado pela UI e testes. |
| `components/cyberdeck/include/apps/shell/cyberdeck_shell_help.h` | `cyberdeck_shell_help::kCatalog`, `cyberdeck_shell_help::text`, `cyberdeck_shell_help::command_text`, `format_app_entry` | Catalogo header-only puro e host-linkable mantém a tabela única ordenada dos 18 comandos legados `[terminal]`; o cálculo de capacidade de cada linha inclui o indent da usage e coincide com os bytes efetivamente formatados. `format_app_entry` apresenta comandos dinâmicos de manifests registrados como `[app]` em cabeçalho próprio e descrição indentada na linha seguinte, na composição da sessão. Não inclui dependência do runtime. |
| `tests/host/keymap/test_save_sidecar_recovery.cpp`, `save_sidecar_traceability.md`, `test_boot_sequence.py` | `TEST-SIDECAR-API` (B01-B05), `TEST-BOOT-SEQUENCE` | Teste comportamental host da API real de recuperação com layout real `<root>/data`: rollback restaurado/idempotente, fixture de regressão para scan apenas da raiz e confinamento (sidecar fora de `/data` intocado), destino ambíguo preservado, `.tmp` intocado, rejeição de symlink/traversal e diagnóstico action/stage/errno/falha de validação/enumeração. Contrato estrutural confirma a chamada após montagem e validação do SD em `app_main`. `save_sidecar_traceability.md` mapeia REQ/AC e casos B01-B05. O alvo declara dependências da VFS e da matriz de rastreabilidade. |
| `components/cyberdeck/include/apps/shell/cyberdeck_vfs_namespace.h`, `components/cyberdeck/src/apps/shell/cyberdeck_vfs_namespace.cpp` | `cyberdeck_vfs_namespace::at`, `resolve`, `backend_for`, `backend_kind`, `is_filesystem_backend`, `is_mutable_backend`, `is_null_device`, `path_kind` | Catalogo compilado fixo e resolver puro bounded dos cinco namespaces (`/apps`, `/data`, `/dev`, `/tmp`, `/system`). `backend_for` centraliza a classificação em `metadata`, `filesystem`, `null_device` ou `invalid`; o shell usa essa política única para separar metadados, backends físicos e a interface `/dev/null`. `/data` mapeia para `<host_root>/data` e permite apenas mutacoes confinadas do shell; `/system` permanece readonly em `<host_root>/system`; `/dev/null` e uma interface virtual vazia e bounded, sem abrir o `/dev` fisico; `/apps` e `/tmp` continuam metadata-only. A raiz `/data` nao pode ser removida. |
| `components/cyberdeck/src/apps/shell/cyberdeck_shell_utils.cpp`, `components/cyberdeck/include/apps/shell/cyberdeck_shell_utils.h` | `cyberdeck_help_text`, `cyberdeck_command_help_text`, `cyberdeck_parse_command`, `CYBERDECK_CMD_WIFI_SURVEY*` | Parser de `wifi survey`, duração `10..120`, cancelamento e exportação com confirmação separada (mascarada por padrão ou explicitamente sem máscara), além dos comandos Wi-Fi/audit existentes e roteamento dos comandos locais. |
| `tests/host/keymap/contracts/cyberdeck_help.h` | `kUnifiedHelpText` | Golden/fixture compartilhado para `test_shell_utils` e `test_help_unification`, atualizado para incluir a linha `wifi [search|saved|audit|survey]` e a descrição de survey; preserva igualdade, ordem e estrutura do catálogo (65 linhas = 18 tags + 47 indentadas). Não é produção. |
| `tests/host/keymap/test_help_unification.cpp` | paridade de `help`, `help -h` e `help --help`; catalogo legado unico de 18 blocos (65 linhas); `TEST-HELP-HIERARCHY-02`; `TEST-HELP-APP-HIERARCHY-01/02` | Teste comportamental host que liga `cyberdeck_shell_utils.cpp` e `cyberdeck_local_shell.cpp`, sem hardware; compara cada formatter de entry com texto esperado e confere tamanho renderizado contra o helper (nunca `std::string::capacity()`); o help de cada comando local (`-h`/`--help`) compara o bloco completo do catalogo. Verificacao estrutural independente (sem auto-comparacao de substring da propria linha): o comando derivado do header `[terminal]` em coluna 0 abre a usage row indentada, a descricao apos ` - ` nao e vazia, cada linha de parametro fica exatamente um nivel de indentacao abaixo com explicacao `:`, e usage/header sao unicos no catalogo. `TEST-HELP-APP-HIERARCHY-01` fixa o formato hierarquico de `format_app_entry` (header `<cmd> [app]` + descricao indentada); `TEST-HELP-APP-HIERARCHY-02` verifica independentemente o bloco hierarquico, a tag `[app]`, a ausencia de colisao legada e a saida dinamica integral. |
| `tests/host/keymap/help_unification_contract.py` | contrato estrutural do catalogo | Impede listas literais independentes em `cyberdeck_shell_utils.cpp`, `cyberdeck_local_shell.cpp`, `cyberdeck_ui.cpp` e `cyberdeck_shell_session.cpp`; o padrão detecta cabeçalho `[terminal]`/`[app]`, linha de uso indentada com ` - `, linha de parâmetro indentada com `:` e tabela `entry` multi-linha, com controles positivo/negativo determinísticos. |
| `tests/host/keymap/help_traceability.md` | matriz REQ-HELP-01..04 -> TEST | Mapeia hierarquia header/usage (REQ-HELP-01), explicacao de parametros (REQ-HELP-02), ordem canônica/65 linhas (REQ-HELP-03) e catalogo unico/paridade (REQ-HELP-04) para TEST-HELP-HIERARCHY-01/02, TEST-HELP-CAT-PINNED, TEST-HELP-ORDER-01/ROWS-01, TEST-HELP-CMD-01/CMD-BLOCKS, TEST-HELP-PARITY, TEST-HELP-GOLDEN e TEST-HELP-APP-HIERARCHY-01/02 (formato hierarquico do formatter dinâmico `[app]`). Registra as contagens do snapshot (shell utils 477, help unification 623, BLE parser 209) e os limites host-only, incluindo o CMD1 manual ainda pendente. |
| `components/cyberdeck/src/apps/shell/cyberdeck_history.cpp` | classe `cyberdeck_history` | Historico limitado a 64 linhas, com navegacao e duplicatas preservadas. |
| `components/cyberdeck/src/apps/shell/cyberdeck_edit_line.cpp` | classe `cyberdeck_edit_line` | Linha UTF-8, cursor, backspace, Enter e comportamento por sessao; o eco local do MENU recebe o cwd capturado antes da execucao e forma `<cwd>$ <comando>\n`, sem alterar o payload. |

### Runtime de aplicações compiladas

Lifecycle diagnostics: `cyberdeck_app_runtime.cpp` preserves the bounded last
error restored for each application, exposes it as `last_error` in `app info`,
and emits structured bounded logger events for start, stop, failure and timeout.
A new start attempt clears diagnostics from the previous attempt; a successful
hook then leaves no stale `last_error`, unless the current hook exceeded its
lifecycle budget and records a fresh timeout. `test_app_runtime` covers recovery
from `failed` to `running` and verifies that `app info` reports `last_error: none`.

Recovery clear is an explicit acknowledgement of the current interrupted boot:
`clear_safe_mode` clears the latch, `boot_pending`, and interruption count in one
persisted transition. This prevents the reset caused by closing/reopening the
USB Serial-JTAG connection from counting the just-cleared safe-mode boot as a
new interruption. The pure policy test and Phase 10 contract cover the atomic
transition.

Foreground execution is exclusive: the runtime tracks `active_foreground`, and
`switch_foreground` completes the previous app's teardown (including dependent
apps) before starting the next. Command dispatch uses the same activation path;
`start_all` does not eagerly start foreground apps. The shell/console owner stays
protected from self-deactivation by the foreground switch API and by the existing
`stops_console_owner` rule. `test_app_runtime` covers
switch, failed teardown/start, restart and command-driven activation.
The console owner itself cannot be reactivated through `switch_foreground` from
its own command channel, so a shell command cannot tear down its only control
surface. The device check remains blocked by the persistent recovery latch.

Command help metadata: `manifest::command_help_entries` in
`cyberdeck_app_runtime.h` aligns usage/parameter text with declared commands;
`cyberdeck_shell_session.cpp` renders it for `<command> help` without starting
an app or dispatching. Editor, demo, and netdiag manifests supply their current
command contracts; unannotated commands retain the generic fallback. Editor
shortcuts remain interactive documentation, not shell commands.

| Arquivo | Simbolos/contrato | Papel |
| --- | --- | --- |
| `components/cyberdeck/include/apps/runtime/cyberdeck_app_logger.h`, `components/cyberdeck/include/apps/runtime/cyberdeck_app_runtime.h`, `components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp` | `cyberdeck_apps::logger`, `manifest`, `application`, `runtime`, `result`, `app_state`, `app_type` | Supervisor bounded de até 16 aplicações compiladas. O manifesto declara `api_version`, tipo, dependências, recursos, capacidades, stack, fila e comandos. Mantém estados `registered`/`starting`/`running`/`stopping`/`failed`, resolve dependências declaradas em ordem determinística, mede o tempo dos hooks, registra diagnóstico da última falha, expõe metadados em `app info` e implementa restart/stop-all. `manifest::owns_console` marca a aplicacao que possui o unico console de usuario, e `runtime::stops_console_owner(id)` percorre a arvore de dependentes com marcadores visitados (bounded contra ciclos) para recusar qualquer `app stop` cuja cascata em `stop_index` alcance o dono do console: dependentes sao parados antes do alvo, entao `app stop <dependencia do shell>` destruiria o console sem nunca nomea-lo e bloquearia toda a entrada ate um reboot. A API de lifecycle do supervisor (`stop_application`, `stop_all`, `restart_application`) nao e afetada, porque o bloqueio se aplica aos comandos que chegam pelo proprio console. O estado segue o retorno real do hook: como os hooks são síncronos e não podem ser preemptados, o estouro pós-retorno de `lifecycle_timeout_ms` é apenas diagnóstico (`start hook timeout`/`stop hook timeout`) e não troca o estado para `failed`, o que impede bloquear dependentes de um serviço lento. Os limites efetivos ficam nos joins cooperativos bounded dos serviços. `application::init()` e `application::teardown()` formam hooks completos com defaults compatíveis com `start()`/`stop()`. `runtime::set_logger()` injeta a porta de logging em cada aplicação registrada e `runtime::app_logger()` permite que compositores consumam o serviço sem incluir o backend de plataforma. O parser continua whitespace-only e limitado a 256 bytes/8 tokens. |
| `components/cyberdeck/include/apps/runtime/cyberdeck_window_manager.h`, `components/cyberdeck/src/apps/runtime/cyberdeck_window_manager.cpp` | `cyberdeck_window_manager::manager`, `view_context`, `surface_state`, `view_status` | Politica pura e bounded da Fase 8: no maximo 8 superficies e 8 notificacoes FIFO, gera contextos opacos com geracao monotona, centraliza foco, invalida/quiesce superficies em teardown (inclusive foco e notificacoes), preserva ownership sem que uma nova superficie antecipe o fallback durante teardown e rejeita handles expirados, overflow e notificacoes oversized sem depender de LVGL, ESP-IDF ou FreeRTOS. |
| `components/cyberdeck/include/apps/runtime/cyberdeck_app_facades.h`, `components/cyberdeck/src/apps/runtime/cyberdeck_app_facades.cpp` | `display_facade`, `input_facade` e facades de network/BLE/Serial/Screenshot/event log/clock/bateria | Fachadas tipadas da Fase 9. Cada operacao revalida o grant; display e input usam exclusivamente `view_context` e o window manager, enquanto os demais recursos expõem somente presença autorizada, sem singleton ou handle de plataforma. A storage facade vive exclusivamente em `cyberdeck_app_storage.h`. |
| `components/cyberdeck/include/apps/runtime/cyberdeck_app_storage.h`, `components/cyberdeck/src/apps/runtime/cyberdeck_app_storage.cpp` | `storage_facade::bounded_read`, `read_result` | API SDK pública de leitura com buffer pertencente ao chamador, EOF/erro explícitos e limite de 12288 bytes; usa internamente a operação `cat` já confinada, sem expor detalhes de filesystem. |
| `components/cyberdeck/include/apps/runtime/cyberdeck_app_logger.h`, `components/cyberdeck/src/apps/runtime/cyberdeck_app_logger.cpp` | `logger::event`, `logger::write` | Porta SDK com níveis tipados, tag/payload de tamanho fixo e rejeição de campos sensíveis; o adaptador `event_log_logger` preserva o lifecycle e o backend persistente existente. |
| `components/cyberdeck/include/apps/runtime/cyberdeck_command_catalog.h`, `components/cyberdeck/src/apps/runtime/cyberdeck_command_catalog.cpp` | `command_catalog::build`, `dispatch` | Catálogo bounded derivado dos manifestos compilados; dispatch retorna nulo para comandos legados, preservando o parser do shell. O runtime encaminha o token declarado, inclusive `edit`, preserva os argumentos de `edit <arquivo>` e inicia de forma lazy/idempotente o app `foreground` antes do execute pelo caminho normal de dependências, grants e lifecycle; falha de start rejeita o comando, enquanto apps `service` não são auto-iniciados. |
| `components/cyberdeck/include/apps/runtime/cyberdeck_app_runtime.h`, `components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp` | `resource`, `grant`, `runtime::app_grant`, `application::app_grant`, `runtime::inspect_sd_package`, `resource_from_name` | Autoridade bounded de grants: valida recursos declarados no manifesto (desconhecidos/duplicados/overflow recusados), reserva quotas fixas de resources/grants no registro e em cada start, valida assets compilados sem bloquear catálogo vazio, separa capabilities metadados, cria geração por ciclo e revoga geração/grant antes do teardown. Mutex curto serializa valid/allows, publicação/cópia do grant da aplicação e as transições de estado observadas por grants; não é mantido durante hooks, logging ou quota operations. Falhas de init/start invalidam sob o mesmo lock; restart recebe geração nova. TEST-GRANT-01..08 planejados pelo tester cobrem linearização, falhas, cópias e concorrência/TSan. Limite de 16 aplicações preservado. A fronteira SD expõe somente validação de metadata/data e nunca instalação ou execução. |
| `tests/host/keymap/grant_traceability.md` | `REQ/AC-GRANT-01..05` → `TEST-GRANT-01..08` | Matriz de rastreabilidade da validação isolada de grants, incluindo cenários comportamentais e execução ThreadSanitizer. |
| `components/cyberdeck/include/apps/runtime/cyberdeck_resource_catalog.h`, `components/cyberdeck/src/apps/runtime/cyberdeck_resource_catalog.cpp` | `resource_catalog`, `compiled_resources`, `asset_metadata`, `verify_asset` | Catálogo readonly de até 16 assets compilados, com metadados estáticos, lookup por índice/ID, checksum e ausência fail-closed. O registro dos system apps faz diagnóstico bounded e não bloqueia boot quando o catálogo está vazio; não possui mutação, backend de escrita ou caminho de código executável. |
| `components/cyberdeck/include/apps/runtime/cyberdeck_sd_package.h`, `components/cyberdeck/src/apps/runtime/cyberdeck_sd_package.cpp` | `sd_package`, `sd_package_metadata`, `sd_package_view` | Validador host-testável de pacote SD somente para dados/assets: `format_version` compatível, versão, nome, tamanho máximo de 12288 bytes e checksum FNV-1a. Ausente, corrompido ou incompatível é rejeitado; o módulo não instala nem interpreta conteúdo e o system-app registry apenas diagnostica a ausência opcional. |
| `tests/host/keymap/test_sd_package.cpp` + `tests/host/keymap/sd_package_traceability.md` + `tests/host/keymap/Makefile` | `test_sd_package`, `REQ-COV-01`/`AC-COV-01` | Teste comportamental liga o TU real `cyberdeck_sd_package.cpp`; cobre vetores FNV-1a de referência, entradas nulas/vazias, tipos media_type aceitos/rejeitados e regras independentes de `validate`, incluindo limites de tamanho e checksum. Alvo `make test_sd_package` com cobertura está listado em `BINS`/`clean` e incluído em `make test`, evitando execução de binário stale; cobertura focada: 26/26 branches. A matriz rastreia requisitos aos cenários TEST-SDP. |
| `tests/host/keymap/test_resource_catalog.cpp` + `tests/host/keymap/resource_catalog_traceability.md` + `tests/host/keymap/Makefile` | `test_resource_catalog`, requisitos de catálogo de recursos | Teste comportamental liga o TU real `cyberdeck_resource_catalog.cpp`; cobre lookup bounded por índice/ID, metadados, checksum e ausência fail-closed. Alvo com cobertura está listado em `BINS`/`clean` e incluído em `make test`, evitando execução de binário stale; cobertura focada: 16/16 branches. A matriz registra a rastreabilidade e os limites da evidência host. |
| `components/cyberdeck/include/apps/runtime/cyberdeck_app_quota.h`, `components/cyberdeck/src/apps/runtime/cyberdeck_app_quota.cpp` | `quota_limits`, `quota_usage`, `quota` | Quotas explícitas e fixas para resources/grants, stack, fila, bounded read, logger, output e lifecycle. O runtime reserva resources/grants por manifesto no registro e em cada ciclo de start; `revoke()` é chamado em arrays de 16 aplicações antes e depois do teardown, incluindo falhas de init/start, com geração monotônica para invalidar ciclos anteriores. |
| `components/cyberdeck/include/platform/display/cyberdeck_window_manager_adapter.h`, `components/cyberdeck/src/platform/display/cyberdeck_window_manager_adapter.cpp` | `cyberdeck_window_manager_adapter::adapter`, `global` | Unico adaptador dono da raiz LVGL composta pelo screen, barra de sistema persistente e area de conteudo; a barra e chrome preto sem borda, padding ou rolagem, com 42 px fixos, enquanto a politica pura continua separada e cada app recebe somente `view_context`. A UI existente usa a area de conteudo e registra a superficie do shell, removendo-a antes do teardown para invalidar callbacks tardios. |
| `components/cyberdeck/include/apps/demo/cyberdeck_demo_app.h`, `components/cyberdeck/src/apps/demo/cyberdeck_demo_app.cpp` | `cyberdeck_apps::demo_application` | Aplicação headless de prova, registrada junto aos system apps, com manifesto, lifecycle e comando `demo`. |
| `components/cyberdeck/include/apps/system/cyberdeck_system_apps.h`, `components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp` | `cyberdeck_system_apps_register`, `cyberdeck_system_apps_start_logging`, `cyberdeck_system_apps_start`, `cyberdeck_system_apps_start_safe_mode`, `service_application`, `event_log_logger`, `make_manifest` | Registro fixo dos system apps do Tab5, incluindo `cyberdeck.event_log`. O adaptador `event_log_logger` implementa a porta `cyberdeck_apps::logger` e encaminha ao backend persistente; o supervisor injeta essa porta ao registrar as aplicações. `cyberdeck_system_apps_start_logging` inicia o logger antes do restante do boot, `cyberdeck_system_apps_start` delega a ordem dos demais apps ao supervisor via `start_all`, e `cyberdeck_system_apps_start_safe_mode` tenta toda a superfície permitida (event log, shell e Serial-JTAG), registra cada falha no recovery e retorna erro sem abortar nem iniciar serviços proibidos. `make_manifest` declara tipo, dependências, recursos, capacidades, stack, fila e orçamento de lifecycle por app (Wi-Fi usa 8 s, pois `wifi_mgr_start()` leva ~2,7 s para subir o radio C6 e o storage SD). SSH, BLE, Serial e Screenshot possuem stop idempotente com joins bounded; Wi-Fi expõe `wifi_mgr_stop()` com join dos dois workers, cancelamento de timers/callbacks e quarentena fail-safe. |
| `components/cyberdeck/include/apps/system/cyberdeck_time_sync.h`, `components/cyberdeck/src/apps/system/cyberdeck_time_sync.cpp` + `tests/host/keymap/time_sync_traceability.md` | `cyberdeck_time_sync_start`, `cyberdeck_time_sync_stop`, `cyberdeck_time_sync_boot_ready` | System app assíncrono `cyberdeck.time_sync`, dependente de Wi-Fi: após o checkpoint de boot e somente com conexão + IP agenda uma única tentativa por token, primeiro em `https://timeapi.io/api/time/current/zone?timeZone=UTC`, com fallback em `https://worldtimeapi.org/api/timezone/Etc/UTC` somente para falha de transporte/HTTP/corpo/parser. Ambas usam CA bundle oficial, timeout independente de 4000 ms e corpo bounded a 2048 bytes; a primeira valida JSON com `year`, `month`, `day`, `hour`, `minute`, `seconds`, `timeZone == UTC`, tipos numéricos e data válida, enquanto o fallback preserva `timezone == Etc/UTC` e `utc_datetime` com fração opcional. Nenhum caminho altera o clock antes da validação; somente `settimeofday` bem-sucedido produz o evento `time_sync`/`success` depois do `fsync` real em `events.log`, sem fallback após tentativa válida de atualização. A task assíncrona (stack de 16384 bytes), criada com `xTaskCreateWithCaps` e encerrada com `vTaskDeleteWithCaps`, callback, token por conexão e teardown são idempotentes/quiescentes; stop é observado entre as duas tentativas e o orçamento de lifecycle cobre ambas sem bloquear boot. O caminho crítico usa fila durável prioritária bounded, retry limitado e diagnóstico de backpressure; a projeção textual é best-effort. A matriz em `time_sync_traceability.md` mantém REQ/AC -> TEST, distinguindo host de TEST-TIME-10 manual. | REQ-TIMELOG-01..04, AC-TIMELOG-01..04 |
| `components/cyberdeck/include/apps/system/cyberdeck_recovery_policy.h`, `components/cyberdeck/src/apps/system/cyberdeck_recovery_policy.cpp` | `cyberdeck_recovery::state`, `begin_boot`, `commit_ready`, `record_error` | Politica pura e bounded de recovery: blob versionado conceitualmente, tentativa pendente, tres boots interrompidos, latch de safe mode e ultimo erro por app. Nao depende de ESP-IDF/NVS. |
| `components/cyberdeck/include/apps/system/cyberdeck_recovery.h`, `components/cyberdeck/src/apps/system/cyberdeck_recovery.cpp` | `cyberdeck_recovery::init`, `safe_mode`, `commit_ready`, `clear_safe_mode`, `record_app_error` | Adaptador NVS da politica. Usa o namespace NVS `cyberdeck_rec` (limite de 15 caracteres; distinto do namespace C++), persiste a tentativa antes do startup e o checkpoint somente no fim; falha, corrupcao ou indisponibilidade nao dispara `nvs_flash_erase`. O adapter real é ligado no host via `test_recovery_runtime.cpp` com NVS fake; é coberto e removido da allowlist de coverage. |
| `components/cyberdeck/include/apps/system/cyberdeck_service_ports.h`, `components/cyberdeck/src/apps/system/cyberdeck_service_ports.cpp` | `cyberdeck_apps::service_ports::{wifi,ssh,ssh_generation,ble,serial_start,serial_stop}` | Gateway das portas de serviço consumidas pela composição da UI. Centraliza Wi-Fi, SSH, BLE e Serial; `cyberdeck_ui.cpp` não referencia mais diretamente `wifi_mgr_*`, `ssh_client_*`, `ble_mgr_*` ou `bridge_start`. Serial expõe stop cooperativo bounded. A porta SSH também expõe a geração monotônica aceita para filtragem de eventos stale; os callbacks continuam bounded nos contratos públicos existentes; a conversão geral para event bus permanece pendente. |
| `components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp` | `session::execute_line`, `help_text` | Resolve help na rota dinâmica antes do shell local e do passthrough SSH, preservando o eco normal do comando. `help_text` concatena os 18 comandos legados com comandos dos manifests atualmente registrados, independentemente do lifecycle; remove colisões/duplicatas e categoriza as linhas como `[terminal]`/`[app]`. Apps como `edit`/`demo` aparecem quando registrados; em safe mode, só aparecem apps registrados nesse runtime. O runtime permanece fora do formatter header-only host-linkable. |
| `components/cyberdeck/include/apps/shell/cyberdeck_shell_console.h`, `components/cyberdeck/src/apps/shell/cyberdeck_shell_console.cpp` | `k_terminal_limit`, `utf8_char_count`, `utf8_valid_start_offset`, `truncate_left_utf8`, `fit_prompt_marker`, `fit_visible_line`, `session_mode`, `mode_for`, `surface_state`, `line_input`, `line_view`, `compose`, `first_token`, `is_legacy_command`, `dispatcher::resolve`, `dispatcher::collisions` | Politica pura do console do shell (Fase 7), sem LVGL, ESP-IDF, terminal ou sessao local. As helpers de UTF-8 e de ajuste de largura foram movidas do TU anonimo de `cyberdeck_ui.cpp` sem alterar comportamento, de modo que prompt, linha e scrollback tem uma unica autoria sobre o limite de 12288 bytes (`k_terminal_limit = cyberdeck_edit_line::limit`). `compose` aplica mascara na senha, suprime o prompt local quando outro dono controla a entrada ou quando SSH esta conectado, preserva a linha remota filtrada sem inferir prompt e clampa o cursor na janela visivel UTF-8. `dispatcher` resolve o primeiro token e preserva o roteamento existente. |
| `components/cyberdeck/include/apps/shell/cyberdeck_shell_app.h`, `components/cyberdeck/src/apps/shell/cyberdeck_shell_app.cpp` | `cyberdeck_shell_app::application` (`get_manifest`, `init`, `start`, `stop`, `teardown`, `running`, `execute`, `attach_console`, `detach_console`, `console`, `console_ready`, `mode`, `compose_line` e a fachada nula-segura de console), `cyberdeck_shell_app::global_application` | `cyberdeck.shell` como aplicacao de primeiro plano real (Fase 7), registrada no supervisor em vez de `service_application` com hooks stub. `start` e idempotente, falha fechado sem host de composicao e cria um `cyberdeck_shell_session::session` novo por ciclo, o que isola historico, edicao, tokens Wi-Fi e buffer de passkey; `stop` e idempotente, apaga o passkey antes de liberar o console e nunca reaproveita uma sessao descartada. `detach_console` para a aplicacao e devolve o host emprestado, e `attach_console` nao cria console: quem cria e o supervisor. A fachada e nula-segura (sem console, entrada e descartada e um chunk virtual nunca reivindica Enter), e `compose_line` sincroniza o editor antes de composing. O manifesto declara `app_type::foreground`, dependencia de `cyberdeck.event_log`, recursos/capacidades, stack 8192, fila 8 e `owns_console`, sem verbo de comando porque o console e dirigido pela entrada. `execute` permanece `not_handled` para que o supervisor nunca reivindique uma linha em nome do shell. Nao inclui LVGL nem chama backends de plataforma; `start` não renderiza diretamente e deixa o primeiro prompt para o timer do contexto LVGL. |
| `tests/host/keymap/test_shell_app.cpp` + `tests/host/keymap/Makefile` | `test_shell_app` | Teste comportamental host da Fase 7 com host de sessao falso: matriz de composicao do prompt (menu, senha mascarada, SSH online sem prompt algum, entrada de outro dono que suprime o prompt por completo), helpers de UTF-8 e de modo com fallbacks, limites bounded e clamp do cursor, linha remota apenas de view (`remote_prompt_literal_contract`: ausencia de marcador fixo, de prompt remoto inferido e de prompt local no modo conectado, `text() == "pwd"` sem marcador, linha remota verbatim, cursor em codepoints sem prompt no conectado e com o prompt local no desconectado, clamp no prefixo oculto, `reserved()` igual a cauda composta sem prompt e payload/porta de senha/host key/banda do compositor sem prompt) e preservacao dos modos (`ssh_mode_preservation_contract`: senha mascarada, TOFU de host key e volta ao console local sem residuo de marcador ou prompt de senha), politica de despacho (exclusividade de `app`, legados nunca interceptados, comando declarado roteado, linha vazia/desconhecida/oversized com falha fechada, colisoes reportadas e registry cheio), manifesto e ownership do console (start/stop idempotentes, start sem host falha fechado, fachada nula-segura, console novo e isolado a cada start, dois consoles independentes, tokens Wi-Fi lidos do console possuido) e propriedade pelo supervisor (dependencia declarada ausente mantem o shell `failed` sem console, `app stop cyberdeck.shell` recusado pelo console dono, lifecycle por API ainda disponivel e `at()` bounded). |
| `tests/host/keymap/test_shell_app_contract.py` | contrato estrutural da Fase 7 | Protege as fronteiras de ownership que o binário host nao alcanca: `cyberdeck.shell` registrado pela aplicacao real (sem `start_shell`/`stop_shell`/`service_application s_shell`), manifesto com tipo/dependencia/`owns_console`, hooks de lifecycle reais, UI sem sessao propria e sem recompor marker/linha, `attach_console` antes do dispatcher de teclado, `detach_console` no teardown, ordem `app` antes da reserva legada e da lista de comandos declarados, switch legado intacto na sessao, reserva derivada do catalogo de ajuda, SSH como `session_mode` derivado de `ssh_phase()`, ausencia de LVGL/ESP-IDF na aplicacao e registro no CMake. |
| `tests/host/keymap/test_lvgl_shell_lifecycle_contract.py` | TEST-LVGL-01/02 | Contrato estrutural do handoff shell/LVGL: `start` sem render direto, primeiro prompt marcado para o timer LVGL, callback cat via `lv_async_call` com wake explícito bounded, rollback quando o agendamento falha, stop/join antes da liberação, geração consumida/incrementada e callback tardio rejeitado por `closing`/geração; teardown da UI quiesce o worker antes de desprender o console. Executar isoladamente por `make test_lvgl_shell_lifecycle_contract`. |
| `tests/host/keymap/test_app_runtime.cpp` + `tests/host/keymap/Makefile` | `test_app_runtime` | Testa registro duplicado, manifesto expandido, `app info` com API/tipo/capacidades/stack/fila/comandos, hooks explícitos `init`/`teardown`, listagem, lifecycle, dispatch, lazy-start de comandos foreground, rejeição sem auto-start para service/background, limites de estado e passthrough de comandos desconhecidos. Cobre a regressão de lifecycle descoberta no dispositivo: `slow_app`/`slow_dependent_app` provam que um hook de start que excede o orçamento mas retorna sucesso permanece `running` e não bloqueia dependentes (orçamento é diagnóstico, não estado), enquanto `failing_app`/`failing_dependent_app` confirmam que falha real continua fail-closed e nomeia a dependência ausente. Adiciona casos de validação de manifesto/registro, lifecycle, comandos, tokenização, logger e supplier; snapshot atual: 166 checks (baseline 84). O Makefile compila o teste host diretamente com a implementação real do runtime e mantém `test_app_runtime` em `BINS` e no agregado `test`. |
| `tests/host/keymap/test_runtime_sdk.cpp` + `tests/host/keymap/Makefile` | `test_runtime_sdk` | Teste host determinístico mínimo dos três TUs SDK anteriormente ausentes no guard: logger tipado com limites/rejeição de campos sensíveis, storage bounded-read fail-closed e catálogo derivado de manifestos com preservação dos comandos legados. Liga as implementações reais com `-O0 --coverage`, sem produção, hardware ou TEST-TIME. |
| `tests/host/keymap/test_os_transformation_backlog_contract.py` + `os_transformation_traceability.md` + `Makefile` | REQ/AC-SDK-01, LOG-01, CMD-01, REPLY-01, BOOT-01, DOC-01 | Contratos host-only, determinísticos e pré-implementação para `storage.bounded_read` sem VFS/handles, logger bounded sem segredos, catálogo/manifesto/dispatch preservando legados, output/SSH sem framing universal, migração declarativa parcial do boot e rastreabilidade. Os cenários não usam hardware nem fornecem implementação fake; o alvo `test_os_transformation_backlog_contract` deve permanecer RED para lacunas de implementação. |
| `tests/host/keymap/test_resource_sd_quota_contract.py` + `os_transformation_traceability.md` + `Makefile` | REQ/AC-RES-01, SD-01, QUOTA-01, GUARD-01, TRACE-01 | Contratos host-only para catálogo readonly bounded de assets compilados, pacote SD somente metadata/version/checksum de dados/assets com falha fechada, quotas bounded revogáveis, guardrails de event bus/reply universal/loader dinâmico e matriz restrita ao escopo aprovado. Sem hardware, instalação ou execução de código; o alvo é isolado e RED enquanto os contratos de produção estiverem ausentes. |
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
| `tests/host/keymap/test_recovery_policy.cpp` + `tests/host/keymap/test_phase10_contract.py` + `tests/host/keymap/test_boot_sequence.py` + `tests/host/keymap/phase10_traceability.md` | Fase 10 (TEST-10-01..07) | Testa a política pura de tentativa/checkpoint, três interrupções, latch/reset e erros por app; os contratos estruturais verificam o namespace NVS `cyberdeck_rec` <=15, persistência/diagnóstico, fail-closed sem erase, safe mode com serviços permitidos e o gate `boot_ready` -> `commit_ready`. A matriz REQ/AC -> TEST fica no arquivo de rastreabilidade. `test_coverage_scope_guard.py` fixa a allowlist aprovada de 16 TUs, incluindo os adaptadores event-log e recovery, sem abrir mão dos TUs host-cobertos e forbidden. |

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
| Fronteira nao linkavel do gap | `test_terminal_output_contract.py` (T-GAP-01/02/03/04) | `discard_epoch` no elemento, `s_ssh_discard_epoch`/`s_ssh_applied_discard_epoch` com CAS saturante, 3 + 2 sítios de `mark_ssh_event_discarded` com contadores exatos, ausencia de `compare_exchange_weak` nos dois callbacks, one-liner `if (s_ssh_event_slot.generation != s_ssh_expected_generation) continue;` antes da guarda de `discard_epoch`, e `s_ssh_applied_discard_epoch = current_ssh_discard_epoch();` uma unica vez, dentro de `if (result == ESP_OK)`; T-GAP-04 fixa `s_ssh_output_filter.resync_after_gap()` uma unica vez e depois da guarda de epoch, alem do contrato do header (`void resync_after_gap();` e `k_gap_parameter_limit = 16`) |

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
| `components/cyberdeck/src/apps/wifi/wifi_mgr.cpp`, `components/cyberdeck/include/apps/wifi/wifi_mgr.h` | API `wifi_mgr_*`, `wifi_mgr_scan_passive`, `wifi_event_worker`, `net_worker`; REQ-SURVEY-08 / AC-08 | Adaptador ESP-IDF para conexão, desconexão, scans e callbacks. `wifi_mgr_scan` preserva `esp_wifi_scan_start(NULL, false)`; `wifi_mgr_scan_passive` fornece configuração survey-specific com `WIFI_SCAN_TYPE_PASSIVE` e dwell explícito `WIFI_PASSIVE_SCAN_DEFAULT_TIME` (360 ms no ESP-IDF 5.5.5 local). A coleta existente limita cada chamada a `WIFI_SCAN_MAX_APS=16`; wiring coberto estruturalmente por `test_wifi_survey_integration_contract.py`, não prova rádio real. |
| `components/cyberdeck/src/apps/wifi/wifi_storage.cpp` | armazenamento de redes | Persistencia de SSID/credencial no SD; senhas nao sao exibidas nem registradas. |
| `components/cyberdeck/src/apps/wifi/cyberdeck_wifi_menu.cpp` | `cyberdeck_wifi_search_menu` | Modelo/renderizacao pura do menu de busca e redes salvas. |
| `components/cyberdeck/src/apps/wifi/cyberdeck_wifi_state_machine.cpp` | `state_machine` | Estados search/saved/password/connecting/forget e acoes derivadas. |
| `components/cyberdeck/src/apps/wifi/cyberdeck_wifi_event_dispatch.cpp` | `event_dispatch` | Snapshots bounded de `GOT_IP`/`LOST_IP`, tokens, ack, retry e invalidacao. |
| `components/cyberdeck/src/apps/wifi/cyberdeck_wifi_persistence_queue.cpp` | `persistence_queue` | Fila bounded com ownership, retry, acknowledge, wipe e teardown. |
| `components/cyberdeck/src/apps/wifi/cyberdeck_wifi_persistence_coordinator.cpp` | coordinator | Worker que serializa persistencia e efeitos derivados fora de `sys_evt`. |
| `components/cyberdeck/include/apps/wifi/cyberdeck_wifi_survey.h`, `components/cyberdeck/src/apps/wifi/cyberdeck_wifi_survey.cpp` | `cyberdeck_wifi_survey::session`, `observation`, `network`; REQ-SURVEY-01..07 / AC-01..07 | Modelo bounded em RAM: duração 10–120 s (default 30), até 6 scans sequenciais, até 32 redes únicas acumuladas e tendências count/min/avg/max efêmeras; consolida por BSSID, ordena RSSI e desempata deterministicamente. Cada lote de entrada suporta até 16 resultados do manager; 32 é limite acumulado, não garantia de descobrir todos os APs em ambiente denso nem >16 em um único scan. Export versionado até 8192 bytes com identificadores mascarados por padrão. TU real exercitado por `test_wifi_survey.cpp`; cenários e limitações em `wifi_survey_traceability.md`. |
| `components/cyberdeck/src/platform/display/cyberdeck_ui.cpp` | `process_wifi_survey`, `on_wifi_survey_scan_done`, `cancel_wifi_survey_scan`, `wait_wifi_survey_callback`, `drain_wifi_survey_scan_results`; REQ-SURVEY-02,05..07 / AC-02,05..07 | Teardown limpa/invalida a sessão, cancela scan e espera até 1 s por callback com sequência correspondente; após quiescência drena resultados e elimina queue/mutex/semaphore. Timeout preserva handles em quarentena. Export mantém confirmação explícita (masked por padrão e unmasked separado), registra motivos síncronos de rejeição sanitizados e marcos accepted/started/completed do worker, sem SSID/BSSID/payload/path; falha do persist_document registra stage genérico e errno capturado. ACK terminal de sucesso só é publicado após persist_document retornar sucesso da transação publicada. |
| `components/cyberdeck/src/apps/wifi/cyberdeck_wifi_audit.cpp`, `components/cyberdeck/include/apps/wifi/cyberdeck_wifi_audit.h` | `cyberdeck_wifi_audit::audit_controller`, `persist_document` | Worker FreeRTOS da auditoria da associação local e backend compartilhado de exportação. O audit mantém snapshot/render/formato key=value atual; `persist_document` aceita somente política enumerada audit/survey, valida path e encaminha I/O por task backend bounded. O adapter retém ownership do request até liberação; `export_backend_busy` serializa audit e survey/quarentena após timeout. A transação adquire lease WRITE global e só retorna sucesso após publicação/persistência concluída. |
| `components/cyberdeck/src/apps/wifi/cyberdeck_wifi_audit_persistence.cpp`, `components/cyberdeck/include/apps/wifi/cyberdeck_wifi_audit_persistence.h` | `destination_policy::{audit,survey}`, `audit_persistence`; REQ-SURVEY-06..07 / AC-06..07 | Núcleo transacional único com allowlist fechada de `/sdcard/wifi-audit/` ou `/sdcard/wifi-survey/`; payload audit até 2048 e survey até 8192 bytes. Policy inválida, path cross-policy e destino não autorizado falham fechado. Compartilha lease/fence WRITE, sidecars `.tmp`/`.bak`, recovery fail-closed e rollback FatFS, sem paths livres nem promessa de replace atômico POSIX. `test_wifi_audit_persistence.cpp` preserva regressões audit e cobre survey; `wifi_survey_traceability.md` mapeia TEST-PERSISTENCE-POLICY e limites da fixture host. |
| `tests/host/keymap/test_wifi_survey.cpp`, `test_wifi_survey_integration_contract.py`, `wifi_survey_traceability.md` | TEST-SURVEY-SESSION/TIMING/RESULTS/BOUNDS/TREND/CANCEL/EXPORT/PERSISTENCE/SAFETY/LIFECYCLE, TEST-AUDIT-HOST-COMPILE, TEST-PERSISTENCE-POLICY, TEST-SURVEY-DWELL, TEST-HELP-GOLDEN; REQ-SURVEY-01..08 / AC-01..08 | A matriz registra cenários, evidência focada e limites. `test_wifi_survey` exercita o TU real do modelo; `test_wifi_audit_persistence` exercita a transação real com fixture host. `test_wifi_survey_integration_contract.py` cobre wiring estrutural, inclusive ciclo de vida sem runtime FreeRTOS. Última execução focada registrada na matriz: survey 178 checks e persistence 507 checks; gcovr focado 228/296 branches (77%) e 280/436 (64%), total 508/732 (69%). Isto não é cobertura global nem comprova o gate global. Rádio real, FatFS e execução FreeRTOS permanecem sem validação física. Manager limita a 16 resultados por scan e survey acumula até 32, sem garantia de inventário completo em ambiente denso. Os alvos focados e seu detalhamento ficam em `tests/host/keymap/wifi_survey_traceability.md`. |
| `components/cyberdeck/src/platform/networking/cyberdeck_net_coordinator.cpp` | `cyberdeck_net_coordinator` | Gating de timers e teardown idempotente de Wi-Fi/SSH. |
| `tests/host/keymap/test_net_session_ipc_contract.py` | contrato estrutural IPC SSH/Wi-Fi | Verifica eventos tipados bounded, fila de quatro itens, descarte nao bloqueante, consumo no `net_worker` e ausencia de chamadas/includes diretos ou montagem de storage no cliente SSH; incluido no agregado host. |
| `tests/host/keymap/test_wifi_evt_worker_lifecycle_contract.py` + `tests/host/keymap/Makefile` | contrato estrutural do rollback do `wifi_evt_worker` | Confirma criação dos sinais de stop/quiescencia, wake, join antes de destruir recursos e retenção/quarentena em timeout. Cobre somente o sub-recorte seguro do rollback; não cria stop público nem altera `net_worker`/Screenshot. |

O callback de `GOT_IP` somente publica snapshot. Trabalho pesado, SNTP,
persistencia e notificacao da UI ocorrem no contexto apropriado. Tokens
monotônicos impedem callbacks atrasados de alterar uma tentativa nova.

## Diagnóstico explícito de rede

- `components/cyberdeck/include/apps/network/cyberdeck_netdiag_app.h` e
  `src/apps/network/cyberdeck_netdiag_app.cpp` implementam a app compilada
  `cyberdeck.netdiag`. É demand-started pelo comando shell `netdiag`, depende
  de event log/Wi-Fi e exige grant `network`; usa worker único, alvos bounded,
  resultado consultável/cancelamento e status local da interface STA. `cancel`
  e `result` são atendidos antes da verificação de worker ocupado: cancelamento
  publica imediatamente o estado lógico no controller, sem tocar nos recursos
  que o worker ainda possui; novas operações seguem bloqueadas até completion/reap.
  Stop timeout
  mantém worker/grant/controller vivos e recusa operações e restart até
  `wait_worker` confirmar completion; o handle é reaped uma única vez e stop
  pendente é finalizado antes da próxima inicialização.
- `cyberdeck_netdiag_controller.{h,cpp}` é o controller puro compilado no host
  pelo `test_netdiag_controller`; seu discriminant escolhe uma ação explícita:
  DNS isolado, TCP (DNS interno para conectar seguido de TCP) ou HTTPS sem
  chamadas DNS/TCP separadas. `netdiag_traceability.md` registra os cenários
   focados. `test_netdiag_app.cpp` liga os TUs reais da app, controller,
   runtime, adapters DNS/TCP/HTTPS e dependências com shims externos; cobre
     grant ausente/revogado, dispatch, concorrência, resultados, cancelamento,
     coexistência do comando SSH, falha de criação, boundary de rate limit,
     callback DNS tardio e quarentena em timeout/reap. O scheduler host é
     determinístico e não executa rede real; HTTP/TLS é simulado, portanto não
      prova conectividade nem TEST-ND-08. O timeout/restart regression exige
      recusa até completion/reap; o cancelamento lógico permanece visível ao
      poll de resultado e não libera recursos do worker.
   `tools/coverage_scope_guard.py` classifica app/controller/adapters como
   obrigatórios cobertos (não allowlist), e `test_coverage_scope_guard.py`
   rejeita ausência do TU da app; o agregado com GCC/gcov 13.4 local passou
   o gate gcovr (86,5% linhas, 94,9% funções e 59,3% branches). TEST-ND-08
   passou no Quality Gate dos commits `dbe3a94` (`38916411814`) e `8e41338`
   (`38918012957`, tentativa 2); commits posteriores exigem novo gate. O teste
   físico dirigido está registrado em `netdiag_traceability.md`. Não implementa
   ICMP/ping nem scan.

### Bluetooth LE no ESP32-C6 hospedado (REQ-BLE-001..014)

`address_type` é preservado entre `cyberdeck_ble_types`, ações, eventos e store,
incluindo public, random static, RPA e NRPA. `ble_mgr` usa scan NimBLE de 5000 ms
com parâmetros zero e filtro de duplicatas, parse bounded por `ble_hs_adv_parse_fields` e
agregação ADV/SCAN_RSP por endereço + tipo; somente o anúncio primário define
`connectable`.

O lifecycle do `ble_mgr` possui uma barreira de quiescência separada do ACK de
STOP: a task só sinaliza após sua última operação em filas, mutexes e estado de
lifecycle; `ble_mgr_stop` só executa NimBLE teardown e destrói recursos depois
dessa confirmação. Em timeout, os handles são preservados.

Após o teste de poweroff de 13/out/2026 revelar abort quando `ble_host` retornava,
a causa foi identificada: a task é criada por `xTaskCreate`, logo não possui o
handle privado que `nimble_port_freertos_deinit()` espera registrar via
`nimble_port_freertos_init()`. A task agora chama `vTaskDelete(nullptr)` após
`nimble_port_run()`; `ble_mgr_stop` solicita a parada via `nimble_port_stop()` e
não deleta a task externa ou concorrentemente. `test_ble_review_regressions.py`
protege esse contrato. Migração física de 20/out/2026: C6 atualizado de 2.12.6
para 3.0.9 por OTA usando temporariamente o host ESP-Hosted 2.13.0; o helper
precisou habilitar `BSP_FEATURE_WIFI` pelo expansor antes de Hosted. Causa
confirmada da falha do firmware final: o construtor auto-inicializava Hosted
antes do `app_main`; a primeira inicialização do expansor, dentro de
`wifi_mgr_start()`, resetava suas saídas e interrompia a alimentação do C6.
Além da alimentação anterior à inicialização, a referência upstream
`examples/mcu_hosted_sdio_sdmmc_combined` identifica o C6 no slot 1 e o SD no
slot 0, mas exige Hosted inicializado primeiro no P4/IDF 6 e substituição dos
callbacks `host.init/deinit` do mount SD por no-op. A inicialização antiga
montava SD antes de Hosted, potencialmente reproduzindo o problema de
compartilhamento do controlador (esp-idf #16233/#17889). Implementado no boot:
NVS/recovery primeiro para decidir safe mode; no boot normal, o prepare
Hosted alimenta o C6 e conecta antes de `bsp_sdcard_mount_hosted()`; o BSP
mantém o controller ao montar/desmontar slot 0. Safe mode pula Hosted.
`wifi_mgr_start()` reutiliza a conexão preparada. Validação física P4 em
21/out/2026: contratos focados, `git diff --check` e build ESP-IDF 6.1 passaram;
firmware `731a462-dirty` gravado em `/dev/ttyACM0` (somente P4; nenhum flash C6).
Boot normal, `safe_mode=false`, `interrupted_boots=0` após 58 s, heap ~31.6 MB;
`ls` listou o conteúdo do SD. Wi-Fi conectou a `AT163032` e obteve
`192.168.68.129`, permanecendo conectado. Duas buscas `bluetooth search` com
Wi-Fi/IP ativo terminaram em `Bluetooth search timed out.` sem dispositivos;
logo, o transporte e Wi-Fi/IP estão validados, mas coexistência BLE+Wi-Fi e
descoberta BLE nesse firmware permanecem FAIL/inconclusivos. No dispositivo, a
captura anterior
mostrou o C6 com `Card init success`,
anunciou capabilities `0x0d` (WLAN + HCI/SDIO + BLE) e versões 3.0.9 pareadas;
não houve novo reset SDIO na captura (uptime ~1m42). Porém, chamadas RPC Wi-Fi
subsequentes não receberam resposta (`esp_wifi_get_mac`/`set_config`), o Wi-Fi
não obteve IP e `cyberdeck.bluetooth` ficou em `start/begin` sem concluir.
Instrumentação temporária de 21/out (removida após a coleta) confirmou que os
RPCs `WifiInit`, `WifiSetMode` e `WifiStart` respondem normalmente, mas pedidos
após `STA_START` são enviados e não recebem resposta; o setup BT repete o RPC
`FeatureControl` por ~260 s antes de falhar. Sem `esp_wifi_start()` o controller
BT e NimBLE sincronizaram, e o scan no Tab5 mostrou 17 dispositivos (99 reports
GAP) via `term.dump`. Nesse teste foi identificado e corrigido um segundo
defeito: stack protection fault da task `ble_mgr` ao receber reports; sua stack
passou de 4096 para 8192 bytes (contrato em
`tests/host/keymap/test_ble_integration_contract.py`). Com BLE iniciado antes da estação, `WifiStart`
expirou, o HCI deixou de responder e houve reset durante rollback; essa ordem
não corrige a coexistência. A ordem normal de Wi-Fi foi restaurada. Na validação
de 21/out, Wi-Fi/IP agora funcionam após Hosted-before-SD, mas duas buscas BLE
com Wi-Fi associado terminaram sem dispositivos. Monitor serial do boot seguinte
isolou a causa: `esp_hosted_bt_host_stack_setup()` completou, C6 3.0.9 foi lido
e controller/HCI ficou pronto; em seguida surgiu `Failed to create BLE host
task`. Vincular `ble_host` a `CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=4096`
permitiu criar a task e sincronizar o NimBLE com Wi-Fi/IP ativo. Contudo, o
primeiro comando HCI retornou `BLE_HS_EUNKNOWN`; depois `BLE_HS_ETIMEOUT_HCI`
repetiu e `ble_gap_disc` falhou com 21. Causa adicional: `ble_host` estava em
prioridade 4, sem afinidade, divergindo do port NimBLE IDF (`configMAX_PRIORITIES - 4`,
`NIMBLE_CORE`). Também surgiram `eh_sdio: dma_alloc(1024) failed` e
`rx_get_buffer failed`, indicando pressão na memória DMA interna quando Wi-Fi
e BLE compartilham o transporte. Habilitado `CONFIG_EH_HOST_PORT_DMA_PREFER_SPIRAM=y`:
o allocator SDIO alinhado tenta primeiro DMA-capable SPIRAM no P4 e cai para
RAM interna, além da stack NimBLE de 4096 bytes e prioridade/afinidade oficiais.
Stacks de tarefas não devem ir para SPIRAM: o teste causou assert durante acesso
flash. Após reset físico em 21/out, a falha DMA RX não reapareceu, mas o primeiro
HCI Reset retornou `BLE_HS_EUNKNOWN` e `LE Set Scan Enable` expirou
(`BLE_HS_ETIMEOUT_HCI`, sem reports). O teste de isolamento posterior não foi
conclusivo: parar `cyberdeck.wifi` desmontou/indisponibilizou o transporte Hosted
compartilhado; depois surgiram `sdio_get_tx_buffer_num: err 263`/sem créditos no
slave, e o Wi-Fi não reiniciou. Com base no exemplo oficial
`bleprph_wifi_coex`, a inicialização Hosted-BT foi alterada para aguardar
boundedmente Wi-Fi+IP antes de subir o controller (15 s; prossegue sem IP após o
limite). Validado fisicamente em 21/out após build/flash apenas P4: Wi-Fi
conectou a `AT163032`, IP `192.168.68.129`; log confirmou `Wi-Fi ready before
Hosted Bluetooth startup`. Apesar de um `BLE_HS_EUNKNOWN` no primeiro HCI Reset,
NimBLE sincronizou. `bluetooth search` completou com status 0, 88 GAP reports,
15 dispositivos renderizados; sem HCI timeout, falha `dma_alloc` ou reboot,
uptime 54 s. Depois, `WirelessKeyboard 2` foi visto no scan; o usuário realizou
manualmente a conexão/pairing e validou a digitação HID. `ui.dump` mostrou o
ícone Bluetooth conectado, `sys.info` manteve uptime contínuo (16:56) e
`wifi.status` permaneceu associado com IP. Coexistência BLE+Wi-Fi, descoberta,
conexão e entrada HID confirmadas no dispositivo.
Para diagnóstico não invasivo do coprocessador, o esquema oficial Tab5
(`Tab5_Schematics_PDF.pdf`, página 1) mostra J1/C6_ISP: pino 2 = RF_C6_TXD,
pino 6 = GND. A USB Serial-JTAG disponível é apenas a do P4; não há adaptador
USB-UART 3,3 V conectado nem acesso físico ao J1 nesta sessão. Capturar logs
diretos do C6 requer RX 3,3 V no pino 2 e GND no pino 6, sem conectar VCC;
não foi alterado nem gravado o firmware do C6 nesse diagnóstico.

No IDF 6, `ble_mgr_start()` usa `esp_hosted_bt_host_stack_setup()` para subir
controller e associar VHCI/NimBLE; a sequência de controller init/enable abaixo
descreve o caminho legado de IDF anterior. A task `ble_host` usa
`CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE` (4096), com `configMAX_PRIORITIES - 4`
e `NIMBLE_CORE`, conforme o port oficial IDF. Seu stack permanece em RAM interna
porque tarefas executam callbacks NVS/flash; stacks SPIRAM falharam com o assert
`esp_task_stack_is_sane_cache_disabled`. A task de gestão mantém 8192 bytes.

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
| `components/cyberdeck/include/apps/bluetooth/ble_mgr.h`, `components/cyberdeck/src/apps/bluetooth/ble_mgr.cpp` | `ble_mgr_start`, `ble_mgr_stop`, `ble_bond_snapshot_t`, `ble_bonds_copy` | Adaptador ESP-IDF, unico modulo autorizado a falar com a stack BLE do C6 via `esp_hosted`. `ble_mgr_start` executa estritamente `esp_hosted_connect_to_slave()` -> `esp_hosted_bt_controller_init()` -> `esp_hosted_bt_controller_enable()` -> `nimble_port_init()`, registra cada retorno e mantém o boot não fatal; após habilitar o controlador consulta e registra `esp_hosted_get_coprocessor_fwversion()` quando disponível. Falha de transporte/controlador encerra a tentativa sem iniciar NimBLE, sem retry/reset loop. Possui task FreeRTOS dedicada e fila bounded; a UI apenas enfileira. Pair/connect usam o endereco e token da acao, a seguranca inicia por `ble_gap_security_initiate` e so callbacks reais publicam conclusoes; cancelamento/desconexao usam terminacao bounded, OOB e passkeys invalidos falham fechados. O ciclo de vida separa o cancelamento de pairing da conexao autenticada promovida apos o bond: disconnect fisico publica DISCONNECTED real e permite nova conexao. Callbacks GAP e comandos de cancelamento/autenticacao validam a geracao ativa; callbacks stale nao alteram estado nem publicam. Bonds existentes sao atualizados, nao duplicados. `ble_bonds_copy` expõe snapshot bounded de identidade (addr+tipo+nome/kind/last, sem segredo) sob `s_dispatch_mutex` com espera curta e fail-closed, para restauracao NVS no boot pelo observador da UI. O `event_dispatch` fica protegido por mutex entre a task `ble_mgr` e callbacks GAP; a task de gestão usa stack 8192 bytes para reports GAP; `ble_host` usa `CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE` (4096) para preservar RAM interna com Wi-Fi ativo; o drain bounded continua fora da UI. Para o scan, o adaptador infere o tipo de endereco com `ble_hs_id_infer_auto(0, ...)` em vez de fixar `BLE_OWN_ADDR_PUBLIC`, registra inicio/fim e mantem contadores bounded de GAP DISC, cinco tipos aceitos, ignorados, stale/mutex, publicacoes, descartes de dispatch e malformed sem payloads irrestritos. Relatorios `BLE_GAP_EVENT_DISC` dos cinco tipos de anuncio LE (incluindo respostas de scan) sao encaminhados a `scan_report_adv`; tipos desconhecidos sao ignorados. Falha de inicializacao e nao fatal no boot. O teardown `nimble_port_freertos` permanece como follow-up fora desta tarefa. |
| `components/cyberdeck/include/apps/shell/cyberdeck_shell_utils.h`, `components/cyberdeck/src/apps/shell/cyberdeck_shell_utils.cpp` | `CYBERDECK_CMD_BLUETOOTH_SEARCH`, `CYBERDECK_CMD_BLUETOOTH_PAIRED`, `CYBERDECK_CMD_LOG`, `"bluetooth search"`, `"bluetooth paired"` | Parser roteia exatamente os dois subcomandos aprovados; o verbo nu e qualquer operando extra continuam `CYBERDECK_CMD_UNKNOWN`. O comando `log` preserva os argumentos para o override de linhas por sessao. Nao ha subcomando para Bluetooth Classic. |
| `components/cyberdeck/include/apps/shell/cyberdeck_shell_help.h` | linha `bluetooth [search|paired]` | Catalogo unico legado de ajuda (18 entradas `[terminal]`); Bluetooth e a 7a entrada legada, entre `battery` e `app`. A sessao acrescenta ao final comandos unicos de manifests registrados como `[app]`, sem alterar a ordem legada. |
| `tests/host/keymap/contracts/cyberdeck_ble_types.h` | ABI de `cyberdeck_ble_types.h` | Fixture de teste, nao e implementacao de producao. |
| `tests/host/keymap/contracts/cyberdeck_ble_state_machine.h` | ABI de `cyberdeck_ble_state_machine.h` + mensagens `k_msg_*`/`k_status_*` | Fixture de teste; fonte unica das mensagens de terminal e dos deadlines. |
| `tests/host/keymap/contracts/cyberdeck_ble_event_dispatch.h` | ABI de `cyberdeck_ble_event_dispatch.h` | Fixture de teste. |
| `tests/host/keymap/contracts/cyberdeck_ble_store.h` | ABI de `cyberdeck_ble_store.h` | Fixture de teste. |
| `tests/host/keymap/contracts/cyberdeck_help.h` | `kUnifiedHelpText` | Fixture compartilhada, oracle byte a byte de `test_shell_utils.cpp` e `test_help_unification.cpp`; inclui as linhas `bluetooth`, `wifi ... survey` e `screen [on|off|timeout <0-1440>]`. |
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
| `tests/host/keymap/test_ble_bond_name_contract.py` + alvo `test_ble_bond_reconnect` | cobertura focada de bonds/name BLE | Contrato estrutural para restaurar todos os bonds com `last_connected` como primeiro alvo, percorrer os demais em janelas de 10 s, matching por identidade/RPA + `addr_type` sem duplicar, tres tentativas por anuncio/ciclo e bloqueio manual; verifica ausencia de novo PAIR, fila/callbacks/tokens/generation bounded, sanitizacao ADV/SCAN_RSP antes/durante pairing, persistencia NVS, restauracao e exibicao em `bluetooth paired`, mantendo nome vazio como `unnamed`. O contrato individual `test_ble_bond_name_contract` também é executado pelo agregado `test`; o alvo composto `test_ble_bond_reconnect` não é invocado pelo agregado. Para execução focada, use `make -C tests/host/keymap test_ble_bond_name_contract test_ble_bond_reconnect`. |

| `test_ble_background_contract.py` + `test_ble_bond_reconnect` | `TEST-BLE-AGG-01..05` | Contrato refinado do alvo composto: dependências apontam para os artefatos `_bin` dos três testes BLE, a ordem state-machine/event-dispatch/store é preservada, cada binário executa uma vez, os dois contratos seguem ao fim e o alvo continua fora do agregado principal. O alvo não executa mais diretórios/nomes legados que colidiram com diretórios de objetos dos alvos BLE incrementais. |

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

`tests/host/keymap/test_display_lock_contract.py` (TEST-DISPLAY-LOCK-01..05)
is the host structural contract for fail-closed display-lock acquisition during
boot, and for keeping `screen.dump` snapshot, copy, and destruction under the
display lock. It also checks that `ui.dump` traverses LVGL only in its locked
branch; it is directed by `test_display_lock_contract` in the host Makefile.

| Arquivo | Simbolos/contrato | Papel |
| --- | --- | --- |
 | `components/cyberdeck/include/apps/serial/cyberdeck_serial_bridge.h` | `cyberdeck_serial::k_max_ndjson_line=4096`, `k_max_rid_len=64`, `k_screen_chunk_bytes=1024`, `k_fs_write_max_bytes=2048`, `dispatch_error`, `request/response/dispatch_result`, `SysInfo/WifiNet`, `LineAssembler`, `crc32/screen_bmp_size/screen_chunk_bounds/screen_dump_*`, `sys_info_* / wifi_scan_* / handle_* / dispatch_one / build_envelope`, `bridge_start`/`bridge_stop` (`#ifdef ESP_PLATFORM`) | Contrato NDJSON bounded e lifecycle da task `serial_brg`: start aguarda prontidão, stop solicita saída e faz join bounded sem desmontar driver, hook global ou sincronização compartilhada. `sys.info` acrescenta heap interno/DMA/PSRAM, inclusive maior bloco interno e DMA; campos são opcionais ao decodificar respostas para compatibilidade. `fs.write` seguro com path confinado, 2048 decoded, Base64 canonico, commit por temp unico no mesmo diretorio, criado com O_CREAT|O_EXCL e removido apenas apos create bem-sucedido, +rename (replace atomico no host; no-clobber no ESP/FATFS) e NDJSON errors. Header puro (sem `esp_err.h`). |
| `components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp` | parser JSON proprio (`parse_value` classifica e decodifica cada valor numa unica passada: string vira `kind::string` com `s` decodificado e `raw` literal com aspas), dispatch, sessao BMP/CRC, LineAssembler; `term.dump` reutiliza a API textual bounded da UI, com JSON escaping e envelope <=3500; `fs.write` (campos `path/data_b64/size` no `k_request_fields`, tipo conhecido `fs.write`, validacao rid/type/path/data_b64/size, path safety traversal/symlink/directory/parent, 2048, Base64 strict canonico `b64_decode_canonical`, temp unico `.fswrite.<sequencia>.tmp` no mesmo diretorio, criado com O_CREAT|O_EXCL e removido apenas apos create bem-sucedido, +rename sem unlink do destino, CRC; no ESP exige `O_NOFOLLOW` e limita `/sdcard` ao VFS FATFS sem symlink/openat, valida parents e o descriptor temporario, rejeita overwrite existente porque nao ha garantia de replace atomico, e preserva o destino em falha de rename; no host a raiz logica `/sdcard` e reancorada em sandbox `/tmp/cyberdeck5_sd` via `CYBERDECK_SD_ROOT`); `sys.info` reporta heap total, interno, DMA e PSRAM, incluindo maior bloco livre interno/DMA; device: task `serial_brg` com handle real, handshake de prontidao, stop cooperativo/join bounded e estado fail-safe, driver USB Serial-JTAG, `esp_log_set_vprintf` com mutex, hooks UI/wifi/screen/fs | Dispatch NDJSON sem alocacao >4096, validacao UTF-8/rid/type, `screen.dump` byte-identical em chunks 1024 com CRC IEEE e flag `end`, feeder tolerante a logs/fragmentacao, `sys.info`/`wifi` e `term.dump`; `fs.write` com validacao completa, commit seguro e erros tipados `invalid_path`/`invalid_payload`/`io_error`. A captura LVGL de `screen.dump` mantém snapshot, cópia e destruição sob o lock do display. Autocontido em relacao a `screenshot_bmp_*` fora de `ESP_PLATFORM`: os tres testes que nao linkam `screenshot_bmp.cpp` nao veem undefined de funcoes externas (stride/size/header/conv sao espelhados localmente com formulas identicas). |
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
| `components/cyberdeck/src/platform/sensors/imu_reader.cpp` | `imu_reader_start`, `imu_reader_stop`, `sensor_handle_t`, `bsp_sensor_init` | Inicializacao do BMI270 via Sensor Hub; `sensor_handle_t` e o handle obrigatorio usado por `bsp_sensor_init`; amostra inicial limitada por timeout, aplicacao da orientacao antes da UI e leitura posterior para rotacao. O semaforo da primeira amostra e estatico para que callback tardio nao use memoria liberada; handler, sensor e timer possuem teardown explicito para init parcial. As chamadas LVGL de startup exigem o lock adquirido pelo compositor (`app_main` usa espera bounded e falha fechado), enquanto o callback do sensor publica somente estado atomico/semaforo; o timer aplica a rotacao no contexto LVGL. O contrato proibe somente a leitura direta `imu_acquire_acce(`, preservando o fluxo do Sensor Hub. |
| `components/cyberdeck/src/platform/sensors/orientation.cpp` | `orientation_from_accel`, `orientation_update` | Conversao da aceleracao em rotacao e debounce da orientacao posterior. |
| `components/cyberdeck/src/platform/sensors/battery_status.cpp`, `components/cyberdeck/include/platform/sensors/battery_status.h` | `percentage_from_bus_voltage_mv`, `classify_current_ma`, `classify_sample` | Logica pura recebe `bus_voltage_mv`, satura a janela validada `6000..8230` mV em `0..100` e classifica corrente positiva como descarregando, negativa como carregando e zero/indeterminada como neutral; ausencia so ocorre com presenca explicita e leitura invalida fica indisponivel sem fabricar percentual. |
| `components/cyberdeck/src/platform/sensors/ina226_reader.cpp`, `components/cyberdeck/include/platform/sensors/ina226_reader.h` | `ina226_reader_start`, `ina226_reader_get_snapshot` | Reader INA226 no barramento BSP e endereco I2C `0x41`: registra de tensao `0x02` convertido em mV, configuracao `0x4527`, calibracao `0x0D55`, identificacao, tarefa dedicada a cada 1 s e snapshot protegido por mutex; falhas de leitura apos startup sao publicadas como indisponiveis. Nao usa `CHG_EN`, `CHG_STAT`, NVS ou politica de protecao; o startup e nao fatal. |
| `components/cyberdeck/src/platform/sensors/cyberdeck_battery_protection.cpp`, `components/cyberdeck/include/platform/sensors/cyberdeck_battery_protection.h` | `decode_chg_stat`, `state::observe`, `state::snapshot`, `state::last_safe_snapshot`, `state::set_protection_enabled`, `charger_enabled`, `current_uncertainty_ma`, `external_voltage_mv`, `absent_voltage_mv`, `state_vote_count`, `protection_enter_percentage`, `protection_enter_voltage_mv`, `protection_exit_percentage`, `default_protection_enabled` | Politica pura host-testavel: estados battery/external/charging/absent/unknown, threshold corrente ±15 mA, external >=7900 mV, absent >=8330 mV com 5 votos, e tensao abaixo de 6000 mV tratada como ausencia, todos com 5 votos; `classify_state` preserva CHG_STAT e corrente como evidências da classificação usada pela política de proteção/CHG_EN. Protecao so em charging + percentual >=90 + tensao >=8200, histerese retoma <=85, opcao enabled persistida no NVS (default true), falhas nao desligam CHG_EN nem perdem ultimo estado seguro. O snapshot publica o sinal `charge` decodificado. Leitura INA valida atualiza tensao/corrente/percentual/disponibilidade mesmo com `CHG_STAT` invalido; leitura INA invalida preserva o estado seguro para a protecao, mas publica `available=false` para ocultar a UI ate uma nova leitura valida. Ausencia/presenca e resolvida antes do sinal do carregador e da corrente. Nao usa ESP-IDF, FreeRTOS, I2C, NVS, LVGL ou BSP. |
| `components/cyberdeck/src/platform/sensors/battery_protection.cpp`, `components/cyberdeck/include/platform/sensors/battery_protection.h` | `battery_protection_init`, `battery_protection_start`, `battery_protection_get_snapshot`, `battery_protection_get_policy_snapshot`, `battery_protection_set_enabled`, `battery_protection_is_enabled`, `battery_protection_is_active`, `battery_protection_charger_enabled`, `sample_and_publish` | Adaptador exclusivo para hardware: Expander B via `bsp_io_expander1_init()` (0x44), CHG_STAT pin 6 active-low input/pull-up, CHG_EN pin 7 output push-pull, CHG_EN=1 por default. Integra reader INA226 (sensor-only), politica pura, NVS para opcao enabled, timer UI 1 s. Um unico `sample_and_publish` alimenta a politica e publica as duas projecoes: `cyberdeck_battery::snapshot` (charge_class, usada pelo shell/status) e o snapshot puro da politica (`battery_protection_get_policy_snapshot`), que e a unica entrada de bateria consumida pela view do header. Falhas de I2C/NVS/CHG logadas sem desligar CHG_EN nem perder estado seguro. |
| `components/cyberdeck/src/platform/display/cyberdeck_ui.cpp` | `battery_indicator_symbol`, `refresh_battery_status`, `refresh_ble_status`, `process_battery_protection`, `execute_line` (battery protection on/off/status), timer LVGL 1 s | Mantem a grade direta 30/40/30, Bluetooth antes do Wi-Fi antes da bateria dentro da celula direita e tres labels no grupo da bateria (glyph semantico, percentual numerico e overlay do percentual). O <PII type="CASE_ID" id="30"> nao classifica estado: ele le `battery_protection_get_policy_snapshot`, aplica `cyberdeck_battery_view::resolve` e fixa o glyph por `battery_indicator_symbol` (`LV_SYMBOL_CHARGE` em carga, `LV_SYMBOL_BATTERY_FULL` constante em bateria presente, `LV_SYMBOL_MINUS` em alimentacao externa sem bateria, vazio quando invisivel); nao ha glyph de nivel nem seletor por porcentagem, e o grupo e ocultado quando a view decide `visible == false` (leitura indisponivel ou estado `unknown`). A celula wrapper Bluetooth (`s_ble_cell`, `LV_SIZE_CONTENT` x 42, layout none, padding 0, `flex_grow=0`, fundo transparente) nasce oculta junto do label interno (`s_ble_status`, `LV_SIZE_CONTENT` content, `CYBERDECK_BLE_HEADER_Y_OFFSET` via `lv_obj_set_y`); o glyph padrao `LV_SYMBOL_BLUETOOTH` usa `style_base(BLACK, WHITE)` e `refresh_ble_status()` apenas aplica `s_ble_model.is_connected()` na celula e no label, sem classificar estado. O percentual e mostrado quando a view expoe `show_percentage` (carga, bateria e external) e omitido em `absent`; o overlay acompanha esse texto e visibilidade sem participar do layout. Timer LVGL 1 s consome snapshot do adaptador; comandos shell `battery protection on/off/status` roteados para adaptador; `ui.type` transita via bridge serial. |
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
libssh e HTTP server. `main/idf_component.yml` declara ESP-IDF 6.1,
`esp_lvgl_port`, LVGL diretamente na faixa `>=9.6.0,<10.0.0`, ESP-Hosted 3,
`esp_wifi_remote` 1.6.5, cJSON do Component Registry, libssh e o override local
de `sock_utils`. IDF 6 moveu o componente `json` para fora do SDK; o componente
usa `espressif/cjson`. A compatibilidade `components/cyberdeck/libssh_idf6_compat/termios.h`
e o include/define privado do alvo libssh adaptam a mudança de caminho do
header termios do IDF 6, sem alterar `managed_components`. O BSP declara
`esp_driver_i2s` diretamente para satisfazer a verificação de dependências de
headers do IDF 6. `sdkconfig.defaults` habilita `CONFIG_FATFS_FS_LOCK=5`
(protege os cinco VFS FAT slots contra rename/unlink de arquivos abertos) e
`CONFIG_FATFS_TIMEOUT_MS=1000`; a task `wifi_audit_io` ainda impõe deadline
próprio de 2 s para chamadas SD/VFS.

### Overlay local

`components/sock_utils/` substitui a dependencia gerenciada de mesmo nome:

- `src/getnameinfo.c`: AF_INET, AF_INET6 e IPv4-mapped.
- `include/netdb_macros.h`: flags `NI_NUMERICHOST`, `NI_NUMERICSERV` e `NI_DGRAM`.
- `test/host/main/test_getnameinfo.cpp`: validacao host dual-stack.

### Concorrencia

- UI/LVGL: atualizacoes protegidas por `bsp_display_lock`.
- Teclado fisico: fila FIFO bounded de 8, despacho por `lv_async_call` e wake explícito da task LVGL após o enqueue.
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
| `components/cyberdeck/include/apps/shell/cyberdeck_shell_help.h` + `cyberdeck_shell_utils.cpp` | `test_shell_utils.cpp` (inclui parsers audit/survey, duração, export/confirm masked e unmasked, `screen on|off|timeout <0-1440>`; catálogo legado mantém 18 blocos (65 linhas = 18 tags + 47 indentadas) com header de tag, usage-indentado e linhas de parâmetros, igualdade com fixture tagged e determinismo) |
| `components/cyberdeck/include/apps/shell/cyberdeck_shell_help.h` + `cyberdeck_shell_utils.cpp` + `cyberdeck_local_shell.cpp` + `cyberdeck_shell_session.cpp` | `test_help_unification.cpp` (paridade do catálogo compartilhado, texto exato `[terminal]`/`[app]`, estimativa de tamanho renderizado e verificação estrutural independente de hierarquia header/usage/parâmetro sob `TEST-HELP-HIERARCHY-02`) e `test_shell_session.cpp` (help dinâmico, manifests registrados, precedência/collision, sem lifecycle start, tags e determinismo); `help_unification_contract.py` verifica fonte canônica, fórmula/reserve de tamanho e ausência de listas literais independentes nas rotas de shell/UI |
| `cyberdeck_screen_protection.cpp` + contrato puro `contracts/cyberdeck_screen_protection.h` | `test_screen_protection.cpp` (default 2 min, limites 0/1/2/120/1440, parsing decimal estrito, zero desabilitando com preservacao do ultimo positivo, reject 1441 sem mutacao, restore/snapshot NVS, transicoes exatas do timer e independencia de power on/off) |
| `screen_off.cpp` + `cyberdeck_ui.cpp` + `cyberdeck_shell_utils.cpp` + `cyberdeck_serial_bridge.cpp` + `app_main.cpp` + `cyberdeck_shell_help.h` | `test_screen_protection_contract.py` (rotas de comando, literal `screen [on|off|timeout <0-1440>]` na linha do bloco screen do catalogo compartilhado (4 campos, incluindo parametros) com delegacao `cyberdeck_help_text()` -> `cyberdeck_shell_help::text()`, fixture `contracts/cyberdeck_help.h` e os dois testes comportamentais que a comparam, timer LVGL, persistencia/restauracao NVS, default seguro, init NVS antes do adapter, duplo toque e caminho transitivo `ui.type` -> `inject_text_segmented`/`inject_enter` -> `cyberdeck_keyboard_input`; o alvo do Makefile declara todos esses arquivos como pre-requisitos) |
| `cyberdeck_history.cpp` | `test_history.cpp` |
| `cyberdeck_edit_line.cpp` | `test_edit_line.cpp`, `test_prompt_behavior.py` |
| `cyberdeck_local_shell.cpp`, `cyberdeck_cat_worker.cpp` | `test_local_shell.cpp` (raiz virtual `/`, caminhos relativos/absolutos, rejeição do alias `/sdcard`, confinamento, operações e regressões de segurança; inclui secure-open/opendir para arquivo, diretório, symlink, traversal e tamanho; focado atual: 1122 checks), `test_cat_multiline.cpp` (API dedicada com cwd `/` e alias físico rejeitado, LF/CRLF/tabs/UTF-8, marcador final, bytes inválidos, limite exato de 12288, NUL embutido e sufixo após newline preservados para sanitização), `cat_contract.py`, `test_cat_multiline_contract.py` (contrato estrutural de ponteiro+tamanho explícito, textarea multiline/max-length, limite UTF-8 e payload completo até append), `test_cat_lifecycle_sanitization_contract.py` (ramos sem task vs. com task, drain/join/ack, reset sincronizado, callback/generation e sanitizacao), `test_cat_start_teardown_start_contract.py` (restart e invalidacao stale), `test_cat_stack_footprint_contract.py` (regressao do stack minimo do worker), `test_cat_worker_path_safety_contract.py` (proibe o caminho worker->shell/resolve/path/vector e exige API cat-specific heap/bounded), `local_shell_security_contract.py`, `local_shell_tokenizer_contract.py` (tokenização e contrato estrutural do root `/`), `test_prompt_behavior.py`, `test_local_prompt_contract.py` |
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
| `cyberdeck_wifi_audit.cpp` (worker passivo/local, snapshot versionado, SSID do driver limitado ao array, scratch/resultado grandes fora da stack e persistência save confinada; lifecycle/token, guard single-shot e descarte stale/cancelado; backend `wifi_audit_io` bounded fora da UI delega ao seam transacional) | `test_wifi_audit.cpp`, `test_wifi_audit_contract.py`, `test_wifi_audit_save.cpp`, `test_wifi_audit_save_contract.py` (conteúdo/sanitização/formato, destino e colisão, persistência/ACK e contrato structural do seam; inclui policy audit/survey, limites payload, recovery/rollback e ownership/quarentena do backend; sem evidência de FatFS runtime) |
| `cyberdeck_wifi_audit_persistence.cpp` (seam de filesystem injetável em produção; adapter nativo e coordinator de completion) | `test_wifi_audit_persistence.cpp` (sucesso com payload real e ACK após publicação; write/fsync/close/rename; destino timestampado em `/sdcard/wifi-audit/`; sidecars `.tmp`/`.bak` derivados de `make_transaction_paths`; destino existente; recovery; rollback e preservação; single-shot/fila cheia; ACK retido; concorrência enqueue/publish e teardown concorrente com pump_one/enqueue/drain), `test_wifi_audit_persistence_contract.py` + `contracts/cyberdeck_wifi_audit_persistence.h` (Makefile/CMake/code-map; ambos os fontes de teste têm exceção explícita no `.gitignore`; a validação de `BINS` ancora a correspondência em definições reais `BINS :=`/`BINS +=`, ignorando menções em comentários, com controles de regressão positivo/negativo) |
| `cyberdeck_ui.cpp` + `cyberdeck_shell_utils.cpp` + `cyberdeck_wifi_audit.cpp` + `cyberdeck_wifi_audit_persistence.cpp` (contrato TDD do fluxo `wifi audit`/`wifi audit save`) | `test_wifi_audit_save.cpp` (comportamental host: renderização silenciosa em `collecting`, linha terminal exata em `ready`/`error`, `<missing>` somente no snapshot final, diretório, timestamp, colisão, sidecars, falhas, rollback e ACK; exercita a implementação de produção) e `test_wifi_audit_save_contract.py` (contrato estrutural de parser/UI, gate de `process_wifi_audit`, renderização once-only, GMT-3 e compatibilidade serial transitiva `exec_ui_type` -> `inject_text_segmented`/`inject_enter` -> `cyberdeck_keyboard_input`, sem hardware) |
| `screenshot_bmp.cpp` | `test_screenshot_bmp.cpp` |
| `cyberdeck_window_manager.cpp` + `cyberdeck_window_manager_adapter.cpp` + `cyberdeck_terminal_view.cpp` + `cyberdeck_ui.cpp` + `components/cyberdeck/CMakeLists.txt` + `tests/host/keymap/Makefile` (REQ/AC-8.1..8.7 / TEST-WM-01..07 + TEST-REG-8-BAR, Fase 8) | `test_window_manager.cpp` (comportamental host: superficies, foco, teardown, notificacoes, adaptador contra o shim LVGL completo e `bar_layout_scenario` para a regressão BUG-8-WM-BAR) e `test_window_manager_contract.py` (contrato estrutural: pureza dos headers, `view_context` opaca, LVGL somente no adaptador, `system_bar`/`content` pela raiz composta, linha fixa da barra e coluna flex sem altura explícita no content, teclado virtual em overlay, ordem teardown antes de `deinit`, guardas de limite/handles obsoletos e wiring CMake/Makefile/guard de cobertura) |
| `cyberdeck_ui.cpp` + `cyberdeck_wifi_survey_export_controller.cpp` | `test_boot_sequence.py`, `test_keyboard_input_contract.py`, `test_ui_resource_contract.py`, `test_wifi_survey_integration_contract.py`, `test_wifi_survey_export_controller.cpp` (TEST-SURVEY-LIFECYCLE/EXPORT) | `test_ui_resource_contract` verifica estruturalmente init/deinit, ownership, 13 fases fail-closed, os 9 timers e preservação dos recursos app-lifetime de export; enumera rollback/teardown, mas não simula execução FreeRTOS. O contrato de survey verifica wait sequence-matched bounded, drain e delete depois de callback completion ou quarentena em timeout; não exercita runtime FreeRTOS, callback tardio real ou corrida de execução. O teste do controller exercita token, admissão, start e completion retida/publicada em host; inclui ainda `test_local_prompt_contract.py` e `test_wifi_enter_routing_contract.py`. |
| `main/app_main.cpp`, `cyberdeck_ui.cpp` | `test_boot_sequence.py` (ordem SD/UI, montagem física `/sdcard` e root virtual `/`) |
| `imu_reader.cpp` | `imu_sensor_contract.py` (callback Sensor Hub, `sensor_handle_t` obrigatorio para `bsp_sensor_init`, proibicao especifica da leitura direta `imu_acquire_acce(`, timeout/fallback seguro, semaforo estatico nao destruido enquanto callback pode executar, rollback de registro/start/timer, ownership e teardown repetido de handler/sensor/timer, e continuidade da rotacao) |
| `battery_status.cpp` + `ina226_reader.cpp` + `battery_protection.cpp` + contrato puro `contracts/cyberdeck_battery.h` | `test_battery_contract.cpp` (REQ-BAT-001/002: tensao `bus_voltage_mv`, janela `6000..8230`, saturacao, estados charging/discharging/neutral, zero/indeterminado e ausencia explicita), `test_battery_reader_contract.py` (REQ-BAT-001/002/004/005/006: `0x41`, `0x4527`/`0x0D55`, conversao do registro de tensao, task 1 s, mutex/snapshot, exclusoes, sensor-only, composicao pelo adaptador e nao-fatal; a UI consome a entrada de bateria apenas pelo seam de snapshot do adaptador, `battery_protection_get_policy_snapshot` ou `battery_protection_get_snapshot`, sempre declarado no header do adaptador, e nao pode conter nenhuma referencia direta ao reader INA226, seja chamada ou include), `test_battery_protection.cpp` + `test_battery_protection_contract.py` (politica e adaptador reais, sem hardware) |
| `cyberdeck_battery_view.cpp` + contrato puro `contracts/cyberdeck_battery_view.h` | `test_battery_view.cpp` (REQ-BAT-UI-001..005 / AC-BAT-UI-001..006: `resolve` sobre a matriz total de estado/sinal/disponibilidade/corrente/percentual; absent sempre glyph externo sem percentual; política `state`/`charge` preservada para proteção/CHG_EN; somente `current_ma < -current_uncertainty_ma` seleciona glyph de carga; sem carga real usa glyph de bateria para bateria/external; `unknown`/INA indisponível oculta; clamp 0..100; `from_snapshot` propaga corrente sem inventar entrada; glyph não depende do nível; regressão CHG_STAT preso em low) e `test_battery_view_contract.py` (view pura sem ESP-IDF/FreeRTOS/LVGL/I2C/NVS/BSP, LVGL delega `from_snapshot`/`resolve`, tabela de glyph, percentual vazio quando `show_percentage` falso e grupo oculto quando `visible` falso). O alvo liga a view com a política pura de `cyberdeck_battery_protection.cpp`, sem ESP-IDF, LVGL, I2C, NVS, simulador, Serial Automation Bridge ou hardware. |
| `cyberdeck_ui.cpp` + `cyberdeck_wifi_icon.cpp` + `cyberdeck_battery_view.cpp` + `battery_protection.cpp` + `app_main.cpp` | `test_battery_ui_contract.py` (REQ-BAT-002/003/004/006/010 + REQ-BAT-UI-004/005: grade direta 30/40/30, ordem Wi-Fi/bateria, layout compacto, percentual numerico, um label de icone, um label de percentual e um overlay de negrito ignorado pelo layout, ausencia de selecao de icone por nivel e de ramo `charge_class::` no refresh, ocultacao em falha, timer/snapshot do adaptador sem dependencia direta do reader, continuacao do boot e rastreabilidade). A selecao de icone foi transferida para a view pura de REQ-BAT-UI: o `refresh_battery_status` consome `battery_protection_get_policy_snapshot` e delega a `cyberdeck_battery_view::resolve`, enquanto `battery_indicator_symbol` fixa o glyph semantico. |
| `cyberdeck_serial_bridge.cpp` (REQ-002/003/005/006/007/008/009) — ponte NDJSON bounded, rid/envelopes, UI/sys/wifi, screen.dump chunks/CRC/end, feeder tolerante a logs/fragmentacao; `cyberdeck_cli.py` | `test_serial_ndjson_dispatch.cpp` (bounded/erros/rid/envelopes/UI, inclusive `ui.type` para `screen on|off|timeout`), `test_serial_screen_dump.cpp` (byte-identical chunks/CRC/end), `test_serial_cli_tolerance.cpp` (logs/leitura fragmentada), `test_serial_sysinfo_wifi.cpp` (sys.info/wifi contratos) — todos host-only, sem pyserial/hardware; GREEN com a producao criada |
| `cyberdeck_serial_bridge.cpp` `fs.write` (REQ-001..REQ-011) — protocolo rid/type/path/data_b64/size, limite 2048, path safety, strict canonical base64, commit por temp unico/O_EXCL+rename (substituicao atomica no host/no-clobber no ESP/FATFS; nunca remove candidato preexistente), CRC response, NDJSON errors; `cyberdeck_cli.py` `fs.write` (`build_request`, encoding, input/stdin, 2048, strict canonical) | `test_fs_write_dispatch.cpp` (dispatch/validacao/path/size/base64/CRC/preservacao; colisao de temp preexistente/no-clobber), `test_fs_write_cli.py` (parser/build_request/encoding/stdin/limite), `test_fs_write_contract.py` (estrutural: disco/path/atomic/CRC/CLI/Makefile/code-map) — todos host-only, RED antes da producao |
| `cyberdeck_ble_types.cpp` + contrato puro `contracts/cyberdeck_ble_types.h` | `test_ble_types.cpp` (classificacao por `appearance` provavel vs. `unknown`, normalizacao estrita de endereco, sanitizador bounded/UTF-8-safe sem C0/C1, clamp de RSSI, passkey estrito + mascara constante, dedup por endereco com RSSI mais forte e primeiro nome, capacidade 32, selecao com clamp, render deterministico, nomes duplicados, beacon nao conectavel) |
| `cyberdeck_ble_state_machine.cpp` + contrato puro `contracts/cyberdeck_ble_state_machine.h` | `test_ble_state_machine.cpp` (scan assincrono com um unico `start_scan`, deadlines exatos, ownership derivado do estado visivel e liberado silenciosamente apos lista vazia/falha/timeout, quatro classes de mensagem distintas, rejeicao de token obsoleto/zero/duplicado, dispositivo desaparecido nao pareia, passkey exibido so em `status_line` e zerado apos submissao, passkey errado nao encaminha, desfechos bonded/rejected/cancelled/timed_out/failed, conectar/escape/desconectar, orcamento de reconexao com rearme manual, concorrencia scan x reconexao) |
| `cyberdeck_ble_event_dispatch.cpp` + contrato puro `contracts/cyberdeck_ble_event_dispatch.h` | `test_ble_event_dispatch.cpp` (tokens monotonicos compartilhados, publicacao obsoleta descartada sem enfileirar, fila bounded 8 com overflow fail-closed, ordem e propriedade no `drain`, `drop_stale`, geracao manual x automatica, `event_summary` sem digitos do passkey) |
| `cyberdeck_ble_store.cpp` + contrato puro `contracts/cyberdeck_ble_store.h` | `test_ble_store.cpp` (encode determinista com ordem de campos fixa, decode estrito rejeitando campo duplicado/reordenacao/lixo, campo desconhecido rejeitado impedindo contrabandear `ltk`/`irk`/`passkey`, capacidade 16, serialize bounded, deserialize atomico fail-closed, round trip de reboot) |
| `cyberdeck_shell_utils.h/.cpp` + `cyberdeck_shell_help.h` (parser BLE e catálogo help legado, incluindo linha de survey) | `test_ble_command_parse.cpp` (comportamental: subcomandos Bluetooth, grafias próximas desconhecidas e golden de help; ciclo 5 também garante a linha survey e 18 blocos) + `test_help_unification.cpp`/`help_unification_contract.py` (fonte única e igualdade byte a byte com `contracts/cyberdeck_help.h`; sessão acrescenta comandos `[app]`) |
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
| `.clang-format`, `.github/workflows/quality-gate.yml`, `tools/coverage_scope_guard.py`, `tests/host/keymap/test_coverage_scope_guard.py` | `quality`, guard de escopo gcovr | Gate usa clang-format 23.1.3, Ruff 0.17.1, gcovr 8.7, Bandit 1.9.4 e checkout v7.0.1 pinado por SHA; agrega testes host e build ESP-IDF 6.1. Mantém os limites globais de cobertura (80% linhas, 94% funções, 59% branches), `coverage_scope_guard.py` fail-closed e allowlist explícita dos adaptadores hardware. A formatação de `ble_mgr.cpp` valida somente faixas explícitas (incluindo pairing/reconnect) para preservar regiões legadas; ao inserir linhas nesses blocos, mantenha os intervalos alinhados. `cyberdeck_time_sync.cpp` e os shims NetDiag listados no workflow também são validados integralmente conforme clang-format 23.1.3.

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
| `REQ-BAT-UI-004` — integracao da UI sem regra de negocio no LVGL | `AC-BAT-UI-005` — tres labels (icone, percentual e overlay), grade 30/40/30, sem acesso direto a I2C/NVS/reader | `cyberdeck_ui.cpp`: `battery_indicator_symbol` fixa o glyph, `refresh_battery_status` consome `battery_protection_get_policy_snapshot` e aplica `resolve`; `battery_protection.cpp`/`battery_protection.h` publicam o snapshot puro |
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
linha `screen [on|off|timeout <0-1440>]` na tabela compartilhada de 18 entradas,
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
- `README.md` e `README.pt-BR.md`: recursos, uso do shell, Wi-Fi, SSH, comandos locais `netdiag` (status, DNS, TCP, HTTPS HEAD, resultado e cancelamento), screenshot e ponte serial.
- `docs/ARCHITECTURE.md` e `docs/ARCHITECTURE.pt-BR.md`: principios, boot, concorrencia Wi-Fi, screenshot,
  ponte serial, bateria e UI.
- `tests/manual/tui-shell-validation.pt-BR.md`: validacao no dispositivo.
- `docs/WIFI.md`: fluxos Wi-Fi, incluindo auditoria local e salvamento explícito
  da rede conectada.
- Bluetooth LE: `bluetooth search` e `bluetooth paired` sao as duas entradas de
  terminal; Bluetooth e a 7ª entrada do catalogo em
  `components/cyberdeck/include/apps/shell/cyberdeck_shell_help.h`.
- `tests/manual/serial-bridge-validation.pt-BR.md`: validacao da ponte
  USB Serial-JTAG no dispositivo.
- `tools/cyberdeck_cli.py`: cliente NDJSON host da ponte serial.
- `components/cyberdeck/CMakeLists.txt`: lista definitiva dos fontes compilados.
- `tests/host/keymap/Makefile`: lista definitiva dos testes e fontes puros.

## Rastreabilidade de logging do time sync

`tests/host/keymap/test_time_sync.cpp` inclui as regressões TEST-TIMELOG-04 para
backpressure retentável e TEST-TIMELOG-08 para timeout de ACK sem retry ou
duplicação; `test_time_sync_contract.py` verifica o adaptador real, append/fsync
e retry bounded. A matriz completa REQ-TIMELOG-01..04 e os passos
Serial-JTAG estão em `tests/host/keymap/time_sync_traceability.md` e
`tests/manual/serial-bridge-validation.pt-BR.md`.
