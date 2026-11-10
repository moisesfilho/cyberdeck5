# coder - shell-tab-completion

- Role: coder
- Updated: 2026-11-09 (carimbo por entrada abaixo; hora local não medida pelo harness)
- Code state: HEAD `e23984d9500ac27e187dc3f8276841366c3f2779` + worktree sujo (diff não commitado; mudanças preexistentes em `docs/agent-state/image-formats/{coder,reviewer,tester}.md` preservadas)

## Dispatch objective

Implementar autocomplete por Tab (comandos e caminhos) no shell local do
cyberdeck5 conforme plano aprovado (`REQ-TAB-01..05`), com testes focados,
documentação/mapa e versionamento coerentes. Fora de escopo: SSH, senhas e
outros apps; gramática/tokenizador (nomes com espaço não são inseridos).
Exit criterion: função ligada ponta a ponta, testes focados PASS, docs/mapa
coerentes e pendência física registrada. Sem flash, sem suíte global.

## Work done

Produção (ponta a ponta):

- `components/cyberdeck/include/apps/shell/cyberdeck_shell_completion.h` (novo,
  header-only puro): `token_at`, `classify`, `command_accepts_path`/`kPathCommands`
  (cd/ls/cat/touch/mkdir/rm/rmdir; `cd` = `path_mode::directory_only`),
  `resolve_virtual_directory`, `match_commands`, `common_prefix`, `apply`,
  `format_candidates`; limites 64 candidatos / 128 B por nome / 256 B token e
  diretório / 512 entradas / linha 64 B. Regras: zero candidatos preservam a
  linha, 1 completa (diretório ganha `/`), vários inserem prefixo comum, segundo
  Tab (`list_requested`) apenas lista, conjunto truncado nunca estreita o token.
  Header-only porque `SHELL_SESSION_MIG_OBJS`/`SHELL_APP_MIG_OBJS` são fixados por
  `tests/host/keymap/incremental_final_baseline.json` e não podem ganhar TUs.
- `components/cyberdeck/src/apps/shell/cyberdeck_shell_completion_scan.cpp`
  (novo): única parte com I/O; enumeração via `cyberdeck_local_shell_opendir`
  (confinada, `O_NOFOLLOW`), namespaces da raiz virtual (`apps/data/dev/tmp/system`,
  `null` em `/dev`), rejeita `/sdcard`, exclui nomes com espaço/controle,
  ocultos só com prefixo `.`, exclui symlinks/especiais, ordena, deduplica e
  trunca.
- `components/cyberdeck/include/apps/shell/cyberdeck_completion_worker.h` +
  `src/apps/shell/cyberdeck_completion_worker.cpp` (novos): worker FreeRTOS
  (fila 4, stack 8192) com lease READ do `cyberdeck_sd_io` por varredura e
  entrega por `lv_async_call` + `lvgl_port_task_wake`; teardown invalida
  callbacks e faz join; `quiesce` para fence. Nunca executa na task LVGL.
- `cyberdeck_edit_line.{h,cpp}`: nova `set_line(text,len,cursor)`
  (tudo-ou-nada, clamp de cursor) usada para reescrever o token.
- `cyberdeck_shell_session.{h,cpp}`: porta `host::completion_scan_async`
  (default `false`), `begin_completion`, `apply_completion_result`,
  `completion_allowed`, `apply_candidates`, `clear_completion_state`; cache por
  (linha, cursor) para o segundo Tab; token monotônico global descarta outra
  sessão, replay e linha editada; `key::next_tab` completa só no `MENU` e
  insere `\t` literal em SSH/senha/host key; Tab físico (`insert_physical_text`
  com `'\t'` único) segue a mesma regra; `help_text()` passou a consumir
  `shell_commands()` (fonte única catálogo+manifests sem duplicatas).
- `cyberdeck_shell_app.{h,cpp}`: fachada nula-segura para
  `begin_completion`/`apply_completion_result`.
- `cyberdeck_ui.cpp`: include do worker, `s_completion_worker_ready`,
  `on_completion_result` (contexto LVGL), override `completion_scan_async`,
  bind do coordenador SD em `cyberdeck_ui_bind_sd_io`, start no init junto do
  `cat_worker` e teardown em `destroy_ui_resource_handles`/deinit.
- `components/cyberdeck/CMakeLists.txt`: registra
  `cyberdeck_shell_completion_scan.cpp` e `cyberdeck_completion_worker.cpp`.

Testes e harness:

- `tests/host/keymap/test_shell_completion.cpp` (novo) + alvo
  `test_shell_completion` no Makefile (`SHELL_COMPLETION_OBJS`, depfiles em
  `MAKE_DEPFILES`, `_bin` em `BINS`, cenário no agregado `test`, `rm -rf` no
  `clean`). Objetos próprios para não alterar o inventário fixado.
- `tests/host/keymap/test_shell_session.cpp`: bloco novo de Tab (sequência,
  segundo Tab, zero candidatos, caminho pela porta assíncrona, token
  stale/replay/linha editada, porta recusando, Tab literal em SSH/senha/Wi-Fi,
  BLE dono da entrada) com porta `completion_scan_async` no `fake_host`
  entregando candidatos canned (a varredura real fica no alvo do scanner).

