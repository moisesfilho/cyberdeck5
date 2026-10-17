# Wi-Fi survey: requisitos, cenários e evidência host

Os cenários ligam os TUs reais do modelo (`cyberdeck_wifi_survey.cpp`), da
sessão de shell (`cyberdeck_shell_session.cpp`) e da persistência
(`cyberdeck_wifi_audit_persistence.cpp`). Shims/fixtures controlam filesystem;
contratos de wiring e compilação não provam rádio físico ou FatFS.

| REQ / AC aprovado | TEST e cobertura | Evidência/resultado focado |
| --- | --- | --- |
| REQ-SURVEY-01 / AC-01: sessão única; segunda inicialização recusada | TEST-SURVEY-SESSION: `test_wifi_survey.cpp::test_duration_and_single_session_boundaries`; início repetido enquanto ativa | PASS: sessão real rejeita segundo início sem trocar token/duração. Exclusão concorrente entre chamadas UI depende do seam/integração. |
| REQ-SURVEY-02 / AC-02: default 30s; faixa 10–120s; scans sequenciais, intervalo mínimo 5s e máximo 6 | TEST-SURVEY-TIMING: 9/10/30/120/121s; relógio antes/no intervalo; 0/1/6/7 scans, durações 10/30/120 | PASS: modelo real; `test_shell_session` verifica default e rejeição 9/121; cadência com relógio controlado, sem sleep. 10s permite 2 scans pela duração; 30/120s permitem até 6. |
| REQ-SURVEY-02/07 / AC-02/07: teardown aguarda callback bounded; timeout preserva recursos em quarentena; drain/delete somente após completion | TEST-SURVEY-LIFECYCLE: `test_wifi_survey_integration_contract.py` verifica espera sequenciada/bounded, branch quiet drain→queue/mutex/semaphore delete e branch timeout sem delete | PASS: contrato estrutural atual 34/34 no UI real. Verifica ambas as saídas e ordem, mas não injeta timeout/atraso FreeRTOS em runtime host. |
| REQ-SURVEY-03 / AC-03: até 32 redes únicas, ordenação RSSI, hidden/unknown e duplicatas | TEST-SURVEY-RESULTS/BOUNDS: 0/1/32/33, duplicata BSSID mantém sinal mais forte, ordenação, hidden SSID e channel/security/BSSID desconhecidos | PASS no modelo: acumula 32 entre dois lotes de 16 e substitui ao inserir 33ª. Manager limita cada scan a `WIFI_SCAN_MAX_APS=16`; modelo sozinho não prova descoberta real de 32 APs no dispositivo nem que um único scan entregue >16. |
| REQ-SURVEY-04 / AC-04: count/min/avg/max voláteis apenas na sessão | TEST-SURVEY-TREND: amostras repetidas, RSSI, desaparece/reaparece, cancel/restart limpa tabela | PASS: count/min/avg/max e reset entre sessões no modelo real; sem backend persistente no TU. |
| REQ-SURVEY-05 / AC-05: cancelamento invalida callback e resultados stale | TEST-SURVEY-CANCEL: complete/fail stale após cancel, tokens divergentes não liberam request pendente, token antigo após nova sessão, complete no deadline | PASS: modelo real descarta stale/cancel/expiry; falhas stale não liberam nem substituem a requisição válida pendente. Corridas FreeRTOS/teardown requerem hardware. |
| REQ-SURVEY-06 / AC-06: export separado, confirmação, formato versionado/bounded, destino confinado e IDs mascarados por padrão | TEST-SURVEY-EXPORT: shell staging sem confirmação; confirm masked; opt-in unmasked com confirmação própria; texto versionado, sanitização, máscara e tamanho <=8192 | PASS: sessão/parser reais; opção unmasked é opt-in separado e exige confirmação própria, compatível com masked-by-default. Caminho/date e execução assíncrona têm apenas guard estrutural, não teste comportamental host. |
| REQ-SURVEY-07 / AC-07: transação `.tmp/.bak`, rollback/recuperação fail-closed em FatFS sem alegar atomicidade POSIX | TEST-SURVEY-PERSISTENCE: survey root, payload 8192/+1, caminho cross-policy, falha write/preservação de candidato; matriz de falhas write/fsync/close/rename/rollback | PASS: TU transacional real sob filesystem determinístico; suite persistence audit cobre rollback/recovery/ACK. Ambos os perfis usam a mesma transação. Não prova FatFS real. |
| REQ-SURVEY-08 / AC-08: scan passivo, sem associação/probe dirigido/payload/deauth/spoof/wardriving; aviso de conectividade e compatibilidade com scan interativo | TEST-SURVEY-SAFETY: shell avisa impacto; `wifi search` recusado durante survey; integração roteia ao scan passive | PASS: sessão shell e guard de wiring; não prova comportamento efetivo do rádio, SSH nem ausência de tráfego dirigida em device. |
| B1: TU da auditoria continua compilável no host após separar dependências ESP/FreeRTOS | TEST-AUDIT-HOST-COMPILE: `test_wifi_audit` e `test_wifi_audit_save` compilam e linkam `cyberdeck_wifi_audit.cpp` real no host | PASS ciclo 5: ambos reconstruídos e executados; audit 415 checks, save 160. Regras dependem dos TUs/headers/Makefile para recompilar sem binário stale. |
| B2: payload audit <=2048, survey <=8192; qualquer policy desconhecida falha fechado | TEST-PERSISTENCE-POLICY: tamanhos exatos/+1 por perfil; unknown enum em ambos os roots; cross-root rejeitado; regressão audit | PASS ciclo 6: `test_wifi_audit_persistence` 476 checks; audit 2048/2049, survey 8192/8193, unknown policy sem I/O e falha de abertura com/sem destino sem ACK. |
| R5: survey usa passive dwell explícito de 360 ms, sem alterar scan legacy | TEST-SURVEY-DWELL: guard de integração exige `survey_config` passive com `WIFI_PASSIVE_SCAN_DEFAULT_TIME` e verifica wrapper legacy com configuração nula | PASS: integração 13/13. ESP-IDF 5.5.5 define `WIFI_PASSIVE_SCAN_DEFAULT_TIME` como 360 ms em `esp_wifi_types_generic.h`; wiring usa essa constante apenas na configuração survey. Guard não simula rádio. |
| REQ-SURVEY-03/04 / AC-03/04: ordenação determinística de sinais empatados e lote nulo não altera amostras | TEST-SURVEY-TIE-NULL: `test_wifi_survey.cpp::test_equal_signal_ordering_and_null_batch` verifica BSSID e canal como chaves de desempate, além de callback nulo com count positivo | PASS: TU real; 178 checks, incluindo ordem estável e preservação dos resultados. |
| REQ-SURVEY-07 / AC-07: erro ao inspecionar artefato ou remover backup stale bloqueia mutação | TEST-PERSISTENCE-INSPECT-UNLINK: `test_wifi_audit_persistence.cpp::test_inspection_errors_fail_closed_without_mutation` e `test_stale_sidecar_unlink_failure_blocks_new_transaction` | PASS: TU transacional real; 507 checks, erro de inspeção em ambos sidecars e unlink negado preservam evidências e não iniciam escrita. |
| B3: help catalog/golden descreve survey sem perder linhas/ordem/igualdade | TEST-HELP-GOLDEN: `test_shell_utils`, fixture `cyberdeck_help.h`, `test_help_unification`, `test_ble_command_parse` e `help_unification_contract` | PASS ciclo 5: shell utils 439, help unification 320, BLE parser 75; structural contract PASS. Golden mantém igualdade e 18 linhas. |
| EXPORT-CONTROLLER: confirmação obrigatória e mode-bound, expiração estrita (igualdade válida), sem replay; busy rejeita sem rearmar/substituir confirmação; falha terminal accepted e handoff stale-safe | TEST-SURVEY-EXPORT-CONTROLLER: `test_wifi_survey_export_controller.cpp` liga o TU real do controller | PASS comportamental; cobre busy request, confirmação não pendente, accepted/start/fail/published, terminal failure do token atual, token stale/replay sem liberar/reportar/ACK e ACK único. Não exercita UI nem FreeRTOS. |
| EXPORT-APP-LIFETIME: worker/filas bounded sem referências UI sobrevivem deinit/reinit; completion fica retida até pump e token valida antes de ACK; nenhum worker/queue/gate é criado no confirm | TEST-SURVEY-EXPORT-WIRING/RESULT: os contratos verificam init lazy, ausência no cleanup, stack 8192, filas cap-1, persistência/wipe e ordem do pump | Estruturais; não executam scheduling FreeRTOS nem reinit real. Retenção/replay da fila e execução ESP_PLATFORM permanecem limites explícitos. |
| EXPORT-OWNERSHIP/PRIVACY: payload/path transitórios e job são zerados nos limites de ownership; completion path é zerado no pump; logs não expõem dados | TEST-SURVEY-EXPORT-WIRING/RESULT verifica ordem de wipe/ownership e caminhos de rejeição | Estrutural; não prova comportamento do alocador/firmware. |
| EXPORT-ACK/FAILURE/STALE: accepted→started→persisted→published→ACK exatamente uma vez; busy não apresenta pending falso; falhas terminais do worker entregam token bounded; pump libera controller/busy uma vez e só reporta falha do token atual | TEST-SURVEY-EXPORT-CONTROLLER, `test_shell_session.cpp`, `test_wifi_survey_integration_contract.py` e `test_wifi_survey_export_result_contract.py` | PASS comportamental no controller e shell real (rejeição `false` não imprime pending) + wiring estrutural de token de handoff nos dois caminhos terminais, `fail_admitted`, rejeição stale e ordem de release. FreeRTOS/FatFS reais não executados. |

