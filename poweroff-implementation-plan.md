# Plano de implementação: `poweroff` no Tab5

**Tipo:** documento de controle de progresso; não é evidência de teste.  
**Estado geral:** **POWER-CYCLE MATRIX PASS — desligamento físico confirmado pelo usuário em bateria-only, bateria+USB e cabo de alimentação externo**. Nos ciclos bateria-only e bateria+USB, pós-relight `sys.info`/`ping` e readback/hash do canário passaram; cancelamentos e `.poweroff-sync` também foram verificados. O cenário somente com cabo externo foi reportado como testado pelo usuário, sem readback/hash independente registrado aqui. Falhas/timeout têm cobertura host; não foi feita injeção destrutiva de falha no hardware.

### Teste no dispositivo — 13/out/2026

- Flash autorizado pelo usuário em `/dev/ttyACM0`, Espressif USB JTAG `E8:F6:0A:E3:22:29`: PASS, hashes de gravação verificados. Binário SHA-256 `eea07174c649a6d8721caa3a5f9de8619d2302350aabe301c0e176473c6de606`.
- Boot/ponte: `sys.info` retorna `e6f1ef3-dirty`, IDF v5.5.5, ESP32-P4, safe_mode=false e interrupted_boots=0.
- `poweroff` via `ui.type`: prompt exato observado em `term.dump`. `y` aceito pela ponte, mas efeito observado depois: `poweroff: quiescing storage; do not remove power.` seguido de **`poweroff: request failed; reboot required.`**.
- `ping` e `sys.info` continuam respondendo; uptime evoluiu de 00:01:16 para 00:02:14 sem reboot. Não há evidência de pulso/corte; teste FAIL na admissão. Causa específica ainda não determinada; não repetir a tentativa one-shot sem reboot controlado.
- Uma captura inicial com alteração explícita de DTR/RTS provocou `CHIP_USB_UART_RESET`, cancelando o prompt anterior; esse envio de `y` não é evidência de shutdown. O cenário válido foi repetido numa única conexão sem alterar essas linhas, verificando prompt antes de confirmar.
- Evidências locais: `/tmp/opencode/cyberdeck5-poweroff-device-flash.log`, `/tmp/opencode/cyberdeck5-poweroff-device-command.log`, `/tmp/opencode/cyberdeck5-poweroff-device-test.log`.

### Flash diagnóstico e captura de heap — 13/out/2026

- Commit testado: `b0ce47fe5bad592744a662aafa6b5e05073b2e41`; Tab5 `/dev/ttyACM0`, Espressif USB JTAG `E8:F6:0A:E3:22:29`, IDF v5.5.5. `sys.info` confirmou firmware `b0ce47f`, ESP32-P4, `safe_mode=false`; `ping` respondeu antes e depois.
- SHA-256 do `build/cyberdeck5.bin` efetivamente gravado/verificado pelo esptool: `05a6f1a498cf7e1a57f437fe9bd00965e8b08e5f98a48f484ab0104882e3c447`. O `idf.py flash` refez o build no commit publicado; o hash pré-flash anterior era outro, portanto este hash pós-build é o da imagem testada.
- Uma única sequência: `ui.type poweroff`; `term.dump` confirmou o prompt exato; `ui.type y` uma vez. Retorno: `poweroff: shutdown task unavailable; reboot required.` `ping` e `sys.info` responderam depois, sem reboot observado. Nenhum retry.
- Log serial bruto: `/tmp/opencode/cyberdeck5-poweroff-attempt-serial.log`. No instante da falha: heap `MALLOC_CAP_8BIT` livre `31469647`, maior bloco `30932992`; heap `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT` livre `6935`, maior bloco `5632` bytes. Como a task solicita stack de `8192` bytes, o heap interno livre e o bloco contíguo eram menores que a stack, identificando insuficiência de heap interno como causa imediata da falha de criação. Isso não determina ainda a correção segura nem prova corte físico.
- `idf.py monitor` competia com a CLI pelo leitor serial; foi fechado sem reset e a captura foi feita por tee serial de leitura única ao redor do protocolo/cliente da CLI. Não houve manipulação manual de DTR/RTS. Sem evidência de pulso/corte físico. Após esta falha one-shot, não repetir `poweroff` sem reboot controlado.