Documentação e versão:

- `README.md` e `README.pt-BR.md`: parágrafo de uso do Tab (comando/caminho,
  prefixo comum, segundo Tab, 64 candidatos, `cd` só diretórios com `/`, nomes
  com espaço fora, enumeração fora da task LVGL, Tab literal em SSH/senha/Wi-Fi).
- `code-map.md`: seção "Completacao por Tab do shell local (fw 1.18.1)" e duas
  linhas na matriz produção -> testes host.
- `version.txt` 1.18.0 -> 1.18.1 (PATCH de app) e manifesto
  `cyberdeck.shell` 1.0.0 -> 1.0.1 em `cyberdeck_shell_app.cpp`; `api_version`
  preservada em "1".

Decisão de segurança registrada: nenhum stage novo no poweroff. A varredura
roda sob `cyberdeck_sd_io::lease` READ, portanto durante `closing`/fence o
`acquire` falha e o worker entrega resultado vazio (linha preservada).

## Evidence

Comandos e resultados executados nesta entrega (focados, sem suíte global, sem
flash):

- `make -C tests/host/keymap test_shell_completion` | exit 0 | HEAD `e23984d...` | 2026-11-09 |
  saída `shell completion: 189 checks, 0 failures`.
- `make -C tests/host/keymap test_shell_session` | exit 0 | mesmo HEAD | 2026-11-09 |
  saída `shell session: 227 checks, 0 failures` (inclui `kLegacyHelpPinned`).
- `make -C tests/host/keymap test_shell_help test_shell_app test_edit_line` | exit 0 |
  mesmo HEAD | 2026-11-09 | PASS nas três metas (`test_shell_app`: 288 checks;
  `test_edit_line`: 1538 checks), provando que o refactor do `help` e a nova
  `set_line` não regrediram.
- `make -C tests/host/keymap test_shell_help_contract` | exit 0 | 2026-11-09 |
  manifests reais/registro/fixtures e wiring (8 mutações rejeitadas) PASS.
- `make -C tests/host/keymap help_unification_contract` | exit 0 | 2026-11-09 |
  PASS (catálogo canônico continua sendo a fonte do help no session TU).
- `make -C tests/host/keymap test_ota_contract` | exit 0 | 2026-11-09 | PASS com
  `version.txt` 1.18.1.
- `make -C tests/host/keymap test_image_viewer_contract` | exit 0 | 2026-11-09 |
  `0 failures` (piso `MIN_VERSION` 1.18.0 continua satisfeito).
- `source /home/moises/esp/esp-idf/export.sh && idf.py build` | exit 0 | 2026-11-09 |
  build ESP32-P4 completo; compilou
  `cyberdeck_shell_completion_scan.cpp.obj` e `cyberdeck_completion_worker.cpp.obj`;
  `build/cyberdeck5.bin` = 2.804.272 B (0x2aca30), 33% livre na maior partição,
  sha256 `ceda75850c2bae85...`; único warning é o pré-existente do libssh
  (`kill is not implemented`).
- `git diff --check` | exit 0 | 2026-11-09 | limpo.

Não executados por este papel: suíte host completa, cobertura global, lint
global, QEMU/simulador, flash e validação física.

## Decisions and assumptions

- Completar apenas no MENU normal (sem BLE dono da entrada, Wi-Fi `IDLE`, sem
  confirmação/execução de poweroff). SSH/senha/host key mantêm `\t` literal.
- Inventário de comandos = catálogo de help + manifests registrados, sem
  duplicatas, com a mesma regra do `help` (agora uma única implementação).
- Nomes com espaço/controle não são oferecidos (tokenizador sem aspas/escape);
  não ampliar gramática sem reaprovação.
- Lista truncada nunca estreita o token: apenas lista.
- Token do pedido é publicado ANTES de chamar a porta, para aceitar entrega
  síncrona (bug encontrado pelo teste `test_shell_session` e corrigido).
- Versão: PATCH `1.18.1` + `cyberdeck.shell` `1.0.1` (mudança funcional de app,
  conforme `AGENTS.md`); `MAJOR`/`MINOR` intactos, `api_version` preservada.
- Jev não utilizado: as decisões acima são determinadas pelo plano/código.

## Pending and risks

- Validação física pendente (não executada, sem flash): Tab no teclado físico
  M5Stack, tecla `LV_KEY_NEXT` do widget virtual, `cd`/`ls`/`cat` com caminhos
  reais no SD, lista de candidatos no terminal, ausência de bloqueio perceptível
  da UI durante a varredura e confirmação de que SSH/senha seguem literais.
  O binário pronto para essa validação é `build/cyberdeck5.bin`
  (2.804.272 B, sha256 `ceda75850c2bae85...`) no HEAD `e23984d` + worktree sujo.
- Risco residual: stack do worker (8192 B) e custo de `std::sort` sobre nomes
  não foram medidos no dispositivo; `readdir` limita a 512 entradas por
  varredura (truncamento sinalizado).
