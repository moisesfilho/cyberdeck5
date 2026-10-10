# Plano de implementação: `poweroff` no Tab5

**Tipo:** documento de controle de progresso; não é evidência de teste.  
**Estado geral:** poweroff **NOT DONE**. Há componentes Stage 1 IMPLEMENTED no worktree, não commitados e ainda não plenamente aceitos. Nenhuma validação final de segurança ou corte físico foi concluída.

## Objetivo e decisões aprovadas

Adicionar o comando terminal `poweroff` para desligar fisicamente o rail do Tab5 pelo sinal de fábrica do BSP. Aprovações humanas registradas: usar o BSP atual e o sinal real de fábrica; exigir a sequência exata `poweroff` e depois `y`; persistir dados e quiescer escritores SD antes do corte; arquitetura de coordenador global de I/O; implementação em etapas por módulo. Stage 1 foi ampliado para transação do editor/storage e recuperação de sidecars no boot. A autorização para continuar além do ciclo normal não elimina o checkpoint atual. O comando de poweroff não foi implementado.

## Evidência de hardware (não confundir com implementação)

- **PASS — evidência documental:** documentação oficial Tab5 identifica PI4IOE5V6408-2, expander B, endereço `0x44`, E2.P4 como `PWROFF_PLUSE`.
- **PASS — evidência de referência:** M5Tab5-UserDemo implementa `bsp_generate_poweroff_signal()` alternando alto/baixo por três ciclos de 100 ms; esquema encaminha o sinal ao controlador de energia para cortar MPWR_EN.
- **PASS — evidência do BSP atual:** há suporte `bsp_io_expander1_init()`; P4 exige `PUSH_PULL`. Expander A `0x43` P4 é `LCD_EN`, não substituto.
- Nunca substituir o corte físico por desligar display ou deep sleep. Esta evidência não prova funcionamento no firmware Cyberdeck nem no dispositivo.

## Implementado no worktree (ainda não totalmente aceito)

- **IMPLEMENTED — Stage 0:** `components/cyberdeck/include/platform/storage/cyberdeck_sd_io_coordinator.h` e `src/platform/storage/cyberdeck_sd_io_coordinator.cpp`: leases RAII move-only, gate OPEN/CLOSING/CLOSED, drenagem bounded e reabertura conforme estado.
- **IMPLEMENTED — Stage 1 parcial:** `main/app_main.cpp` mantém a instância pelo ciclo do processo e injeta a recuperação de boot, UI, shell local e runtime/editor. Recovery usa lease WRITE; editor usa READ no open e WRITE abrangendo save-as/save_current até flush/fsync/rename/rollback/fsync de diretório. Shell local protege operações síncronas de SD. Testes relacionados também foram adicionados anteriormente; não são aceitação completa.
- A integração não cobre todos os leitores/escritores assíncronos. Não declarar segurança global, sincronização global, unmount seguro ou poweroff pronto.

## Matriz de progresso e evidências

| ID/escopo | Evidência registrada | Estado |
| --- | --- | --- |
| REQ-COORD — leases/gate Stage 0 | `test_sd_io_coordinator` | PASS |
| REQ-SHELL — shell local e contratos de integração | `test_local_shell`, contratos de integração, editor, sidecar, boot/runtime | PASS nos alvos registrados, mas agregado posterior não passou |
| REQ-BUILD/QUALITY — firmware e qualidade | build ESP-IDF 5.5.5, clang-format, cppcheck, ruff, bandit | PASS nos comandos registrados |
| REQ-SESSION — sessão de shell | agregado reviewer: três asserts `cd` falham porque fixture não injeta coordinator OPEN | FAIL; correção pendente |
| REQ-FIXTURES — paridade de fixtures | `test_shell_app` também deve vincular coordinator OPEN | NOT DONE |
| REQ-CAT — caminho real de leitura cat | produção encaminha `cyberdeck_shell_session` a `cat_worker`; leases do `local_shell::cat` não cobrem esse caminho | NOT DONE |
| REQ-COVERAGE — thresholds locais | gcovr local 83.7/93.5/51.1 contra baseline 83.9/93.8/51.3 | BLOCKED/inconclusivo ambiental; exige gate confirmado em CI |
| REQ-GLOBAL-QUIESCE — todos os I/O e handles | event_log persistente/global vprintf, Wi-Fi config/audit, Serial fs.write/vprintf, SSH known_hosts, cat-worker, screenshot/leitores e hooks ainda sem fence/stop/join completo | NOT DONE; bloqueador de segurança |
| REQ-SD-SYNC — persistência final | IDF 5.5.5/FatFS R0.15 sem API pública de sync global; fsync é por descritor/FIL. BSP `bsp_sdcard_unmount()` desliga SD antes do unmount VFS e não verifica handles abertos | BLOCKED; estratégia suportada não estabelecida |
| REQ-POWER — comando e corte de rail | confirmação exata, precondições duráveis, HAL expander1 `0x44` P4 push-pull e pulso de fábrica | NOT DONE |
| REQ-HW-ACCEPT — validação Tab5 | corte físico de rail sob condições controladas | NOT DONE |

## Trabalho restante, em ordem

- [ ] **REQ-FIXTURES:** corrigir fixture de `test_shell_session` vinculando coordinator OPEN compartilhado; fazer o mesmo em `test_shell_app` para paridade. Executar somente alvos relacionados.
- [ ] **REQ-CAT:** incluir `cat_worker` e demais leitores no coordinator/fence; se os critérios forem alterados, obter revisão/aprovação humana antes de reduzir cobertura.
- [ ] **REQ-REVIEW:** rerodar agregado do reviewer, guard de escopo/gcovr e gates de qualidade/build ESP-IDF; registrar resultados sem converter falha ambiental em PASS.
- [ ] **REQ-GLOBAL-QUIESCE:** projetar e integrar fence de admissão e lifecycle bounded para cada escritor/leitor/event logger/hook; parar/join tarefas e fechar handles antes de declarar quiescência. Falha/timeout deve impedir corte.
- [ ] **REQ-SD-SYNC:** determinar estratégia final suportada de persistência sem presumir sync global inexistente; compreender e tratar a ordem de energia do BSP. Não chamar unmount como atalho.
- [ ] **REQ-POWER:** implementar comando só após resolver os bloqueadores: prompt em duas etapas exatamente `poweroff` e `y`, condições de persistência/quiescência satisfeitas, falha fechada sem retry e sinal confirmado via HAL no expander B 0x44 P4 configurado push-pull com pulso de fábrica. Não tocar `LCD_EN`.
- [ ] **REQ-TEST/REVIEW:** acrescentar testes de comando, confirmação, falhas, timeouts e pré-condições; executar alvos locais e revisão completa. Testes não substituem validação física.
- [ ] **REQ-HW-ACCEPT:** validação manual em Tab5 real de corte do rail, sequência, persistência, rejeições/timeout e riscos de alimentação; registrar dispositivo, firmware e evidências. Sem isso, declarar hardware BLOCKED.

## Restrições de aceitação

O comando só pode ser considerado pronto após todos os itens bloqueadores resolvidos, revisão e gates aceitos, e validação física documentada. Timeout, I/O pendente, handle aberto ou sync não demonstrado deve falhar fechado: não emitir pulso. Este documento e os resultados host não são evidência de corte físico.
