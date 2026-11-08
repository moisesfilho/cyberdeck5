# coder - image-viewer

- Role: coder
- Updated: 2026-11-08 19:56 -03:00
- Code state: HEAD `16392858881d775bb68a6733fffcfa67223ab808` com worktree sujo;
  snapshot ativo `git diff | sha256sum` =
  `1fd3f767fefb56180a0802b7af8b3151e5bf7745f6ec7865a7ba51d6ed6232e2`,
  49 entradas em `git status --porcelain` (image-viewer + transporte
  `/control/upload` + formatação/wiring do format gate). Nada commitado, sem
  tag, sem flash.

## Dispatch objective

Implementar o plano aprovado do visualizador de imagens isolado (REQ/AC-IMG-01..05):
app foreground `cyberdeck.image_viewer` (`image <path>`), BMP 24-bit `BI_RGB`
sem compressão/transparência, ≤ 5 MiB, 1..1280 px por eixo, até 64 imagens do
diretório, setas com indicador `i/N`, proporção preservada, `Esc` volta ao
shell; PNG/JPEG, zoom e recursão fora. Inclui testes focados, contratos da
capacidade 20→21, docs, `code-map.md`, roteiro manual e versionamento PATCH.
Sem suíte completa, coverage global, flash, gates ou tag.

## Work done

Produção (ver `code-map.md` §“Visualizador de imagens” para a tabela completa):

- Camada pura: `apps/image/cyberdeck_image_bmp.{h,cpp}` (envelope BMP),
  `cyberdeck_image_fit.{h,cpp}` (colocação inteira, sem upscale),
  `cyberdeck_image_decode.{h,cpp}` (streaming por `pread`, uma linha BGR por
  vez, sem buffer de 5 MiB; alocação única `w*h*2` + scratch de linha),
  `cyberdeck_image_list.{h,cpp}` (até 64 `.bmp` ordenados por bytes, marcador de
  truncamento, clamp de setas, `format_position`, `classify_image_key`) e
  `cyberdeck_image_types.h` (`viewer_state`, `published_image`, sem FreeRTOS/LVGL).
- Aplicação: `apps/image/cyberdeck_image_app.{h,cpp}` — manifesto literal,
  worker `image_load` (6144 B) com geração monotônica, cancelamento + join de
  5 s, slot único de publicação, `navigate()` sobre a listagem publicada,
  mensagens de erro tipadas, I/O pelo `storage_facade::open_readonly_stream`.
- View LVGL: `platform/display/cyberdeck_image_view.{h,cpp}` — única dona do
  frame exibido (`lv_image_dsc_t` RGB565, stride `w*2`), libera o frame anterior
  em `set_image()` e o último em `destroy()`, cromo do tema zerado, layout em
  `LV_EVENT_SIZE_CHANGED`.
- Integração `cyberdeck_ui.cpp`: `s_image_view`, timer `process_image_status`
  (100 ms), `image_local_key`/`classify_image_key` no `local_key`, gate
  `image_view_active()` no teclado físico, `terminal_key`/`terminal_insert`,
  destroy no teardown.
- Registro: `k_max_applications` 21 nos dois headers + `static_assert` de
  igualdade; `k_apps` e diagnóstico de falhas em `cyberdeck_system_apps.cpp`;
  fontes no `CMakeLists.txt`.

Testes e contratos: `test_image_bmp`, `test_image_fit`, `test_image_decode`,
`test_image_list`, `test_image_view` (ASan+UBSan) e
`test_image_viewer_contract.py`; `test_shell_help.cpp` +
`test_shell_help_contract.py` + `test_app_runtime.cpp` atualizados para o 21º
app; `tools/coverage_scope_guard.py` com o app na allowlist de hardware e os
cinco TUs host no `REQUIRED_COVERED` (espelho em
`test_coverage_scope_guard.py`); shim `shim/full/lvgl.h` com `lv_image_*` e
`lv_obj_delete` recursivo.

Docs: `code-map.md` (seção + matriz REQ/AC→TEST), `README.md`,
`README.pt-BR.md`, `docs/ARCHITECTURE.md`, `docs/ARCHITECTURE.pt-BR.md`,
`tests/host/keymap/help_traceability.md` (Ciclo 6),
`tests/manual/image-viewer-validation.pt-BR.md`. `version.txt` 1.15.2 → 1.15.3
(PATCH, sem MAJOR, sem tag); manifesto do app 1.0.0.

## Evidence

Comandos executados neste worktree, na ordem:

- `make -C tests/host/keymap test_image_bmp` → `test_image_bmp: 79 checks, 0 failures`
- `make -C tests/host/keymap test_image_fit` → `test_image_fit: 222 checks, 0 failures`
- `make -C tests/host/keymap test_image_decode` → `test_image_decode: 48 checks, 0 failures`
- `make -C tests/host/keymap test_image_list` → `test_image_list: 103 checks, 0 failures`
- `make -C tests/host/keymap test_image_view` (ASan+UBSan, `--coverage`) →
  `test_image_view: 70 checks, 0 failures`; nenhum relatório de leak/UAF/double-free
- `make -C tests/host/keymap test_image_viewer_contract` → `0 failures`
- `make -C tests/host/keymap test_shell_help` → `249 checks, 0 failures`
- `make -C tests/host/keymap test_shell_help_contract` → PASS (8 mutações rejeitadas)
- `make -C tests/host/keymap test_app_runtime` → `PASS: app runtime (226 checks)`
- `make -C tests/host/keymap test_display_views` → `display view tests passed`
- `make -C tests/host/keymap test_window_manager` → `PASS: window manager (260 checks)`
- `make -C tests/host/keymap test_ui_resource_contract` → PASS
- `make -C tests/host/keymap help_unification_contract` → PASS
- `make -C tests/host/keymap test_system_apps_contract` → PASS
- `make -C tests/host/keymap test_phase10_contract` → PASS (9 cenários)
- `make -C tests/host/keymap test_boot_sequence` → PASS
- `make -C tests/host/keymap test_ssh_output_gap` → PASS (contrato host é Python)
- `make -C tests/host/keymap test_coverage_scope_guard` → `coverage scope guard tests passed`
- `source ~/esp/esp-idf/export.sh && idf.py build` → **EXIT 0**;
  `cyberdeck5.bin` 0x2a13e0 bytes (2.756.576), partição de app 0x400000,
  0x15ec20 (34%) livres. Único warning: `kill is not implemented` do libssh
  (pré-existente, linker). Sem `idf.py flash`.
- `git diff --check` → limpo.

Não executado por escopo: suíte agregada `make test`, gcovr/coverage global,
mutação dos cenários do tester, flash, validação física, reviewer.

## Decisions and assumptions

- Fit = `min(area/image)` com clamp `<= 1.0` (nunca amplia); geometria exata e
  `zoom = floor(display_w*256/w)`, erro de quantização ≤ 1 px (premissa para
  TEST-IMG-FIT-01/TEST-IMG-FIT-04).
- Objeto LVGL mantém o tamanho natural com centro no centro do retângulo
  encaixado — é o que faz `lv_image_set_scale()` desenhar dentro da área.
- Posse única do frame: worker → slot (mutex) → view; `stop()` libera apenas o
  slot não retirado; `destroy()` da view libera o frame exibido. Pico transitório
  de 2 frames (exibido + recém-decodificado); regime = 1.
- Falha de carga: view destruída, uma mensagem no terminal e retorno a
  `cyberdeck.shell` (sem indicador obsoleto, sem foreground zumbi).
- `commands_require_active_foreground = false` é obrigatório: com `true` o
  supervisor recusaria `image <path>` antes de ativar o app (regra
  `opens_foreground = no_arguments`).
- `copy_bounded` virou template `<std::size_t N>` limitado a `N-1` + NUL para
  servir arrays de 257 (path) e 129 (nome) sem overflow (correção do bloqueio
  de build registrado em 2026-11-08 14:4x).
- Jev não usado: nenhum empate técnico reversível; o recorte de formato e os
  limites são decisão de aceite humano já aprovada.
- Dashboard/OS-Tab5 avaliado como **não afetado**: a entrega adiciona um app novo
  e não avança recorte de `docs/OS-IMPLEMENTATION-PLAN.pt-BR.md`; nenhum
  checkbox do dashboard foi alterado.

## Pending and risks

- **Validação física pendente** (nenhum flash executado): exibição RGB565 em
  retrato/paisagem, escala no painel real, pico de PSRAM durante a troca de
  imagens, navegação por setas físicas (não injetáveis pela ponte serial) e
  ausência de artefato visual. Roteiro em
  `tests/manual/image-viewer-validation.pt-BR.md`.
- **Coverage gate global não executado**: `tools/coverage_scope_guard.py` só roda
  de verdade com o `gcovr` JSON da CI; as entradas novas (app na allowlist de
  hardware, cinco TUs em `REQUIRED_COVERED`) foram validadas apenas pelo
  self-test do guard. `cyberdeck_image_app.cpp` não é host-linkável (FreeRTOS);
  seu comportamento é coberto estruturalmente por
  `test_image_viewer_contract.py`.
- TEST-IMG-LIFE-06 (stop concorrente ao decode com TSan) e LIFE-07 (SD
  indisponível) não têm alvo host dedicado: o app é FreeRTOS e o lease SD só
  existe no alvo. Ficam para o tester/reviewer e para o roteiro físico.
- Mutações obrigatórias do tester (cap 5 MiB `<=`→`<`, 1280→1279, 64→63/65,
  clamp→wrap, `min`→`max`, stride `&~3`, ordem de linha, guarda `/sdcard`,
  geração do decode, gate de posse, capacidade 21→20) **não foram executadas**
  neste papel.
- Artefatos de teste (`test_image_*_bin`, `*.gcda`, `*.gcno`) ficam em
  `tests/host/keymap/` e são cobertos pelo `.gitignore`; não foram commitados.