- Fence de poweroff não inclui o worker novo: a proteção vem do lease READ do
  `cyberdeck_sd_io` (documentado acima), não de um stage dedicado.
- Suíte host completa, cobertura global e lint/format amplos não foram rodados
  por este papel; ficam para tester/reviewer.
- `AGENTS.md` ("Memoria de projeto") pede atualizar o bloco correspondente em
  `.opencode/memory/` quando um fluxo muda. Este despacho autorizou escrever
  somente a nota do papel, então esse bloco NÃO foi alterado; o developer deve
  criar/atualizar o registro de memória do autocomplete (limites 64/128/256/512,
  header-only por baseline, token monotônico, lease READ no poweroff, versão
  1.18.1 / shell 1.0.1).

## References

Plano/nota do planner: `docs/agent-state/shell-tab-completion/planner.md`.
Arquivos alterados/novos listados em "Work done". Testes:
`tests/host/keymap/test_shell_completion.cpp`, `tests/host/keymap/test_shell_session.cpp`,
`tests/host/keymap/Makefile`. Roteiro manual aplicável:
`tests/manual/serial-bridge-validation.pt-BR.md`.

## Updates

### 2026-11-09 -03:00 - checkpoint de implementação e testes focados

- Fact: código ligado ponta a ponta (sessão -> porta -> worker -> varredura),
  testes focados PASS (`test_shell_completion` 189/0, `test_shell_session`
  227/0, `test_shell_help`/`test_shell_app`/`test_edit_line` PASS),
  docs/README e versão 1.18.1 / `cyberdeck.shell` 1.0.1 aplicados.
- Supersedes or discards: none.

### 2026-11-09 -03:00 - build ESP-IDF e contratos de documentação/versão

- Fact: `idf.py build` (ESP32-P4, IDF 6.1) exit 0 com as duas TUs novas
  compiladas e `build/cyberdeck5.bin` 2.804.272 B / 33% livre; contratos
  `test_shell_help_contract`, `help_unification_contract`, `test_ota_contract`
  e `test_image_viewer_contract` PASS com `version.txt` 1.18.1; `git diff
  --check` limpo. Nenhum flash executado.
- Supersedes or discards: a pendência de build ESP-IDF registrada antes desta
  entrada (agora verificada); permanecem pendentes a validação física e a
  suíte/cobertura amplas, que são do tester/reviewer.

### 2026-11-09 20:5x -03:00 - delta do tester: comentário do singleton truncado

- Snapshot: HEAD `e23984d9500ac27e187dc3f8276841366c3f2779` + worktree sujo;
  `git diff | sha256sum` = `921e5184a7bceb61bd08644eab3f409fa6696b50381297a5a2f828d213486af5`
  (inclui as mudanças preexistentes de `docs/agent-state/image-formats/`).
- Fact: corrigido SOMENTE o texto que contradizia a implementação —
  `cyberdeck_shell_completion.h` (bloco de regras de domínio) agora registra a
  exceção: lista truncada com MAIS DE UM candidato apenas lista no segundo Tab e
  nunca insere prefixo; um ÚNICO candidato real completa mesmo com varredura
  truncada, porque não há ambiguidade. Comportamento não mudou (nenhuma linha de
  código funcional foi tocada; `apply()` e seus limites ficaram intactos).
- Fact: `code-map.md` (texto espelhado) atualizado com a mesma exceção.
  `README.md`/`README.pt-BR.md` não mencionavam truncamento, portanto seguem
  coerentes e não foram alterados.
- Fact (defeito de harness do próprio alvo, corrigido sem mudar comportamento):
  `MAKE_DEPFILES` recebia `$(SHELL_COMPLETION_DEPFILES)` (já com sufixo `.d`) e
  o `addsuffix .d` produzia `*.o.d.d`, então headers não entravam como
  dependência incremental. Trocado por `$(SHELL_COMPLETION_OBJS)` e removida a
  variável `.d` intermediária; `.d.d` órfãos removidos. Prova: após a edição do
  header, o alvo recompilou as 7 TUs (incluindo `completion_worker.o`, objeto
  adicionado pelo tester).
- Evidence: `make -C tests/host/keymap test_shell_completion` | exit 0 |
  `shell completion: 228 checks, 0 failures` (contagem subiu de 189 para 228 por
  cobertura do tester; recompilação real das 7 TUs) | 2026-11-09 |
  `git diff --check` | exit 0 | limpo. Nenhum outro gate executado; sem flash.
- Supersedes or discards: a redação anterior de
  `cyberdeck_shell_completion.h` ("lista truncada NUNCA estreita o token") e a
  mesma frase em `code-map.md`; ambas ficam substituídas pela versão com
  exceção do candidato único.

### 2026-11-09 21:00 -03:00 - rebuild do firmware exato para a etapa física

- Snapshot: HEAD `e23984d9500ac27e187dc3f8276841366c3f2779` + worktree sujo;
  `git diff | sha256sum` = `921e5184a7bceb61bd08644eab3f409fa6696b50381297a5a2f828d213486af5`
  (idêntico ao snapshot revisado pelo reviewer em development; desde então só a
  nota mudou, e ela é untracked).