### Correção de alocação da task — validada até admissão

- A task de shutdown agora conserva os 8192 bytes de stack e usa `xTaskCreateWithCaps(..., MALLOC_CAP_SPIRAM)`. O projeto já habilita `CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y`; a configuração efetiva do firmware habilita `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y`, e `cyberdeck_time_sync.cpp` já usa a mesma API para stack em PSRAM.
- A escolha evita reduzir stack de uma rotina de shutdown complexa e não depende do heap interno fragmentado observado. Se não houver alocação PSRAM, a admissão permanece falha, fence sticky e sem retry.
- Validação local pós-delta: teste isolado `test_poweroff` e contrato `test_poweroff_contract.py` PASS; agregado `make test` PASS; build ESP-IDF 5.5.5 PASS; formato das linhas alteradas e `git diff --check` PASS. O CI remoto ainda não foi executado para este delta.
- Imagem construída e gravada na rodada seguinte: SHA-256 `a760fe7d57ca4f070a9555c51e7fdb71815e80c1ad6a0b4b5c50ea25a938571a`.

### Flash e tentativa única após correção — 13/out/2026

- Pré-condições confirmadas pelo usuário: backup/dados descartáveis, alimentação segura e captura serial disponível. Tab5 `/dev/ttyACM0`, USB JTAG `E8:F6:0A:E3:22:29`; `idf.py flash` concluiu com verificação de hash esptool. Firmware reportou `cef3257-dirty`, IDF v5.5.5, ESP32-P4, safe_mode=false; `ping` respondeu antes do teste.
- Uma única sequência confirmou o prompt exato, enviou `y` uma vez e recebeu `poweroff: admitted; quiescing storage. Do not remove power.` A admissão confirma que a criação da task com stack PSRAM passou neste dispositivo.
- Log serial bruto: `/tmp/opencode/cyberdeck5-poweroff-psram-validation-serial.log`. Na primeira etapa de quiescência BLE, o log registrou `BLE manager stopped`, `BLE host task stopped` e imediatamente `FreeRTOS: FreeRTOS Task "ble_host" should not return, Aborting now!`, seguido de dump de registradores e `Rebooting...`. Depois do reboot `sys.info` mostrou uptime ~7 s e `ping` voltou a responder. Isto é panic/reboot observado, não sucesso de poweroff nem prova de corte físico.
- Hash do binário gravado: `b9da9404e293f86454c082e2862766c71587be58b379f5d961600a0f8c769e03`. `sys.info` confirmou `cef3257-dirty`, IDF v5.5.5, ESP32-P4, `safe_mode=false`; `ping` respondeu antes do teste.
- A única sequência confirmou o prompt, enviou `y` uma vez e recebeu `poweroff: admitted; quiescing storage`. O log `/tmp/opencode/cyberdeck5-poweroff-ble-fix-validation-serial.log` registra BLE manager parado, `BLE host task exiting`, e novamente `FreeRTOS Task "ble_host" should not return`, dump e reboot. Após o reboot, `sys.info` respondeu com uptime ~51 s; não houve retry.
- A mudança anterior, que removeu apenas a chamada externa duplicada, **não corrigiu** o panic. Investigação do port ESP-IDF confirma que `nimble_port_freertos_deinit()` apaga somente o `host_task_h` criado por `nimble_port_freertos_init()`, mas este firmware cria `ble_host_task` diretamente via `xTaskCreate`; portanto o handle privado é nulo e deinit retorna sem apagar a task. A correção proposta é autoexclusão explícita `vTaskDelete(nullptr)` após `nimble_port_run()`.
- Esta falha não comprova que a hipótese explique todo o panic até runtime validar a mudança. O corte físico não foi comprovado. A autorização para esta rodada foi de uma tentativa; não repetir sem novo checkpoint específico.
- Hash do `build/cyberdeck5.bin` após flash: `a760fe7d57ca4f070a9555c51e7fdb71815e80c1ad6a0b4b5c50ea25a938571a`.

