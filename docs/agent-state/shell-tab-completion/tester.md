# tester - shell-tab-completion

- Role: tester
- Updated: 2026-11-09 20:52 -03:00
- Code state: HEAD `e23984d9500ac27e187dc3f8276841366c3f2779` + worktree sujo. Snapshot de produção (inalterado por este papel): `git diff -- components version.txt | sha256sum` = `4c179118942cb01185bb0af6da4d61ae7b4fdfaa9ca707015bbc65cf5c27d757`. Diff composto após as mudanças de teste: `ef6b97f8c74befeb3ee280b18cff767a73c51a9ea162e5a573b56cc9ec67797d` (o despacho registrou `e11bb31a...` antes das minhas edições; a diferença está só em `tests/host/keymap/{Makefile,test_shell_completion.cpp,test_shell_session.cpp,test_shell_completion_worker_contract.py}` e `code-map.md`).
- Note history: o despacho anterior terminou por limite de steps **sem escrever esta nota**; o conteúdo abaixo consolida aquele avanço (checado de novo) e o delta posterior.

## Dispatch objective

Validação independente do autocomplete por Tab (REQ-TAB-01..05) antes do reviewer
development, sem tocar produção e sem flash: reexecutar evidência focada,
conferir rastreabilidade AC -> TEST, acrescentar testes focados onde houver
lacuna material e registrar lacunas/riscos. Autorizado: mexer apenas em testes,
contratos e Makefile de testes.

## Work done

### 1. Inspeção independente (fonte, não relatório)

Conferidos: `cyberdeck_shell_completion.h` (puro), `cyberdeck_shell_completion_scan.cpp`
(único I/O), `cyberdeck_completion_worker.{h,cpp}`, `cyberdeck_shell_session.{h,cpp}`
(diff completo), `cyberdeck_shell_app.cpp`, `cyberdeck_edit_line.{h,cpp}`,
`cyberdeck_ui.cpp` (diff completo), `CMakeLists.txt`, `Makefile`, README/README.pt-BR,
`code-map.md`.

### 2. DEFEITO DE TESTE (corrigido, só harness)

`tests/host/keymap/Makefile`: `SHELL_COMPLETION_DEPFILES` era definido **depois** de
`MAKE_DEPFILES :=` (expansão imediata), então `-include $(MAKE_DEPFILES)` não recebia
nenhum depfile do alvo novo. Prova: `touch cyberdeck_edit_line.h` recompilava
`test_edit_line` (3 compilações) e **0** em `test_shell_completion`. Corrigido movendo a
definição para antes de `MAKE_DEPFILES`; após o ajuste o mesmo `touch` gera 6 compilações
+ link. Sem isso, qualquer edição de header rodaria binário velho em silêncio.

### 3. DEFEITO DE GATE (corrigido dentro do escopo de teste)

`components/cyberdeck/src/apps/shell/cyberdeck_completion_worker.cpp` é TU de produção
nova com corpo sob `#if defined(ESP_PLATFORM)` e **não era compilada por nenhum alvo
host**; `tools/coverage_scope_guard.py` (`missing = production - covered - ALLOWLIST`)
a rejeitaria ("production TUs missing from gcovr JSON"), e `FORBIDDEN_ALLOWLIST` já
proíbe o padrão allowlist para o `cyberdeck_cat_worker.cpp` equivalente. Correção no
lado de teste: o TU real é compilado pelo alvo focado (`test_shell_completion/completion_worker.o`),
o que o coloca no relatório de cobertura **e** permite exercitar a metade host.
Prova de instrumentação (focada, sem cobertura global): compilação com `--coverage` +
execução do probe gerou `worker.gcda` com `Lines executed:100.00% of 5`.

### 4. Testes acrescentados/ajustados (nenhuma asserção removida ou enfraquecida)

`tests/host/keymap/test_shell_completion.cpp` — 189 -> 228 checks:

- TEST-TAB-01 token/cursor: `token_at` com cursor interior; `classify` de token entre
  dois argumentos; conclusão a partir do início/interior do token; cauda da linha
  preservada; cursor pós-inserção exato; prefixo comum substituindo só o intervalo.
- TEST-TAB-02 comandos/opções: verbo com espaço à esquerda, linha só de espaços,
  token de opção não completado, operando vazio após opção, caminho absoluto após
  opção (`ls -a /da` -> `ls -a /data/`), verbos sem operando de caminho.
- TEST-TAB-03 limites (borda nos dois lados): token em `k_max_token_bytes` aceito ×
  +1 recusado; nome em `k_max_name_bytes` aceito × +1 recusado (classify e scanner);
  diretório virtual acima de `k_max_directory_bytes` recusado (resolver e scanner).
- TEST-TAB-04 truncamento: multi-candidato truncado nunca estreita × sem truncamento
  estreita para o prefixo comum; nota de truncamento é a última linha da lista e
  ausente em lista completa; lista termina em newline.
- TEST-TAB-05 worker (metade host, TU real linkado): `start` recusa, `enqueue` recusa
  inclusive token 0, `quiesce` imediato, `teardown` idempotente, com o coordenador SD
  ligado via `bind_sd_io`.

`tests/host/keymap/test_shell_session.cpp` — 227 -> 249 checks:

- Tab **físico** (`insert_physical_text("\t",1)`) literal em SSH sem tocar a porta;
  texto físico multi-byte com `\t` embutido não dispara completion.
- Cursor no meio do token no nível da sessão (linha e cursor finais).
- Resultado **truncado** (novo campo do fake): não estreita, segundo Tab lista e emite
  a nota; cache do segundo Tab, terceiro Tab reenumera, edição invalida o cache,
  candidato único não deixa lista e reenumera no Tab seguinte.

`tests/host/keymap/test_shell_completion_worker_contract.py` (novo, 14 mutações
rejeitadas): contrato estrutural do corpo ESP_PLATFORM — lease READ antes da varredura
e varredura só sob lease, resultado vazio quando o lease/scan falha, `enqueue`
não bloqueante com token 0/bounds/`memcpy` exatos, entrega gated por
mutex/closing/delivery/callback com payload liberado em todos os caminhos, worker
publica `s_stopped` e libera payload quando o `lv_async_call` falha, teardown desabilita
entrega antes do lock, invalida callback antes de acordar, join bounded antes de
`vQueueDelete`, `start` idempotente/recusa após shutdown/drena token de parada/stack
>= 4 KiB derivada, `quiesce` fecha e para, header puro sem `dirent/lvgl/esp/freertos`,
wiring da UI (porta exige `ready`, bind, start após cat worker, teardown no destroy,
forward de token/items/count/truncated) e presença do TU host no Makefile/BINS.
Sensibilidade provada por mutação in-memory (12 no worker/UI/header + 2 no Makefile).

`code-map.md`: seção de testes do autocomplete atualizada (novo contrato, 228/249
checks, metade host do worker e motivo da compilação do TU).

### 5. Reconciliação "lista truncada nunca estreita" × singleton

Semântica: o único produtor de `truncated` com 1 candidato é o teto
`k_max_scan_entries` (linha 166 do scanner); `match_commands` só trunca com 65+
matches (64 candidatos). Em `apply()` a exceção `truncated && candidates.size() != 1`
(linha 282) completa o singleton. Isso **não é defeito de comportamento**: o nome
inserido é uma entrada real do diretório, a linha continua executável, não há
perda/corrupção, e o README já promete "um único candidato completa o token".
O efeito limitado é de descoberta (com >512 entradas, o usuário não vê que existe
outro match fora da janela). Conclusão: divergência **documental Low** — o comentário
do header (linha 34: "lista truncada NUNCA estreita o token: com incerteza so listamos")
está mais rígido que o contrato publicado. Ação recomendada ao coder: ajustar **só o
comentário** para declarar a exceção do singleton (sem mudar comportamento); se
preferirem a regra estrita, é mudança de comportamento e precisa de decisão do
developer. O teste fixa o comportamento atual com essa justificativa
(`test_truncated_single_candidate_boundary`).

## Evidence

