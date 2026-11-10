# reviewer - shell-tab-completion

- Role: reviewer
- Updated: 2026-11-09 20:59 -03:00
- Code state: HEAD `e23984d9500ac27e187dc3f8276841366c3f2779` + worktree sujo; hash `git diff --binary | sha256sum` = `921e5184a7bceb61bd08644eab3f409fa6696b50381297a5a2f828d213486af5`; snapshot incluindo conteúdo dos untracked, por método local explícito = `f27d106b90fdb8514d19a68d739ae893c22f6ae1c96ba185f40bd4c81bf8896c`.

## Dispatch objective

Revisão incremental em modo development de autocomplete Tab, REQ-TAB-01..05. Sem suíte completa, cobertura global, CI, build amplo ou flash. Saída não é aprovação final.

## Work done

Revistos plano, notas coder/tester, teste focado, implementação e wiring de entrada. Inspecionados tokens/cursor e limites, modos de path, root virtual, symlink/entradas especiais, lease SD READ, fila e entrega ao contexto LVGL, token stale, teardown e isolamento por estado/menu. Sem defeito bloqueante confirmado neste snapshot. A regra revisada permite completar candidato único mesmo quando scan sinaliza truncamento; múltiplos candidatos truncados não alteram o token. O worker invalida entrega antes do teardown e a sessão rejeita resultado cuja linha/cursor/token não correspondam.

## Evidence

- `git rev-parse HEAD` | exit 0 | `e23984d9500ac27e187dc3f8276841366c3f2779` | 2026-11-09 20:59 -03:00.
- `git diff --binary | sha256sum` | exit 0 | `921e5184a7bceb61bd08644eab3f409fa6696b50381297a5a2f828d213486af5` | 2026-11-09 20:59 -03:00.
- `make -C tests/host/keymap test_shell_completion test_shell_session test_shell_completion_worker_contract` | exit 0 | snapshot acima | 2026-11-09 20:59 -03:00; 228 checks/0 falhas, 249 checks/0 falhas, contrato: 14 mutações rejeitadas.
- `git diff --check` | exit 0 | snapshot acima | 2026-11-09 20:59 -03:00.
- Não executados: suíte agregada, cobertura/gcovr, lint/build amplo, CI e hardware/flash; gates finais permanecem pendentes.

## Decisions and assumptions

Mantidos os limites do modo development. Nenhuma preferência ou achado adjacente encaminhado como correção obrigatória. Validação manual requerida para Tab físico e virtual, navegação de caminhos no SD, não bloqueio perceptível da UI e preservação de Tab literal em SSH/senha.

## Pending and risks

- Pendência de aceite físico: comportamento efetivo do firmware não foi observado no dispositivo; feedback host/estrutural não satisfaz esse critério.
- Gates finais pendentes para dispatch `final`: suíte completa, cobertura exigida, lint/typecheck/build/CI aplicáveis e validação do head revisado.
- Risco residual ainda não medido em hardware: custo/stack da enumeração (worker 8192 B; até 512 entradas e sort) e interação real do lease durante fechamento/poweroff.

## References

Plano `docs/agent-state/shell-tab-completion/planner.md`; evidência independente `docs/agent-state/shell-tab-completion/tester.md`; fontes em `components/cyberdeck/{include,src}/apps/shell/`, wiring em `components/cyberdeck/src/platform/display/cyberdeck_ui.cpp`, testes `tests/host/keymap/test_shell_completion.cpp`, `test_shell_session.cpp` e `test_shell_completion_worker_contract.py`.

## Updates

### 2026-11-09 20:59 -03:00 - revisão incremental e checkpoint

- Fact: revisão delimitada concluída no snapshot registrado; três alvos focados e `git diff --check` passaram. Nenhum achado acionável confirmado. Retorno `review_feedback_ready`, não aprovação; implementação/testes físicos e gates finais ainda pendentes.
- Supersedes or discards: nenhuma conclusão anterior.

### 2026-11-10 13:49 -03:00 - revisão development após correção do token Tab