- A contagem de apps em `docs/03-audio-player-plan.pt-BR.md` e nas notas de
  outras tarefas ainda menciona `1.15.2`/snapshots antigos: são menções
  históricas explícitas, preservadas de propósito.

## References

`docs/agent-state/image-viewer/planner.md`, `docs/agent-state/image-viewer/tester.md`,
`code-map.md` (§ Visualizador de imagens), `AGENTS.md`,
`components/cyberdeck/{include,src}/apps/image/*`,
`components/cyberdeck/{include,src}/platform/display/cyberdeck_image_view.*`,
`tests/host/keymap/test_image_*`, `tests/host/keymap/test_shell_help*.{cpp,py}`,
`tools/coverage_scope_guard.py`,
`tests/manual/image-viewer-validation.pt-BR.md`, `version.txt`.

## Updates

### 2026-11-08 14:49 -03:00 - build destravado e escopo fechado

- Fact: `idf.py build` passou (EXIT 0, `cyberdeck5.bin` 0x2a13e0, 34% livre)
  depois de transformar `copy_bounded` em template limitado a `N-1`; os seis
  alvos focados de imagem e o contrato estrutural estão verdes e
  `git diff --check` está limpo no snapshot `1639285` + worktree sujo.
- Supersedes or discards: o bloqueio de compilação em
  `cyberdeck_image_app.cpp:174` (`copy_bounded` 257 vs 129) registrado antes
  deste ciclo; permanece pendente apenas a validação física e o gate de
  coverage executado pela CI/global.

### 2026-11-08 15:12 -03:00 - findings bloqueantes do reviewer corrigidos

- Fact: os dois achados de `reviewer.md` (development) foram corrigidos no
  snapshot `1639285` + worktree sujo, sem ampliar escopo:
  1. **Corrida de handoff**: o worker deixou de ser criado por `request()`. Ele é
     agora **persistente** — criado uma única vez em `start()`, acordado por
     semáforo (`wake_`) e encerrado apenas por cancelamento, sinalizando `done_`.
     A decisão fila/estado vive em `worker_state` (novo TU puro
     `apps/image/cyberdeck_image_worker_state.{h,cpp}`, sempre chamado com
     `request_mutex_` retido); `next()` aposenta `alive()`/`pending()` no mesmo
     passo que observa a fila, então um pedido que chegue na transição terminal é
     instruído a criar task (`enqueue()` retorna true) em vez de ficar órfão em
     `loading`. Não existe mais janela entre observar `has_pending_` e limpar
     `running`.
  2. **Varredura não cancelável / espera de 5 s na UI**: `listing_ops` ganhou
     `cancelled`/`cancel_context` e `list_images()` faz poll por entrada (e antes
     de abrir o stream), fechando o stream e devolvendo falha sem publicar página
     parcial. O app liga esse poll ao próprio `cancel_`. O join do `stop()` caiu
     de 5000 ms para `k_worker_join_budget_ms = 200` e continua **não liberando**
     nada enquanto o worker puder estar vivo (timeout → `stop()` falha, sem UAF;
     `start()` posterior recusa enquanto o worker antigo não for colhido). O
     caminho de Esc (`leave_image_viewer`) só destrói a view **depois** de um
     `switch_foreground` bem-sucedido.
- Evidence (comandos e resultado neste worktree):
  - `make -C tests/host/keymap test_image_worker_state` → novo alvo,
    `788 checks, 0 failures`; inclui sweep exaustivo de 81 sequências
    request/take/cancel provando TEST-IMG-LIFE-05/06 e o cenário que travava.
  - `make -C tests/host/keymap test_image_list` → `111 checks, 0 failures`
    (+8 checagens novas: aborta no 1º poll, aborta após 3 entradas sem publicar
    página parcial, e completa normalmente sem cancelamento).
  - `make -C tests/host/keymap test_image_bmp` 82, `test_image_fit` 222,
    `test_image_decode` 50, `test_image_view` 70 (ASan+UBSan), todos 0 falhas.
  - `make -C tests/host/keymap test_image_viewer_contract` → `0 failures`
    (novas asserções: um único `xTaskCreate` e nenhum em `request()`, decisão via
    `worker_state::next` sob mutex, orçamento `k_worker_join_budget_ms = 200` no
    `stop()`, ausência do antigo `k_worker_join_timeout_ms` no corpo do stop,
    poll `scan.cancelled`/`scan.cancel_context`, TU do worker no CMake).
  - `make -C tests/host/keymap test_coverage_scope_guard` → `coverage scope guard
    tests passed` (novo TU em `REQUIRED_COVERED` nos dois lados).
  - `source ~/esp/esp-idf/export.sh && idf.py build` → **EXIT 0**;
    `cyberdeck5.bin` 0x2a15b0, 34% livre (0x15ea50), sem erro; sem flash.
  - `git diff --check` → limpo.
- Supersedes or discards: o item de pendência “join de 5 s no caminho de UI” e a
  alegação de que o handoff task-per-request era seguro; a arquitetura agora é
  worker persistente com protocolo puro host-testado. Permanecem pendentes:
  validação física (`tests/manual/image-viewer-validation.pt-BR.md`), mutações
  obrigatórias do tester e o gate de coverage global/CI. Nenhuma alteração de
  risco ou arquitetura fora do plano foi necessária.

### 2026-11-08 15:38 -03:00 - BUG-IMG-01 (reboot ao abrir BMP) corrigido

- Fact: causa **confirmada estaticamente** e corrigida. `sizeof(cyberdeck_image::image_directory)` = **8.272 B**
  (medido no host com `image_entry` = 129 B × 64 + contadores), e havia **duas** cópias dessa página
  na stack da task `image_load` (`k_worker_stack_bytes = 6144`): `run_job()`
  (`cyberdeck_image_app.cpp:293`, `image_directory listing{}`) e, na mesma pilha de chamada,
  `list_images()` (`cyberdeck_image_list.cpp:139`, `image_directory staging{}`). Somando
  `directory_path` (257 B), `name` (129 B), `published_image`, `decoded_image`, `decode_ops` e
  `std::string`, o primeiro `image <arquivo>` excedia o stack e derrubava o Tab5 — consistente com o
  reboot observado pelo tester às 15:22 (uptime 00:00:23, sem backtrace). Classificação **BUG-IMG-01**
  (defeito de implementação, AC-IMG-04/stack), REQ/AC-IMG inalterados.
- Fact: correção sem aumentar stack, sem global compartilhado e sem afrouxar limites:
  1. `list_images()` deixou de alocar `staging` local e preenche a página do chamador diretamente; em
     falha/cancelamento a página é limpa (`out = image_directory{}`), então nenhum chamador observa página
     parcial (semântica all-or-nothing preservada).
  2. Novo `directory_workspace` (público, em `cyberdeck_image_list.h`/`.cpp`) é o staging **heap-backed**
     com hooks de alocação no mesmo formato de `decode_ops` (PSRAM no alvo via
     `heap_caps_malloc`, fallback interno), `release()` idempotente chamado pelo destrutor.
  3. `run_job()` passou a usar o workspace: a página vem por ponteiro, a stack da task fica com o
     holder pequeno + `directory_path`/`name` + locais triviais.
- Evidence (neste worktree, HEAD `1639285` + diff sujo):
  - `g++ -std=c++17 … /tmp/opencode/sz.cpp` → `image_entry=129 image_directory=8272` (confirma a
    hipótese numérica do tester).
  - `make -C tests/host/keymap test_image_list` → **139 checks, 0 failures**, incluindo o novo
    `test_directory_workspace_is_heap_backed`: 1 alocação por acquire, tamanho exato da página, página
    zerada, `release()` idempotente (sem free duplo), reacquire sem leak, destrutor liberando uma vez,
    alocação recusada sem alocar/liberar nada, e `list_images` em diretório inexistente limpando página
    previamente preenchida.
  - `make … test_image_viewer_contract` → `0 failures` (novas asserções BUG-IMG-01: proíbe
    `image_directory listing{}` em `run_job` e `image_directory staging{}` em `list_images`, exige
    `directory_workspace` no worker, `page_` como ponteiro no header, e mantém
    `k_worker_stack_bytes = 6144`).
  - `make … test_image_bmp` 82, `test_image_fit` 222, `test_image_decode` 50, `test_image_view` 70
    (ASan+UBSan), `test_image_worker_state` 788 — todos 0 falhas.
  - `source ~/esp/esp-idf/export.sh && idf.py build` → **EXIT 0**, `cyberdeck5.bin` **0x2a16d0**,
    34% livre (0x15e930). Sem flash.
  - `git diff --check` → limpo.
- Fact: roteiro manual corrigido — `tests/manual/image-viewer-validation.pt-BR.md` E4 passa a pedir
  `image help` (sintaxe real, igual ao `TEST-IMG-HELP-02`), com nota de que `help image` responde
  `usage: help`.
- Supersedes or discards: a hipótese do tester sobre a causa (aceita e confirmada) e a pendência
  “validação física bloqueada por reboot”: o bloqueio agora tem correção + regressão host; a nova
  tentativa física (sob monitor serial) segue pendente e não foi feita por mim (sem flash). Nenhuma
  contradição encontrada entre código e relato de stack; não foi necessário escalar ao planner.

### 2026-11-08 15:58 -03:00 - BUG-IMG-02 (temporário de 8 KiB) corrigido e medido

- Fact: causa **provada pela desmontagem do ELF** e reproduzida no fonte. O
  conserto de BUG-IMG-01 removeu os locais, mas `directory_workspace::acquire`
  fazia `page_ = new (memory) image_directory{};`: a value-initialization do
  agregado materializa um **temporário de 8 KiB** na stack do chamador (inline,
  dentro de `run_job`). Prova: `riscv32-esp-elf-objdump -d build/cyberdeck5.elf`
  no binário 0x2a16d0 mostrava prólogo `addi sp,sp,-848` + `lui t0,0xffffe;
  add sp,sp,t0` ⇒ frame **9.040 B** na task `image_load` de 6.144 B; o panic do
  tester (`Stack protection fault`, SP 908 B abaixo do limite na entrada de
  `run_job`) fecha o diagnóstico. Havia ainda quatro `image_directory{}`
  (atribuição) em `start`/`stop`/`fail`/`list_images` com o mesmo risco —
  `fail()` roda no worker.