| comando | exit | resultado | head | timestamp |
| --- | --- | --- | --- | --- |
| `git rev-parse HEAD` + `git diff \| sha256sum` | 0 | `e23984d...` / `e11bb31a...` (pré-edição), depois `ef6b97f8...` | `e23984d` | 2026-11-09 20:41/20:49 -03:00 |
| `make -C tests/host/keymap test_shell_completion` | 0 | `shell completion: 228 checks, 0 failures` (baseline do coder: 189) | `e23984d` | 2026-11-09 20:49 -03:00 |
| `make -C tests/host/keymap test_shell_session` | 0 | `shell session: 249 checks, 0 failures` (baseline: 227) | `e23984d` | 2026-11-09 20:46 -03:00 |
| `make -C tests/host/keymap test_shell_completion_worker_contract` | 0 | `PASS: shell completion worker contract (14 mutations rejected)` | `e23984d` | 2026-11-09 20:52 -03:00 |
| `make -C tests/host/keymap test_edit_line` | 0 | `PASS: cyberdeck_edit_line (1538 checks)` (set_line não regrediu) | `e23984d` | 2026-11-09 20:50 -03:00 |
| `make -C tests/host/keymap test_coverage_scope_guard` | 0 | `coverage scope guard tests passed` (a linha "invalid allowlist" é fixture contrafactual) | `e23984d` | 2026-11-09 20:50 -03:00 |
| probe `--coverage` + `gcov` do worker | 0 | `worker.gcda` gerado, `Lines executed:100.00% of 5` | `e23984d` | 2026-11-09 20:48 -03:00 |
| `touch cyberdeck_edit_line.h && make -n test_edit_line` × `test_shell_completion` | 0 | 3 compilações × 0 (defeito), depois 6+link (corrigido) | `e23984d` | 2026-11-09 20:41 -03:00 |
| `git diff --check` | 0 | limpo | `e23984d` | 2026-11-09 20:52 -03:00 |

Não executados por este papel: suíte host completa, cobertura global/gcovr do agregado,
lint/format global, `idf.py build`, flash e qualquer interação física.

## Decisions and assumptions

- Sensibilidade de borda sem mutar produção: cada limite é exercitado dos **dois lados**
  (valor no limite aceito × limite+1 recusado), então uma inversão de comparação muda o
  resultado de exatamente uma asserção. Para o truncamento, o par é
  multi-candidato-truncado (não estreita) × um-candidato-truncado (estreita).
- REQ-TAB-01..05 foram tomados do despacho e do `code-map.md`; a matriz REQ/AC/TEST do
  planner não está em `planner.md` (ela ficou no retorno do despacho dele). Rastreabilidade
  abaixo é a minha leitura conferida no código; se o developer tiver os AC literais,
  vale conferir os dizeres de AC-TAB-04/05.
- O contrato do worker é **estrutural** (corpo ESP_PLATFORM não roda no host): prova
  invariantes de ordenação/ownership, não comportamento em FreeRTOS/VFS real. A metade
  host (recusa fail-closed) é comportamental via `test_shell_completion`.
- Jev não utilizado: nenhuma escolha semântica em aberto que alterasse próxima ação;
  a reconciliação do item 5 é determinada por README + código.

## Traceability REQ -> TEST

| AC esperado | TEST |
| --- | --- |
| REQ-TAB-01 comandos completáveis (catálogo help + manifests, sem duplicata) | TEST-TAB-01 (consistência `kPathCommands` × `kCatalog`, prefixo comum, comando único, zero candidatos), TEST-TAB-09 (`kLegacyHelpPinned`, já existente) |
| REQ-TAB-02 caminhos no root virtual, `cd` só diretórios com `/`, `/sdcard` recusado | TEST-TAB-02, TEST-TAB-06 (`set_line`), TEST-TAB-10 (varredura real em fixture: files/dirs/ocultos/nome com espaço/symlink/namespaces), TEST-TAB-03 (limites de caminho) |
| REQ-TAB-03 primeiro Tab único/prefixo comum; segundo Tab lista bounded; zero matches preserva linha | TEST-TAB-01, TEST-TAB-04, TEST-TAB-11 (sessão: sequência, lista, zero candidatos, cache/reenumeração) |
| REQ-TAB-04 escopo MENU local; SSH/senha/Wi-Fi/BLE sem completion | TEST-TAB-12 (virtual e **físico** literal em SSH, senha, Wi-Fi, BLE dono, texto multi-byte com `\t`) |
| REQ-TAB-05 assíncrono fora da LVGL, bounded, stale/teardown, lease SD | TEST-TAB-05 (metade host), TEST-TAB-13 (token stale/replay/linha editada/porta recusando/truncado), TEST-TAB-14 (contrato estrutural do worker com 14 mutações) |

## Pending and risks

- **Validação física pendente (não executada, sem flash):** Tab real no teclado M5Stack;
  `LV_KEY_NEXT` do widget virtual; `cd`/`ls`/`cat` com caminhos reais no SD; lista de
  candidatos visível no terminal; ausência de bloqueio perceptível da UI durante a
  varredura; confirmação de que SSH/senha seguem literais. Passos: boot, `sys.info`,
  `ui.type "cd "` + Tab físico, `ui.type "ls alb"` + Tab, segundo Tab para listar,
  `ui.type "ssh ..."` + Tab (deve colar `\t`), `sys.info` final (uptime/heap). Binário
  de referência do coder: `build/cyberdeck5.bin` 2.804.272 B.
- **Comentário do header (produção, coder):** `cyberdeck_shell_completion.h:34` afirma
  "nunca estreita" enquanto `:282` abre a exceção do singleton. Ação: ajustar o texto
  (documentação), sem mudar comportamento, ou escalar se a regra estrita for desejada.
- Comentário em `code-map.md` precisa acompanhar essa decisão se o comportamento mudar.
- Riscos residuais herdados do coder, não medidos aqui: stack do worker (8192 B) e custo
  de `std::sort` no dispositivo; fence do poweroff depende do lease READ (sem stage
  dedicado); `readdir` limitado a 512 entradas por varredura.
- Gates amplos (suíte completa, gcovr/coverage global, `idf.py build`) ficam para o
  reviewer em modo final — inclusive o efeito real do TU novo no relatório de cobertura.

## References

Plano: `docs/agent-state/shell-tab-completion/planner.md`; implementação:
`docs/agent-state/shell-tab-completion/coder.md`. Produção:
`components/cyberdeck/include/apps/shell/{cyberdeck_shell_completion.h,cyberdeck_completion_worker.h}`,
`components/cyberdeck/src/apps/shell/{cyberdeck_shell_completion_scan.cpp,cyberdeck_completion_worker.cpp,cyberdeck_shell_session.cpp,cyberdeck_shell_app.cpp,cyberdeck_edit_line.cpp}`,
`components/cyberdeck/src/platform/display/cyberdeck_ui.cpp`.
Testes alterados/criados: `tests/host/keymap/{test_shell_completion.cpp,test_shell_session.cpp,test_shell_completion_worker_contract.py,Makefile}`.
Roteiro físico: `tests/manual/serial-bridge-validation.pt-BR.md`.

## Updates

### 2026-11-09 20:52 -03:00 - nota criada após despacho interrompido; delta de testes concluído

- Fact: nota substitui a ausência do despacho anterior (interrompido sem nota). Estado:
  `test_shell_completion` 228/0 (TU do worker compilado e executado), `test_shell_session`
  249/0, contrato estrutural do worker com 14 mutações rejeitadas, `test_edit_line` 1538/0,
  guard de cobertura com testes próprios PASS; defeito de depfile e defeito de gate de
  cobertura corrigidos no harness; divergência do comentário de truncamento classificada
  como documentação Low com ponto exato para o coder
  (`cyberdeck_shell_completion.h:34`). Produção não alterada por este papel
  (`git diff -- components version.txt | sha256sum` = `4c17911...`).
- Supersedes or discards: o retorno anterior que declarava "resultado bloqueado / nota não
  criada" e a pendência "chamar test_completion_worker_host_stubs em main()" (agora
  executada). Permanece aberta apenas a validação física e os gates amplos do reviewer.

### 2026-11-09 20:59 -03:00 - reexecução pós-reconciliação do coder (snapshot atualizado)

