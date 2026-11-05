# Requisitos operacionais do Tab5 — baseline em elaboração

Este documento reúne recortes da **Fase 1** de
[`OS-IMPLEMENTATION-PLAN.pt-BR.md`](OS-IMPLEMENTATION-PLAN.pt-BR.md). Ele registra
comportamento observado no código, não certifica a imagem instalada no Tab5 nem
introduz requisitos para mudar o firmware. Os critérios físicos continuam
pendentes até confirmação da identidade da imagem e execução segura no aparelho.
O inventário é uma lista de funcionalidades a preservar, não uma promessa de
que todas as apps estejam ativas simultaneamente.

## Inventário e criticidade atual

Fonte: `main/app_main.cpp`, `cyberdeck_system_apps_start()` e
`cyberdeck_system_apps_start_safe_mode()` em
`components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp`, manifestos
dessas apps e de `cyberdeck_shell_app.cpp`. A classificação abaixo descreve o
**gate de boot atual**: não é uma proposta de mudar o que é essencial.

| ID/app ou etapa | Classificação atual | Dependências e efeito da falha no checkpoint | Evidência |
| --- | --- | --- | --- |
| PLAT-BOOT: NVS/recovery, ESP-Hosted e SD mount | Plataforma pré-checkpoint; NVS/recovery tem fallback reportado, Hosted pode falhar e tentar mount comum, SD mount é obrigatório | NVS indisponível seleciona recovery fail-closed; Hosted indisponível degrada rede/C6; mount SD falho interrompe `app_main` antes da UI | `app_main.cpp`, `test_boot_sequence`, `test_recovery_runtime` |
| PLAT-UI: display/LVGL, IMU/UI e screen-off | Plataforma pré-checkpoint | Display/lock/IMU/UI/screen-off falhos impedem a sequência de alcançar os apps e o checkpoint | `app_main.cpp`, `test_display_lock_contract`, contratos de screen-off/orientação |
| PLAT-INPUT: teclado e brilho | Plataforma pré-checkpoint | Falha de init do teclado ou brightness via `ESP_ERROR_CHECK` interrompe a sequência; sem input não se considera o console operacional | `app_main.cpp`, `test_boot_sequence`, `test_wake_key_contract`, contratos keyboard |
| PLAT-BATTERY: reader/proteção | Plataforma observável, **não** gate de boot | `battery_protection_start()` é não fatal; o indicador/endpoint pode ficar indisponível sem encerrar boot | `app_main.cpp`, testes battery protection/view/HTTP |
| `cyberdeck.event_log` | Serviço de infraestrutura/dependência; necessário indiretamente ao shell e serial | Inicialização de logging é tentada antes da UI sem abort imediato; se o serviço falhar, dependentes essenciais podem não iniciar e checkpoint fica pendente | Manifestos em `cyberdeck_system_apps.cpp`, `test_system_apps_contract`, `test_app_runtime` |
| `cyberdeck.shell` | App foreground dona do console; essencial ao checkpoint | Depende de event log; não deve ser parada pelo próprio console ou por parada transitiva de dependência | `test_shell_app`, `test_app_runtime`, `test_system_apps_contract` |
| `cyberdeck.serial` | Serviço essencial ao checkpoint e à superfície de recuperação | Depende de event log e shell; falha mantém boot pendente mesmo que a TUI esteja visível | `test_system_apps_contract`, testes serial/recovery |
| `cyberdeck.wifi` | Serviço opcional no checkpoint; tentativa no boot normal | Depende de event log; falha degrada rede e dependentes, mas não bloqueia checkpoint se shell + serial estiverem `running` | `test_system_apps_contract`, testes Wi-Fi/lifecycle |
| `cyberdeck.ssh` | Background opcional | Depende de event log + Wi-Fi; falha não bloqueia checkpoint, sessão exige rede autorizada e peer conhecido | `test_system_apps_contract`, testes SSH |
| `cyberdeck.screenshot` | Serviço opcional | Depende de event log + Wi-Fi e display; sem IP, endpoint não está disponível; não bloqueia checkpoint | `test_screenshot_lifecycle_contract`, `test_system_apps_contract` |
| `cyberdeck.bluetooth` | Background opcional; C6/Hosted compartilhado | Depende de event log; falha BLE fail-closed não bloqueia console/checkpoint; scan vazio não é falha do boot | `test_ble_integration_contract`, `test_ble_background_contract` |
| `cyberdeck.time_sync` | Background opcional, agendado após boot pronto | Depende de event log + Wi-Fi; falha de rede/servidor mantém hora atual e não bloqueia checkpoint | `test_time_sync_contract`, `test_system_apps_contract` |
| `cyberdeck.demo` | App `demo`, tentativa no boot normal, não essencial | Falha da app não bloqueia checkpoint; não é iniciada em safe mode | `test_demo_app`, `test_app_runtime` |
| `cyberdeck.editor`, `cyberdeck.netdiag`, `cyberdeck.music` | Apps foreground opcionais; não auto-iniciadas por `start_all()` | A falha é reportada no despacho/abertura; shell/serial devem manter o console. Cada app tem dependências e recovery específicos no Recorte 2 | Testes editor, netdiag, music, runtime/shell |
| VFS local, SD data, screen-off, poweroff e persistência | Fluxos transversais, não são todos entradas em `k_apps` | SD mount participa do boot crítico; falha de acesso a arquivo é local ao comando; screen-off preserva a UI; poweroff requer confirmação e encerra o dispositivo, não é recovery normal | Recortes 1 e 3, contratos storage/screen/poweroff |