## Evidência de export lifecycle atualizada (cycle 7)

`test_ui_resource_contract` verifica as 13 fases de init/rollback fail-closed,
incluindo os recursos de export, preservados como app-lifetime no cleanup; os 9
timers LVGL continuam sendo removidos. `test_wifi_survey_integration_contract`
verifica teardown e exige que o host da UI recuse busy antes de solicitar
confirmação; a mensagem esperada é `wifi survey export busy; confirmation remains available for retry`.
O shell host real (269 checks) mantém regressão de rejeição sem mensagem `pending`.
O worker publica o token admitted em falha de gate ou queue-send; o pump usa
`fail_admitted`, libera ownership e só escreve failure para a conclusão atual.
O controller exercita falha terminal current/stale/replay e busy sem rearmar.
`test_wifi_survey_export_result_contract` verifica os enlaces estruturais da
ligação confirmação→fila→worker→pump/ACK. Separadamente,
`test_wifi_survey_export_controller` compila e exercita comportamentalmente o TU
real do controller (não o worker/UI): missing/expired/equality,
active/duplicate, capacidade/unavailable/rearm, accepted-start-success/failure,
resultado stale e ACK único. Assim, o wiring não é alegado como execução do
worker: contratos estruturais detectam alterações óbvias no fluxo da UI; a
transição de estado do controller tem teste comportamental independente. Isso
não executa scheduling FreeRTOS, lifecycle real/reinit, ou persistência FatFS.
Somente os alvos focados foram executados; sem suíte global, firmware build ou
device validation. O resultado ambiental conhecido de branch coverage global
local é 58.06% (<59%); este trabalho não altera o gate nem reivindica aprovação.

## Execução focada

```sh
make -C tests/host/keymap test_ui_resource_contract test_wifi_survey_integration_contract test_wifi_survey_export_controller test_wifi_survey_export_result_contract
make -C tests/host/keymap test_shell_session
```

Executar os testes comportamentais criados/alterados, não a suíte completa.
Nesta rodada: controller comportamental PASS; integração 34/34; result contract
16/16; shell session 269 checks; UI resource contract PASS. Não foram medidos
novos percentuais locais de cobertura; a discrepância global conhecida permanece.
Os contratos de integração/result são guardas estruturais: não comprovam execução
FreeRTOS, retenção real da fila através de reinit, nem persistência FatFS. O
controller comportamental compila o TU real do controller, mas não executa o
worker/UI. Validação device continua separada e não foi executada nesta rodada.
Validação física pendente: observar com e sem associação Wi-Fi, verificar
aviso/conectividade/SSH, cancelar durante scan e exportar masked/unmasked com
confirmação explícita; ler arquivo SD e verificar conteúdo/limite e ausência de
histórico após reboot. Capturar `sys.info` e logs antes/depois; nenhuma operação
ativa de probe/associação deve ocorrer.