- Fact: `source /home/moises/esp/esp-idf/export.sh && idf.py build` | exit 0 |
  2026-11-09 21:00 | IDF v6.1, target esp32p4. Recompilou
  `cyberdeck_shell_completion_scan.cpp.obj`,
  `cyberdeck_completion_worker.cpp.obj`, `cyberdeck_shell_session.cpp.obj` e
  `cyberdeck_ui.cpp.obj` (regra de truncamento no header incluída no binário).
  `cyberdeck5.bin` = 0x2aca30 (2.804.272 B), 33% livre na maior partição
  (0x400000), sha256 `dfcf0d104257365e54b34cb1c5c7aabbc8983fbd5cf4f7be11bf284be933544c`.
  Único warning: libssh pré-existente (`kill is not implemented`). Bootloader
  0x5b80 com 5% livre.
- Fact: versão embutida confirmada por extração do `esp_app_desc_t` no binário
  (magic `0xABCD5432` em 0x20): `version=1.18.1`, `project_name=cyberdeck5`,
  `idf_ver=v6.1`, build `20:39:31 Nov  9 2026`; `version.txt` = 1.18.1 e
  `build/project_description.json` `project_version` = 1.18.1.
- Fact: nenhum flash executado; nenhuma suíte rodada nesta entrada (os testes
  focados já estavam verdes no snapshot atual).
- Supersedes or discards: o binário `ceda75850c2bae85...` citado na entrada de
  2026-11-09 (build anterior, antes da correção do comentário e do harness) como
  referência para validação física; o artefato válido agora é
  `build/cyberdeck5.bin` sha256 `dfcf0d10...` com `version=1.18.1`.

### 2026-11-09 21:42 -03:00 - delta: token fisico `tab` do M5Stack A164 (bug medido no hardware)

- Snapshot: HEAD `e23984d9500ac27e187dc3f8276841366c3f2779` + worktree sujo;
  `git diff | sha256sum` = `e41cb9e82e0697d4e3bb132e73868f70b1438674bdf041901031113473fc8af8`
  (cobre apenas tracked; os arquivos novos de completacao seguem untracked, como
  nas entradas anteriores).
- Evidencia (tester, Tab5 1.18.1): usuario digitou `hel` + Tab e a linha nao
  mudou; log `2026-11-09 21:16:10  W (760221) tab5_kb: Evento nao mapeado:
  len=3 'tab'`. Nenhum panic/reboot no mesmo log.
- Causa na fonte (confirmada): `tab5_keyboard_keys.cpp` mapeava apenas o byte
  `"\t"`; o firmware do teclado envia o NOME `tab` (3 bytes) e
  `tab5_keyboard.cpp::emit_event()` so tem fallback para token de 1 byte, logo
  o evento era descartado antes de qualquer rota de sessao.
- Achado que corrigiu o patch proposto: adicionar APENAS o alias
  `{"tab", 0, '\t', TAB5_KEY_CHAR}` nao bastava — `emit_event()` entregava o
  token CRU como texto (`len=3`), entao a UI inseriria literalmente `tab` em vez
  de completar. Correcao minima em dois pontos do mesmo seam de entrada:
  1. `tab5_keyboard_keys.cpp`: variante nomeada `{"tab", 0, '\t', TAB5_KEY_CHAR}`
     (paridade com `{"\t"}`).
  2. `tab5_keyboard.cpp::emit_event()`: variantes NOMEADAS de `TAB5_KEY_CHAR`
     (token com `copy != 1`) passam a entregar o caractere mapeado
     (`text = &mapped`, `text_len = 1`); token de 1 byte permanece intacto.
  Resultado no fio: a UI recebe exatamente um `'\t'`, exercitando a rota ja
  testada `insert_physical_text("\t",1)` -> `begin_completion()` no MENU e
  `'\t'` literal em SSH/senha/host key. Worker, logica de completion, gramatica,
  SSH e ponte NAO foram tocados.
- Consequencia aceita e delimitada: onde o Tab fisico antes era descartado
  (editor de codigo, campo de comando da musica), ele passa a chegar como
  caractere Tab/`LV_KEY` correspondente; nenhum comportamento novo foi
  adicionado a esses apps.
- Correcao lateral de mesmo mecanismo (nao bloqueante, ja coberta pelo teste):
  a variante nomeada `space` tinha o mesmo defeito (inseriria `space`); a
  normalizacao no emissor resolve as duas.
- Testes escritos/atualizados (focados):
  - `tests/host/keymap/test_keymap.cpp`: `check_char("tab", '\t')`, `tab` na
    whitelist `known[]` e par de alias `{"tab", "\t"}` (paridade de tipo/lvgl/ch
    e entrada distinta).
  - `tests/host/keymap/test_keyboard_input_contract.py`: exige a entrada
    nomeada `tab` -> `TAB5_KEY_CHAR` com `'\t'` e o emissor entregando o char
    mapeado (`entry->ch`), rejeitando o token cru; dependencias do alvo no
    `Makefile` ampliadas para `tab5_keyboard.cpp` e `tab5_keyboard_keys.cpp`.