No safe mode, `cyberdeck_system_apps_start_safe_mode()` tenta somente event log,
shell e serial. Falha de qualquer um marca a superfície incompleta. NVS/recovery
são inicializados antes do SD; o transporte ESP-Hosted é preparado **apenas no
boot normal** antes do mount porque C6 e SD compartilham o controlador SDMMC.
O SD é montado também no safe mode. Não prometer boot sem SD: a metadata opcional
de pacote no SD é diferente da montagem física obrigatória.

**Decisão do operador (05/11/2026, posterior ao ensaio físico):** o cartão SD
é o armazenamento obrigatório do sistema. Sem ele, não iniciar é o resultado
esperado; os reinícios automáticos após a falha de mount são consequência
aceita dessa dependência no produto pessoal, não defeito ou bloqueio para apps
simples. No firmware atual, `ESP_ERROR_CHECK` aborta e o boot seguinte volta a
tentar montar o SD; não há uma busca contínua dentro de um mesmo boot. Uma
melhoria **opcional futura**, sem compromisso de implementação nesta etapa,
seria detectar a ausência e apresentar uma tela informando que o sistema não
pode ser iniciado sem cartão de memória. Como o mount ocorre antes da UI, isso
exigiria um caminho de display/erro anterior à UI normal, sem prometer boot
degradado ou tornar o SD opcional. Não repetir ensaio físico sem controle de
alimentação, pois a tentativa anterior acumulou 65 boots interrompidos.

## Recorte 1: pré-condições, resultado, falha e recuperação

| ID | Pré-condição | Resultado observável e limite atual | Falha/ausência e recuperação existente | Dono | Prova host / gate Tab5 |
| --- | --- | --- | --- | --- | --- |
| OS-TAB5-BOOT-01 | Imagem válida, BSP e SD físico montável; NVS acessível ou falha reportada | `app_main` decide safe mode, prepara Hosted no modo normal, monta SD, valida handle, recupera sidecars e só então inicializa UI; `HOSTED_DIAG` registra resultado/tempo de mount. O mount não tem deadline universal especificado neste contrato | Erro de mount causa `ESP_ERROR_CHECK` antes da UI; não há fallback sem SD. Falha NVS é reportada, sem erase automático; recovery seleciona safe mode na falha de leitura/abertura. Restaurar SD/NVS exige diagnóstico, não uma tentativa destrutiva automática | ESP-IDF/BSP: boot, SDMMC/FAT; cyberdeck5: ordem e política de recovery | `test_boot_sequence`, `test_save_sidecar_recovery`, `test_recovery_runtime`; em Tab5, `sys.info` e logs de boot de uma imagem identificada. Ausência de SD **não** é teste físico autorizado por este documento |
| OS-TAB5-BOOT-02 | UI, teclado e serviços essenciais disponíveis após mount | Checkpoint de recovery somente após `cyberdeck.shell` e `cyberdeck.serial` em `running` no modo normal; no safe mode, event log + shell + serial tentados. Falhas opcionais são registradas, sem exigir que cada app esteja ativa | Se a superfície essencial falhar, checkpoint é adiado e o erro permanece para diagnóstico; após boots interrompidos, o latch de safe mode persiste. Não usar `sys.safe_mode.clear` como teste de rotina: o histórico de persistência/reversão ainda é inconclusivo | FreeRTOS/ESP-IDF: tasks/NVS; cyberdeck5: gate e latch | `test_recovery_policy`, `test_recovery_runtime`, `test_phase10_contract`, `test_system_apps_contract`; em Tab5 apenas inspeção passiva de `sys.info`/logs e uptime, sem forçar resets ou clear |
| OS-TAB5-CONSOLE-01 | UI inicializada, shell com host anexado e dispatch de comandos liberado | Prompt local na raiz virtual `/` (SD físico `/sdcard`), `help` e `pwd` respondem; console permanece vivo na troca de foreground para outra app | Sem host, `shell.start()` falha fechado; durante startup, `runtime::execute_line()` recusa comandos de apps; retorno seguro é aguardar checkpoint e tentar comando normal, não forçar start durante o boot. Não há timeout universal para comandos locais | cyberdeck5: shell, runtime, UI; FreeRTOS/LVGL: execução e renderização | `test_local_shell`, `test_shell_app`, `test_app_runtime`, `test_boot_sequence`; em Tab5, `ui.type "pwd"` + `term.dump`, depois app foreground somente quando seguro e autorizado |
| OS-TAB5-SERIAL-01 | Shell/serial inicializados, USB Serial-JTAG conectado e tela ativa para comandos de UI | `ping`/`sys.info`/`term.dump` usam NDJSON correlacionado por `rid`; `term.dump` verifica resultado textual. A ponte mantém fila/frames bounded segundo os testes próprios; não inferir um timeout único para todas as operações | Falha de serial impede o checkpoint; tela apagada rejeita UI/`term.dump` com `screen_off`, mantendo `ping`/`sys.info`; `screen.wake` é o caminho explícito antes de repetir interação com UI. Falha do canal USB deve ser diagnosticada por logs/CLI, não tratada como sucesso por `ui.type ok` | ESP-IDF: USB Serial-JTAG; cyberdeck5: bridge, gates e CLI | `test_serial_ndjson_dispatch`, `test_serial_term_dump`, `test_serial_lifecycle_contract`, `test_serial_screen_guard`; roteiro `tests/manual/serial-bridge-validation.pt-BR.md` P10, T1, SC5/SC6 e W1; confirmar resposta e uptime no Tab5 |