- Fact: correção (sem aumentar stack, sem global, sem afrouxar limites):
  1. `image_directory::clear()` (in-place, `noexcept`) substitui **todas** as
     atribuições `image_directory{}` em produção; entradas ≥ `count` nunca são
     observadas.
  2. `directory_workspace::acquire` zera o storage bruto com
     `std::memset(memory, 0, sizeof(image_directory))` e faz o cast para ponteiro;
     sem placement-new, sem temporário. Um
     `static_assert(std::is_trivially_copyable<image_directory>)` transforma
     qualquer membro não trivial futuro em erro de build (que exigiria revisão
     de orçamento de stack).
  3. `k_worker_stack_bytes = 6144` passou a morar em `cyberdeck_image_types.h`
     (fonte única, sem FreeRTOS) e o `xTaskCreate` deriva dele, para que a
     regressão host use o mesmo limite.
- Evidence (HEAD `1639285` + worktree sujo; sem flash):
  - `riscv32-esp-elf-objdump -d build/cyberdeck5.elf` (binário novo 0x2a13b0):
    `run_job=1600 B`, `worker_entry=896 B`, `publish=416 B`, `fail=704 B`,
    `start=448 B`, `stop=704 B` — todos ≤ 6.144 B. O temporário de 8 KiB
    desapareceu (era 9.040 B só em `run_job`).
  - Novo alvo `test_image_stack_frame_contract` → `0 failures`; saída:
    `host -fstack-usage: chain=368 B (offer inlined), budget=6144 B` e
    `elf frames: fail=704B, publish=416B, run_job=1600B, start=448B, stop=704B,
    worker_entry=896B`. O script mede `-fstack-usage` real do TU de listagem
    (`g++ -O2 -fstack-usage`) e lê os frames do ELF pelo prólogo
    (`addi sp,sp,-N` + ajuste `lui`/`add sp,sp`), com reserva de 2.048 B para a
    pilha de VFS/log abaixo das funções checadas; sem ELF/toolchain imprime SKIP
    com o motivo (busca também `~/.espressif/tools/riscv32-esp-elf/*`).
  - `make … test_image_list` 139 checks; `test_image_viewer_contract` 0 falhas
    (novas asserções BUG-IMG-02: proíbe `image_directory{}` em produção, exige
    `clear()`, `memset` e o `static_assert`); `test_image_bmp` 82, `fit` 222,
    `decode` 50, `view` 70 (ASan+UBSan), `worker_state` 788 — 0 falhas.
  - `source ~/esp/esp-idf/export.sh && idf.py build` → **EXIT 0**,
    `cyberdeck5.bin` **0x2a13b0**, 34% livre (0x15ec50).
  - `git diff --check` → limpo.
- Supersedes or discards: a afirmação anterior de que BUG-IMG-01 estava
  “corrigido e coberto” (o staging era heap, mas a inicialização criava o
  temporário) — fica substituída por BUG-IMG-01 (locais removidos) +
  BUG-IMG-02 (inicialização sem temporário) com medição de frames. Nenhuma
  mudança de escopo/risco foi necessária, então o planner não foi acionado.
  Pendente continua: tentativa física única sob monitor com este binário
  (não feita por mim — sem flash), mutações do tester e coverage global/CI.

### 2026-11-08 16:12 -03:00 - BUG-IMG-02 fechado: lifetime C++17 por placement default-init

- Fact: o reviewer (15:42) apontou corretamente que `memset` + `reinterpret_cast` **não** inicia
  lifetime de `image_directory` em C++17 (UB ao acessar `page_->count`/`entries`);
  `is_trivially_copyable` só autoriza representação/cópia, não construção. Correção:
  - `directory_workspace::acquire` → `page_ = new (memory) image_directory;` (placement
    **default-init**, sem `{}`, portanto sem temporário de 8 KiB) seguido de `page_->clear()`
    para o estado lógico in-place.
  - `release()` → `page_->~image_directory();` explícito antes de desalocar (hoje trivial;
    garantido por `static_assert(std::is_trivially_destructible<image_directory>)`, que força
    revisão se o tipo deixar de ser trivial). `is_trivially_copyable` mantido (cópia sob mutex).
  - Guards: o contrato e o teste de stack **permitem** a forma default-init e rejeitam
    especificamente qualquer forma com chaves (`image_directory{`), que é a value-init
    regressiva; também exigem `page_->~image_directory()` e proíbem `memset` como inicialização.
- Evidence (HEAD `1639285` + worktree sujo; sem flash):
  - `make -C tests/host/keymap test_image_stack_frame_contract` → `0 failures`:
    `host -fstack-usage: chain=368 B (offer inlined), budget=6144 B` e
    `elf frames: fail=704B, publish=416B, run_job=1600B, start=448B, stop=704B, worker_entry=896B`
    (o guard de ELF obsoleto — adicionado pelo tester, fail-closed por mtime — passou após rebuild).
  - `riscv32-esp-elf-objdump -d build/cyberdeck5.elf`: `run_job` agora abre com
    `addi sp,sp,-800` (antes `-848` + `lui/add sp,sp` de 8192); nenhum ajuste de 8 KiB.
  - `source ~/esp/esp-idf/export.sh && idf.py build` → **EXIT 0**, `cyberdeck5.bin`
    **0x2a13d0**, 34% livre (0x15ec30).
  - Focados: `test_image_bmp` 82, `fit` 222, `decode` 50, `list` 139, `view` 70 (ASan+UBSan),
    `worker_state` 788, `test_image_viewer_contract` 0 falhas — todos verdes.
  - `git diff --check` → limpo.
- Supersedes or discards: a implementação anterior com `memset` bruto (correta em frame, mas UB de
  lifetime) e a afirmação de que BUG-IMG-02 estava encerrado. Frame e lifetime agora são
  simultaneamente válidos; permanece pendente a tentativa física única (não feita — sem flash),
  revalidação independente do tester e coverage global/CI.

### 2026-11-08 16:38 -03:00 - plano B aprovado: promoção do pedido fora da janela 64

- Fact: causa do `image: not found in directory` era **janela**, não comando/nome:
  `run_job()` chamava `list_images()` (top-64) e `find_image()` só procurava ali;
  com ≥78 BMP em `/screenshots`, `print_20261020_172101.bmp` ordena depois das 64
  primeiras. Implementado o plano B aprovado, mantendo o cap 64:
  - `list_images(dir, out, ops, requested = {})` ganhou o pin opcional. O próprio
    scan valida elegibilidade (extensão, `k_max_name_bytes`, arquivo regular,
    não vazio, ≤ `k_max_file_bytes`, symlink) antes de marcar o pedido como
    visto, então o pin **não** contorna confinamento nem limites.
  - Fechamento: se o pedido foi visto e não está na página cheia, ele substitui
    `entries[count-1]` (o maior da janela), a página é reordenada, `truncated`
    passa a verdadeiro e o índice do pedido é `cap-1` (`64/64+`). Sem duplicata,
    `count` nunca passa de `cap`, e um pedido já presente não altera a página.
    Cancelamento/erro continuam limpando a página (sem página parcial).
  - App: a abertura explícita passa `value.name.data()` como pin; o scan de
    navegação (`by_index`) **não** promove. A abertura real refaz
    `open_readonly_stream` após o scan (TOCTOU fail-closed, sem frame/indicador).
- Evidence (HEAD `1639285` + worktree sujo; sem flash, sem retry físico):
  - `make -C tests/host/keymap test_image_list` → **1003 checks, 0 failures**,
    com os cenários novos: PATH-06 (promoção cap+1 e 78, forma exata, índice,
    `64/64+`, e bytes reais decodificados do arquivo pedido via seam + decoder),
    PATH-07 (arquivo removido após o scan → `open_readonly` < 0; pin inexistente
    não altera a página), PATH-08 (png, diretório `x.bmp`, vazio, >5 MiB,
    symlink e nome 129 **não** entram; nome de exatamente 128 é promovido;
    `split_image_path("/a/../../b.bmp")` e `list_images("/sdcard", …)` seguem
    falsos), NAV-10 (cap+1 e 78, extremos), NAV-11 (rank 10 e rank `cap` sem
    perturbação; `== cap` sem `+`; `< cap`; idempotência), NAV-12 (2×cap, rank
    90, vítima = último ordenado, enumeração reversa com probe real de
    arquivo regular, 3 execuções idênticas), NAV-13 (cancelamento no 1º poll e
    após 3 entradas com página promovida semeada → limpa; diretório ausente →
    limpa; clamp `cap-1`/`0`).
  - Demais focados: `test_image_bmp` 82, `fit` 222, `decode` 50, `view` 70
    (ASan+UBSan), `worker_state` 788, `test_image_stack_frame_contract` 0 falhas
    (`host chain=160 B`; ELF `run_job=816B`, `worker_entry=448B`, `fail=352B`,
    `publish=208B`, `start=224B`, `stop=352B`), `test_image_viewer_contract`
    0 falhas (novas asserções do pin e do parâmetro default).
  - `source ~/esp/esp-idf/export.sh && idf.py build` → **EXIT 0**,
    `cyberdeck5.bin` **0x2a1cc0**, 34% livre (0x15e340).
  - `git diff --check` → limpo.
- Fact: versionamento conforme `AGENTS.md` — mudança funcional de app: manifesto
  `cyberdeck.image_viewer` `1.0.0` → `1.0.1` e firmware PATCH `1.15.3` →
  `1.15.4` (sem MAJOR, sem tag). O contrato deriva o piso de `version.txt`.