### Correção BLE por autoexclusão explícita

- Diagnóstico corrigido: `ble_host_task` é criado diretamente por `xTaskCreate`, sem handle no adaptador NimBLE. `nimble_port_freertos_deinit()` não é mecanismo válido para autoexclusão nesse caminho; o log da segunda tentativa mostra que a task ainda retorna após a chamada.
- Ajuste local atual: após `nimble_port_run()`, `ble_host_task` registra saída e chama `vTaskDelete(nullptr)`. `ble_mgr_stop()` mantém o `nimble_port_stop()` e não tenta deletar o handle externo. `test_ble_review_regressions.py` exige essa ordem, autoexclusão final e criação direta por `xTaskCreate`.
- Validação local deste delta: agregado `make test` PASS; build ESP-IDF 5.5.5 PASS; contratos BLE, `git diff --check` e clang-format nas linhas alteradas PASS. Imagem local **não gravada**: SHA-256 `38e3279b2c8e8c6e3a1cb1ea9d8b53df71d959616d754930fee2e878e99517a5`.
- No momento do build local, a imagem ainda não havia sido gravada; o teste físico da imagem está registrado na seção seguinte. CI remota pendente.

### Flash e validação da autoexclusão BLE — 13/out/2026

- Usuário confirmou novamente backup/dados descartáveis, alimentação segura e captura serial, autorizando ciclos controlados com no máximo uma tentativa por boot. Binário flashado: SHA-256 `38e3279b2c8e8c6e3a1cb1ea9d8b53df71d959616d754930fee2e878e99517a5`; esptool verificou os hashes.
- `sys.info` confirmou `cef3257-dirty`, ESP32-P4, IDF v5.5.5, safe_mode=false; `ping` respondeu antes. Prompt exato confirmado; um `y` foi enviado. `term.dump` retornou `poweroff: admitted; quiescing storage. Do not remove power.`
- Log bruto `/tmp/opencode/cyberdeck5-poweroff-self-delete-validation-serial.log` mostra `BLE manager stopped` e `BLE host task exiting`, sem o assert anterior `FreeRTOS Task "ble_host" should not return`; em seguida Wi-Fi reportou stop e o console emitiu `E BOD: Brownout detector was triggered`. A USB Serial-JTAG desconectou e `/dev/ttyACM0` não reapareceu na verificação subsequente.
- Resultado: progresso além do panic BLE anterior, mas **corte físico não confirmado**. Brownout e ausência USB são compatíveis com corte, mas não o provam; não há mensagem observada `factory signal sent`, nem medição independente do rail, nem validação de persistência pós-religamento. Nenhuma nova tentativa foi feita. Aguardando confirmação física do estado/rail e decisão sobre religar/validar dados antes de novo ciclo.
- Após o usuário religar o Tab5, houve um reset `CHIP_USB_UART_RESET` durante a conexão host; o boot voltou a completar (`sys.info`: cef3257-dirty, safe_mode=false, uptime 19 s; `ping` PASS). Uma nova tentativa autorizada neste boot confirmou prompt e enviou `y` exatamente uma vez. Captura `/tmp/opencode/cyberdeck5-poweroff-relight-cycle-serial.log`: `BLE manager stopped`, `BLE host task exiting` sem assert/abort, `wifi station stopped`, `E BOD: Brownout detector was triggered`, seguida de USB disconnect. A porta não reapareceu. Resultado igualmente compatível com desligamento físico, mas sem prova independente nem verificação de dados após religar; não repetir no estado atual.
- O usuário confirmou que esse ciclo desligou corretamente e que, após religar, o dispositivo e os dados pareciam corretos. Verificação pós-relight: `sys.info` (`cef3257-dirty`, IDF v5.5.5, ESP32-P4, safe_mode=false), `ping` PASS e `cat /.poweroff-sync` retornou `1`; captura `/tmp/opencode/cyberdeck5-poweroff-post-relight-check.log`. Confirma boot normal, SD/barreira presentes e o resultado físico reportado pelo usuário, mas não substitui canário de dados com hash nem a matriz completa.
- Rodada com canário: boot verificado e `ping` respondeu; cancelamentos com `Y` maiúsculo e `yes` passaram sem admissão. Canário descartável `/data/poweroff-acceptance-2259ed40.txt` (1272 bytes) foi gravado via `fs.write` e lido integralmente antes do shutdown. SHA-256 esperado: `860367d95a4c84b3790ddf5306fc5d2807cfbce37e2c1e9128dfa4b18c96cc14`. Prompt exato confirmado e `y` enviado uma vez; ponte retornou `poweroff: admitted; quiescing storage`. USB desconectou às 20:19:33. Captura do ciclo: `/tmp/opencode/cyberdeck5-poweroff-canary-cycle-serial.log`.
- O usuário confirmou que o desligamento foi correto e religou o dispositivo. Pós-relight, `sys.info` confirmou `cef3257-dirty`/IDF v5.5.5/ESP32-P4/safe_mode=false; `ping` passou e o `cat` retornou todos os 24 registros do canário, byte por byte igual ao conteúdo esperado. SHA-256 calculado do conteúdo esperado/readback: `860367d95a4c84b3790ddf5306fc5d2807cfbce37e2c1e9128dfa4b18c96cc14` (1272 bytes). Captura: `/tmp/opencode/cyberdeck5-poweroff-canary-post-relight.log`. Ciclo de corte + persistência do canário PASS.
- Cenário selecionado pelo usuário: bateria + USB conectadas. No boot `cef3257-dirty`/IDF v5.5.5, `sys.info`/`ping` passaram, o canário existente foi lido integralmente antes do comando e o prompt exato foi conferido; um único `y` foi enviado e a ponte retornou `poweroff: admitted; quiescing storage`. USB Serial-JTAG desconectou às 20:30:00; captura `/tmp/opencode/cyberdeck5-poweroff-battery-usb-cycle.log`. O usuário confirmou o desligamento físico e religou o Tab5. Pós-relight, `sys.info`/`ping` passaram e o canário foi lido com os 24 registros intactos; SHA-256 `860367d95a4c84b3790ddf5306fc5d2807cfbce37e2c1e9128dfa4b18c96cc14`, captura `/tmp/opencode/cyberdeck5-poweroff-battery-usb-post-relight.log`. Resultado: cenário bateria+USB PASS, inclusive persistência; não inferido pela desconexão USB.
- Cenário bateria-only executado pelo usuário, sem USB/fonte externa durante o corte; usuário confirmou desligamento e religamento. Pós-relight no Tab5: `sys.info` confirmou `cef3257-dirty`, IDF v5.5.5, ESP32-P4, safe_mode=false; `ping` PASS e leitura dos 24 registros do canário PASS (1272 bytes, SHA-256 `860367d95a4c84b3790ddf5306fc5d2807cfbce37e2c1e9128dfa4b18c96cc14`). Captura `/tmp/opencode/cyberdeck5-poweroff-battery-only-post-relight.log`. Bateria-only PASS.
- O usuário informa também que o cenário somente com cabo de alimentação externo já foi testado e que todas as formas suportadas foram exercitadas. Registro como confirmação do usuário; não há nesta sessão evidência serial pós-relight/hash específica desse cenário.