## Recorte 2: conectividade e aplicações compiladas

O runtime registra os IDs listados no inventário acima. `start_all()` percorre
serviços/background e a app `demo`, pula apps foreground e pode iniciar a
shell **como dependência do serviço serial**. `test_app_runtime.cpp` verifica
esse comportamento com hooks reais do runtime, sem copiar a implementação do
supervisor para um teste estrutural. A tabela abaixo fixa observáveis e riscos
para os próximos critérios físicos; **não** marca cenários de rede, áudio ou
persistência como aceitos no firmware instalado.

| ID / superfície | Pré-condição e resultado a preservar | Falha/ausência e caminho seguro | Dono e evidência existente | Gate físico pendente |
| --- | --- | --- | --- | --- |
| OS-TAB5-NET-01: Wi-Fi | Boot normal com Hosted e SD; associação atual expõe enabled/connected/has_ip; credencial salva é reutilizada e só persiste após conexão/IP | Sem AP/IP, status indica desconectado e rede pode ser escolhida/tentada novamente; worker/callback stale não deve alterar uma geração posterior; falha opcional não bloqueia checkpoint | `wifi_mgr.cpp`, `wifi_storage.cpp`, state/persistence coordinators; testes Wi-Fi, `test_system_apps_contract` | `wifi.status`, sem iniciar alteração de configuração; verificar status/IP e uptime; conexão/forget só com ação explícita |
| OS-TAB5-NET-02: SSH | Requer Wi-Fi/IP, endereço autorizado, credenciais do operador e verificação de host key conforme política atual | Falha de DNS/conexão/auth encerra apenas sessão SSH; TUI volta ao shell; desconexão Wi-Fi deve liberar sessão/worker sem reiniciar aparelho | `ssh_client.cpp`, parser/session/UI; testes SSH, `test_terminal_filter`, `test_ssh_line_composer` | Só conectar a host próprio/autorizado; comandos de leitura, capturar saída textual e uptime; nenhuma credencial deve aparecer em log/relatório |
| OS-TAB5-NET-03: screenshot HTTP | Requer servidor iniciado e endereço local permitido; resposta é BMP 24-bit bottom-up, não captura arbitrária | Sem IP/peer fora da allowlist/request concorrente, retornar indisponível ou erro tipado sem travar display; reiniciar serviço via estado Wi-Fi | `screenshot_server.cpp`, `screenshot_bmp.cpp`; `test_screenshot_lifecycle_contract`, `test_screenshot_bmp`, manual screenshot | GET read-only em LAN própria; comparar BMP e `sys.info`; sem carga excessiva |
| OS-TAB5-NET-04: sincronização UTC | Requer Wi-Fi conectado com IP; worker tenta endpoints declarados e valida resposta antes de alterar relógio | Sem rede/HTTP/JSON válido, preservar relógio e registrar falha; cancelamento/stop deve quiescer o worker; app não bloqueia boot | `cyberdeck_time_sync.cpp`, `test_time_sync`, `test_time_sync_contract`, `time_sync_traceability.md` | Apenas rede autorizada; conferir log/relógio e uptime, não ajustar relógio manualmente para simular êxito |
| OS-TAB5-BLE-01: Bluetooth | No boot normal a app background prepara BLE via C6; se Wi-Fi está habilitado, aguarda prontidão até o limite da constante do system-app, depois tenta BLE mesmo sem IP | Falha parcial NimBLE é fail-closed; não repetir init para mascarar erro. Sem peer não é erro de boot; safe mode não inicia BLE. Pareamento só explícito | `cyberdeck_system_apps.cpp`, `ble_mgr.cpp`, state machine/store; contratos BLE scan/auth/HID/bonds | Com periférico conhecido e autorizado, observação de estado/scan; não criar bond nem apagar pareamentos por conveniência |
| OS-TAB5-APP-01: editor | Shell/SD e app foreground disponíveis; `edit <path>` usa storage SDK confinado e grants do runtime | Erro de leitura/save mantém documento/dirty e diagnóstico; recovery de sidecars não substitui arquivos ambíguos; FAT não promete rename POSIX | editor, runtime, storage facade; `test_editor_runtime_contract`, `test_storage_facade`, `test_save_sidecar_recovery` | Arquivo descartável novo em `/data`; conferir leitura, edição, save e retorno ao shell por `term.dump`; comparar conteúdo/hash |
| OS-TAB5-APP-02: netdiag | Wi-Fi e event log são dependências; `status`, DNS/TCP/HTTPS atuam somente após operação/destino explícitos | Sem rede, grant ou slot disponível, rejeitar/mostrar erro; cancelamento é cooperativo e resultado stale não pode sobrepor job atual | `cyberdeck_netdiag_app.cpp`, `docs/NETDIAG.md`; controller/adapters/app host | Observar `status`/`result`; DNS/TCP/HTTPS somente a destino autorizado e com rede estável |
| OS-TAB5-APP-03: player MP3 | Shell/SD/codec prontos; `music` ocupa foreground; saída deve devolver console sem parar serial | Arquivo ausente/inválido ou falha de codec encerra/recusa playback; uma ação de tecla/Enter pode controlar playback e não é observação passiva | `cyberdeck_music_app.cpp`, `test_music_app`, `test_music_player`, plano de áudio | Com faixa de teste autorizada, play/pause/stop/Esc, logs/uptime e observação audível; aceite integral pendente |
| OS-TAB5-APP-04: demo | `runtime::start_all()` tenta iniciar app compilada `demo` no modo normal; comando é explícito | Falha de demo não altera disponibilidade do shell; safe mode não inicia demo | `cyberdeck_demo_app.cpp`, `test_demo_app`, `test_app_runtime` | Em modo normal identificado, `demo` + `term.dump`, seguido de uptime; não inferir execução pelo manifesto |