- Fact: documentação atualizada no mesmo trabalho: `code-map.md` (linha da
  lista com o pin e "posição na janela", linha do app, matriz REQ/AC-IMG-01 com
  PATH-06..08 e REQ/AC-IMG-03 com NAV-10..13), `README.md`/`README.pt-BR.md`,
  `docs/ARCHITECTURE.md`/`.pt-BR.md` e roteiro manual (A1b e C4/C4b com o caso
  do arquivo fora da janela e o significado do `+`).
- Supersedes or discards: o achado de 16:10 tratado como "arquivo fora da
  janela não abre" fica **resolvido por decisão de produto (plano B)**, não era
  defeito de teste nem de comando. Permanecem pendentes: tentativa física única
  autorizada com o binário 1.15.4 sob monitor (`/screenshots/print_20261020_172101.bmp`),
  setas/Esc reais, mutações independentes do tester (incluindo M-PIN-01..11) e
  coverage global/CI. Nenhum risco material novo de segurança/performance
  apareceu: cap, filtros, confinamento, stack e lifetime inalterados.

### 2026-11-08 16:52 -03:00 - achado low de docs (versão do manifesto no espelho)

- Fact: corrigido **somente** o espelho de documentação apontado pelo reviewer:
  `tests/host/keymap/help_traceability.md` (TEST-IMG-HELP-02) citava
  `cyberdeck.image_viewer` `1.0.0`, mas o manifesto real
  (`components/cyberdeck/src/apps/image/cyberdeck_image_app.cpp:36`) e o golden de
  `test_shell_help.cpp` estão em `1.0.1` desde o incremento funcional do plano B.
  A linha passa a `1.0.1` e explicita que o valor é a versão do manifesto do app,
  preservando a rastreabilidade TEST-IMG-HELP-01/02/03 e o restante do Ciclo 6.
- Fact: nenhuma linha de produção ou de teste comportamental foi alterada; o
  contrato estrutural continua reconciliando `EXPECTED_APPS`/fixtures/goldens
  contra o manifesto real.
- Evidence (HEAD `1639285` + worktree sujo):
  - `make -C tests/host/keymap test_shell_help_contract` → PASS nos 7 cenários
    (`TEST-1` + `TEST-HELP-APP-MANIFEST-01..05` + `TEST-HELP-WIRING-01`, 8
    mutações rejeitadas).
  - `git diff --check` → limpo.
  - `grep -n '1\.0\.0' tests/host/keymap/help_traceability.md` → sem ocorrências.
- Fact: o reviewer confirmou que a autorização existente do usuário cobre **uma**
  tentativa física do caminho exato `/screenshots/print_20261020_172101.bmp` após
  implementação/revisão B. Nenhum device/flash foi executado por mim.
- Supersedes or discards: o achado low de documentação de 16:4x fica encerrado.
  Permanecem pendentes (fora deste papel): tentativa física única autorizada com
  o binário `1.15.4` sob monitor, mutações independentes do tester (M-PIN-01..11)
  e coverage global/CI.

### 2026-11-08 18:05 -03:00 - POST /control/upload (transporte autenticado de fixture BMP full-res)

- Fact: implementação concluída no snapshot **HEAD
  `16392858881d775bb68a6733fffcfa67223ab808`** + worktree sujo
  (`git diff | sha256sum` = `ca8a3a8f0f76361d19211312770224cc89494f6a86d831bf747a2327b92ec1f3`,
  47 entradas em `git status --porcelain`, preservando todo o trabalho anterior
  do image-viewer). `version.txt` **1.15.4 → 1.16.0** (MINOR direto, sem tag);
  manifesto `cyberdeck.screenshot` **1.1.0 → 1.1.1** com recursos/capabilities
  `storage`; manifesto `cyberdeck.image_viewer` **intocado** em `1.0.1`.
- Fact: novo transporte conforme o plano 17:29 (`POST /control/upload`):
  - `apps/control/cyberdeck_control_protocol.{h,cpp}`: `k_upload_domain`
    (`cyberdeck-control-upload/v1`), `k_upload_uri`, `build_upload_message`
    (domínio + nonce + método + URI + tamanho + digest) e `verify_upload`
    (nonce de uso único; MAC inválido não queima o desafio; MAC válido consome);
    novos erros `invalid_request`/`invalid_path` (400), `conflict` (409),
    `checksum` (422), `storage_unavailable` (503), `io_error` (500).
  - `apps/control/cyberdeck_control_upload.{h,cpp}` (TU puro, host-testável):
    `k_max_upload_bytes = 3 MiB` com `static_assert < k_max_file_bytes`, nome
    basename `[A-Za-z0-9._-].bmp` com limite de `k_max_name_bytes`, destino fixo
    `/sdcard/screenshots`; `receive_upload` com digest incremental, contagem
    exata, `fsync`+`close` com `fstat`, publicação sem clobber e `discard`
    somente do temporário próprio; sink `upload_files` com
    `.upload.<seq>.tmp` `O_CREAT|O_EXCL|O_NOFOLLOW`, 8 tentativas (nunca remove
    nome alheio), host `link`+`unlink`, ESP `stat`+`rename` (mesmo precedente do
    `fs.write`); `format_upload_json` determinístico.
  - `cyberdeck_control_server.{h,cpp}`: `upload_handler` com ordem peer →
    `ready` → limites → single-flight `try_lock` → basename/tamanho/digest →
    `authorize_upload` (lock curto, nunca lê corpo sob `s_mutex`) → lease WRITE
    do `cyberdeck_sd_io` → transação; digest mbedtls incremental com RAII,
    buffer fixo `s_upload_scratch[4096]`, timeouts consecutivos
    (`k_max_recv_timeouts`), `Connection: close` em erro,
    `record_global_persistence_failure()` em I/O, **sem** gate de
    `screen_off_is_active` (upload é FS-only) e sem logar chave/corpo/MAC/digest.
  - `screenshot_server.cpp`: `max_uri_handlers = 8` (8 entradas reais) e rota
    `POST /control/upload`, mantendo o rollback `httpd_stop`/`ESP_FAIL`.
  - `tools/cyberdeck_http_cli.py`: `upload <arquivo> [--name]` com validação
    local (regular, 0 < tamanho ≤ 3 MiB, basename `.bmp`), SHA-256 local,
    mensagem de domínio idêntica ao firmware, headers `X-CTL-Name/Size/Digest`,
    `Content-Length` e comparação do `sha256` devolvido; imprime o comando
    auxiliar `image /screenshots/<nome>`.
- Evidence (executados no estado acima):
  - `make -C tests/host/keymap test_control_upload_protocol` →
    `PASS: control upload protocol (160 checks)`. Cobre layout/domínio da
    mensagem, MAC de `/control/type` rejeitado no upload, tamper de
    tamanho/digest, replay/expiração, nomes (incl. limite 128/129, `..`,
    separadores, não-ASCII/NUL), tamanho (0, cap, cap+1, sinais, overflow),
    segmentações 1/7/4096/mix, digest independente de segmentação, truncado,
    timeout, erro de socket, falha de write/digest/fsync/close/publish, conflito
    antes de abrir temp, ownership real (arquivo `KEEP` preservado, alvo
    byte-idêntico, symlink, sem overwrite) e JSON pinado.
  - `make -C tests/host/keymap test_control_upload_contract` →
    `PASS: authenticated upload contract`. Deriva `max_uri_handlers` da tabela,
    exige `authorize_upload` antes de abrir/receber, ausência de I/O de arquivo
    no handler, `try_lock`, lease WRITE, buffer fixo (sem alocação por corpo),
    `O_EXCL/O_NOFOLLOW/fsync/link/rename`, CMake + TU puro **fora** da allowlist
    de cobertura, CLI com limite/domínio derivados do firmware e docs.
  - `make -C tests/host/keymap test_control_protocol test_control_cli
    test_control_http_contract` → `123 checks` PASS, `Ran 9 tests ... OK`,
    `PASS: LAN HTTP command channel contract` (inclui novos mapeamentos de erro
    e 4 casos de CLI/upload).
  - Regressões focadas: `test_fs_write_dispatch` (118 checks) PASS,
    `test_fs_write_cli` PASS, `test_fs_write_contract` PASS,
    `test_sd_io_coordinator` PASS, `test_image_bmp` 82/0, `test_image_fit`
    222/0, `test_image_list` 1028/0, `test_image_viewer_contract` 0 falhas,
    `test_shell_help` 249/0, `test_shell_help_contract` PASS (8 mutações),
    `test_system_apps_contract` PASS.
  - `source ~/esp/esp-idf/export.sh && idf.py build` → `Project build complete`;
    `build/cyberdeck5.bin` **0x2a3530** (2.766.128 B), SHA-256 prefixo
    `badaa225ccb1f6b9`, 34% livre; sem warning novo (apenas o aviso pré-existente
    `kill is not implemented` da libssh).
  - `git diff --check` limpo. Varredura de valores antigos (`1.15.4`, `1.1.0`,
    `max_uri_handlers = 7`, "three /control routes") sem ocorrências fora de
    notas históricas; documentação atualizada (`code-map.md` com
    `cyberdeck_control_upload` e matriz REQ/AC-UPLOAD, `README(.pt-BR)`,
    `ARCHITECTURE(.pt-BR)`, `tests/manual/http-control-validation` §6,
    `tests/manual/image-viewer-validation` e `.opencode/memory/lan-control-channel.md`).
  - **Não executado por instrução**: suíte agregada (`make test`), gcovr/scope
    guard real, `test_incremental_final_contract`, mutações M-UP-*, CI, flash e
    qualquer comando de dispositivo.
- Decisions and assumptions:
  - A mensagem autenticada do upload cobre **nonce + método + URI + tamanho +
    SHA-256** (conjunto aprovado); o basename viaja em `X-CTL-Name` e é
    validado no dispositivo, não entra no MAC. Em HTTP sem TLS um intermediário
    já pode alterar o corpo; a integridade do conteúdo é garantida pelo digest.
  - Publicação no FATFS: `stat` do alvo + `rename` depois de `fsync/close`,
    seguindo o precedente do `fs.write`; **não** há promessa de atomicidade
    contra um escritor concorrente (o lease é cooperativo), apenas recusa de
    alvo observado e nunca remoção do destino. Risco residual registrado.
  - "Helper command" do despacho interpretado como o próprio subcomando
    `upload` + a linha `image /screenshots/<nome>` impressa na resposta.
  - Jev não usado: não havia empate técnico reversível; o formato FATFS/host
    seguiu o precedente já aprovado do `fs.write`.