## Objetivo e decisões aprovadas

Adicionar o comando terminal `poweroff` para desligar fisicamente o rail do Tab5 pelo sinal de fábrica do BSP. Aprovações humanas registradas: usar o BSP atual e o sinal real de fábrica; exigir a sequência exata `poweroff` e depois `y`; persistir dados e quiescer escritores SD antes do corte; arquitetura de coordenador global de I/O; implementação em etapas por módulo. Stage 1 foi ampliado para editor/storage e recuperação de sidecars. A instrução inicial de flash manual foi substituída por autorizações explícitas para os flashes diagnósticos registrados acima; não há autorização vigente para novo flash.

## Evidência de hardware (não confundir com implementação)

- **PASS — evidência documental:** documentação oficial Tab5 identifica PI4IOE5V6408-2, expander B, endereço `0x44`, E2.P4 como `PWROFF_PLUSE`.
- **PASS — evidência de referência:** M5Tab5-UserDemo implementa `bsp_generate_poweroff_signal()` alternando alto/baixo por três ciclos de 100 ms; esquema encaminha o sinal ao controlador de energia para cortar MPWR_EN.
- **PASS — evidência do BSP atual:** há suporte `bsp_io_expander1_init()`; P4 exige `PUSH_PULL`. Expander A `0x43` P4 é `LCD_EN`, não substituto.
- Nunca substituir o corte físico por desligar display ou deep sleep. Esta evidência não prova funcionamento no firmware Cyberdeck nem no dispositivo.