As operações de rede acima podem ter limites distintos em seus adapters; os
valores não são duplicados neste documento. O resultado de `start_all()` é
diagnóstico para serviços opcionais; o critério de checkpoint de boot permanece
shell + serial. Bugs conhecidos ou aceite incompleto no hardware são mantidos
como pendência, não transformados em promessa por esta matriz.

## Recorte 3: periféricos, armazenamento e desligamento

As fontes de domínio continuam sendo os headers e adapters indicados abaixo.
Esta matriz **não substitui** os contratos de bateria, poweroff, editor/SD e
display existentes. Os limites, estados e schemas são derivados do código; não
duplicar valores de tensão, capacidade ou versão aqui. “Pendente” em Tab5 se
refere ao aceite deste plano integrado, mesmo quando existirem ensaios físicos
históricos de uma função isolada.

| ID / superfície | Pré-condição e resultado a preservar | Falha/ausência e recuperação segura | Dono e teste host | Gate Tab5 deste plano |
| --- | --- | --- | --- | --- |
| OS-TAB5-HW-01: display, IMU, teclado e entrada | BSP inicializa display/touch; `app_main.cpp` inicia IMU e UI sob lock, depois teclado e brilho. Orientação e renderização não podem concorrer com LVGL; primeira tecla física com tela apagada acorda, não executa ação invisível | Falha de display/lock/IMU/UI/teclado impede checkpoint; diagnóstico em logs. Não contornar lock nem inferir touchscreen/rotação funcional pelo host | BSP/ESP-IDF: DSI, I2C, IMU e tasks; cyberdeck5: UI, guard de entrada. `test_boot_sequence`, `test_display_lock_contract`, `test_wake_key_contract`, testes de orientação/teclado | `sys.info`/uptime, `ui.dump` para geometria e `screen.dump` para orientação; testar toque e teclado físico apenas com tela visível, sem usar `ui.type` como prova de tecla física |
| OS-TAB5-HW-02: bateria e proteção | INA226/expander disponíveis; task amostra, política publica snapshot para UI/shell e `GET /battery` lê somente cache. Opção de proteção persiste em NVS, sem tratar percentual como tensão instantânea | `battery_protection_start()` falho é registrado sem abortar boot. Leitura INA inválida torna medição indisponível para UI, preservando decisão segura anterior do carregador. Não alternar a proteção nem forçar carga/corte para inspecionar estado | ESP-IDF/BSP: I2C/expander; cyberdeck5: `battery_protection.cpp`, `cyberdeck_battery_protection.cpp` e view. `test_battery_protection`, `test_battery_protection_contract`, `test_battery_view`, `test_battery_http_contract` | Observação passiva por `GET /battery` quando HTTP estiver disponível, sem cabo Serial-JTAG se possível; série de 1 s atravessando o transiente, hash da imagem e falhas de leitura registrados. Roteiros `tests/manual/battery-http-validation.pt-BR.md` e `battery-soc-validation.pt-BR.md`; sem alterar limites por esta matriz |
| OS-TAB5-HW-03: proteção de tela | LVGL/NVS disponíveis; política mantém timeout configurável/restaurável, oculta UI, acorda com interação explícita. Bridge recusa operações de UI enquanto apagada e mantém diagnóstico sem tocar a tela | Falha de NVS usa default de política sem apagar NVS; em `display busy`, não inferir estado e repetir consulta. `screen.wake` não equivale a `screen on` persistido; primeiro wake físico não deve acionar comando | cyberdeck5: `cyberdeck_screen_protection.cpp`, `screen_off.cpp`, bridge e UI; FreeRTOS/NVS: persistência fora da task LVGL. `test_screen_protection`, `test_screen_protection_contract`, `test_serial_screen_guard`, `test_wake_key_contract` | `screen.status`, depois cenário SC1–SC9 do roteiro Serial-JTAG; não automatizar toques às cegas nem confundir wake com execução de comando |
| OS-TAB5-STORAGE-01: SD, namespace e persistência | SD montado antes da UI; shell opera raiz virtual `/` sobre `/sdcard`; editor usa fachada de leitura bounded e save em `/data`; recuperação de sidecars ocorre após mount sob lease de escrita | Falha de mount é fatal antes da UI, sem boot degradado. Erro de save preserva dado/dirty e sidecars até recuperação; FS do ESP/FAT não garante as mesmas operações atômicas do host. Não retirar SD nem corromper dados reais para validar falha | BSP/ESP-IDF: FAT/VFS/SDMMC; cyberdeck5: `cyberdeck_local_shell.cpp`, `cyberdeck_app_storage.cpp`, `cyberdeck_sd_io_coordinator.cpp`. `test_boot_sequence`, `test_local_shell`, `test_storage_facade`, `test_save_sidecar_recovery`, `test_sd_io_integration_contract` | Com SD e arquivo descartável, conferir `pwd`, leitura e round-trip do editor; registrar hash antes/depois e erros. Falhas de I/O injetadas no host, não em SD com dados do usuário |
| OS-TAB5-POWER-01: desligamento | Shell local ativo, fontes/SD conhecidos e confirmação exata; `poweroff` solicita operação one-shot assíncrona. Controller drena serviços e I/O, sincroniza metadata, desmonta SD e só então emite pulso físico | Cancelamento não corta; erro/timeout retém recursos e bloqueia pulso/retry automático. Perda de USB ou tela preta não é prova de desligamento; após falha exige diagnóstico e procedimento de religamento. Não invocar sem autorização e backup | cyberdeck5: `cyberdeck_poweroff.cpp`, shell e portas de serviço; BSP: rail SD/expander. `test_poweroff`, `test_poweroff_contract`, `test_poweroff_link_guard`; detalhes em `poweroff-implementation-plan.md` | Validação integrada não será executada nesta etapa documental. `tests/manual/poweroff-validation.pt-BR.md` registra ensaios históricos por fonte; esta matriz ainda requer evidências do firmware/commit, fonte e canário/hash pós-relight quando seguro |