- Pending and risks:
  - Validação independente do `tester` (cenários TEST-UPLOAD-* e mutações
    M-UP-01..10) e gates finais do `reviewer` (suíte agregada, cobertura/scope
    guard, CI).
  - Validação física no Tab5 (TEST-UPLOAD-DEV-01..12) **não executada**; exige
    autorização específica, build/hash, monitor e fixture 1280x720. Nenhum
    device/flash foi tocado por mim.
  - Riscos residuais: nome não autenticado pelo MAC (ver acima); corrida
    TOCTOU de publicação no FATFS aceita como no `fs.write`; `fsync` no VFS
    FATFS pode ser no-op (erro é tratado, mas a durabilidade física não é
    provada); `SHA-256` do corpo é calculado em software na task HTTP.
- Supersedes or discards: encerra o "checkpoint sem wiring/testes/build/nota" do
  ciclo anterior (o código agora está compilado, testado de forma focada e
  documentado). Não altera as decisões B do viewer nem os REQ/AC-IMG-01..05; a
  fixture full-res pode agora ser transportada por `/control/upload`.
- References: `docs/agent-state/image-viewer/planner.md` 17:29;
  `docs/agent-state/image-viewer/tester.md` 17:36; `code-map.md` § Canal HTTP;
  `components/cyberdeck/{include,src}/apps/control/cyberdeck_control_upload.*`,
  `cyberdeck_control_protocol.*`, `cyberdeck_control_server.*`;
  `components/cyberdeck/src/apps/screenshot/screenshot_server.cpp`;
  `components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp`;
  `tools/cyberdeck_http_cli.py`; `tests/host/keymap/{Makefile,
  test_control_upload_protocol.cpp,test_control_upload_contract.py,
  test_control_cli.py,test_control_protocol.cpp,test_control_http_contract.py}`;
  `tests/manual/http-control-validation.pt-BR.md`.

### 2026-11-08 18:05 -03:00 - fechamento do handoff: correções de contrato/escopo e evidência fresca no snapshot `150aedb1…`

- Fact: este é o fechamento do handoff ausente (`@`) do ciclo anterior. Ordem de
  leitura: a entrada anterior (rótulo `18:05`) foi gravada às **17:57** (mtime do
  arquivo antes deste append) e tem o rótulo adiantado; esta é a entrada mais
  recente. Snapshot verificado agora: HEAD `16392858881d775bb68a6733fffcfa67223ab808`,
  `git diff | sha256sum` = `150aedb187c1349ed4b3a8225a6d9e7b6dabc0f399918a1526ea8df739e26a20`
  (48 entradas em `git status --porcelain`), inalterado antes e depois das
  execuções abaixo. Nenhum arquivo de produção/teste foi alterado nesta rodada;
  só esta nota.
- Fact: três defeitos materiais do próprio ciclo foram encontrados e corrigidos
  **antes** deste fechamento (por inspeção + foco, não por suíte agregada):
  1. `tests/host/keymap/test_battery_http_contract.py` pinava
     `max_uri_handlers = 7` e a lista de 7 rotas; com a 8ª rota o contrato
     quebraria sem que nenhum alvo da lista anterior o cobrisse. Agora o
     orçamento é **derivado** da tabela (`len(routes)`), as quatro rotas de
     telemetria continuam exigidas no cabeçalho e `/control/challenge|type|terminal`
     continuam exigidas por nome. Não enfraquece: acrescenta a exigência de que
     toda entrada da tabela seja registrada.
  2. `tools/coverage_scope_guard.py`: o TU puro
     `apps/control/cyberdeck_control_upload.cpp` não estava em
     `REQUIRED_COVERED` (podia sumir da cobertura sem falhar). Adicionado, com
     espelho em `tests/host/keymap/test_coverage_scope_guard.py`.
     `test_control_upload_contract.py` foi ajustado para inspecionar o bloco
     `ALLOWLIST`/`REQUIRED_COVERED` em vez de procurar o caminho no texto cru
     (a checagem antiga passou a dar falso negativo assim que o TU entrou na
     lista obrigatória).
  3. `upload_handler` respondia `401/400/409/503` sem consumir o corpo e sem
     fechar a conexão em quase todos os caminhos (só `not_ready`, `too_large`,
     `busy` e falha de transação fechavam). Agora **toda** recusa posterior ao
     gate de tamanho envia `Connection: close` — o corpo restante no socket não
     pode ser reusado como próxima requisição. Sem mudança de ordem
     auth→arquivo, de contrato de erro ou de mensagem.
  Documental: removido o `max_uri_handlers = 3` obsoleto em `code-map.md`
  (duas ocorrências, já erradas antes deste ciclo) e registrado em
  `tests/manual/http-control-validation.pt-BR.md` §6 que
  `/sdcard/screenshots` precisa existir antes do primeiro upload (a rota não
  cria diretórios e responde erro de I/O quando falta).
- Evidence (executados neste snapshot `150aedb1…`, todos exit 0):
  - `make -C tests/host/keymap test_control_upload_contract test_control_http_contract
    test_battery_http_contract test_coverage_scope_guard test_control_cli
    test_control_protocol test_control_upload_protocol` →
    `PASS: authenticated upload contract`; `PASS: LAN HTTP command channel
    contract`; `PASS: battery HTTP structural contract (REQ-BAT-HTTP-01..05…)`;
    `coverage scope guard tests passed` (a linha `coverage scope error: invalid
    allowlist` é o cenário negativo esperado); `Ran 9 tests … OK`;
    `PASS: control protocol (123 checks)`; `PASS: control upload protocol
    (160 checks)`.
  - `make -C tests/host/keymap test_fs_write_dispatch test_fs_write_cli
    test_fs_write_contract test_sd_io_coordinator test_image_bmp test_image_fit
    test_image_decode test_image_list test_image_view test_image_worker_state
    test_image_viewer_contract test_image_stack_frame_contract test_shell_help
    test_shell_help_contract test_system_apps_contract` →
    `fs_write_dispatch (118 checks)`, `fs_write_cli`, `fs_write structural
    contract`, `cyberdeck_sd_io_coordinator`, `test_image_bmp` 82/0,
    `test_image_fit` 222/0, `test_image_decode` 50/0, `test_image_list` 1028/0,
    `test_image_view` 70/0, `test_image_worker_state` 788/0,
    `test_image_viewer_contract` 0 falhas, `test_image_stack_frame_contract`
    0 falhas (host chain 160 B, budget 6144; frames ELF fail 352/publish 208/
    run_job 816/start 224/stop 352/worker_entry 448 — **sem SKIP**),
    `shell help` 249/0, `test_shell_help_contract` PASS (8 mutações),
    `test_system_apps_contract` PASS. Os dois alvos C++ de protocolo/upload
    foram recompilados com `-B` no ciclo anterior sobre fontes inalteradas.
  - `git diff --check` → exit 0; `git diff | sha256sum` continua `150aedb1…`.
  - Build ESP-IDF **não repetido por não haver mudança de fonte de firmware**:
    `find components/cyberdeck main components/m5stack_tab5 components/sock_utils
    components/usb_host_uac -newer build/cyberdeck5.bin` = 0 arquivos (última
    fonte de firmware `cyberdeck_control_server.cpp` 18:02:36; bin 18:03:13) e
    `ninja -C build -n all` mostra 0 passos de compilação/link. Artefato
    correspondente ao snapshot: `build/cyberdeck5.bin` **0x2a3630**
    (2.766.384 B), SHA-256
    `04e3d43e6bb37e517b08dd6949dc2a7b45da85e5148903c08f5eb47eb66b07ab`,
    34% livre; único warning o pré-existente `kill is not implemented` da libssh.
  - **Não executado por instrução**: suíte agregada (`make test`), gcovr/cobertura
    global real, `test_incremental_final_contract` (ver risco abaixo), mutações
    M-UP-*, CI, flash e qualquer comando de dispositivo. Nenhuma execução sob
    ASan/UBSan nesta rodada (o alvo puro compila com `CXXFLAGS` padrão); o
    tester pode rodar sanitizers no fechamento.
- Decisions and assumptions:
  - O fechamento do contrato de bateria foi tratado como **regressão da rota
    nova** (dependência mínima, mesmo módulo), não como melhoria adjacente:
    manter o literal `7` deixaria o gate verde apenas por omissão de execução.
  - `Connection: close` em toda recusa pós-tamanho foi escolhido em vez de
    drenar o corpo: drenar até 3 MiB de um peer não autenticado é custo sem
    benefício e o cliente CLI abre conexão por requisição.
  - O diretório de destino não é criado pela rota (decisão registrada): criar
    diretório seria ampliar escopo e esconderia erro de provisionamento do SD;
    o manual passa a exigir `mkdir /screenshots` uma vez.
- Pending and risks:
  - `test_incremental_final_contract` permanece fora desta rodada: a tentativa
    direta falhou por depfile obsoleto de um alvo do baseline
    (`test_time_sync: header rebuilds unrelated test_time_sync/cJSON.o`), o que
    exige `make -C tests/host/keymap test-build` antes; pertence ao fechamento
    do reviewer e **não** é evidência de defeito do upload.
  - Risco residual inalterado: nome do arquivo trafega em `X-CTL-Name` e não é
    autenticado pelo MAC (validado no dispositivo); corrida TOCTOU de publicação
    no FATFS aceita como no `fs.write`; `fsync` no VFS FATFS pode ser no-op.
  - Validação independente do `tester` (TEST-UPLOAD-* e mutações M-UP-01..10) e
    gates finais do `reviewer` (agregada, cobertura/scope guard, CI) pendentes.
  - **Validação de dispositivo NÃO executada**: TEST-UPLOAD-DEV-01..12 seguem
    pendentes de autorização específica, build/hash e monitor; nenhum comando
    de device ou flash foi feito. Não chamar este ciclo de aprovado.