## Implementado no worktree (ainda não totalmente aceito)

- **IMPLEMENTED — Stage 0:** `components/cyberdeck/include/platform/storage/cyberdeck_sd_io_coordinator.h` e `src/platform/storage/cyberdeck_sd_io_coordinator.cpp`: leases RAII move-only, gate OPEN/CLOSING/CLOSED, drenagem bounded e reabertura conforme estado.
- **IMPLEMENTED — Stage 1 parcial:** `main/app_main.cpp` mantém a instância pelo ciclo do processo e injeta a recuperação de boot, UI, shell local e runtime/editor. Recovery usa lease WRITE; editor usa READ no open e WRITE abrangendo save-as/save_current até flush/fsync/rename/rollback/fsync de diretório. Shell local protege operações síncronas de SD. Testes relacionados também foram adicionados anteriormente; não são aceitação completa.
- **IMPLEMENTED — cat READ/lifecycle:** READ lease e quiesce permanente bounded; timeout mantém fila/root/lease. A admissão de callbacks é invalidada atomicamente antes de tentar o lock de lifecycle. Worker self-delete após completion, cleanup único depois de ACK e no-reopen. Harness ESP real em `test_cat_worker_runtime.cpp`.
- **IMPLEMENTED — event log M2:** fence permanente, leases de callers/readers, drain/completion e close pelos workers, erros binários sticky. Worker textual aguarda o binário e reconsulta sua fila após completion. Runtime inclui falha de criação textual com rollback/reinit, falha após apenas o worker textual, corrida de deadlines e drain de cinco projeções aceitas. Matriz em `event_log_traceability.md`.
- **IMPLEMENTED — I/O global:** Wi-Fi config, backend de audit, Serial `fs.write`, shell/editor/recovery e known_hosts usam o coordinator único. Known_hosts tem I/O próprio bounded, mantendo export/parse/compare criptográfico da libssh; nenhuma confiança em fclose ignorado pela biblioteca. Readers HTTP/Serial do event log usam o fence próprio do logger e são parados antes dele.
- **IMPLEMENTED — NVS e fontes de trabalho:** screen NVS tem fence serializado de enqueue, sentinel FIFO e ACK após persistência; BLE para antes de Wi-Fi. A task Serial é parada antes de desmontar serviços que suas RPCs poderiam acessar. Falhas/timeout preservam recursos potencialmente vivos e bloqueiam corte.
- **IMPLEMENTED — orquestração/comando:** confirmação local exata, cancelamento por qualquer outra entrada/Escape/contexto alterado; one-shot e descarte de suffix após paste confirmado. Task `poweroff` e watchdog de 45 s não rodam teardown sob lock LVGL. Fence SD é fechado na admissão, inclusive em falha de alocação da task. UI é pausada; expiração durante preparação impede qualquer fase posterior. O início irreversível do sinal tem transição atômica própria.
- **IMPLEMENTED — SD/HAL:** fsync/close por proprietário, barreira de metadata `.poweroff-sync`, erro disk-I/O sticky e unmount VFS antes do LDO. HAL B (`0x44`, assert de build), P4, estabelece latch low em HIGH-Z antes de PUSH_PULL e emite três ciclos high/low de 100 ms. Não usa LCD_EN/deep sleep; sinal enviado não comprova corte físico.