Os recursos fora de `k_apps` não são todos degradáveis: SD, display e UI estão
no caminho fatal pré-checkpoint; a falha ao iniciar bateria é não fatal, mas a
falha de inicialização do teclado também interrompe o boot. O gate de shell +
serial se aplica **depois** dessas etapas, não as substitui.
Testes host cobrem políticas e várias integrações, mas não provam latch físico,
integridade sob retirada de energia, duração de boot, toque, áudio ou margens de
stack no Tab5. Esses resultados exigem os gates da Fase 3.

## Timeout, capacidade e política de espera

O campo `manifest.lifecycle_timeout_ms` é um orçamento **diagnóstico medido
depois** de hooks síncronos retornarem; não preempta start/stop nem é deadline de
operação. A implementação efetiva pertence ao adapter/worker que possui cada
recurso. A tabela aponta a fonte normativa e separa limite real, espera
cooperativa e ausência de deadline global:

| Fluxo | Fonte do limite/espera em produção | Semântica operacional e teste |
| --- | --- | --- |
| Hosted, mount SD e boot | `wifi_mgr.cpp`, BSP SD e marcadores `HOSTED_DIAG` em `app_main.cpp`; sem deadline global do mount declarada pelo produto | Registrar duração/resultado; mount fatal, Hosted pode degradar/fallback. Não criar timeout documental nem injetar falha no aparelho; `test_boot_sequence` cobre ordem/política |
| Display e UI | Argumento do `bsp_display_lock` nas chamadas de `app_main.cpp`/UI; `cyberdeck_lvgl_dispatch` define seu orçamento e grace period no header | Lock expirado rejeita operação; nunca executar UI concorrente. `test_display_lock_contract`, `test_lvgl_dispatch_state` |
| Apps/services | `k_*_lifecycle_timeout_ms` e budgets de `make_manifest` em `cyberdeck_system_apps.cpp`; limites específicos nos `stop_*`/workers | Budget do runtime é diagnóstico pós-retorno; Wi-Fi/SSH/Serial/Screenshot/BLE/time-sync têm waits/joins próprios. `test_app_runtime`, contratos de lifecycle por serviço |
| Serial-JTAG | `cyberdeck_serial_bridge.cpp`, `tools/cyberdeck_cli.py` e limites do roteiro manual | Limite por frame, fila e comando; não existe um único timeout firmware para toda requisição. A CLI deve tratar timeout como falha, não como execução bem-sucedida; `test_serial_ndjson_dispatch`, `test_serial_cli_tolerance`, `test_serial_term_dump` |
| Shell local e UI foreground | Parser/help/local shell e timeout do `bsp_display_lock` nas chamadas da UI | Comandos locais síncronos não têm deadline global de resposta; caminhos, tamanhos e fila de entrada têm limites próprios. Falha de app foreground deve devolver/retomar o console, não remover shell/serial. `test_local_shell`, `test_shell_session`, `test_app_runtime`, testes por app |
| Teclado físico/virtual e touch | Task `tab5_keyboard`, despacho assíncrono e fila bounded documentada no contrato de input | Não há timeout de “tecla aceita→ação física” comum a todos os donos; ao acordar de screen-off, a primeira tecla apenas desperta a tela. `test_keyboard_dispatch`, `test_keyboard_input_contract`, `test_wake_key_contract` |
| Wi-Fi e conectividade | Constantes dos workers/controller Wi-Fi; wait Hosted/BLE no system-app; timeouts de operação netdiag em `cyberdeck_netdiag_controller.h`/adapters | Scan/connect/DNS/TCP/HTTPS têm limites separados; cancelamento é cooperativo onde callbacks/tcpip precisam progredir. `test_wifi_*`, `test_netdiag_*`, `test_ble_*` |
| BLE | `cyberdeck_ble_state_machine.h`, `cyberdeck_ble_background.h`, `ble_mgr.cpp` | Deadlines de scan/pair/auth/connect e janelas/retries background são separados; callbacks stale não estendem nem concluem a operação atual. Testes de state machine, dispatch e background |
| Time sync | `cyberdeck_time_sync.cpp` e `test_time_sync_contract.py` | Timeout HTTP independente por endpoint; lifecycle stop deve cobrir ambas tentativas sem bloquear boot. O teste de stack/timeout protege valores a partir das constantes de produção |
| Proteção de tela | `cyberdeck_screen_protection.h`, `screen_off.cpp` | Tick do timer, janela de wake por duplo toque, fila bounded NVS e quiesce têm papéis diferentes; persistência não roda na task LVGL. `test_screen_protection`, `test_screen_protection_contract` |
| Bateria | `ina226_reader.cpp`, `battery_protection.cpp` e cabeçalhos de domínio | Poll periódico e votos consecutivos são de política; falha do sensor não deve virar leitura zero nem autorização de carga. `test_battery_protection`, `test_battery_soc`, `test_battery_http_contract` |
| SD/storage SDK | `cyberdeck_sd_io_coordinator.h/.cpp`, `cyberdeck_app_storage.h`, shell e transações dos writers | `acquire` concede ou recusa lease sob gate; `drain` recebe deadline do chamador. Leitura/escrita bounded não implica deadline universal de I/O. `test_sd_io_integration_contract`, `test_storage_facade`, `test_save_sidecar_recovery` |
| Player/codec | Controller, task e backend de áudio do player | Operações dependem do avanço do codec/I/O; timeout de teardown não pode fingir que playback parou. Validar com teste do player e gate de áudio dedicado |
| Poweroff | `cyberdeck_poweroff.cpp` / header | Controller one-shot tem deadline global e deadlines de etapa derivados do tempo restante; timeout/falha mantém recursos e impede pulso/retry. `test_poweroff` e `test_poweroff_contract`; não testar fisicamente como rotina |