- Supersedes or discards: substitui no item `### 2026-11-08 18:05` (a) o snapshot
  `ca8a3a8f…/47 entradas` e o binário `0x2a3530`/prefixo `badaa225`, agora
  `150aedb1…/48` e `0x2a3630`/`04e3d43e…`; (b) a afirmação "varredura de
  `max_uri_handlers = 7` sem ocorrências", que era **incorreta** — o literal
  existia em `test_battery_http_contract.py` (e `= 3` em `code-map.md`), ambos
  corrigidos aqui; (c) a alegação de `Connection: close em erro`, que era
  parcial e agora vale para todas as recusas. Permanecem válidos: desenho de
  autenticação com domínio/tamanho/digest, semântica de nonce, lease WRITE,
  temporário exclusivo/cleanup, limites 3 MiB/128 e decisões REQ/AC-IMG-01..05 +
  plano B do viewer.
- References: `docs/agent-state/image-viewer/planner.md` 17:29;
  `docs/agent-state/image-viewer/tester.md` 17:36; `code-map.md` § Canal HTTP;
  `tests/host/keymap/{test_battery_http_contract.py,test_control_upload_contract.py,
  test_coverage_scope_guard.py,test_control_upload_protocol.cpp}`;
  `tools/coverage_scope_guard.py`;
  `components/cyberdeck/src/apps/control/cyberdeck_control_server.cpp`;
  `tools/cyberdeck_http_cli.py`; `tests/manual/http-control-validation.pt-BR.md`
  §6.

### 2026-11-08 18:20 -03:00 - TEST-UPLOAD-AUTH-12 corrigido: basename autenticado nos 7 campos

- Fact: bloqueio do tester (`tool_1bd046cc5001AhN0Tg2MT5I1BZ`) fechado. Antes, a
  mensagem do upload vinculava domínio+nonce+método+URI+tamanho+digest e o
  basename viajava só em `X-CTL-Name`, validado mas **não autenticado**: um
  intermediário podia trocar o nome e o corpo seria publicado com MAC válido
  (misatribuição silenciosa). O plano 17:29 exige nome autenticado com
  tamanho/hash. Agora o basename é o 5º campo assinado.
- Fact: produção alterada (somente o módulo de controle):
  - `cyberdeck_control_protocol.h`: `k_upload_method[]`, `k_upload_name_capacity = 128`,
    `k_upload_size_digits = 20`, e `k_max_upload_message_bytes` **derivado**
    (`k_upload_message_bytes = domain + 1 + nonce + 1 + method + 1 + uri + 1 +
    name_capacity + 1 + size_digits + 1 + digest`, mais NUL) em vez do literal
    192; `static_assert`s cobrem o menor `.bmp` e o pior caso. Assinaturas de
    `build_upload_message`/`verify_upload` recebem `const char *name` entre a URI
    e o tamanho (7 valores, 6 `\n` no `snprintf`).
  - `cyberdeck_control_protocol.cpp`: layout `"%s\n%s\n%s\n%s\n%s\n%llu\n%s"`;
    `verify_upload` rejeita nome nulo (401) e encaminha o nome à mensagem.
  - `cyberdeck_control_server.cpp`: `authorize_upload(req, name, declared_size,
    digest_hex)` e a chamada `authorize_upload(req, name, declared, digest_text)`
    usam **exatamente** o basename já validado que depois é publicado — nome
    validado, autenticado e publicado são o mesmo ponteiro/objeto.
  - `tools/cyberdeck_http_cli.py`: `upload_signed_headers(key, nonce, name, size,
    digest_hex)` assina `domain\nnonce\nPOST\nuri\nname\nsize\ndigest`, chamado
    com `target` (o nome já validado localmente).
  - `cyberdeck_control_upload.h`: `static_assert(k_max_upload_name_bytes <=
    k_upload_name_capacity)` liga o limite do visualizador ao teto da mensagem,
    e o comentário de escopo registra o nome autenticado.
- Fact: goldens atualizados (pedido do tester, sem enfraquecer asserção):
  - `test_control_upload_protocol.cpp`: `sign_upload(nonce, name, size, digest)`;
    layout esperado passa a `…\n/control/upload\nfixture.bmp\n42\ndigest`; todas
    as chamadas de `verify_upload` recebem o nome. Casos novos: nome diferente
    no MAC → 401; nome nulo → 401; mensagem com basename diferente difere; o
    maior basename (128 B) + maior `uint64` cabem na capacidade derivada e a
    mensagem fica NUL-terminada. Preservado o caso TOCTOU de publicação que o
    tester acrescentou.
  - `test_control_cli.py`: mensagem esperada do caso
    `test_upload_signs_domain_size_and_digest` passa a incluir `fixture.bmp`;
    o `test_upload_mac_binds_basename` do tester não foi tocado e agora passa.
- Evidence (snapshot HEAD `16392858881d775bb68a6733fffcfa67223ab808`,
  `git diff | sha256sum` = `c2ccb016831f3ef40e419b870533bc03304fee83ca22743cf9450068f648504a`,
  48 entradas em `git status --porcelain`, `version.txt` = 1.16.0):
  - `make -B -C tests/host/keymap test_control_upload_protocol` →
    `PASS: control upload protocol (196 checks)` (0 falhas).
  - `make -C tests/host/keymap test_control_upload_contract` →
    `PASS: authenticated upload contract` — **AUTH-12 verde** (os seis grupos
    do contrato avaliados; antes falhavam handler_order/protocol/wiring).
  - `make -C tests/host/keymap test_control_cli` → `Ran 10 tests … OK`
    (inclui o caso comportamental de binding do basename).
  - `make -C tests/host/keymap test_control_protocol` → `PASS: control protocol
    (123 checks)`; `test_control_http_contract` → `PASS: LAN HTTP command channel
    contract`; `test_battery_http_contract` → `PASS`; `test_coverage_scope_guard`
    → `coverage scope guard tests passed` (linha de erro é cenário negativo).
  - `git diff --check` → exit 0.
  - `idf.py build` (ESP-IDF v6.1, firmware mudou) → `Project build complete`;
    `build/cyberdeck5.bin` **0x2a3670** (2.766.448 B), SHA-256
    `f593ab61e5ba323bf828774f061f5f28656b75659f8aa1b89df45553fa63c81c`,
    34% livre; único warning o pré-existente `kill is not implemented` (libssh).
  - Docs/rastreabilidade ajustados à nova semântica: `README.md`,
    `README.pt-BR.md`, `docs/ARCHITECTURE.md`, `docs/ARCHITECTURE.pt-BR.md`
    (mensagem de 7 campos), `tests/manual/http-control-validation.pt-BR.md`
    (§6 caso U6b de adulteração do `X-CTL-Name`, §8 lista o basename como
    autenticado), `code-map.md` (parágrafo do transporte, tabela de símbolos com
    `k_upload_name_capacity`/`k_upload_message_bytes`, REQ-UPLOAD-01 com
    “basename autenticado”) e `.opencode/memory/lan-control-channel.md`.
- Decisions and assumptions:
  - `k_upload_name_capacity = 128` é o teto **do protocolo**; o limite aceito
    continua derivando do viewer (`k_max_name_bytes`) e o `static_assert` no
    header do upload impede divergência. Nenhum literal de teste foi pinado.
  - `version.txt` **mantido em 1.16.0**: o MINOR não tem tag e o transporte
    ainda não foi gravado no dispositivo, então a correção pertence ao mesmo
    conjunto não liberado (AGENTS permite compartilhar o MINOR sem tag). Quem
    for validar deve identificar o build pelo SHA-256 `f593ab61…`, não pela
    versão. Se o developer preferir PATCH, é um one-liner, mas muda o piso do
    contrato (`>= 1.16.0` continua válido).
  - Ordem dos campos: nome **antes** do tamanho, conforme a lista canônica do
    contrato do tester (domain, nonce, method, URI, name, size, digest).
- Pending and risks:
  - Nota do tester (17:36+) continua **não appendada**, por sua própria sessão;
    os itens de docs que ele listou (code-map REQ-UPLOAD-01 e cenário de
    adulteração no manual §6) foram cobertos aqui, restando a ele registrar o
    append e reconfirmar `git diff --check` no snapshot.
  - Continua pendente: suíte agregada, gcovr/cobertura global, mutações M-UP-*
    (o tester já matou 12/12 em espelho; o estado mudou depois), CI e
    **validação no Tab5** (TEST-UPLOAD-DEV-01..12) — nenhum flash/device.
  - Risco residual: corrida TOCTOU de publicação no FATFS (aceita, `f_rename`
    devolve `FR_EXIST` sem sobrescrever segundo o tester); `EEXIST` de rename
    no ESP vira 500 em vez de 409 (cosmético, alvo já recusado antes); `fsync`
    no VFS FATFS pode ser no-op.
- Supersedes or discards: encerra o risco residual “nome não autenticado pelo
  MAC” registrado na entrada de 18:05 e o bloqueio do tester; substitui o
  binário `0x2a3630`/`04e3d43e…` por `0x2a3670`/`f593ab61…` e o snapshot
  `150aedb1…` por `c2ccb016…`. Permanecem válidos: no-clobber, validação de
  nome/path, semântica de nonce (MAC inválido não consome, válido consome),
  lease WRITE, buffer fixo, single-flight e o recorte REQ/AC-IMG-01..05.
- References: `tests/host/keymap/{test_control_upload_protocol.cpp,
  test_control_upload_contract.py,test_control_cli.py}`;
  `components/cyberdeck/{include,src}/apps/control/cyberdeck_control_protocol.*`,
  `cyberdeck_control_server.cpp`, `cyberdeck_control_upload.h`;
  `tools/cyberdeck_http_cli.py`; `code-map.md` § Canal HTTP;
  `tests/manual/http-control-validation.pt-BR.md` §6/§8;
  `docs/agent-state/image-viewer/tester.md` 17:36; output
  `tool_1bd046cc5001AhN0Tg2MT5I1BZ`.