## Matriz de progresso e evidências

| ID/escopo | Evidência registrada | Estado |
| --- | --- | --- |
| REQ-COORD — leases/gate Stage 0 | `test_sd_io_coordinator` | PASS |
| REQ-SHELL — shell local e contratos de integração | 13/out: agregado `make CXXFLAGS='-std=c++17 -Wall -Wextra -Werror -O0 --coverage' test` completo após correções de logger e barreira do teste Wi-Fi | PASS local |
| REQ-BUILD/QUALITY — firmware e qualidade | Build ESP-IDF final e guard real de linker; Quality Gate remoto no commit `b0ce47f` (run `38476339304`) | PASS local e remoto |
| REQ-SESSION — sessão de shell | Confirmação exata, respostas inválidas/Escape, contexto SSH alterado, worker indisponível, one-shot/paste suffix e admissão bloqueada; fixture OPEN/CLOSING/CLOSED | PASS local no agregado final |
| REQ-FIXTURES — paridade de fixtures | 13/out/2026: fixture da aplicação já vinculada; `test_shell_app`: 281 checks PASS, incluindo preservação do gate CLOSED após stop/start do console | PASS local |
| REQ-CAT — leitura/lifecycle real | Harness ESP retém read, prova timeout sem cleanup, reject de start/enqueue, liberação/join/drain e cleanup único; contratos e seam READ | IMPLEMENTED; testes locais PASS |
| REQ-COVERAGE — thresholds locais | 13/out final: gcovr 86.9% linhas, 95.5% funções, 59.3% branches; gates 80/94/59 PASS; guard: 61 TUs cobertas, 16 hardware allowlisted; CI run `38476339304` | PASS local e remoto |
| REQ-LOG-M2 — quiescência do logger | 15 cenários runtime, incluindo dois pontos de falha na criação de tasks e drain multi-item, com ASan/UBSan; contratos de projection/durable | IMPLEMENTED; testes locais PASS |
| REQ-GLOBAL-QUIESCE — I/O e handles | Inventário SD/NVS, leases/fences, stop/join/close e erro sticky; controller por falha/timeout/cancelamento em cada etapa, known_hosts e cat reais | IMPLEMENTED; PASS local e exercitado no ciclo de hardware |
| REQ-SD-SYNC — persistência final | FIL modificado dispara sync_fs; hooks ff_disk_write/ioctl observam inclusive FSInfo ignorado pelo upstream; guard real de linker confirma callers FatFS; BSP unmount antes de LDO | IMPLEMENTED; build/link guard e persistência física verificados |
| REQ-POWER — comando/HAL | Stack PSRAM admitida; BLE encerrou sem panic; cortes confirmados nas fontes reportadas pelo usuário; canário/hash e cancelamentos verificados | IMPLEMENTED; validação funcional PASS |
| REQ-HW-ACCEPT — validação Tab5 | Corte em todas as fontes suportadas confirmado pelo usuário; canário/hash pós-relight verificado em bateria-only e bateria+USB. Cabo-only reportado testado, sem hash independente. Fault/timeout exercitados em host, não por injeção física. | FUNCIONAL PASS; sem fault injection física |

## Trabalho restante, em ordem