Não foram encontrados contratos universais de timeout para mount SD,
comandos locais síncronos, I/O FatFs ou reprodução completa. São lacunas de
medição/limite da fase apropriada, não valores a inventar durante a Fase 1.

Recuperação sem periférico: Hosted indisponível tenta o mount SD comum, mas
mantém conectividade degradada; Wi-Fi sem AP/IP não habilita SSH/HTTP/sync e
pode ser tentado novamente pelo fluxo explícito de rede; BLE sem peer deixa o
console disponível e permite novo scan, sem pairing automático; INA indisponível
oculta telemetria e preserva decisão segura; screen-off retorna por wake
explícito; app foreground ausente/falha deve retornar ao shell quando o
lifecycle permitir. Sem SD não há boot degradado: corrigir/montar o cartão e
reiniciar após diagnóstico. Essas regras não autorizam remover periféricos,
limpar NVS ou cortar energia como injeção de falha.

## Formatos persistidos e observáveis

O formato é documentado na fonte que o lê/escreve; campos de segredo não são
copiados para capturas de evidência. Mudança de formato exige teste de leitura
legada/migração compatível ou decisão explícita, não edição incidental do
baseline.

| Dados/interface | Formato e versão atuais | Fonte/teste de compatibilidade | Falha e observação segura |
| --- | --- | --- | --- |
| Versão do firmware | `version.txt`, três segmentos decimais; única fonte de `PROJECT_VER` e `esp_app_desc_t::version` | `version.txt`, integração ESP-IDF e contratos de versão | Registar `sys.info.fw_version`; não inferir commit/hash pelo número da versão |
| Manifestos das apps | Metadados compilados: `id`, `version`, `api_version`, tipo, dependências, recursos, capabilities, stacks/queues e comandos; não é pacote executável do SD | `cyberdeck_app_runtime.h`, manifests de apps, `test_app_runtime`, `test_system_apps_contract` | Pacote SD é metadata/data opcional; código não é instalado/executado do cartão |
| Recovery/checkpoint | NVS namespace `cyberdeck_rec`; blob de estado em `state`, journal diagnóstico best-effort em `diag` | `cyberdeck_recovery.cpp`, `test_recovery_runtime`, `test_phase10_contract` | Blob inválido/indisponível não autoriza erase; selecionar recovery seguro e registrar diagnóstico |
| Opção de proteção de tela | NVS `screen_pro`: dois `u16` (`eff_min`, `last_min`) | `screen_off.cpp`, política pura e contrato screen protection | Ausente/inválido usa política default; não limpar NVS como teste |
| Proteção de bateria | NVS `battery_prot/prot_enabled`; estado ativo do latch é estado de runtime, não persistido | `battery_protection.cpp`, `test_battery_protection_contract` | NVS/INA inválido reporta indisponibilidade/falha, sem reconstruir estado elétrico a partir de zeros |
| Wi-Fi | NVS `radios/wifi_en`; redes em `/sdcard/.cyberdeck5/wifi.cfg`, texto por seção `[SSID]` e chave `password` (compatível com `pwd` legado), até o limite definido no header | `wifi_mgr.cpp`, `wifi_storage.h/.cpp`, contratos Wi-Fi/persistência | Credencial está em texto claro no SD; não imprimir senha nem incluí-la em evidência. Erro de leitura não equivale a rede vazia gravável sem confirmação |
| Bonds BLE | NVS namespace `ble_bonds`, blob `bonds_v2`; leitura compatível com chave legacy `bonds_v1`; registros incluem identidade tipada/nome/kind/last, não chaves de pareamento no store lógico | `cyberdeck_ble_store.h/.cpp`, `ble_mgr.cpp`, `test_ble_store`, contratos bond/background | Decode inválido falha fechado; material de segurança da conexão é gerenciado separadamente pelo NimBLE. Não apagar bonds físicos nem criar pareamento para validar formato |
| Event log | `/sdcard` binário de records com magic/checksum/sequence; projeção textual rotativa é best-effort | `event_log.cpp`, `event_log_recent`, testes event-log/recovery | Binário autoritativo; erro de projeção textual não prova perda do record. Não truncar logs reais para teste |
| Arquivos de usuário/editor | Bytes arbitrários confinados a `/data`; save escreve temp, flush/fsync, rename e sync de diretório; `.rollback` recupera somente se destino ausente; `.tmp` não é publicado | `cyberdeck_app_storage.h/.cpp`, `cyberdeck_local_shell.cpp`, `test_storage_facade`, `test_save_sidecar_recovery` | FAT/FatFs não promete a semântica atômica POSIX em todos os casos; usar arquivo descartável e conferir hash |
| Barreira de poweroff | `/sdcard/.poweroff-sync`, arquivo marcador de um byte criado/truncado, escrito e `fsync` antes do unmount; não é um journal de recuperação nem pode provar sozinho que o corte ocorreu | `cyberdeck_poweroff.cpp`, `test_poweroff`, `test_poweroff_link_guard` | Timeout/falha não emite pulso; confirmar corte e dados requer fonte conhecida e canário/hash pós-relight segundo o roteiro manual |
| GET `/battery` | JSON somente leitura com `schema: 1`, campos/ordem em `cyberdeck_battery_json.h`; corpo bounded pelo header | `cyberdeck_battery_json.cpp`, `test_battery_json`, `test_battery_http_contract` | Valor desconhecido vira `null`/unavailable; usar `Cache-Control: no-store` e respeitar allowlist |
| Serial-JTAG | NDJSON por mensagem com `rid`, envelope `ok/result` ou erro tipado; comandos JSON não declaram uma versão global do protocolo | `cyberdeck_serial_bridge.cpp`, `tools/cyberdeck_cli.py`, testes serial/manuais | Validar rid e resultado, logs intercalados, limites e timeout da CLI; `ui.type` admitido não prova o efeito |
| Screenshot | BMP 24-bit BI_RGB, bottom-up, no endpoint HTTP e no transporte Serial-JTAG chunked/Base64 com CRC32 | `screenshot_bmp.*`, serial bridge, `test_screenshot_bmp`, `test_serial_screen_dump` | Verificar dimensões, orientação, continuidade e CRC; captura não deve entrar no mapa como artefato |
| Estado UTC | Relógio do sistema ajustado em runtime após resposta HTTP validada; não há snapshot próprio persistido pelo adapter de sync | `cyberdeck_time_sync.cpp`, `test_time_sync` | Sem validação/sync não declarar hora confiável; registrar sync observável sem expor tráfego ou credenciais |