- Fact: HEAD `e23984d9500ac27e187dc3f8276841366c3f2779`; `git diff | sha256sum` =
  `921e5184a7bceb61bd08644eab3f409fa6696b50381297a5a2f828d213486af5`; snapshot de
  conteúdo incluindo arquivos NOVOS/untracked (`git diff` + `git ls-files -o
  --exclude-standard | cat`) = `764d339cea95b541734ce818e88b272ed2f934966417d78f09fc25546ae81fa8`.
  O snapshot anterior desta nota (`ef6b97f8...`) está superado: o coder reconciliou o
  comentário da regra de truncamento (`cyberdeck_shell_completion.h`: "lista truncada com
  MAIS DE UM candidato nunca estreita... exceção deliberada: um único candidato real
  completa", espelhado no `code-map.md`) e refez o wiring de depfiles do alvo
  (`MAKE_COMPLETION_DEPFILES` abandonado; `$(SHELL_COMPLETION_OBJS)` inlined em
  `MAKE_DEPFILES`), sem mudança funcional.
- Evidência reexecutada no estado atual (sem suíte completa, sem cobertura global, sem
  build, sem flash):
  - `make -C tests/host/keymap test_shell_completion test_shell_session test_shell_completion_worker_contract`
    | exit 0 | `shell completion: 228 checks, 0 failures` / `shell session: 249 checks, 0 failures`
    / `PASS: shell completion worker contract (14 mutations rejected)` | 2026-11-09 20:59 -03:00.
  - Wiring de depfiles revalidado após a mudança do coder: `touch cyberdeck_shell_completion.h`
    + `make -n test_shell_completion` gera 4 linhas `g++` (3 compilações dos TUs que incluem o
    header + link), não 0 — antes da correção o mesmo `touch` não reconstruía nada.
  - Âncoras de mutação do contrato continuam sendo encontradas no worker/UI/header, o que
    prova que o corpo funcional desses arquivos não mudou nesta rodada; o comportamento do
    singleton truncado continua asserido e verde em
    `test_truncated_single_candidate_boundary`, então a exceção documental agora bate com o
    código.
- Correção de método (para não induzir o próximo leitor): a linha "produção não alterada
  (`git diff -- components version.txt`) = `4c17911...`" da entrada anterior NÃO cobre os
  arquivos novos, porque `git diff` ignora untracked — `cyberdeck_shell_completion.h`,
  `cyberdeck_completion_worker.{h,cpp}` e `cyberdeck_shell_completion_scan.cpp` estão
  untracked. Fingerprint válido de agora em diante é o snapshot de conteúdo acima
  (`764d339c...`), que inclui os untracked; `4c17911...` permanece apenas como hash do
  diff rastreado.
- Supersedes or discards: supersede o snapshot `ef6b97f8...` e a pendência de comentário do
  header (fechada pelo coder). Permanente e único pendente: validação física (Tab real e
  `LV_KEY_NEXT`, `cd`/`ls`/`cat` no SD, SSH/senha literais, ausência de bloqueio da UI) e os
  gates amplos do reviewer (suíte completa, gcovr/coverage, `idf.py build`).

### 2026-11-09 21:06 -03:00 - validação física: flash 1.18.1 executado; Tab real DEFERIDO

- Fact (identidade do artefato): `build/cyberdeck5.bin` = 2.804.272 B, SHA-256
  `dfcf0d104257365e54b34cb1c5c7aabbc8983fbd5cf4f7be11bf284be933544c` (confere com o
  despacho), mtime `2026-11-09 21:00:26 -03:00` **inalterado após o flash** -> `idf.py flash`
  não recompilou nada e gravou exatamente esse binário. HEAD
  `e23984d9500ac27e187dc3f8276841366c3f2779`. Fingerprint código+testes (sem notas):
  `03c0c978078ab71e190a23da0b07c3c4dcb7ea3e8258059d41644f421ee3b202` (o snapshot de
  conteúdo da entrada anterior, `764d339c...`, mudou só porque esta nota foi apendada).
- Fact (flash, autorizado no despacho): `idf.py -p /dev/ttyACM0 flash` | exit 0 |
  `Hash of data verified` + `Hard resetting via RTS pin` + `Done` | 2026-11-09
  21:02-21:03 -03:00 | IDF 6.1 (`CONFIG_IDF_INIT_VERSION "6.1.0"`), ESP32-P4, app em
  `0x20000` (factory), 16 MB. Sem erase de flash/NVS, sem OTA, sem poweroff, sem escrita
  em SD.
- Fact (identidade em execução, medido pela ponte):
  - antes: `sys.info` -> `fw_version 1.18.0`, uptime `04:51:30`, `safe_mode false`,
    `interrupted_boots 0`.
  - depois: `fw_version 1.18.1`, IDF `v6.1`, uptime `00:00:20` -> `00:00:54` ->
    `00:02:02` (monotônico, sem reboot), `psram_free` ~31,38 MB estável,
    `internal_free 104015` e `internal_min_free 90468` constantes, `safe_mode false`,
    `interrupted_boots 0`. Boot com o worker de completação iniciado
    (`s_completion_worker_ready`) não alterou heap nem gerou panic/WDT observável.
- Fact (regressão funcional do shell no dispositivo, via `ui.type` + `term.dump`):
  `help` publica catálogo [terminal] + [app] (help/clear/app/pwd/cd/ls/cat/touch/mkdir/
  rm/rmdir/poweroff, wifi/ssh/bluetooth/battery/screen/log/music/image/edit/demo); `ls`
  lista a raiz do SD (`cat-multiline.txt`, `data`, `notas`, `screenshots`, ...); `pwd`
  -> `/`; `cd apps` -> prompt `/apps$` e `pwd` -> `/apps`; `cd /sdcard` ->
  `cd: invalid path` com cwd preservado em `/apps` (alias físico rejeitado pelo
  namespace virtual, coerente com a regra da completação); `cat cat-multiline.txt` ->
  5 linhas + `marcador-final`. Nenhum destes é aceite do recurso Tab; são regressão.
- Fact (caminho novo do input físico, medido): `ui.type $'a\tb'` (3 bytes, TAB embutido)
  -> linha exibida `a<TAB>b` + `unknown command; type help`, ou seja o guard
  `length == 1 && text[0] == '\t'` **não** capturou texto multi-byte (comportamento
  esperado). `ui.type $'\t'` (byte único) -> `ok:true`, terminal sem mudança visível e
  prompt limpo em seguida; prova apenas que o caminho de completação executou no device
  sem crash, reboot ou corrupção de linha (o primeiro Tab com prefixo vazio não estreita
  o token, então não há efeito observável).
- Limitação (DEFERIDO, não é aceite): **o efeito da completação não é injetável pela
  ponte**. `ui.type` sempre injeta Enter depois do texto, e Enter executa/limpa a linha e
  invalida o cache do segundo Tab (`clear_editor()` -> `clear_completion_state()`); a
  ponte não expõe injeção de texto sem Enter nem `LV_KEY_NEXT`/Tab como tecla especial
  (só ENTER/ESC/BACKSPACE). O teclado virtual LVGL não tem tecla Tab. Portanto ficaram
  DEFERIDOS: completar comando (`hel` -> `help`, `blu` -> `bluetooth`), caminho único,
  prefixo comum, segundo Tab listando, zero matches preservando linha, sufixo/cursor
  preservados e `cd` só diretórios no device.
- Instrução exata para o developer (validação humana, ~2 min, sem estado arriscado):
  1. teclado físico M5Stack, prompt `/$ `; digitar `hel` e pressionar Tab -> linha vira
     `help`; 2. Tab de novo sem editar -> nada a listar (candidato único); 3. digitar
     `ls ca` + Tab -> `ls cat-multiline.txt`; 4. digitar `c` + Tab (prefixo comum entre
     `cat`/`cd`/`clear`) e depois Tab de novo -> lista de comandos com o prefixo;
  5. digitar `zzz` + Tab -> linha intacta; 6. digitar `cd /sd` + Tab -> linha intacta;
  7. digitar `cd sc` + Tab -> `cd screenshots/` (diretório com `/` final); 8. digitar
     `ls cat-multiline.txt x` e posicionar o cursor antes do `x`, Tab -> sufixo ` x`
     preservado e cursor após o nome completado; 9. `ssh` conectado (opcional, sem
     autenticar em host novo): Tab deve inserir `\t` literal e não completar.
  Evidência esperada: `term.dump` (texto/linha) e `sys.info` antes/depois (uptime
  monotônico, `safe_mode=false`); qualquer panic/reboot encerra o cenário.
- Alternativa sem humano (mudança de PRODUÇÃO, decisão do coder/developer): expor na
  ponte uma injeção de texto sem Enter e/ou `special_key = LV_KEY_NEXT`
  (ex.: `ui.key next` ou `ui.type --no-enter`), o que permitiria automatizar todos os
  cenários acima. Não implementei: fora do escopo do tester.
- Classificação das evidências: MEDIDO = flash/identidade 1.18.1, boot estável,
  regressão de shell/help/namespace, guard de TAB multi-byte, byte único aceito sem
  crash. RELATO = nenhum. DEFERIDO = efeito funcional da completação e tecla física/
  `LV_KEY_NEXT`. Não há aceite declarado pelo usuário nesta etapa.
- Supersedes or discards: resolve parcialmente a pendência "validação física" da entrada
  anterior (flash + boot + regressão medidos); mantém aberto o efeito funcional do Tab no
  dispositivo. Gates amplos (suíte completa, gcovr/coverage) continuam no reviewer.

### 2026-11-09 21:15 -03:00 - captura serial passiva ativa (diagnóstico do Tab físico)

- Motivo: relato medido pelo usuário (não reproduzido por mim) de que digitar `hel` e
  pressionar Tab físico deixa a linha em `hel`, sem crash. Hipótese do worker está
  descartada: comandos completam de forma síncrona (nenhum scan). Falta observar o evento
  cru; a tarefa aqui é **só** capturar serial, sem flash, reset, teclas ou comandos.
- Precondition medida sem enviar tecla (21:14:07 -03:00, `python3 tools/cyberdeck_cli.py
  --port /dev/ttyACM0 sys.info`): `fw_version 1.18.1`, `idf_version v6.1`, uptime
  `00:10:35`, `safe_mode false`, `interrupted_boots 0`, `psram_free 31275384`,
  `internal_free 104015`. Porta livre (`fuser /dev/ttyACM0` sem holders).
- Opção segura confirmada na fonte: `idf.py monitor --no-reset` (IDF 6.1,
  `esp-idf-monitor 1.10.0`). `SerialReader.open_serial(reset)` só executa a sequência de
  reset quando `reset=True` (`esp_idf_monitor/base/serial_reader.py`), e `reset = not
  no_reset` vem de `idf_monitor.py`; com stdin não-TTY o `CommandReader` entra em modo
  não-interativo e, sem comandos, apenas observa ("watching serial output only"), sem
  escrever nada na porta. O manual (`tests/manual/serial-bridge-validation.pt-BR.md`,
  §4.3, linhas ~155-160) confirma que um segundo leitor concorrente rouba bytes — por
  isso **nenhum** comando da CLI foi executado depois de abrir o monitor.
- Captura ativa (não encerrar até orientação posterior):
  - comando: `/tmp/opencode/start-tab-capture.sh` (conteúdo: `source
    ~/esp/esp-idf/export.sh`; `cd ~/Projetos/cyberdeck5`; `exec idf.py -p /dev/ttyACM0
    monitor --no-reset --timestamps`), stdin `/dev/null`, `umask 077`.
  - PIDs: wrapper `idf.py` **523608** (PPID 1130) e monitor real
    **523693** (`python -m esp_idf_monitor -p /dev/ttyACM0 -b 115200 --toolchain-prefix
    riscv32-esp-elf- --target esp32p4 --revision 100 --decode-panic backtrace
    build/cyberdeck5.elf build/bootloader/bootloader.elf --no-reset --timestamps
    -m '...idf.py -p /dev/ttyACM0'`), que é o dono da porta (`fuser -v`).
  - log privado: `/tmp/opencode/tab-capture-20261109-211425.log` (modo 600, 395 B no
    início, `--- esp-idf-monitor 1.10.0 on /dev/ttyACM0 115200`,
    `--- Standard input is not a TTY, running in non-interactive mode...`,
    `--- No commands on standard input, watching serial output only...`).
  - verificação de não-reset: após ~25 s de monitor, `grep -cE
    "ESP-ROM|boot: |Project name|rst:"` no log = **0** e nenhuma linha de boot; o
    dispositivo não reiniciou ao abrir a captura.
- Limitação: com o monitor dono da porta eu **não** consigo rodar `sys.info` para provar
  continuidade de uptime; a prova de "sem reboot" durante a captura é a ausência de
  banner de boot/panic no log (o monitor decodifica backtrace se houver panic). Uptime
  pós-captura só será verificado quando a captura for encerrada, sob nova orientação.
- Próximo passo (aguardando): solicitar ao usuário, **uma única vez**, digitar `hel` e
  pressionar Tab no teclado físico M5Stack enquanto a captura roda. Evidência a procurar
  no log: linhas de `ESP_LOG` do driver de teclado (`tab5_keyboard`), qualquer
  `LV_KEY_*`/`key`/`char` no instante da tecla, ou ausência total (que também é
  resultado: sem instrumentação, o evento cru não é observável). Não inferir mapeamento
  da tecla nem repetir o cenário sem nova evidência; se não houver linha alguma,
  o próximo passo é instrumentação temporária/injeção de `LV_KEY_NEXT` na ponte — mudança
  de produção do coder, não do tester.
- Supersedes or discards: nenhum resultado anterior é invalidado; esta entrada apenas
  registra o checkpoint da captura e a limitação de não poder consultar `sys.info` com a
  porta ocupada.

### 2026-11-09 21:18 -03:00 - ACHADO CONFIRMADO: teclado físico envia a string `tab`, keymap não mapeia

- Contexto: usuário relatou (relato, não medido por mim) que digitou `hel` e pressionou uma
  vez Tab físico com a captura ativa. Log novo (medido, monitor PID 523693, sem CLI aberta):
  - `2026-11-09 21:16:03  I (753018) M5Stack Tab5: Setting LCD backlight: 20%` e
    `I (753019) cyberdeck_screen: tela religada com duplo toque, brilho=20%` -> a proteção
    de tela estava ativa e o usuário acordou o painel 7 s antes da tecla (sem confundir o
    ramo de tela apagada com o evento do Tab).
  - `2026-11-09 21:16:10  W (760221) tab5_kb: Evento nao mapeado: len=3 'tab'` -> **é o
    evento cru da tecla Tab**: o firmware do teclado M5Stack envia o NOME `tab` (3 bytes),
    não o byte de controle `0x09`.
  - Nenhum banner de boot/panic no log; uptime de device no log (753 s às 21:16:03) contra
    `sys.info` de 635 s às 21:14:07 (~+118 s em 116 s de relógio) => sem reboot e sem panic.
- Causa raiz confirmada na fonte (`components/cyberdeck/src/platform/input/`):
  - `tab5_keyboard_keys.cpp` mapeia o BYTE `"\t"` e a variante nomeada só para
    `esc/enter/backspace/del/up/down/left/right/space` — **não existe entrada `"tab"`**
    (`grep '"tab"'` = nenhum).
  - `tab5_keyboard.cpp::emit_event()` só cai no fallback de caractere quando
    `copy == 1`; com 3 bytes a string vai para o `ESP_LOGW` e o evento é **descartado**.
  - Consequência: o Tab físico nunca chega à sessão (nem como `key::next_tab`, nem como
    texto `\t`), então a completação não roda. Isso explica o `hel` inalterado e **não**
    tem relação com o worker (síncrono para comandos) nem com a regra de `\t` já
    implementada e host-testada: aquela rota jamais é exercitada porque o firmware do
    teclado usa a variante por nome.
- Classificação: **achado confirmado** com evidência medida (log) + leitura de fonte.
  `hel` digitado e "linha permaneceu `hel`" continuam sendo **relato** — não pude medir a
  linha porque o monitor é dono da serial (CLI proibida nesta etapa).
- Correção mínima proposta (PRODUÇÃO, coder): acrescentar na tabela de variante nomeada
  `{"tab", 0, '\t', TAB5_KEY_CHAR}` (paridade com a entrada `{"\t"}`), o que faz o caminho
  `insert_physical_text(len==1, '\t')` já testado receber o evento e manter a semântica
  literal em SSH/senha. Alternativa (mais invasiva): mapear para `LV_KEY_NEXT`
  `TAB5_KEY_SPECIAL` e usar `key::next_tab`. Depois da correção:
  - regressão host em `tests/host/keymap/test_keymap.cpp` (entrar `"tab"` em `known`/
    whitelist e no par de alias `{"tab", "\t"}`; hoje a tabela não pina `tab` como
    desconhecido, então a adição não quebra o teste existente, mas também não há cobertura);
  - rebuild + flash autorizado e repetição humana de `hel`+Tab: esperado **sem** linha
    `Evento nao mapeado` e linha completando para `help`.
- Próximo diagnóstico mínimo: nenhuma nova hipótese antes da correção — o evento cru já
  identifica o defeito. Se o coder quiser evitar humano, a ponte precisaria injetar a
  string `tab` como evento de teclado (não como texto), o que é mudança de produção.
- Captura: **segue ativa** (PIDs 523608/523693, log
  `/tmp/opencode/tab-capture-20261109-211425.log`); encerrar exige autorização explícita.
  Uptime pós-captura só poderá ser medido depois de liberar a porta.
- Supersedes or discards: **confirma** a hipótese do despacho ("a rota física mapeia `\t`
  mas não o alias `tab`") e a eleva de hipótese a causa medida: o firmware do teclado envia
  exatamente o nome `tab`, que não existe na tabela; o evento é descartado em
  `emit_event()` antes de qualquer rota de sessão. Descarta definitivamente o worker como
  fator (comandos completam de forma síncrona) e não invalida nenhum resultado anterior.

### 2026-11-09 21:19 -03:00 - log cresceu sem novo input (tela apagou por inatividade)

- Fact: após a tecla às 21:16:10, o log recebeu só
  `21:17:09 I (818989) Setting LCD backlight: 0%` + `cyberdeck_screen: tela desligada por
  inatividade ou comando` (modo/limiar de brilho, sem tecla). Nenhuma nova linha
  `tab5_kb`, `panic` ou boot; uptime segue monotônico (818 s). Captura
  `/tmp/opencode/tab-capture-20261109-211425.log` (1460 B) permanece ativa (PIDs
  523608/523693) e não foi encerrada.
- Supersedes or discards: none; é apenas a atualização do conteúdo do log, sem mudar o
  achado do `Evento nao mapeado: len=3 'tab'`.

### 2026-11-09 21:37 -03:00 - monitor/captura encerrados por autorização; porta livre; log preservado

- Autorização: o usuário autorizou explicitamente encerrar **somente** o monitor/captura
  passiva que este papel iniciou, com shutdown gracioso, sem tocar firmware, sem enviar
  teclas e sem abrir outro monitor.
- Verificação de identidade ANTES de sinalizar (para não matar processo alheio):
  - `523608` = `python .../esp-idf/tools/idf.py -p /dev/ttyACM0 monitor --no-reset
    --timestamps` (ELAPSED 21:54, iniciado por este papel às 21:14);
  - `523693` = `python -m esp_idf_monitor -p /dev/ttyACM0 ... build/cyberdeck5.elf
    build/bootloader/bootloader.elf --no-reset --timestamps` (ELAPSED 21:51) e era o dono da
    porta (`fuser -v`); ambos conferem com o processo criado nesta sessão.
- Parada: `kill -TERM 523693` (SIGTERM = shutdown gracioso previsto pelo próprio monitor,
  "stopped from the outside: Ctrl+C, SIGTERM"); em 3 s **523693 ausente** e **523608
  ausente** (o wrapper saiu junto). Sem SIGKILL, sem reset, sem flash, sem tecla, sem novo
  monitor.
- Estado final verificado:
  - `/dev/ttyACM0` presente (`crw-rw---- root uucp`) e **sem holders** (`fuser` vazio);
  - nenhum processo `esp_idf_monitor`/`idf.py … monitor`/`start-tab-capture` remanescente;
  - log preservado em `/tmp/opencode/tab-capture-20261109-211425.log` (modo 600):
    1460 B / sha256 `d0c7142e92940d0e872341575a3d2a3c120f7d8e3853d3a8e4a4b8360cd9bb4a`
    antes da parada, 1461 B / sha256
    `ee69442b2eed04cb49a58a0b38fd3c8725250a0bc01dff8bf7f4913819c95e40` depois (só o
    newline final acrescentado no fechamento). O conteúdo do achado está intacto,
    incluindo `21:16:10 W (760221) tab5_kb: Evento nao mapeado: len=3 'tab'`.
- Prova de continuidade que faltava (medida agora, com a porta liberada, sem tecla):
  `sys.info` 21:36:41 -> `fw_version 1.18.1`, uptime `00:33:09` (era `00:10:35` às
  21:14:07; +22,6 min em 22,6 min de relógio), `safe_mode false`, `interrupted_boots 0`,
  `psram_free 31275408` e `internal_free 104015` praticamente idênticos aos de antes da
  captura. Confirma **nenhum boot/panic/leak** durante toda a captura, inclusive após o
  evento do Tab.
- Recapitulação do achado registrado (medido): o teclado físico envia a string de 3 bytes
  `tab`; `tab5_kbd` emite `Evento nao mapeado` e descarta o evento porque a tabela só tem
  a entrada `"\t"` (variante nomeada tem esc/enter/backspace/del/setas/space, sem `tab`).
  Não houve boot nem panic no log. O usuário **relatou** (não medido) que `hel` permaneceu
  na linha. Causa independente do worker de completação.
- Próximo passo: coder adiciona `{"tab", 0, '\t', TAB5_KEY_CHAR}` (ou `LV_KEY_NEXT`) e a
  regressão host em `test_keymap.cpp`; depois disso, flash autorizado + repetição humana de
  `hel`+Tab pelo tester. Nenhuma captura está ativa neste momento; reabrir exige novo
  pedido.
- Supersedes or discards: fecha a limitação "uptime pós-captura só após liberar a porta"
  das entradas de 21:15/21:19; não altera o achado do `tab`.

### 2026-11-09 21:52 -03:00 - fix do alias físico `tab` verificado em código/testes (física pendente)

- Snapshot: HEAD `e23984d9500ac27e187dc3f8276841366c3f2779`; `version.txt` **1.18.2**;
  conteúdo tracked+untracked =
  `404ed20e5c2e206593128c68834c5cb1dac845e374ae9f65b0662148d2c7fe45`; recorte de código
  sem notas = `95bb12a775cd13cebda88dcb66ba34fc73d46a82871e4d445a0752905cf723a1`;
  hashes dos arquivos do fix: `tab5_keyboard.cpp` `0df806ac55580042…`,
  `tab5_keyboard_keys.cpp` `2ab86e9413ade7f8…`, `test_keymap.cpp` `cc0b7f79cde8e84f…`,
  `test_keyboard_input_contract.py` `23ade2074f63a4e4…`. Sem flash nesta rodada; o binário
  1.18.2 / `81248dd7…` é **relato do coder** (não construí nem gravei).
- Inspeção independente do diff:
  - `tab5_keyboard_keys.cpp`: **única** mudança útil é `{"tab", 0, '\t', TAB5_KEY_CHAR}`
    (paridade com `{"\t"}`); nenhuma outra entrada alterada — a variante nomeada continua
    esc/enter/backspace/del/up/down/left/right/space.
  - `tab5_keyboard.cpp::emit_event()`: o `switch` CHAR foi colapsado e normaliza **só**
    `copy == 3 && strcmp(buf, "tab") == 0` para `'\t'` com `text_len = 1`; qualquer outro
    token CHAR mantém `text = buf, text_len = copy`. A chamada
    `s_event_callback(text, text_len, modifier, entry->ch)` é síncrona (sem ponteiro
    pendente para `tab_byte`, que é local) e preserva o comportamento anterior para
    `modifier != 0` (antes: dois ramos com os mesmos argumentos).
- Rota resultante verificada na fonte (`cyberdeck_ui.cpp::on_keyboard_event`): o evento do
  Tab chega com **texto E special_key**; o ramo final é
  `if (text != nullptr && text[0] != '\0' && length != 0) { … insert_physical_text(text,
  length) … } else if (special_key) { local_key(special_key); }`. Logo `"\t"` (1 byte,
  `modifier 0`) vai para `s_shell_app.insert_physical_text("\t", 1)` -> sessão
  `completion_allowed()` -> `begin_completion()`, exatamente a rota já coberta por
  `test_shell_session`; em SSH/senha o mesmo byte segue literal.
- Lacuna material fechada (teste, sem produção): `test_keyboard_input_contract.py` só
  verificava a presença das duas rotas, não a **prioridade** — com o Tab carregando os
  dois campos, uma inversão de ordem faria `local_key` consumir a tecla (o mesmo sintoma
  observado no device). Acrescentei três asserções: ramo de texto existe e vem antes do
  fallback `else if (special_key)`; `insert_physical_text` acontece dentro desse ramo.
  Sensibilidade provada por probe independente: o predicado aceita a fonte real e rejeita
  (a) ordem invertida e (b) ramo de texto sem insert.
- Testes focados reexecutados (sem suíte completa/cobertura/build):
  - `make -C tests/host/keymap test_keymap` | exit 0 | `PASS: tab5_keymap_lookup (670 checks)`
    (inclui `check_char("tab", '\t')`, alias pair `{"tab","\t"}`, negativa de `space`/`ENTER`).
  - `test_keyboard_input_contract` | exit 0 | `PASS: keyboard async-dispatch contract`
    (com as asserções novas de prioridade).
  - `test_shell_session` | exit 0 | `shell session: 249 checks, 0 failures` (Tab físico
    literal em SSH e conclusão por `insert_physical_text("\t")` seguem verdes).
  - `test_shell_completion` | exit 0 | `shell completion: 228 checks, 0 failures`.
  - `test_shell_completion_worker_contract` | exit 0 | `14 mutations rejected`.
  - `test_keyboard_event` | exit 0 | `PASS: tab5_char_event_parse (76 checks)`.
  - `test_ota_contract` | exit 0 | `PASS` (1.18.2 aceito); `test_image_viewer_contract` |
    exit 0 | `0 failures` (piso 1.18.0).
  - `git diff --check` limpo.
- Achados/riscos acionáveis (nenhum bloqueia o fix):
  1. `code-map.md:1751` descreve o emissor como entregando "o caractere mapeado para
     variantes NOMEADAS de TAB5_KEY_CHAR", mas o código normaliza **apenas o token exato
     `tab`**. Ajustar o texto (documentação) para não prometer normalização genérica.
  2. Risco residual medido só para `tab`: `{"space", 0, ' ', TAB5_KEY_CHAR}` continua
     entregue **cru**. Se o A164 também emitir o nome `space` (como faz com `tab`), a
     barra de espaço digitaria a palavra `space`. Não medido até agora; na próxima sessão
     física basta uma tecla de espaço e olhar o terminal (não gera warning porque está
     mapeado). Registrar como hipótese, não como defeito.
  3. Efeito colateral de design (baixo): antes o `tab` físico era descartado; agora o byte
     `'\t'` chega também ao editor (`handle_key(character, "\t")`) e a contextos não-MENU
     (ex.: senha Wi-Fi), onde é inserido literalmente. É coerente com "Tab literal fora do
     menu", mas convém o reviewer confirmar que inserir `\t` no editor é desejável.
- Classificação: o achado antigo "teclado envia `tab` e keymap não mapeia" está
  **corrigido em código** e coberto por testes host (keymap + contrato de prioridade);
  **validação física continua pendente** (flash 1.18.2 + `hel`+Tab humano: esperado
  completar para `help` e nenhum `Evento nao mapeado`).
- Supersedes or discards: fecha a parte de código do achado das entradas de 21:18/21:37;
  mantém aberta a física, agora para a versão 1.18.2 (o firmware 1.18.1 gravado no device
  ainda contém o defeito).

### 2026-11-09 22:08 -03:00 - flash do firmware 1.18.2 executado (autorizado); validação funcional NÃO executada

- Autorização: usuário autorizou explicitamente o flash do firmware corrigido e delegou a
  validação funcional física a ele próprio. Permitido: somente
  `idf.py -p /dev/ttyACM0 flash` (sem build/erase/NVS, sem teste de comportamento, sem
  comando de UI depois, sem monitor).
- Pré-flight (todos conferem; nada divergiu):
  - `build/cyberdeck5.bin`: 2.804.320 B = `0x2aca60`, SHA-256
    `81248dd79b150855e67814b89e586f3bc911bcda54181110cc6cfc4c1d519f60` (igual ao reportado
    pelo coder), mtime `21:47:23` (posterior à última mudança de produção,
    `tab5_keyboard.cpp` 21:46:12).
  - `version.txt` = **1.18.2**; `build/project_description.json` = versão 1.18.2, target
    `esp32p4`, projeto `cyberdeck5`; `CONFIG_IDF_INIT_VERSION "6.1.0"`.
  - `find components main -newer build/cyberdeck5.bin` (fontes) = **vazio** -> produção
    inalterada desde o build; só testes/`.o`/`code-map.md` (21:50:38, doc) são mais novos.
  - `/dev/ttyACM0` presente e **livre** (sem holders); nenhum monitor/CLI ativo.
  - Identidade do alvo confirmada pelo próprio esptool (sem enviar comando antes, para
    respeitar "somente flash"): `Chip type: ESP32-P4 (revision v1.3)`,
    `MAC: e8:f6:0a:e3:22:29`.
- Comando e resultado (medido): `source ~/esp/esp-idf/export.sh && idf.py -p /dev/ttyACM0
  flash` | **exit 0** | 22:06:48 -> 22:07:07 -03:00 | log privado
  `/tmp/opencode/flash-1.18.2-20261109-220647.log` (15.314 B, modo 600):
  `Wrote … at 0x00020000`, `0x0002c000`, `0x000e1000`, `0x000e3000` (app no slot
  `factory` em 0x20000), `Verifying written data...`, **`Hash of data verified.`**,
  `Hard resetting via RTS pin...`, `Done`.
  - **Nenhuma compilação** no log (`grep -cE "Building C object|Building CXX object|
    Compiling"` = 0); `build/cyberdeck5.bin` manteve mtime `21:47:23` e o mesmo SHA-256
    depois do flash -> foi gravado exatamente o artefato inspecionado, sem rebuild.
  - Sem erase, sem alteração de NVS/otadata, sem OTA, sem flash de outra imagem.
- Estado ao terminar: dispositivo **ligado** após o reset do próprio flash; `/dev/ttyACM0`
  **livre** (sem holders); nenhum monitor iniciado; nenhum comando de UI/teste executado
  depois do flash. Não há `sys.info` pós-flash por decisão de escopo (a evidência de
  identidade é o pré-flight do arquivo + saída do esptool).
- Snapshot no momento do flash: HEAD `e23984d9500ac27e187dc3f8276841366c3f2779`;
  recorte código+testes+docs (sem notas) `50286784b033e1f89d7048c8d11c5a41c67c45a77a97a8a68f2b4c9a22fcfc29`;
  tracked+untracked completo `de14ce23d0aaa9a0155c4ce7b88e0fe319419500f22570b2882f21f0b9e039a7`
  (esta entrada altera o segundo).
- Limite desta evidência: **"flash verified" não é validação funcional**. O defeito
  medido em 1.18.1 (`Evento nao mapeado: len=3 'tab'`) está corrigido em código e coberto
  por testes host, e agora está gravado no device, mas o comportamento do Tab físico em
  1.18.2 continua **não verificado**. Não declaro aceite.
- Próximo passo: o **usuário** executa a validação funcional (digitar `hel` + Tab -> deve
  completar para `help`; sugestão: também `space` físico para checar o risco residual do
  token nomeado). Se quiser evidência objetiva de novo, o tester pode reabrir captura
  passiva **somente com nova autorização** (a porta está livre agora). Reviewer development
  ainda precisa refazer o gate sobre 1.18.2.

### 2026-11-09 22:11 -03:00 - aceite funcional relatado pelo usuário no firmware 1.18.2 (evidence_kind: relato)

- Fact (relato, não medido por mim): após o flash verificado do firmware **1.18.2**
  (SHA-256 `81248dd79b150855e67814b89e586f3bc911bcda54181110cc6cfc4c1d519f60`, app em
  `factory` 0x20000, `Hash of data verified` às 22:06:48-22:07:07 -03:00), foi pedido ao
  usuário testar `hel` + Tab -> `help` e `c` + Tab **duas vezes** para conferir a lista.
  O usuário respondeu: "Funcionou tudo certo."
- Classificação: `evidence_kind: **relato**` (aceite funcional do usuário). Não é log, não é
  captura serial e não é medição independente minha. Cobre, **por relato**:
  - REQ-TAB-01 (comando único: `hel` -> `help`) e, por consequência, o alias físico
    `tab` -> `'\t'` que era o defeito medido em 1.18.1 (`Evento nao mapeado: len=3 'tab'`);
  - REQ-TAB-03 (primeiro Tab/segundo Tab com lista) no caminho físico real, para o prefixo
    `c` (cat/cd/clear/app/...), sem que eu tenha visto o conteúdo da lista.
- Limites explícitos desta evidência:
  1. não há `term.dump`/`ui.dump`/log/captura desta rodada; a frase não distingue os dois
     cenários individualmente nem registra hora, estado de tela ou saída textual;
  2. não houve captura serial, então não sei se o aviso `Evento nao mapeado` desapareceu de
     fato — a conclusão decorre do comportamento relatado;
  3. não cobre zero matches, `/sdcard` recusado, `cd` só diretórios, sufixo/cursor nem
     SSH/senha literal (não foram pedidos nesta rodada);
  4. o risco residual do token nomeado `space` (item 2 da entrada de 21:52) segue **não
     testado**; não foi mencionado no relato.
- Gates fora do meu escopo nesta etapa: suíte host completa, cobertura/gcovr e
  `idf.py build` **não são minhas ações** e continuam pendentes do reviewer development,
  que precisa refazer o gate sobre 1.18.2 (o gate anterior era do código de completion antes
  do fix do teclado). Nenhum dispositivo nem teste foi executado nesta atualização.
- Snapshot: o código/firmware não mudou nesta rodada; cito o mesmo HEAD
  `e23984d9500ac27e187dc3f8276841366c3f2779` e o binário já gravado
  (`81248dd7…`, 2.804.320 B, mtime 21:47:23).
- Supersedes or discards: fecha, **como relato**, a pendência "validação física em 1.18.2"
  das entradas de 21:52/22:08; a antiga pendência "física 1.18.1" (antes do fix) permanece
  apenas histórica. Não converte relato em medição e não declara aceite técnico
  independente.

### 2026-11-10 13:48 -03:00 - correção de roteamento: reviewer development não roda gates amplos

- Correção (fact do workflow canônico, `developer-workflow.md`): **`reviewer` em mode
  development** faz revisão e checagens **direcionadas** (inspeção, testes focados
  pertinentes à mudança, coerência de docs/escopo) e **não** executa gates amplos. Gates
  amplos — suíte host completa, cobertura/gcovr + `coverage_scope_guard`, CI/Quality Gate e
  `idf.py build` final — pertencem ao **`reviewer` em mode final**.
- Motivo do registro: as entradas de 2026-11-09 21:52 e 22:11 atribuíram "suíte host
  completa, cobertura/gcovr e `idf.py build`" ao "reviewer development". Isso é impreciso e
  poderia fazer o próximo papel esperar gates no lugar errado; fica **substituído** por esta
  entrada. Nenhum resultado técnico muda: esses gates seguem **não executados por mim** (nem
  por este papel em nenhuma rodada) e continuam pendentes até o reviewer final.
- Classificação preservada do aceite do usuário (sem alteração e sem reconversão): o relato
  "Funcionou tudo certo" após o flash verificado do firmware **1.18.2**
  (SHA-256 `81248dd79b150855e67814b89e586f3bc911bcda54181110cc6cfc4c1d519f60`) para
  `hel` + Tab -> `help` e `c` + Tab duas vezes (lista) permanece
  **`evidence_kind: relato`** — aceite funcional do usuário, **não** medição independente,
  sem log/captura/`term.dump` desta rodada; limites já registrados na entrada de 22:11
  (cenários não separados, aviso `Evento nao mapeado` não observado nesta rodada, `space`
  físico não testado, zero matches/`/sdcard`/cursor/SSH literal não cobertos).
- Sem testes e sem dispositivo nesta atualização: nenhum comando de device, nenhuma suíte,
  nenhuma cobertura, nenhum build. HEAD e firmware seguem
  `e23984d9500ac27e187dc3f8276841366c3f2779` / 1.18.2 `81248dd7…` (2.804.320 B).
- Supersedes or discards: substitui apenas a atribuição de gates nas entradas de 21:52 e
  22:11 ("pendentes do reviewer development"); preserva todo o histórico e a classificação
  `relato` do aceite do usuário.

### 2026-11-10 14:26 -03:00 - revisão independente do fix TEST-WM-05 (porta `cyberdeck_ui_post`)

- Snapshot (conferido na fonte): HEAD `e23984d9500ac27e187dc3f8276841366c3f2779`;
  `git diff | sha256sum` = `24ba180496378ae6538269a434e50d2c940ce802e68803677a11a0e8d9967680`
  (**igual ao reportado pelo coder**); meu recorte "diff + untracked excluindo
  `docs/agent-state/`" = `fb763513c516d42d0191eed617e7728d909a9625c49efd2c5efbaa582f23c89c`;
  tracked+untracked completo (inclui esta nota) =
  `77f0d9af9dcdec73480ef9a7e1f9cafcdeb3271a910b8f350792eeb6918287fa`.
  - Divergência a registrar: **não consegui reproduzir** o hash `ad4347d2…` citado como
    "task files inc untracked excl notes"; testei quatro recortes (v1 diff+untracked all,
    v2 excluindo só `tester.md`, v3 só arquivos de tarefa rastreados, v4 só untracked) e
    nenhum bate. O fingerprint auditável desta nota é o par acima com a receita explícita
    (ordem: `git diff` seguido de cada untracked com cabeçalho `=== <path>`).
- Inspeção independente da porta (`include+src/platform/display/cyberdeck_ui_post`):
  - header **sem tipos LVGL** (só `using callback = void (*)(void *payload)` e
    `bool post(callback, void*)`); nenhum include de `lvgl.h`/`esp_lvgl_port`/FreeRTOS.
  - impl: recusa `fn == nullptr` antes de tocar o display; agenda por `lv_async_call` e
    **só depois** chama `lvgl_port_task_wake(LVGL_PORT_EVENT_USER, nullptr)`; recusa de
    agendamento retorna `false` **antes** de qualquer wake; a porta nunca executa o callback
    inline, nunca copia e nunca libera o payload (sem `delete`/`free`).
  - APIs conferidas no vendor real: `lv_result_t lv_async_call(...)` em
    `managed_components/lvgl__lvgl/src/misc/lv_async.c:44` e
    `esp_err_t lvgl_port_task_wake(lvgl_port_event_type_t, void*)` em
    `components/esp_lvgl_port/include/esp_lvgl_port.h:157` — assinaturas conferem com o uso.
- Worker (app) e camada de aplicação:
  - `cyberdeck_completion_worker.cpp` não tem nenhum `lv_*(`, `lvgl.h` ou
    `esp_lvgl_port` em código (verificado por mim com strip de comentários:
    9.050 de 9.761 bytes de código, zero ocorrências); agenda via
    `cyberdeck_ui_post::post(deliver_result, completed)` **sob o mutex de ciclo de vida**.
  - posse do payload: libera em **ambos** os caminhos de não-entrega (falha do lock de
    ciclo de vida e agendamento recusado) e **não** libera no caminho aceito — as duas
    ocorrências de `delete completed;` conferidas na fonte.
  - headers de app (`cyberdeck_completion_worker.h`, `cyberdeck_shell_completion.h`) sem
    `lvgl`/`freertos`; nenhum header fora de `include/platform/` inclui `lvgl.h` (varredura
    própria). `cyberdeck_shell_app` só ganhou forwarding (`begin_completion`/
    `apply_completion_result`), sem LVGL.
- Testes: nenhuma asserção anterior foi removida ou enfraquecida.
  - `test_shell_completion.cpp` passou a exercitar a porta com o shim completo do LVGL
    (`test_ui_post_port`, **14 checks**): aceita callback válido, enfileira exatamente 1,
    acorda 1, **não roda inline**, executa 1 vez no "display task", recebe o payload opaco,
    recusa sem enfileirar, **sem wake** na recusa, sem execução inline na recusa, recusa
    callback nulo sem enfileirar nem acordar.
  - `test_shell_completion_worker_contract.py` foi de 14 para **21 mutações**, todas
    presentes: as 14 originais continuam (lease READ, scan sob lease, fila não bloqueante,
    token 0, flag de entrega, callback, join bounded, payload em recusa, dreno do token,
    porta ignorando readiness, teardown ausente, header puro/worker/Makefile) e entraram
    `worker schedules LVGL itself`, `display port wake removed`,
    `display port wakes before scheduling`, `display port keeps the payload on refusal`,
    `worker header drags FreeRTOS in`, `port header exposes an LVGL type`,
    `port TU dropped from the host build`. `code_only()` é real (não vacuoso): 9.050/9.761
    bytes de código e sem resíduo de comentário.
  - Minhas asserções de prioridade do ramo de texto sobre `special_key` (adicionadas em
    2026-11-09 21:52 em `test_keyboard_input_contract.py`) continuam presentes e passando.
- CI/qualidade (escopo ampliado conferido localmente, sem rodar a CI):
  `clang-format 23.1.4` (CI fixa 23.1.3) `--dry-run --Werror` passa nos 7 arquivos novos
  (completion/shell/worker/porta/teste focado) e nas duas faixas de linhas:
  `tab5_keyboard.cpp --lines=126:143` cobre exatamente o `case TAB5_KEY_CHAR` completo
  (127-143, comentário..chave de fechamento) e `tab5_keyboard_keys.cpp --lines=33:34`
  cobre comentário+entrada `{"tab"}`. `cppcheck 2.22.0` com os mesmos flags do workflow
  (`--enable=warning,performance,portability --error-exitcode=1`) passa nos 5 TUs, inclusive
  no passe `ESP_PLATFORM`.
- Código do alias físico do Tab: **intacto** desde a minha validação anterior
  (`tab5_keyboard.cpp` sha256 `0df806ac…`, `tab5_keyboard_keys.cpp` `2ab86e94…`), com
  `{"tab", 0, '\t', TAB5_KEY_CHAR}` na linha 34 e a normalização exata em `copy == 3 &&
  strcmp(buf,"tab") == 0`.
- Comandos e resultados (focados; sem suíte completa, cobertura, build ou device):
  - `make -C tests/host/keymap test_shell_completion` | exit 0 | `shell completion: 242
    checks, 0 failures` (228 -> 242, +14 da porta).
  - `test_shell_completion_worker_contract` | exit 0 | `PASS … (21 mutations rejected)`.
  - `test_window_manager_contract` | exit 0 | `PASS: window manager structural contract
    (TEST-WM-03/05/06/07, TEST-REG-8-BAR, TEST-REG-8-CHROME)` + `PASS: TEST-WM-03/06/07
    wiring contract (9 mutations rejected)`.
  - `test_shell_session` | exit 0 | `shell session: 249 checks, 0 failures`.
  - `test_keymap` | exit 0 | `PASS: tab5_keymap_lookup (670 checks)`.
  - `test_keyboard_input_contract` | exit 0 | `PASS: keyboard async-dispatch contract`.
  - `test_keyboard_event` | exit 0 | `PASS: tab5_char_event_parse (76 checks)`.
  - `test_ota_contract` | exit 0 | `PASS` (version.txt 1.18.2).
  - `make -C tests/host/keymap verify` | exit 0 | `OK: constantes LV_KEY_* identicas ao
    LVGL managed_components/lvgl__lvgl`.
  - `git diff --check` | limpo.
- Desvios/observações (nenhum bloqueante):
  1. `post()` ignora o retorno de `lvgl_port_task_wake` e devolve `true`: o contrato do
     header diz "sucesso = agendado **e** task acordada". Se o wake falhar, o call segue na
     fila do LVGL e roda no próximo tick (atraso, não perda). Mesmo critério do código
     anterior do worker; Low.
  2. Um call já enfileirado no LVGL no instante do teardown pode não rodar (LVGL
     desinicializado) e o payload vazar; a mitigação existente é `deliver_result` gateado
     por `s_closing`/`s_delivery_disabled` **quando o call roda**. Classe pré-existente
     (igual ao worker do cat), não introduzida por este fix; Low, para o reviewer final.
  3. `clang-format` local 23.1.4 vs CI 23.1.3: divergência de patch pode mudar formatação;
     a CI continua sendo a autoridade do gate.
  4. Hash `ad4347d2…` do coder não reproduzido (ver snapshot acima); usar a receita
     explícita desta nota.
- Traceability: **TEST-WM-05** (aplicações não alcançam a árvore LVGL) passa a ter a
  fronteira explícita na porta de plataforma — worker de app sem `lv_*`, porta com
  interface sem tipos LVGL, e ambas as propriedades pinadas por contrato estrutural com
  mutações + teste comportamental da porta. **TEST-TAB-05** (assíncrono fora da task LVGL,
  bounded, stale/teardown, lease) permanece coberto; **REQ-TAB-01..05** sem regressão nos
  focados acima.
- Gate routing (reafirmado após a correção de 13:48): **reviewer development** = revisão e
  checagens direcionadas; **reviewer final** = suíte host completa, cobertura/gcovr +
  `coverage_scope_guard`, CI/Quality Gate e `idf.py build` final. Nesta rodada não executei
  nenhum desses gates amplos, nem device/flash; o aceite físico em 1.18.2 continua
  `evidence_kind: relato`.

### 2026-11-10 14:30 -03:00 - flash medido do binário com a porta platform/display (f96b28c8)

- Autorização explícita do usuário nesta rodada: gravar esta imagem exata para que **ele**
  faça o teste físico. Escopo permitido: somente `idf.py -p /dev/ttyACM0 flash`, sem
  erase/NVS/OTA, sem monitor e sem comando de teste.
- Pré-flight (tudo conferido; nada divergiu):
  - `build/cyberdeck5.bin` 2.804.352 B = `0x2aca80`, SHA-256
    `f96b28c851d59cf737d3ef9768ee3aba24f531aab14ccddbfada04b803ab52cf` (idêntico ao
    confirmado pelo developer), mtime `2026-11-10 14:19:29`.
  - `version.txt` 1.18.2; `project_description.json` 1.18.2 / target `esp32p4`.
  - `find components main -newer build/cyberdeck5.bin` (fontes `.c/.cpp/.h`) = **vazio**
    -> o binário inclui a nova porta `platform/display/cyberdeck_ui_post` (produção
    alterada em relação à imagem anterior `81248dd7…`).
  - `/dev/ttyACM0` presente e **livre**; HEAD `e23984d9500ac27e187dc3f8276841366c3f2779`.
- Flash (medido): `source ~/esp/esp-idf/export.sh && idf.py -p /dev/ttyACM0 flash` |
  **exit 0** | 14:28:51 -> 14:29:09 -03:00 | log privado
  `/tmp/opencode/flash-1.18.2-f96b28c8-20261110-142850.log` (17.521 B, modo 600).
  Alvo confirmado pelo esptool: `Chip type: ESP32-P4 (revision v1.3)`,
  `MAC: e8:f6:0a:e3:22:29`. Escritas em `0x00020000` (app `factory` e segmentos) até
  `0x000e3000`, `Verifying written data...`, **`Hash of data verified.`** (1 ocorrência),
  `Hard resetting via RTS pin...`, `Done`. **Zero compilações** no log; o binário manteve
  SHA-256 e mtime depois do flash -> foi gravada exatamente a imagem `f96b28c8…`.
  Sem erase, sem NVS/otadata, sem OTA, sem segundo monitor.
- Estado ao terminar: dispositivo **ligado** após o reset do próprio flash; `/dev/ttyACM0`
  **livre**; nenhum monitor/processo `idf.py` ativo; **nenhum** comando de UI/teste
  executado depois do flash (não rodei `sys.info` pós-flash por escopo).
- Pendência (do usuário, `evidence_kind: relato` quando reportada): teste físico no
  firmware **1.18.2 / SHA-256 `f96b28c851d59cf737d3ef9768ee3aba24f531aab14ccddbfada04b803ab52cf`**
  — `hel` + Tab -> `help`; `c` + Tab duas vezes para a lista; opcionalmente `space`
  físico (risco residual do token nomeado) e `cd` + Tab para diretório. O aceite anterior
  (`81248dd7…`) fica **histórico**: a imagem gravada agora é outra.
- Snapshot desta entrada: HEAD `e23984d9500ac27e187dc3f8276841366c3f2779`;
  `git diff` = `24ba180496378ae6538269a434e50d2c940ce802e68803677a11a0e8d9967680`; diff+untracked excluindo `docs/agent-state/` = `fb763513c516d42d0191eed617e7728d909a9625c49efd2c5efbaa582f23c89c`;
  o hash tracked+untracked completo muda a cada append desta nota (por isso não é
  fingerprint estável); os estáveis são `git diff` e "diff+untracked excluindo
  `docs/agent-state/`" acima. A produção não mudou nesta rodada
  (só esta nota).
- Gate routing (reafirmado): gates amplos — suíte host completa, cobertura/gcovr +
  `coverage_scope_guard`, CI/Quality Gate e `idf.py build` final — pertencem ao
  **reviewer final**; `reviewer development` faz checagens direcionadas. Nada disso foi
  executado aqui, nem device além do flash autorizado.

### 2026-11-10 14:38 -03:00 - teste físico do usuário em 1.18.2/f96b28c8 (evidence_kind: relato)

- Firmware testado (relato do usuário, não medido por mim): **1.18.2**, SHA-256
  `f96b28c851d59cf737d3ef9768ee3aba24f531aab14ccddbfada04b803ab52cf` (2.804.352 B,
  `0x2aca80`) — exatamente a imagem gravada em 2026-11-10 14:28:51-14:29:09 com
  `Hash of data verified`. Não houve captura serial, `term.dump`, `ui.dump` nem qualquer
  comando meu nesta rodada: **não** alego verificação independente nem aceite automatizado.
- Cenários e resultados **reportados pelo usuário**, sem paráfrase de efeito além do dito:
  - `hel` + Tab -> `help` (comando único completado);
  - `c` + Tab duas vezes -> lista mostrando `clear`, `cd`, `cat` (prefixo comum + segundo
    Tab listando);
  - `ls ca` + Tab -> funcionou corretamente (path completion de arquivo/diretório existente);
  - `cd sc` + Tab -> funcionou corretamente (path completion de diretório).
- Classificação: `evidence_kind: **relato**`. Cobre, por relato, REQ-TAB-01 (comando
  único), REQ-TAB-03 (prefixo comum e segundo Tab listando) e REQ-TAB-02 no que diz
  respeito a **path completion em diretório existente** (`ls`/`cd`).
- Pendências atualizadas:
  - **fechada por relato**: validação física de *path completion* (diretório/arquivo
    existente com prefixo) no firmware 1.18.2/f96b28c8 — substitui a pendência aberta na
    entrada de 14:30, que citava `ls`/`cd` como não testados.
  - **seguem explicitamente DESCONHECIDOS** (não foram citados no relato, nenhuma
    inferência): preservação de cursor/sufixo com token no meio da linha; zero match
    preservando a linha; `/sdcard` recusado pela completação; Tab literal em SSH/senha/
    Wi-Fi; tecla `space` física (risco residual do token nomeado); lista truncada (>64);
    `cat`/`touch`/`mkdir`/`rm`/`rmdir` com caminho no device; widget virtual
    (`LV_KEY_NEXT`) — este último continua sem evidência física em nenhuma versão.
- Limites: o relato não informa hora, estado de tela, saída textual completa da lista nem
  comportamento em caso de erro; não substitui log/captura. Gates amplos (suíte completa,
  gcovr/`coverage_scope_guard`, CI/Quality Gate, `idf.py build` final) continuam do
  **reviewer final**; nenhum foi executado aqui e nenhum device/teste foi tocado nesta
  atualização.
- Snapshot: HEAD `e23984d9500ac27e187dc3f8276841366c3f2779`; `git diff` =
  `24ba180496378ae6538269a434e50d2c940ce802e68803677a11a0e8d9967680`; diff+untracked
  excluindo `docs/agent-state/` =
  `fb763513c516d42d0191eed617e7728d909a9625c49efd2c5efbaa582f23c89c`. Produção inalterada
  nesta rodada (só esta nota).