- [x] **REQ-FIXTURES:** fixtures vinculam coordinator OPEN compartilhado; testes de sessão/aplicação executados em 13/out/2026. Regressões adicionais verificam rejeição CLOSING/CLOSED e stop/start sem reabertura.
- [x] **REQ-CAT-READ:** caminho dedicado com READ lease e contratos locais aprovados; não abrange teardown bounded nem fence global.
- [x] **REQ-LOG-IMPLEMENTATION:** fence/stop/drain/close do logger implementados; corrigida ordem binário → textual e adicionados cenários partial-init/race ao alvo padrão. Aceitação/revisão final segue aberta.
- [x] **REQ-QUALITY-LOCAL:** gates locais de formatação do workflow, lint e segurança passam; firmware compila após correção. Os flashes/testes estão registrados acima; ciclo físico básico confirmado pelo usuário.
- [x] **REQ-AGGREGATE-LOCAL:** agregado completo com coverage passa após correção da barreira do teste concorrente de auditoria Wi-Fi. O teste espera a resposta de enqueue antes de liberar rename, com watchdog de 5 s; nenhuma mudança em produção Wi-Fi.
- [x] **REQ-COVERAGE-LOCAL:** gcovr e guard finais passam: 86.9/95.5/59.3 contra gates 80/94/59; 61 TUs cobertas, 16 hardware allowlisted.
- [x] **REQ-CAT:** lifecycle bounded, fence permanente e cleanup somente após completion; callback fence antes de lock, READ lease e testes reais/contratos.
- [x] **REQ-GLOBAL-QUIESCE:** inventário e admissão por módulo, stop/join/close antes de SD detach, recursos retidos em timeout; controller não autoriza sinal após falha ou budget expirado.
- [x] **REQ-SD-SYNC:** estratégia suportada por fsync/FIL modificado e metadata barrier; erro sticky também na fronteira disk-I/O; BSP ordenado sem unmount como atalho de sync.
- [x] **REQ-POWER:** comando, confirmação/cancelamento/one-shot, deadline/watchdog e HAL de fábrica implementados. Testes não emitem sinal real.
- [x] **REQ-TEST-IMPLEMENTATION:** testes de comando, erros por etapa, prazo/cancelamento, admissão concorrente, metadata/pulso, leases/close reais de known_hosts, worker ESP cat e logger multi-item/creation failure.
- [x] **REQ-REVIEW-LOCAL-FINAL:** revisão de ownership/fences, criação de tasks, fechamento opaco da libssh, FSInfo mascarado, ordem de RPCs/parada e deadline absoluto antes do primeiro high; agregado limpo/gcovr/guard/qualidade/build/link guard finais PASS. Nenhum threshold ou módulo de cobertura foi excluído/reduzido.
- [x] **REQ-CI-REMOTE:** Quality Gate no commit `b0ce47fe5bad592744a662aafa6b5e05073b2e41`, run `38476339304` — PASS.
- [x] **REQ-HW-POWER-MATRIX:** usuário confirma desligamento nas condições bateria-only, bateria+USB e cabo externo; pós-relight/hash verificados em bateria-only e bateria+USB. Cenário de cabo externo anotado como user-reported, sem hash serial independente.
- [x] **REQ-HW-ACCEPT-FUNCTIONAL:** corte físico, persistência do canário/hash, cancelamentos (`Y`/`yes`), `sys.info`/`ping` e `.poweroff-sync` PASS. Fault/timeout continuam cobertos por injeções host; não se recomenda induzir falhas destrutivas no Tab5.
- [x] **REQ-POWER-HEAP-FIX:** stack de 8192 B alocada via PSRAM; build, agregado host e tentativa autorizada confirmam que a admissão progride.
- [x] **REQ-POWER-BLE-STOP-LOCAL:** `ble_host_task` criada por `xTaskCreate` autoexclui com `vTaskDelete(nullptr)`; regressão estrutural e gates locais passam.
- [x] **REQ-POWER-BLE-STOP-HW:** log real mostra `BLE manager stopped`/`BLE host task exiting` sem panic/assert; execução avançou até Wi-Fi stop/BOD, seguida de ciclo físico básico confirmado pelo usuário.

### Rechecagem host após validação do canário

- `make -C tests/host/keymap test` PASS no snapshot atual.
- Alvos poweroff focados executados diretamente: `./test_poweroff`, `python3 test_poweroff_contract.py` e `python3 test_poweroff_link_guard.py` — todos PASS (ordem/erros/deadlines/one-shot/barreira/pulso; contrato de admissão/stack/fences; hooks de linker).
- Esta cobertura é simulada/estrutural e não substitui injeção de falha/timeout no Tab5. Nenhum comando de desligamento, flash ou mudança de alimentação foi executado nesta rechecagem.