Observáveis existentes incluem `sys.info`, `app info`, `wifi.status`,
`screen.status`, `GET /battery`, `term.dump`, `events.read` e logs de boot
`HOSTED_DIAG`. Cada uma tem superfície/readiness e limite próprios; o quadro
não presume disponibilidade de HTTP no safe mode nem cria um schema agregado.

**Segurança da validação:** `sys.safe_mode.clear`, poweroff, remoção de SD,
corromper NVS e injeção de falha física não pertencem a este recorte. Uma
resposta `ok:true` a `ui.type` confirma admissão, não a execução do comando;
conferir `term.dump` quando o texto for o observável. A tela apagada exige
`screen.wake` antes de comandos de UI. Não abrir um segundo leitor da porta
durante captura Serial-JTAG.

## Baseline de compatibilidade e próximos gates

A matriz documental identifica os donos de responsabilidade e os cenários de
uso/falha/recuperação. Isso fecha a evidência documental de OS-TAB5-01/02 para
esta fase; **não** aceita o produto físico nem conclui OS-TAB5-07. Compatibilidade
de variantes legadas, análise de falhas por módulo e validação integrada no
firmware identificado permanecem gates das fases seguintes. O dashboard mantém
essa distinção entre critérios aceitos documentalmente e comportamento físico.