### 2026-11-08 18:39 -03:00 - TEST-UPLOAD-STREAM-11: deadline absoluto por request (S11-a..g)

- Fact: blocker do reviewer 18:28 fechado. Antes, `http_upload_read` só contava
  `k_max_recv_timeouts` **consecutivos** e zerava o contador a cada leitura
  positiva; um peer que entrega ≥1 byte antes de cada janela mantinha a única
  task HTTP, o lease WRITE e o temporário abertos indefinidamente (3 MiB de teto
  não limita tempo). Agora existe orçamento **absoluto por request** no módulo
  puro, independente do tamanho e da cadência.
- Fact: produção alterada (somente módulo de controle):
  - `cyberdeck_control_upload.h`: `k_max_upload_duration_us = 120'000'000`
    (`int64`, positivo, `static_assert`; NÃO deriva de `declared_size`) —
    corresponde a ≥ 26 KiB/s para o teto de 3 MiB (fixture 2.764.854 B exige
    ≥ 23 KiB/s); `struct upload_clock { void *context; std::int64_t (*now_us)(void *); }`;
    `k_upload_max_consecutive_timeouts = 3` como defesa secundária;
    `receive_upload` recebe `const upload_clock &clock`.
  - `cyberdeck_control_upload.cpp`: `started_us` lido **uma vez** no início;
    no topo de cada iteração `clock.now_us(...) - started_us >=
    k_max_upload_duration_us` → `upload_status::timeout` (boundary inclusivo:
    D-1 continua, D aborta); `-1` do read passou a ser **retentável** com
    contador consecutivo (zera em chunk positivo) e só aborta ao atingir o
    orçamento — é a defesa secundária agora no módulo puro, condição para
    S11-b ser injetável. Todo abort continua por `discard` do temporário
    próprio, sem publish.
  - `cyberdeck_control_server.cpp`: `upload_stream_now()` (único site de
    `esp_timer_get_time()` no caminho de stream) é injetado como
    `upload_clock`; `http_upload_read` deixou de reter o loop de timeouts e
    reporta `HTTPD_SOCK_ERR_TIMEOUT` como `-1` (uma tentativa por chamada).
    `k_max_recv_timeouts` permanece no servidor para `/control/type` e
    `/control/terminal` (`read_body`/`drain_body`) — não foi removido.
  - Inalterados: buffer fixo 4096, lease WRITE RAII por handler, single-flight,
    validação/auth/publicação sem clobber, nonce.
- Evidence (snapshot HEAD `16392858881d775bb68a6733fffcfa67223ab808`,
  `git diff | sha256sum` = `51b678d7c4be365ac0186169072a4efda9818839da76649dc24ae720f7927a59`,
  48 entradas, `version.txt` 1.16.0):
  - `make -B -C tests/host/keymap test_control_upload_protocol` →
    `PASS: control upload protocol (290 checks)` — cobre S11-a (progresso
    contínuo sem `-1` aborta no deadline), S11-b (parcial + `-1` intercalados,
    secundário nunca esgota, aborta no deadline), S11-c (dentro do prazo: `ok`,
    publish 1x, close antes de publish, bytes/digest exatos, 0 discard), S11-d
    (D-1 conclui, D e D+1 abortam — derivado de `k_max_upload_duration_us`),
    S11-e (64 B e teto de 3 MiB com a **mesma** constante), S11-f (discard 1x do
    temp próprio, publish 0x, outcome zerado, sem write/close após falha) e
    S11-g (segundo request no mesmo relógio falso tem orçamento novo). Relógio
    falso determinista (`fake_clock` com agenda por chamada), fake stream com
    script (`{4}`, `{64,-1}`, `{1000}`…), cap de chamadas e asserção de que o
    cap não é atingido; `clock.calls == reads + 2` em abort prova ≤1 leitura por
    iteração + início; nenhuma dependência de wall clock.
  - `test_control_upload_contract` → `PASS: authenticated upload contract`
    (novo grupo `test_deadline`): constante e seam no header, comparação `>=`
    inclusiva, nenhum termo de relógio de parede/sleep na TU pura, ≤1 leitura de
    clock por iteração, `esp_timer_get_time()` exatamente 1x em
    `upload_stream_now`, clock injetado em `receive_upload`, `k_max_recv_timeouts`
    preservado no leitor de texto, e `timeouts` ausente do adapter de upload.
  - `test_control_cli` → `Ran 10 tests … OK`; `test_control_protocol` → 123
    checks; `test_control_http_contract` → PASS; `test_battery_http_contract` →
    PASS; `test_fs_write_dispatch` 118 / `test_fs_write_cli` /
    `test_fs_write_contract` / `test_sd_io_coordinator` → PASS.
  - `git diff --check` → exit 0 (rodado após as edições finais).
  - `idf.py build` (ESP-IDF v6.1, firmware mudou) → `Project build complete`;
    `build/cyberdeck5.bin` **0x2a3790** (2.766.544 B), SHA-256
    `cc797330d78714acd5a618749f7daf04579f8907d8a2fdc25d9972f71daa6bbf`,
    34% livre; único warning o pré-existente `kill is not implemented` (libssh);
    `ninja -n` = 0 passos pendentes de compilação/link.
  - Docs: `code-map.md` (parágrafo do deadline, símbolos
    `k_max_upload_duration_us`/`k_upload_max_consecutive_timeouts`/`upload_clock`,
    REQ-UPLOAD-02 → STREAM-01..11), `README(.pt-BR)` e
    `docs/ARCHITECTURE(.pt-BR)` (prazo absoluto), manual HTTP §6 **U8b**
    (progresso lento ad-hoc, pendente de autorização) e §8 (limite de 120 s),
    `.opencode/memory/lan-control-channel.md`.
- Decisions and assumptions:
  - O contador de timeouts consecutivos foi movido para o módulo puro. Sem isso
    S11-b é inexequível: no contrato anterior um único `-1` era terminal, então
    "parcial + `-1` sem esgotar o secundário" não podia ser injetado. A
    propriedade de STREAM-05 passa a ser provada no host (um timeout seguido de
    progresso não aborta; três consecutivos abortam) e o adapter ficou mais
    simples. O teste antigo "um `-1` isolado aborta" foi **substituído** por
    essas duas asserções — não foi enfraquecido.
  - 120 s: folga de ~3 ordens de grandeza sobre a taxa real de LAN e limite
    objetivo de ocupação; documentado como derivado da taxa mínima aceitável,
    não de `declared_size`.
  - `version.txt` mantido em **1.16.0** (MINOR sem tag; o transporte ainda não
    foi gravado no dispositivo). Identificar o build por `cc797330…`.
- Pending and risks:
  - Mutações M-UP-15..20: especificadas pelo tester, **não** executadas nesta
    rodada (não estava no gate do coder); devem ser rodadas em cópia descartável
    na validação independente. O desenho do teste já torna M-UP-15/16/19
    detectáveis (cap → `io_error`) e M-UP-17/20 pelos pares de borda/S11-g.
  - Validação física pendente: DEV-11 cobre silêncio total; a variante de
    **progresso lento** (U8b) exige cliente ad-hoc e autorização específica.
  - Sem suíte agregada, cobertura global, CI, flash ou device nesta rodada.
  - Limite conhecido: a garantia depende de fonte monotônica que progrida;
    relógio travado não atinge o deadline (sem teste útil, registrado pelo
    tester).
- Supersedes or discards: substitui a afirmação da entrada de 18:20 de que um
  timeout de recv isolado aborta imediatamente (agora retenta até 3
  consecutivos sem progresso) e o binário `0x2a3670`/`f593ab61…` por
  `0x2a3790`/`cc797330…`; snapshot `c2ccb016…` → `51b678d7…`. Permanecem
  válidos: basename autenticado (AUTH-12), no-clobber, validação de nome/path,
  semântica de nonce, lease WRITE, buffer fixo, single-flight.
- References: `components/cyberdeck/{include,src}/apps/control/cyberdeck_control_upload.*`,
  `cyberdeck_control_server.cpp`; `tests/host/keymap/{test_control_upload_protocol.cpp,
  test_control_upload_contract.py}`; `code-map.md` § Canal HTTP;
  `tests/manual/http-control-validation.pt-BR.md` §6/§8;
  `docs/agent-state/image-viewer/reviewer.md` 18:28;
  `docs/agent-state/image-viewer/tester.md` 18:33.

### 2026-11-08 19:14 -03:00 - checkpoint final: clang-format 23.1.3 e wiring do format gate

- Fact: blocker `medium` do reviewer 19:10 fechado. Os arquivos novos de upload
  e do visualizador falhavam `clang-format 23.1.3 --dry-run --Werror` e o step
  "Format C++ and Python" de `.github/workflows/quality-gate.yml` não os
  listava. Corrigi **apenas formatação** nos arquivos novos (produção e TUs de
  teste) com a versão pinada, e adicionei os caminhos ao step.
- Fact: arquivos formatados (18) — produção:
  `include/apps/control/cyberdeck_control_upload.h`,
  `src/apps/control/cyberdeck_control_upload.cpp`,
  `include/apps/image/cyberdeck_image_{bmp,decode,fit,list}.h`,
  `src/apps/image/cyberdeck_image_{app,bmp,decode,fit,list}.cpp`; testes:
  `test_control_upload_protocol.cpp`, `test_image_{bmp,decode,fit,list,view,worker_state}.cpp`.
  (`cyberdeck_image_{app,types,worker_state}.h`, `cyberdeck_image_view.{h,cpp}` e
  `cyberdeck_image_worker_state.cpp` já passavam; foram incluídos no gate.)