- Estado revisado: HEAD `e23984d9500ac27e187dc3f8276841366c3f2779`, worktree sujo. Snapshot SHA-256 `482b779d3291a5e03f4e394a182b14a86db75c04dfcab38b8c8d90c8fe9a8234`, calculado sobre HEAD + `git diff --binary` e conteúdo/nome de arquivos untracked fora de `docs/agent-state/`; inclui fontes, testes, docs e novos arquivos de produção/teste. Mudanças preexistentes em `docs/agent-state/image-formats/{coder,reviewer,tester}.md` também estão no diff global. Não alego SHA de commit para o estado sujo.
- Revisão de fonte/diff: `tab5_keyboard_keys.cpp` inclui alias nomeado `tab` como `TAB5_KEY_CHAR`; `emit_event` normaliza exclusivamente token de 3 bytes idêntico a `tab` para um byte `\t`, mantém o default `buf/copy` para tokens restantes, inclusive `space`, e preserva modifier/`entry->ch`. UI consome primeiro o ramo textual e só usa `local_key(special_key)` como fallback, importante porque o evento reporta ambos. `session::insert_physical_text` dispara completion apenas para Tab de um byte quando `completion_allowed()` (MENU, Wi-Fi IDLE, sem BLE dono, sem poweroff); fora disso mantém a inserção literal. Tecla virtual `LV_KEY_NEXT` segue por `local_key`/`key::next_tab`; os estados SSH/senha/host key não iniciam completion. README EN/PT e code-map especificam token exclusivo e preservação de outros tokens; teste negativo fixa `space` sem normalização. Sem defeito bloqueante confirmado no delta.
- Evidência focused independente consultada em `tester.md` para este delta/snapshot: `test_keymap` 670 checks PASS; `test_keyboard_input_contract` PASS (prioridade texto antes do special e guard exclusivo); `test_shell_session` 249 checks PASS; `test_shell_completion` 228 checks PASS; worker contract 14 mutações rejeitadas; `test_keyboard_event` 76 checks PASS; contratos de versão + `git diff --check` PASS. Os comandos focados não foram repetidos: as notas do tester vinculam-nos ao estado atual dos fontes/testes e a única alteração posterior reportada é documentação. Nesta sessão, `git diff --check` | exit 0 | snapshot acima | 2026-11-10 13:49 -03:00.
- Hardware/AC manual: referência de flash é firmware 1.18.2, 2.804.320 bytes, SHA-256 `81248dd79b150855e67814b89e586f3bc911bcda54181110cc6cfc4c1d519f60`. Usuário reportou “Funcionou tudo certo.” após `hel`+Tab→`help` e `c`+Tab duas vezes; registro como `evidence_kind: relato`, aceitação qualitativa do comportamento-alvo, não medição independente. Não houve flash nem interação física nesta revisão. Limites: sem term/UI dump/log desta rodada; demais cenários de caminhos, zero match, SSH/senha e token físico `space` não foram estabelecidos por esse relato.
- Pendências: suíte completa, cobertura global, CI e gates de revisão final não executados (fora deste modo); não aprova finalização. Riscos residuais não bloqueantes: `space` físico do A164 não foi medido, e o relato não demonstra individualmente cada subcenário. Sem achado acionável; saída `review_feedback_ready` (feedback, não aprovação). Nenhum Jev: classificação/ação não dependem de desempate semântico.
- Supersedes or discards: substitui a conclusão anterior desta nota de “validação física pendente sem evidência do comportamento” pela evidência qualitativa relatada acima; mantém explícitos seus limites e não converte relato em medição. Revisa o estado anterior que não incluía a correção do decoder de teclado.
- Checkpoint: código e testes direcionados revisados; diff check passou. Gates finais completos e evidências manuais adicionais seguem pendentes.

### 2026-11-10 14:27 -03:00 - re-review development: porta de display após TEST-WM-05