| Superfície | Fonte de verdade / regressão | Estado |
| --- | --- | --- |
| Catálogo de comandos, aliases e help | `components/cyberdeck/include/apps/shell/cyberdeck_shell_help.h`, `test_shell_help_contract.py` e testes de sessão | Preservar; extrair cenário por app sem fixar snapshot de lista |
| Console, mudança de foreground e grants | `cyberdeck_app_runtime.cpp`, `cyberdeck_shell_app.cpp`, `test_app_runtime.cpp`, `test_shell_app.cpp` | Baseline inicial deste recorte; faltam critérios físicos por app |
| Bridge NDJSON, identidade e texto | `cyberdeck_serial_bridge.cpp`, `test_serial_ndjson_dispatch.cpp`, `test_serial_term_dump.cpp` e roteiro manual | Baseline inicial deste recorte; identidade em hardware pendente |
| Conectividade e apps foreground/demo | Manifestos e adapters listados no Recorte 2, `test_app_runtime.cpp` e testes por módulo | Matriz inicial; validação física por serviço/app pendente |
| Periféricos, SD e poweroff | Recorte 3, contratos por módulo e roteiros manuais de bateria/ponte/poweroff | Matriz inicial; validação física integrada deste plano pendente |
| Formatos persistidos e schemas HTTP/JSON/NDJSON | Tabela “Formatos persistidos e observáveis” e contratos por módulo | Baseline documental inicial; compatibilidade de formatos legados ainda requer revisão por fluxo |

O inventário, a classificação atual, os limites encontrados e os formatos
persistidos/observáveis estão catalogados. A revisão de compatibilidade das
variantes legadas continua aberta em cada linha de sua fonte/teste; a validação
integrada do firmware atual pertence aos gates físicos das fases 3 e 5. A Fase 1
conclui a definição e rastreabilidade dos requisitos, não certifica o produto
Tab5 nem inicia otimização.