- Versao (AGENTS.md, confirmado antes de aplicar): `git tag --list` esta VAZIO
  (nao existe `v1.18.1` nem qualquer tag), portanto a mudanca compartilha o
  MINOR corrente em vez de abrir 1.19.0 sem rollover: `version.txt`
  1.18.1 -> **1.18.2**. `MAJOR`/`MINOR` intactos. Manifesto
  `cyberdeck.shell` permanece 1.0.1: a correcao e do firmware base/entrada, o
  app de shell nao mudou.
  Sweep do valor anterior: `grep -rn "1\.18\.1" README.md README.pt-BR.md
  code-map.md docs/ARCHITECTURE*.md tests/manual components/cyberdeck/include`
  -> unica ocorrencia viva era o titulo da secao do `code-map.md`, atualizada
  para 1.18.2; a restante e historica e explicita ("defeito medido no Tab5
  1.18.1").
- Documentacao: `code-map.md` com o paragrafo do seam do token fisico, o titulo
  da secao do Tab em 1.18.2 e as linhas de entrada/`emit_event` nas tabelas de
  input e da matriz producao -> testes host. `README*` nao citam o token nem a
  versao, portanto seguem coerentes.

### 2026-11-09 21:42 -03:00 - comandos e resultados deste delta

- `make -C tests/host/keymap test_keymap` | exit 0 | `PASS: tab5_keymap_lookup
  (664 checks)` | 2026-11-09 21:40.
- `make -C tests/host/keymap test_keyboard_input_contract` | exit 0 | `PASS:
  keyboard async-dispatch contract` | 2026-11-09 21:41 (a primeira execucao
  falhou por o seletor casar a linha de comentario; filtro corrigido para exigir
  `TAB5_KEY_CHAR`, sem afrouxar a assercao).
- `make -C tests/host/keymap test_shell_session` | exit 0 | `shell session: 249
  checks, 0 failures` | 2026-11-09 21:41 (Tab fisico/virtual, SSH/senha/Wi-Fi/BLE
  preservados).
- `make -C tests/host/keymap test_ota_contract` | exit 0 | PASS com
  `version.txt` 1.18.2 | 2026-11-09 21:41.
- `make -C tests/host/keymap test_image_viewer_contract` | exit 0 | `0
  failures` | 2026-11-09 21:41.
- `git diff --check` | exit 0 | limpo | 2026-11-09 21:41.
- `source /home/moises/esp/esp-idf/export.sh && idf.py build` | exit 0 |
  2026-11-09 21:41 | IDF v6.1, target esp32p4. Recompilou e religou;
  `build/cyberdeck5.bin` = 0x2aca50 (2.804.304 B), 33% livre em 0x400000,
  sha256 `86577c212c8a311391781bc017ee89b3d1e9c2935197a764e0fd6af3dcaa4f97`;
  `esp_app_desc_t` extraido do binario: `version=1.18.2`,
  `project_name=cyberdeck5`, `idf_ver=v6.1`; unico warning e o pre-existente do
  libssh (`kill is not implemented`). Nenhum flash executado.
- Nao executados por este papel: suite host completa, cobertura/lint globais,
  QEMU/simulador, flash e re-teste fisico.
- Supersedes or discards: substitui como referencia de validacao fisica o
  binario `dfcf0d10...` (`version=1.18.1`) da entrada de 21:00; o artefato a
  gravar agora e `86577c21...` com `version=1.18.2`. Nao invalida os resultados
  de host/build registrados antes (a suite focada foi reexecutada e segue verde).
- Proximo (tester): reexecutar os focados `test_keymap`,
  `test_keyboard_input_contract`, `test_shell_session`; flash autorizado do
  `build/cyberdeck5.bin` 1.18.2 e repetir `hel` + Tab no teclado fisico,
  esperando ausencia de `Evento nao mapeado: len=3 'tab'` e completacao para
  `help`. Reviewer final so depois do hardware.

### 2026-11-09 21:47 -03:00 - alias fisico restrito ao token exato `tab` (replanejamento)

- Supersedes or discards: substitui o patch generico anterior de `emit_event`,
  que normalizava TODO `TAB5_KEY_CHAR` nomeado (incluindo `"space"`) para o
  caractere mapeado. Essa normalizacao ampla nao estava aprovada e foi
  descartada; o restante da entrega (sessao, worker, completion, UI, docs)
  permanece intacto.
- Producao (`components/cyberdeck/src/platform/input/tab5_keyboard.cpp`,
  `emit_event`): unica normalizacao e `copy == 3 && strcmp(buf, "tab") == 0`,
  que entrega `'\t'` com `text_len = 1`. O default continua `text = buf` /
  `text_len = copy`, portanto `"space"` e qualquer outro token `TAB5_KEY_CHAR`
  seguem crus como antes (mesmo argumento `entry->ch` e mesmo `modifier`).
  `LV_KEY_NEXT` nao foi usado; worker/completion nao foram tocados. Nenhuma
  outra behavior de named char mudou (comparado com HEAD `e23984d`, o diff de
  `tab5_keyboard.cpp` e apenas esse bloco).
- Testes: `test_keymap.cpp` mantem `tab` -> `'\t'` e ganhou prova negativa
  (`tab` != `space`, `space` continua CHAR `' '` com `lvgl_key == 0`);
  `test_keyboard_input_contract.py` agora exige o literal unico
  `strcmp(buf, "tab")`, um unico site de normalizacao (regex `== ["tab"]`),
  `text = buf`/`text_len = copy` como default e ausencia de `"space"`/`char
  mapped` no codigo (comentarios removidos antes da checagem). O caminho
  textual->sessao (`insert_physical_text("\t", 1)` completando `rmd` -> `rmdir`)
  ja estava coberto em `test_shell_session.cpp` e segue verde.
- Regressao por mutacao em copia fora do worktree (mesmo contrato): normalizacao
  generica -> FAIL; caso especial `"space"` -> FAIL; remover o alias -> FAIL;
  arvore real -> PASS.
- `code-map.md`: linha de `tab5_keyboard.cpp` e as duas linhas da matriz de
  entrada/`emit_event` atualizadas para o alias restrito e a prova negativa.
  `version.txt` permanece 1.18.2 (PATCH de app ja aplicado); nenhum segmento
  adicional foi incrementado.
- Comandos desta entrada: `test_keymap` PASS (670 checks);
  `test_keyboard_input_contract` PASS (primeira execucao falhou porque o texto
  de comentario citava `"space"`; a checagem passou a ignorar comentarios, sem
  afrouxar a assercao); `test_shell_session` PASS (249 checks);
  `git diff --check` limpo; `idf.py build` exit 0, `build/cyberdeck5.bin`
  0x2aca60 (2.804.320 B), 33% livre, sha256
  `81248dd79b150855e67814b89e586f3bc911bcda54181110cc6cfc4c1d519f60`,
  `version=1.18.2`, `project_name=cyberdeck5`; sem flash.
- Pendente para validacao independente: repetir os tres focados e a validacao
  fisica (`hel` + Tab) com o binario `81248dd7...`; suite completa/cobertura/
  flash nao foram executados por este papel.

### 2026-11-09 21:50 -03:00 - correcao de redacao do code-map (delta do tester)

- Supersedes or discards: supera a descricao do paragrafo "Seam do token fisico
  da tecla Tab" (`code-map.md`, secao da UI/entrada, ~linhas 1746-1757), que
  ainda dizia que `emit_event()` entregava o caractere mapeado para variantes
  NOMEADAS de `TAB5_KEY_CHAR`. A implementacao efetiva converte somente o token
  exato `"tab"` (`copy == 3 && strcmp(buf, "tab") == 0`) em `'\t'`; `"space"` e
  os demais tokens seguem crus com o mesmo `entry->ch`.
- Alteracao: apenas essa descricao no `code-map.md` (texto do seam + cobertura
  citando a prova negativa de `"space"` e a ausencia de normalizacao generica).
  Nenhum codigo, teste, versao (`version.txt` = 1.18.2) ou nota de outro papel
  foi tocado.
- Verificacao deste delta: `git diff --check` limpo; HEAD
  `e23984d9500ac27e187dc3f8276841366c3f2779`; `git diff | sha256sum` =
  `e4a2114586a95ded27b151e96a7f060a81136dc7384e13588fdf9d0fb7dccd2c`;
  `sha256sum code-map.md` =
  `0ead1220e8bb41978de14844873c1fa4f3a6a206334db0930e5f13f11cd33491`.
  Nenhum teste ou build executado nesta rodada, conforme dispatch; permanecem
  validos os resultados de 21:47 (`test_keymap` 670 checks,
  `test_keyboard_input_contract`, `test_shell_session` 249 checks, build
  1.18.2 sha256 `81248dd7...`) apenas como historico da arvore anterior a esta
  edicao de documentacao.
- Pendente: tester repetir os tres focados e a validacao fisica (`hel` + Tab)
  com o binario `81248dd7...`; build/testes nao foram reexecutados apos esta
  mudanca de texto.

### 2026-11-10 14:19 -03:00 - reset: porta estreita de display, formato e CI (bloqueio do reviewer final)

- Supersedes or discards: substitui a afirmacao das entradas anteriores de que o
  worker `cyberdeck_completion_worker.cpp` entrega via `lv_async_call` +
  `lvgl_port_task_wake` diretamente. Essa forma violava `test_window_manager_contract.py`
  (TEST-WM-05: TU de app nao pode incluir/chamar LVGL) e derrubava o agregado
  (`exit 2`). Tambem substitui os hashes/binarios citados antes como referencia
  de validacao fisica: a arvore mudou (ver snapshot abaixo). Os demais fatos
  historicos (lease READ, fila bounded, token monotônico, alias `"tab"`,
  `version.txt` 1.18.2, relato fisico qualitativo) seguem validos.
- Snapshot: HEAD `e23984d9500ac27e187dc3f8276841366c3f2779` + worktree sujo.
  `git diff | sha256sum` = `11aeb819208b1ecd324db7f12fe025e844f990688b1ccfe8a80dcb0d4e1db807`
  (tracked). Incluindo os arquivos novos relevantes (untracked, excluindo
  `docs/agent-state/`): `e24a05d1e6bbdea605c076fe4b5fbb4f6ffde6c8dcb19658c1d13bad6f169f71`.
  `docs/agent-state/image-formats/*` permanece intocado.
- Producao nova: `components/cyberdeck/include/platform/display/cyberdeck_ui_post.h`
  + `src/platform/display/cyberdeck_ui_post.cpp` — porta estreita de despacho
  para a task do display. Header sem tipo LVGL (`using callback = void (*)(void
  *payload)` + `bool post(callback, void*)`); a implementacao e agora a UNICA
  superficie com `lv_async_call` + `lvgl_port_task_wake` para entrega
  assincrona: agenda primeiro, acorda depois; recusa (`LV_RESULT_INVALID` ou
  callback nulo) retorna `false` antes de qualquer wake e nunca assume a posse
  do payload. TU registrada em `components/cyberdeck/CMakeLists.txt`.
- Worker (`cyberdeck_completion_worker.cpp`): trocou os includes de
  `esp_lvgl_port.h`/`lvgl.h` por `platform/display/cyberdeck_ui_post.h` e agenda
  por `cyberdeck_ui_post::post(deliver_result, completed)` sob o mutex de ciclo
  de vida; libera o payload quando `!scheduled`. Fila bounded 4, stack 8192,
  lease READ, `deliver_result` gated (closing/flag/callback), join bounded e
  `quiesce`/`teardown` INALTERADOS. Nenhum `lv_*`, `lvgl` ou `esp_lvgl_port`
  restou no TU de app; `test_window_manager_contract.py` (TEST-WM-05) PASS sem
  allowlist e sem enfraquecer a assercao.
- Teste comportamental da porta em `tests/host/keymap/test_shell_completion.cpp`
  (shim `shim/full`, 14 checks novos): call enfileirado com o payload opaco
  intacto, wake contado somente apos agendamento aceito, recusa nao enfileira/
  nao acorda/nao executa inline, callback nulo recusado. O alvo compila a TU da
  porta (`test_shell_completion/ui_post.o`, `-Ishim/full`), mantendo a TU nova
  coberta pelo `tools/coverage_scope_guard.py` SEM allowlist.
- Contrato estrutural do worker atualizado (`test_shell_completion_worker_contract.py`):
  seções de teardown/start/quiesce restauradas e novas invariantes da porta
  (ordem agendamento->wake, recusa sem wake, payload nunca liberado pela porta,
  header sem `lv_`/`esp_`/`freertos`, worker sem `lvgl`/`lv_`/`esp_lvgl_port`/
  `LV_RESULT`, wiring do objeto `ui_post.o` no Makefile/BINS). 21 mutacoes
  rejeitadas (antes 14).
- Formato e CI: `clang-format -i` aplicado aos 7 arquivos novos de
  completion/porta/teste (todos agora `--dry-run --Werror` limpos) e, no
  `.github/workflows/quality-gate.yml`, o step "Format C++ and Python" passou a
  listar esses 7 arquivos; as duas TUs legadas de teclado entram apenas por
  ranges delimitados (`--lines=126:143` em `tab5_keyboard.cpp` e `--lines=33:34`
  em `tab5_keyboard_keys.cpp`, mesmo criterio do `ble_mgr.cpp`). O step cppcheck
  ganhou `cyberdeck_shell_completion_scan.cpp`, `cyberdeck_completion_worker.cpp`,
  `cyberdeck_ui_post.cpp`, `tab5_keyboard.cpp` e `tab5_keyboard_keys.cpp`
  (todos verificados localmente exit 0 antes de entrar no gate).
- `code-map.md`: worker descrito com a porta, nova secao e linha de tabela para
  `cyberdeck_ui_post.{h,cpp}`, paragrafo de testes atualizado (porta + contrato
  com 21 mutacoes). `version.txt` NAO mudou (1.18.2): sem mudanca de
  comportamento, constante ou app.

### 2026-11-10 14:19 -03:00 - comandos e resultados desta rodada

- `make -C tests/host/keymap test_shell_completion` | exit 0 | `shell completion:
  242 checks, 0 failures` (228 anteriores + 14 da porta) | 2026-11-10.
- `make -C tests/host/keymap test_shell_completion_worker_contract` | exit 0 |
  `PASS: shell completion worker contract (21 mutations rejected)`.
- `make -C tests/host/keymap test_window_manager_contract` | exit 0 | `PASS:
  window manager structural contract (TEST-WM-03/05/06/07, TEST-REG-8-BAR,
  TEST-REG-8-CHROME)` + `PASS: TEST-WM-03/06/07 wiring contract (9 mutations
  rejected)`. Restam 2 `FINDING` nao bloqueantes e pre-existentes: includes
  `lvgl.h` ociosos em `cyberdeck_control_server.cpp` e
  `cyberdeck_shell_utils.cpp` (divida adjacente, fora do escopo).
- `make -C tests/host/keymap test_shell_session` | exit 0 | `shell session: 249
  checks, 0 failures`.
- `make -C tests/host/keymap test_keymap` | exit 0 | `PASS: tab5_keymap_lookup
  (670 checks)`.
- `make -C tests/host/keymap test_keyboard_input_contract` | exit 0 | `PASS:
  keyboard async-dispatch contract`.
- Formato: `clang-format --dry-run --Werror` exit 0 nos 7 arquivos novos e nos
  dois ranges de teclado (clang-format local 23.1.4; CI pina 23.1.3 — diferenca
  de patch nao exercitada localmente). `cppcheck --enable=warning,performance,
  portability --error-exitcode=1 --suppress=missingIncludeSystem` exit 0 nos 3
  TUs de completion/porta e nos 2 TUs de teclado.
- `git diff --check` | exit 0 | limpo.
- `source /home/moises/esp/esp-idf/export.sh && idf.py build` | exit 0 |
  ESP-IDF 6.1, target esp32p4; compilou
  `cyberdeck_ui_post.cpp.obj` e `cyberdeck_completion_worker.cpp.obj`;
  `build/cyberdeck5.bin` = 2.804.352 B, sha256 `f96b28c851d59cf737d3...`;
  unico warning e o pre-existente do libssh (`kill is not implemented`).
  Nenhum flash executado.
- Nao executados por este papel: suite host completa, cobertura global, CI
  remoto, QEMU/simulador, flash e re-teste fisico.

### 2026-11-10 14:19 -03:00 - pendencias para validacao independente

- Tester: reexecutar `test_shell_completion` (242), worker contract (21
  mutacoes), `test_window_manager_contract`, `test_shell_session`, `test_keymap`
  e `test_keyboard_input_contract`; revisar as novas assercoes da porta
  (ordem/wake/posse do payload) e o `code-map.md`.
- Reviewer final: agregado `make -C tests/host/keymap clean test` (o bloqueio de
  TEST-WM-05 agora deve passar), cobertura 80/90/50 + `coverage_scope_guard.py`
  (a TU `cyberdeck_ui_post.cpp` esta no gcovr via
  `test_shell_completion`, sem allowlist), formato/lint/build no snapshot
  `11aeb819...`/`e24a05d1...`; CI remoto somente se autorizado.
- Fisico: o binario `f96b28c8...` NAO foi gravado; o relato qualitativo de
  `hel`+Tab no 1.18.2 (`81248dd7...`) continua como `evidence_kind: relato` e
  nao valida este snapshot.
- Nota gravada por append manual (tool MCP `note_coder_append` indisponivel
  nesta sessao); carimbo e caminho foram medidos localmente.

### 2026-11-10 14:20 -03:00 - correcao de snapshot e fechamento da rodada

- Supersedes or discards: substitui os hashes de snapshot registrados na entrada
  de 14:19. Depois deles eu editei `code-map.md` (linhas da matriz
  producao -> testes host: 189 -> 242 checks e 14 -> 21 mutacoes), portanto o
  snapshot valido desta arvore e: HEAD
  `e23984d9500ac27e187dc3f8276841366c3f2779` + worktree sujo;
  `git diff | sha256sum` =
  `24ba180496378ae6538269a434e50d2c940ce802e68803677a11a0e8d9967680` (tracked) e
  `ad4347d2641800c52323ada1648cd9998a0396e9e4a0a1be00f76c378527082f` incluindo os
  arquivos novos relevantes (untracked, excluindo `docs/agent-state/`). Os
  resultados de teste/build da entrada anterior continuam validos para esta
  arvore porque nenhuma fonte, header, Makefile ou CMake foi tocado depois
  deles — apenas documentacao.
- Reexecutado APOS a edicao do `code-map.md` (contratos que parseiam o mapa):
  `make -C tests/host/keymap test_window_manager_contract` exit 0 (`PASS`
  estrutural + `PASS` wiring, 9 mutacoes),
  `make -C tests/host/keymap help_unification_contract` exit 0 e
  `make -C tests/host/keymap test_shell_help_contract` exit 0.
- `git diff --check` exit 0 (limpo) apos todas as edicoes.
- Checkpoint final: implementacao, testes focados, formato, escopo de CI,
  `code-map.md` e build IDF concluidos nesta arvore. Pendem apenas a validacao
  independente (tester/reviewer), a suite agregada/cobertura do reviewer e o
  gate fisico, que este papel nao executa.
- Evidencia adicional da mesma arvore (regressao da formatacao dos headers):
  `make -C tests/host/keymap test_shell_app` exit 0 (`PASS: shell application
  288 checks`), `test_edit_line` exit 0 (`PASS: cyberdeck_edit_line 1538
  checks`) e `test_shell_help` exit 0 (`shell help: 249 checks, 0 failures`).