- Estado revisado: HEAD `e23984d9500ac27e187dc3f8276841366c3f2779`, worktree sujo. `git diff --binary | sha256sum` = `24ba180496378ae6538269a434e50d2c940ce802e68803677a11a0e8d9967680`. Snapshot de conteúdo completo (diff tracked seguido de cada untracked ordenado por caminho, com marcadores `TRACKED_DIFF\0`/`UNTRACKED\0` e bytes integrais) = `defa4c6fe1a4b263a0cc29e058dc99a5818b4eca9a912feb9c54a5968b9d9344`; inclui notas untracked existentes antes desta entrada. Nenhum código/teste editado pelo reviewer.
- Conferência direta do plano 14:04, diff, fonte e notas: `cyberdeck_completion_worker.cpp` não importa/chama LVGL; envia callback + payload opaco pela porta em `include/src/platform/display/cyberdeck_ui_post`. Porta agenda `lv_async_call` antes do wake, retorna recusa sem wake e não toma posse/libera payload. O worker libera resultado nos caminhos sem entrega e o callback libera após entregar/descartar; lock de lifecycle cerca postagem e callback e bloqueia entrega após closing/disabled. `destroy_ui_resource_handles()` para keyboard/cat/completion workers antes de desmontar views; join do worker não aguarda callbacks LVGL já enfileirados. REQ-TAB-05 / TEST-WM-05: a separação arquitetural está corrigida sem allowlist; contrato preserva os checks anteriores e adiciona mutações da porta.
- Verificação de CI: `.github/workflows/quality-gate.yml` agora inclui headers/sources/teste novos de completion e `cyberdeck_ui_post` no clang-format; trechos tocados de `tab5_keyboard*.cpp` são delimitados por ranges. Cppcheck lista as TUs de produção completion/porta e teclado. Não observei omissão dos novos arquivos do escopo indicado nem weakening de TEST-WM-05. O teste novo está no alvo formatado.
- Gates focados: `make -C tests/host/keymap test_shell_completion test_shell_completion_worker_contract` | exit 0 | snapshot acima | 2026-11-10 14:27 -03:00; completion 242 checks/0 falhas, worker contract 21 mutações rejeitadas. Sem suíte agregada, cobertura, build amplo, CI remoto ou flash, conforme modo development.
- Findings/riscos de baixa severidade (não bloqueantes neste modo):
  - `components/cyberdeck/src/platform/display/cyberdeck_ui_post.cpp:16 | low | retorno de wake ignorado, então `post()` pode retornar true apesar do contrato “agendado e task acordada” | API vendor `lvgl_port_task_wake` pode retornar `ESP_ERR_INVALID_STATE` quando eventos LVGL indisponíveis; resultado já foi aceito por `lv_async_call`. Não há evidência de perda em operação normal; recomenda-se alinhar contrato/telemetria ou tratar a falha numa revisão futura.`
  - `components/cyberdeck/src/apps/shell/cyberdeck_completion_worker.cpp:133-137 | low | payload aceito depende do callback LVGL eventual para ser liberado | teardown junta worker e invalida callback, mas não drena callbacks já aceitos; se task/LVGL deixar de processá-los antes do callback, payload fica alocado. Padrão semelhante ao cat e sem reprodução neste fluxo; risco condicional a shutdown definitivo do dispatcher, não bloqueio confirmado do comportamento normal.`
- Evidência manual AC: relato do usuário “Funcionou tudo certo” em firmware 1.18.2 permanece `evidence_kind: relato` (conforme tester); não foi nova validação nem medição deste snapshot. Não atribuo satisfação física ao código novo sem flash/observação específica.
- Pendências: revisão final ainda exige suíte completa/regressão, cobertura e scope guard, lint/format/build/CI aplicáveis sobre snapshot imutável; evidência física específica deste snapshot se requerida pelo fluxo final. Sem blockers confirmados no escopo incremental; feedback não equivale a aprovação.
- Supersedes or discards: substitui o finding bloqueante anterior TEST-WM-05/format/CI do reviewer final, resolvido pela porta de plataforma e pelo wiring de CI verificados nesta revisão. Mantém os gates finais como pendentes e preserva o relato físico como relato.
- Checkpoint: fronteira worker/plataforma, ownership/lifecycle, testes focados e inventário de format/lint CI revisados; correção de TEST-WM-05 confirmada. Ficaram pendentes gates amplos, CI remoto e validação física do snapshot.