## Restrições de aceitação

### Evidências locais da rodada de 13/out/2026

- Agregado com coverage: `/tmp/opencode/cyberdeck5-poweroff-host-coverage-test.log`.
- gcovr: `/tmp/opencode/cyberdeck5-poweroff-coverage.log` e JSON `/tmp/opencode/cyberdeck5-poweroff-coverage.json`.
- Build: `/tmp/opencode/cyberdeck5-poweroff-build.log`.
- Binário da primeira rodada de implementação (histórico, não o diagnóstico posterior): `build/cyberdeck5.bin`; SHA-256 **eea07174c649a6d8721caa3a5f9de8619d2302350aabe301c0e176473c6de606**. Hash do flash diagnóstico posterior consta acima.
- Guard de linker real: `python3 tools/poweroff_link_guard.py --map build/cyberdeck5.map` PASS; ambos os hooks têm caller `esp-idf/fatfs/libfatfs.a(ff.c.obj)` no cross-reference table.
- Execução final limpa do snapshot revisado: agregado PASS, 86.9/95.5/59.3 de cobertura, scope guard PASS. O harness cat separa objetos de filesystem/VFS/coordinator com gcov convencional; worker, teste e adapters de task mantêm ASan/UBSan. A primeira variante instrumentava helpers de filesystem novamente com UBSan e inflava branches do compilador (54.3%); foi corrigida a construção do harness, sem remover fontes ou baixar o gate.
- Primeira tentativa limpa com coverage falhou no teste concorrente Wi-Fi; execução isolada passou. A barreira incorreta foi corrigida e o agregado completo rerodado com sucesso, sem mascarar a falha ou reduzir asserts. Estes artefatos são locais/temporários, não evidência física.

### Estratégia SD resolvida em software — 13/out/2026

- `vfs_fat_fsync` no ESP-IDF aplica `f_sync` a um único `FIL`; não é sync global.
- `esp_vfs_fat_sdcard_unmount` chama o core que faz `f_mount(0, ...)`, remove diskio/host e libera o card; não é prova de persistência nem validação de ausência de handles.
- O BSP vendored foi corrigido: unmount VFS antes de eliminar LDO, propagando erro sem pulso.
- Proprietários terminam writes, verificam flush/fsync/close e reportam completion antes de detach. A barreira escreve um byte em `.poweroff-sync`: em IDF 5.5.5/FatFS R0.15, FA_MODIFIED em f_sync chama sync_fs, que sincroniza window/FAT/FSInfo e CTRL_SYNC. Não usar fsync de diretorio como garantia FatFS.
- O upstream ignora o resultado de disk_write de FSInfo. Interposição GNU dos símbolos públicos **ff_disk_write/ff_disk_ioctl** delega ao original e registra erro sticky; o estado é rechecado depois da barreira e do unmount. O guard de map confirma que ff.c.obj realmente chama os hooks, evitando um wrapper presente mas inativo. Não foram alterados o IDF instalado nem componentes gerenciados.
- Hardware/cartão/alimentação ainda requerem validação; a estratégia de software não prova durabilidade física nem corte real.

### Próximo checkpoint humano

O binário `cef3257-dirty` (SHA-256 registrado acima) passou nos cenários de
alimentação que o usuário informa serem suportados. Canário/hash está verificado
após ciclos bateria-only e bateria+USB. Não há necessidade de novo flash nem de
outra tentativa física para o aceite funcional. Qualquer fault injection real
exigiria procedimento seguro, dados descartáveis e checkpoint específico; preferir
a cobertura host já existente. Consultar `tests/manual/poweroff-validation.pt-BR.md`
antes de qualquer nova rodada.

O comando só pode ser considerado pronto após todos os itens bloqueadores resolvidos, revisão e gates aceitos, e validação física documentada. Timeout, I/O pendente, handle aberto ou sync não demonstrado deve falhar fechado: não emitir pulso. Este documento e os resultados host não são evidência de corte físico.