- Fact: preservação semântica provada por comparação de tokens: para cada um dos
  18 arquivos, o conteúdo com todo espaço em branco e continuações `\` removidos
  é **idêntico** antes/depois (`TOKEN-EQ`, 18/18). O diff é só linha dobrada/
  desdobrada (ColumnLimit 120) e macros `do { } while (false)` reindentadas —
  nenhum identificador, literal, include ou ordem de token mudou.
- Fact: CI — novo bloco `clang-format --dry-run --Werror` no step
  "Format C++ and Python" com os 17 headers/sources de produção novos e as 7 TUs
  novas (24 caminhos). YAML validado (`pyyaml`: 11 steps, bloco presente) e o
  comando exato do bloco passa com o binário pinado 23.1.3
  (`/tmp/opencode/cf-venv/bin/clang-format`). Nada além desse step foi tocado.
- Fact: um contrato estrutural era sensível a espaço em branco e passou a falhar
  após a reflow: `test_control_upload_contract.py` exigia o literal
  `"if (errno != EEXIST) return false;"`. Troquei por regex com `\s*`
  (`if\s*\(\s*errno\s*!=\s*EEXIST\s*\)\s*return\s+false\s*;`) — mesma condição,
  sem enfraquecer; nenhum outro contrato precisou mudar.
- Evidence (snapshot HEAD `16392858881d775bb68a6733fffcfa67223ab808`,
  `git diff | sha256sum` = `1fd3f767fefb56180a0802b7af8b3151e5bf7745f6ec7865a7ba51d6ed6232e2`,
  49 entradas, `version.txt` 1.16.0):
  - `clang-format (23.1.3) --dry-run --Werror` nos 18 arquivos → 0 falhas; no
    bloco novo do workflow (24 caminhos) → 0 falhas.
  - `make -C tests/host/keymap test_control_upload_protocol` →
    `PASS: control upload protocol (293 checks)` (contagem estável em duas
    execuções; recompilado após a formatação).
  - `test_control_upload_contract` → `PASS: authenticated upload contract`;
    `test_control_cli` → `Ran 10 tests … OK`; `test_control_protocol` → 123
    checks; `test_control_http_contract` → PASS; `test_battery_http_contract` →
    PASS.
  - `test_image_bmp` 82/0 · `test_image_fit` 222/0 · `test_image_decode` 50/0 ·
    `test_image_list` 1028/0 · `test_image_view` 70/0 ·
    `test_image_worker_state` 788/0 · `test_image_viewer_contract` 0 falhas ·
    `test_image_stack_frame_contract` 0 falhas (host chain 160 B/budget 6144;
    frames ELF `fail=352, publish=208, run_job=816, start=224, stop=352,
    worker_entry=448`, **sem SKIP**; a primeira execução antes do rebuild
    acusou ELF stale — esperado, ver abaixo).
  - `idf.py build` (ESP-IDF v6.1; a formatação tocou fontes de firmware) →
    `Project build complete`; `build/cyberdeck5.bin` **0x2a3790**
    (2.766.544 B), SHA-256
    `c69781518a65d51e896b36f033c4302878f38d2437b1d2f884dbc65fbaa99b07`,
    34% livre; único warning o pré-existente `kill is not implemented` (libssh).
    **Não** é o `9d1f0622…` do reviewer: aquele artefato veio de outro worktree
    (`/tmp/opencode/cyberdeck5-review-final-…`) e o binário embute
    data/hora/caminho de build; o tamanho e o conteúdo-fonte é o mesmo. O
    reviewer deve reconstruir no estado atual e citar o próprio hash.
  - `git diff --check` → exit 0 (após todas as edições).
- Decisions and assumptions:
  - Formatei também as TUs novas de imagem/upload (embora o reviewer tenha
    enumerado headers/sources de produção) porque o despacho exige dry-run em
    **todos** os C++ adicionados e elas falhavam; incluí-las no gate evita
    reincidência. Nenhum arquivo legado fora do escopo foi reformatado.
  - Nenhum artefato gerado entrou no repo; o venv com clang-format 23.1.3 ficou
    em `/tmp/opencode/cf-venv` (fora do worktree) para não alterar dependências.
- Pending and risks:
  - Gates finais reais permanecem com o reviewer: suíte agregada (o PASS 19:11 é
    evidência do usuário para o snapshot `51b678d7…`, **stale** para este
    `1fd3f767…`), cobertura gcovr + `coverage_scope_guard.py` (sem resultado),
    CI remota (verde atual é do HEAD base, stale), e os cenários de dispositivo
    DEV-08..12/U8b (não executados; U8b exige autorização).
  - Warnings `libgcov profiling error … different checksum` em
    `test_image_view` são sidecars `.gcda` antigos vs. binário recompilado, não
    falha; a CI roda `clean` antes, então não afeta o gate.
- Supersedes or discards: substitui o snapshot `51b678d7…` por `1fd3f767…` e o
  binário `0x2a3790`/`cc797330…` por `0x2a3790`/`c6978151…`. Permanecem válidos:
  deadline absoluto, basename autenticado, no-clobber, validação de nome/path,
  nonce, lease WRITE, buffer fixo, single-flight, e todas as decisões do viewer.
- References: `.github/workflows/quality-gate.yml` (step "Format C++ and
  Python"); `.clang-format`; os 18 arquivos listados acima;
  `tests/host/keymap/test_control_upload_contract.py`;
  `docs/agent-state/image-viewer/reviewer.md` 19:10/19:11.

### 2026-11-08 19:56 -03:00 - correção de wiring do `clean` (artefatos de música)

- Fact/snapshot: HEAD `16392858881d775bb68a6733fffcfa67223ab808`; `git diff --binary | sha256sum` = `43002380798a3440c01a851b07a56ee193e882d3a121790bca5c4ca1ae07050a`; 50 entradas em `git status --porcelain`; `version.txt` 1.16.0. Único trabalho desta sessão: `tests/host/keymap/Makefile`, `tests/host/keymap/test_incremental_final_contract.py` e `code-map.md`. Nada de produção, testes do viewer/upload, notas de outros papéis, flash ou device.
- Causa raiz (reviewer 19:37): `BINS` é expandido quando lido (`:=`/`+=`), mas a linha `BINS +=` referenciava `$(MUSIC_DURATION_BIN)` e `$(MUSIC_PRESENCE_BIN)` **antes** das definições (linhas 257/299) → as referências expandiam vazio, `test_music_presence_bin`/`test_music_duration_bin` nunca entravam em `BINS` e `clean` não os removia. Varredura da mesma classe em todas as atribuições eager do Makefile encontrou exatamente esses dois. Com o `_bin` sobrevivente, a execução seguinte roda o binário antigo e reescreve `*.gcda` sem `*.gcno` (os dois órfãos que o reviewer removeu à mão).
- Fix: removidas as duas referências mortas da linha `BINS +=` e acrescentado `BINS += $(MUSIC_DURATION_BIN) $(MUSIC_PRESENCE_BIN)` depois das definições, com comentário curto sobre a ordem de expansão. `test_incremental_final_contract.py` passou a derivar o inventário de executáveis das receitas dos wrappers `.PHONY` **expandidas** (`expand_recipe`, cobre `@./test_x_bin` e `@./$(X_BIN)`) e a avaliar `clean` expandido; nenhum nome ou contagem fixado (regra anti-pin). `code-map.md` documenta a regra de ordem e a derivação.
- Evidence (todas no snapshot acima, sem suíte completa/coverage/IDF):
  - Reprodução segura em cópia `/tmp/opencode/clean-repro/tests/host/keymap` (symlinks para `components`/`tools`), Makefile pré-fix: `make clean` preservou `test_music_presence_bin`; rerun do alvo gerou `test_music_presence_bin-{cyberdeck_audio_presence,test_music_presence}.gcda` sem `.gcno`.
  - Mesma cópia com o Makefile corrigido: `make clean` remove `_bin`+`.gcda`+`.gcno`; rerun recompila e produz 0 órfãos.
  - `make -C tests/host/keymap -j4 test-build` → EXIT=0 (65 s).
  - `make -C tests/host/keymap test_incremental_final_contract` → `PASS: final incremental migration (27 targets, 176 TUs, baseline parity)`.
  - Focados, todos EXIT=0: `test_incremental_batch_contract`, `test_music_presence` (1071 checks), `test_music_duration`, `test_window_manager_contract`, `test_ble_background_contract`, `test_wifi_audit_contract`, `test_screen_protection_contract`, `test_shell_help_contract`, `test_image_viewer_contract`, `test_control_upload_contract`.
  - Controle negativo end-to-end: com o `BINS +=` novo removido, o contrato falhou em `assert not missing` listando `['test_music_duration_bin', 'test_music_presence_bin']`; Makefile restaurado com `cp -p` (sha256 `00cca901…` e mtime originais conferidos) e o contrato voltou a PASS.
  - `git diff --check` → exit 0. Limitação: `ruff` não está instalado no host, então o Python foi validado apenas pela execução do próprio contrato.
- Loose end adjacente (deferable, não corrigida por escopo): existe em disco `tests/host/keymap/test_music_command_input_bin`, sobra de uma revisão antiga do Makefile sem nenhuma referência de wiring atual e fora de `LEGACY_TEST_ARTIFACTS`; `clean` não a remove. Impacto baixo — nenhum alvo a executa e os `.gcda` dela continuam cobertos pelo glob `./*.gcda`. Cabe a um dispatch futuro decidir se entra em nomes legados.
- Pending: gates finais continuam do reviewer (suíte agregada com `--coverage`, gcovr/limiares, `coverage_scope_guard.py`, lint/format, build IDF, CI remota) — a suíte PASS de 19:11 é do snapshot `51b678d7…` e está stale; CI verde segue no HEAD base. Cenários DEV-08..12/U8b inalterados, sem autorização nova.
- Supersedes or discards: substitui o snapshot `1fd3f767…` citado no cabeçalho desta nota por `43002380798a…`; a falta dos dois artefatos em `BINS`/`clean` deixa de existir. Não é aprovação final.
