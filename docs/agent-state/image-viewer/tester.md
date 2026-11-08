# tester - image-viewer

- Role: tester
- Updated: 2026-11-08 18:26 -03:00
- Code state: HEAD `16392858881d775bb68a6733fffcfa67223ab808`; worktree sem alterações
  rastreadas, apenas `docs/agent-state/image-viewer/` não rastreado (nota do planner).
  `version.txt` = 1.15.2. Nenhum arquivo de produção ou teste foi criado/alterado e
  nenhum alvo de teste foi executado nesta fase.

## Dispatch objective

Fase pré-implementação de risco `high`: definir cenários de aceitação, borda, falha e
regressão e a rastreabilidade `REQ/AC -> TEST-IMG-*` para o app foreground isolado de
visualizador de imagens, além de riscos/lacunas mensuráveis para o `coder`. Escopo
aprovado: BMP 24-bit BI_RGB, caminho confinado, até 5 MiB, até 1280x1280, navegação
1..64 com indicador e limites de setas, lifecycle/isolamento com Esc e troca rápida,
manifesto/registro de 21 apps e help. PNG/JPEG, zoom e recursão ficam fora de escopo.

Exit criterion: matriz de cenários e recomendações mensuráveis entregues. Nota não é
prova: os cenários abaixo são prescrições; sensibilidade de borda só se prova com o
teste executado e a mutação correspondente falhando.

## Work done

Inspeção estática do runtime de apps, seams de storage/path, UI foreground, config
LVGL do alvo, contratos host que serão impactados e ponte serial (para validade dos
cenários físicos). Derivadas 9 premissas de contrato (A1..A9) que o plano ainda não
fixou literalmente; cada teste aponta a premissa. Nenhuma mutação foi executada.

## Evidence

Comandos de leitura/estado executados em `HEAD 16392858881d775bb68a6733fffcfa67223ab808`
com worktree rastreado limpo (`git status --porcelain` = apenas `?? docs/agent-state/image-viewer/`).

- Registro e capacidade: `components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp:295-316`
  (`k_apps` com 20 entradas) e `:339-343` (retorna `ESP_ERR_NO_MEM` quando
  `register_application` falha). `include/apps/runtime/cyberdeck_app_runtime.h:19` e
  `include/apps/runtime/cyberdeck_app_quota.h:35` fixam `k_max_applications = 20` em
  constantes independentes, sem `static_assert` de igualdade.
  `include/apps/runtime/cyberdeck_command_catalog.h:11` deriva
  `k_max_catalog_commands` da capacidade.
- Contratos existentes que pinam inventário: `tests/host/keymap/test_system_apps_contract.py:97-107`
  compara `k_apps` apenas com a capacidade do **quota**;
  `tests/host/keymap/test_shell_help_contract.py` mantém inventário pinado de 20
  manifests em ordem de registro, fixtures de usage e goldens de
  `tests/host/keymap/test_shell_help.cpp`; `components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp`
  (`help_text`) deriva as linhas `[app]` dos manifests.
- Catálogo legado de help: `components/cyberdeck/include/apps/shell/cyberdeck_shell_help.h:33`
  é `std::array<entry, 12>` fixo; comando novo não pode entrar nele.
- Roteamento foreground: `components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp`
  (`execute_line`) chama `activate_foreground()` antes de `app.execute()`; um `execute`
  rejeitado deixa o app running/foreground. O editor compensa com gate de entrada
  `document().path().empty()` em `src/platform/display/cyberdeck_ui.cpp` (helper
  `editor_input_active()`).
- Storage/path: `include/apps/runtime/cyberdeck_app_storage.h` (`k_max_read_bytes = 12288`;
  `open_readonly_stream(path, max_bytes, size)`); `src/apps/shell/cyberdeck_local_shell.cpp`
  (`cyberdeck_local_shell_open_readonly`, raiz virtual `/` sobre `/sdcard`, rejeita
  prefixo `sdcard`/`sdcard/`, path >256 B, arquivo vazio, não-regular e `>max_bytes`;
  host usa `lstat` + `openat(O_NOFOLLOW)`, ESP usa `stat` e `O_NOFOLLOW` quando
  disponível; FATFS não tem symlink). Precedente de leitura grande: app de música usa
  `open_readonly_stream` com teto de 32 MiB.
- LVGL do alvo: `managed_components/lvgl__lvgl/include/lvgl/lv_version.h` = 9.6.0;
  `sdkconfig`/`build/config/sdkconfig.h`: `CONFIG_LV_COLOR_DEPTH=16`,
  `CONFIG_LV_USE_IMAGE=y`, `CONFIG_LV_USE_BMP` **não setado**, `LV_USE_FS_STDIO/POSIX`
  desligados. `managed_components/lvgl__lvgl/include/lvgl/draw/lv_image_dsc.h` expõe
  `stride`/`data_size`; `lv_image_set_scale` existe em `src/widgets/image/lv_image.c`.
  PSRAM: `CONFIG_SPIRAM=y`, `CONFIG_SPIRAM_USE_MALLOC=y`,
  `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0`.
- UI/harness: `include/apps/runtime/cyberdeck_window_manager.h` (`k_max_surfaces = 8`);
  shell/editor têm surfaces pré-criadas em `cyberdeck_ui.cpp`, música não usa surface
  do window manager; `tests/host/keymap/shim/full/lvgl.h` não possui APIs `lv_image_*`;
  `tools/coverage_scope_guard.py` exige TU de produção no gcovr ou allowlist e mantém
  `FORBIDDEN_ALLOWLIST` para views de header/terminal;
  `tests/host/keymap/test_incremental_final_contract.py` deriva `BINS` do wiring.
- Ponte serial (validação física): `src/apps/serial/cyberdeck_serial_bridge.cpp` aceita
  `ui.type`, `ui.clear`, `ui.click`, `ui.tap`, `ui.escape`, `ui.dump`, `term.dump`,
  `screen.*`; não há injeção de setas.

Não executado: build ESP-IDF, suíte host, gcovr/scope guard, mutações, flash e
validação no Tab5.

## Decisions and assumptions

Premissas para tornar os cenários concretos; se o plano fixar diferente, o teste
correspondente muda junto (sem enfraquecer asserção):

- A1. Comando primário `image <path>`; caminho absoluto no namespace virtual do shell
  (`/fotos/a.bmp`); `/sdcard/...` e relativos rejeitados.
- A2. Somente `.bmp` (extensão case-insensitive), `biPlanes=1`, `biBitCount=24`,
  `biCompression=0`, `biSize>=40` (BITMAPINFOHEADER ou V4/V5 lendo os 40 primeiros
  bytes). `biHeight<0` (top-down) suportado; se o plano rejeitar, usar a mesma falha de
  formato inválido.
- A3. Limites: arquivo <= 5 MiB, `1<=w<=1280`, `1<=h<=1280`, validados antes de alocar
  pixels.
- A4. Navegação por nome (ordem de bytes) das imagens do mesmo diretório, máx. 64;
  acima disso, as 64 primeiras + marcador de truncamento. Setas fazem clamp, sem wrap.
- A5. Indicador de posição 1-based (`i/N`) mais nome do arquivo; formato exato definido
  por função pura no plano.
- A6. Buffer de pixels RGB565 (`w*h*2`, máx. 3.276.800 B) no heap/PSRAM, nunca na stack;
  uma única imagem publicada e liberada no stop/destroy.
- A7. Falha em qualquer etapa: mensagem de erro, nenhuma imagem parcial, nenhuma
  superfície/imagem criada e entrada preservada no shell (predicado puro de posse).
- A8. Esc -> `switch_foreground("cyberdeck.shell")` + destroy da view/buffer.
- A9. Se o decode for assíncrono: token/geração monotônica; resultado stale após
  Esc/troca nunca publica.

Decisões de teste: exercitar o código de produção real nos seams já host-testáveis
(`storage_facade`/`open_readonly`, app real, view sob o shim LVGL) em vez de reimplementar
parse/decodificação no teste; os oráculos de BMP são independentes do decoder; nenhum
teste pina contagem literal de `BINS`, versão exata ou constante duplicada — limites e
inventários são derivados da fonte (headers/Makefile/`version.txt`).

## Pending and risks

### Matriz de cenários

#### AC-IMG-01 / REQ-IMG-01 - caminho confinado, BMP 24-bit BI_RGB, limites, falha fechada

| ID | Caso e critério | Fonte atual que sustenta |
| --- | --- | --- |
| TEST-IMG-PATH-01 | `/fotos/a.bmp` válido abre e lê bytes pelo seam real | `cyberdeck_local_shell_open_readonly` (`cyberdeck_local_shell.cpp`) |
| TEST-IMG-PATH-02 | `/sdcard/a.bmp` rejeitado (EACCES/mensagem de path inválido) | guarda `relative == "sdcard"` no mesmo arquivo |
| TEST-IMG-PATH-03 | `..`, `a/../../b.bmp`, `//`, path >256 B rejeitados | `cat_relative_safe` + `valid_virtual_path` |
| TEST-IMG-PATH-04 | symlink, diretório e arquivo vazio rejeitados (host) | host `lstat`/`openat(O_NOFOLLOW)`; ESP usa `stat` + FATFS |
| TEST-IMG-PATH-05 | relativo sem `/` rejeitado (A1) | app não tem cwd; padrão do app de música é absoluto |
| TEST-IMG-BMP-01 | BMP 24-bit BI_RGB 2x2 decodifica pixels RGB exatos e ordem de linha correta | decoder puro exigido por `CONFIG_LV_USE_BMP` desligado |
| TEST-IMG-BMP-02 | stride com padding: w=100 (300 B) e w=101 (304 B) corretos | stride BMP = `((w*3)+3)&~3` |
| TEST-IMG-BMP-03 | `biHeight<0` (top-down) lido na ordem do arquivo (ou rejeitado, A2) | spec BMP; decisão pendente no plano |
| TEST-IMG-BMP-04 | `biCompression!=0`, `biBitCount` 8/16/32, `biPlanes!=1`, `biSize<40` rejeitados com erro tipado | REQ-IMG-01 (sem compressão/transparência) |
| TEST-IMG-BMP-05 | header truncado, `bfOffBits`/`biSizeImage` inconsistentes, dados de pixel insuficientes rejeitados sem ler além do buffer | parse bounded (risco de OOB) |
| TEST-IMG-BMP-06 | arquivo sem assinatura `BM` rejeitado | idem |
| TEST-IMG-LIMIT-01 | tamanho exatamente 5 MiB aceito na validação; 5 MiB+1 rejeitado antes do decode | A3 + `max_bytes` do seam |
| TEST-IMG-LIMIT-02 | 1280x1280 aceito; 1280x1281 e 1281x1280 rejeitados por dimensão | A3 |
| TEST-IMG-LIMIT-03 | coerência cap x dimensões: dados 24-bit de 1280x1280 + header <= cap, derivado das constantes | cálculo `w*h*3+54` vs 5 MiB |
| TEST-IMG-LIMIT-04 | arquivo >5 MiB em disco rejeitado pelo seam (`EFBIG`) sem alocar buffer de pixels | `open_readonly_stream(..., max_bytes, size)` |
| TEST-IMG-FAIL-01 | header inválido/limite excedido => zero alocação de pixels | A7 + contador de alocação |
| TEST-IMG-FAIL-02 | falha de alocação do buffer => erro, sem imagem, sem leak | A6/A7 |
| TEST-IMG-FAIL-03 | nenhum caminho publica buffer parcial; estado só muda após decode completo | A7 |

#### AC-IMG-02 / REQ-IMG-02 - exibição completa preservando proporção

| ID | Caso e critério | Fonte atual que sustenta |
| --- | --- | --- |
| TEST-IMG-FIT-01 | 1000x500 em área 700x600 => escala `min` = 0.7 => 700x350, dentro dos limites, erro de proporção <=1 px | math pura de fit (novo); área = `content_` |
| TEST-IMG-FIT-02 | retrato (ex. 704x1203) e paisagem (ex. 1256x643): sempre `w'<=W`, `h'<=H`, proporção preservada | bounds de `content_` usados pela view de música |
| TEST-IMG-FIT-03 | `w/h=0`, área 0 => resultado inválido fail-closed, sem divisão por zero | UBSan no alvo focado |
| TEST-IMG-FIT-04 | política de upscale definida em uma única variante (recomendado clamp <=1.0 ou maximizar) e testada | decisão do plano |
| TEST-IMG-VIEW-01 | imagem dentro da área útil, sem sobrepor header de 42 px, centralizada em 720x1280 e 1280x720 | shim LVGL + header 42 px |
| TEST-IMG-VIEW-02 | `LV_EVENT_SIZE_CHANGED` recalcula escala/posição; portrait->landscape->portrait mantém contenção | padrão `resized()`/`parent_resized()` da view de música |
| TEST-IMG-VIEW-03 | escala aplicada sem recriar a imagem; sem `bg_opa`/tema lavando os pixels | `lv_image_set_scale` (LVGL 9.6.0) |

#### AC-IMG-03 / REQ-IMG-03 - navegação determinística 1..64, indicador, limites

| ID | Caso e critério | Fonte atual que sustenta |
| --- | --- | --- |
| TEST-IMG-NAV-01 | `readdir` fora de ordem => lista ordenada por bytes, determinística em N execuções | `readdir` não ordena (scanner novo/generalizado) |
| TEST-IMG-NAV-02 | filtra somente `.bmp` case-insensitive; ignora subdiretórios e não-imagens | escopo aprovado |
| TEST-IMG-NAV-03 | índice 1-based corresponde à posição da imagem aberta | A5 |
| TEST-IMG-NAV-04 | 64 imagens => todas listadas, sem truncamento | A4 |
| TEST-IMG-NAV-05 | 65+ => 64 primeiras em ordem + truncamento sinalizado | A4 |
| TEST-IMG-NAV-06 | seta direita em 0 permanece 0; esquerda em N-1 permanece N-1 (clamp) | A4 |
| TEST-IMG-NAV-07 | formatador do indicador `i/N` derivado de índice e contagem | A5 |
| TEST-IMG-NAV-08 | imagem removida entre listagem e carga => erro explícito, sem indicador obsoleto | TOCTOU real no SD |
| TEST-IMG-NAV-09 | setas roteadas ao viewer só quando ele é foreground; Esc tem precedência | roteamento de `local_key` em `cyberdeck_ui.cpp` |

#### AC-IMG-04 / REQ-IMG-04 - lifecycle, isolamento, troca rápida, stop/reopen

| ID | Caso e critério | Fonte atual que sustenta |
| --- | --- | --- |
| TEST-IMG-LIFE-01 | `image` ativa `cyberdeck.image_viewer`; Esc retorna `cyberdeck.shell` | `switch_foreground` + padrão de fechamento da música |
| TEST-IMG-LIFE-02 | após Esc: app parado/sem imagem, view inexistente, buffer liberado | A8 |
| TEST-IMG-LIFE-03 | N ciclos abrir/fechar sem crescimento de alocação nem de filhos/objetos do shim | contador de alocação + shim |
| TEST-IMG-LIFE-04 | A->B direto e A->B->A publica o conteúdo correto (sem bitmap remanescente) | A6 |
| TEST-IMG-LIFE-05 | conclusão de decode de A após Esc/B não publica nada (geração/token) | A9 |
| TEST-IMG-LIFE-06 | stop/teardown concorrente com decode: join/limpeza idempotente, sem UAF (ASan/TSan no alvo) | risco de concorrência declarado |
| TEST-IMG-LIFE-07 | SD indisponível (lease do `cyberdeck_sd_io::coordinator` falha) => erro sem imagem/crash; posterior abre | `cyberdeck_sd_io_coordinator.h` |
| TEST-IMG-LIFE-08 | load rejeitado não sequestra entrada: teclado continua no shell | runtime ativa foreground antes de `execute` |
| TEST-IMG-LIFE-09 | `music` -> `image` para a música; `image` -> Esc -> `music` reabre | `switch_foreground` para foreground não-console |
| TEST-IMG-LIFE-10 | UI sem decode/FS direto; worker não chama LVGL fora de `cyberdeck_lvgl_dispatch`/lock; sem estado compartilhado com música/editor | `cyberdeck_lvgl_dispatch.h` + contrato estrutural |

#### AC-IMG-05 / REQ-IMG-05 - manifesto, registro de 21 apps, help

| ID | Caso e critério | Fonte atual que sustenta |
| --- | --- | --- |
| TEST-IMG-APP-01 | manifesto: id `cyberdeck.image_viewer`, `type=foreground`, recursos `display/input/storage`, `owns_console=false`, comandos/help com usage e parameters | `manifest` em `cyberdeck_app_runtime.h`; vocabulário de `resource` |
| TEST-IMG-APP-02 | `k_apps` = 21 e <= capacidade do runtime **e** do quota; as duas constantes iguais; registro de todos sem `ESP_ERR_NO_MEM` | 20 entradas em `:295-316`; capacidades 20/20 |
| TEST-IMG-APP-03 | `app list` inclui a linha; `app info` expõe tipo/recursos/comandos/API | `execute_line` (`app list/info`) |
| TEST-IMG-APP-04 | `start_all()` não inicia foreground; `app start` funciona | `runtime::start_all` |
| TEST-IMG-APP-05 | contratos de apps/help reconciliam inventário e usage reais; goldens e traceability atualizados | `test_system_apps_contract.py`, `test_shell_help_contract.py`, `help_traceability.md` |
| TEST-IMG-APP-06 | `version.txt` no formato MAJOR.MINOR.PATCH sem zero-padding e >= piso; manifesto com semver próprio; nenhum teste pina o número | `AGENTS.md` (versionamento) |
| TEST-IMG-HELP-01 | `help` contém exatamente uma linha `image [app]`; catálogo legado segue com 12 | `cyberdeck_shell_help.h:33` + `help_text` |
| TEST-IMG-HELP-02 | `help image` devolve usage + parâmetros indentados | `app_command_help` |
| TEST-IMG-HELP-03 | `image` não duplicado entre catálogo legado e apps; ordem = registro | inventário pinado do contrato |

#### Gates de fechamento

| ID | Caso e critério | Fonte atual que sustenta |
| --- | --- | --- |
| TEST-IMG-GATE-01 | todo TU novo de produção aparece no gcovr JSON; `FORBIDDEN_ALLOWLIST` não absorve view/modelo novos | `tools/coverage_scope_guard.py` |
| TEST-IMG-GATE-02 | alvos novos em `BINS`, `.PHONY`, agregado `test` e `clean`; contrato incremental continua derivado do wiring | `tests/host/keymap/Makefile`, `test_incremental_final_contract.py` |

Mutações obrigatórias (sensibilidade de borda; executar quando os testes existirem):
`<=` -> `<` no cap de 5 MiB (TEST-IMG-LIMIT-01); 1280 -> 1279 no limite de dimensão
(TEST-IMG-LIMIT-02); cap de navegação 64 -> 63/65 (NAV-04/05); clamp -> wrap (NAV-06);
`min` -> `max` no fit (FIT-01); `(w*3+3)&~3` -> `w*3` (BMP-02); inverter ordem de linha
(BMP-01/03); remover guarda `sdcard` (PATH-02); `lstat` -> `stat` no host (PATH-04);
remover geração do decode (LIFE-05); remover gate de posse de entrada (LIFE-08);
reverter uma das capacidades para 20 (APP-02). Mutação sobrevivente = borda não
distinguida: ajustar asserção ou registrar em riscos.

### Riscos e lacunas para o coder

1. Capacidade do registry é dependência mandatória: 20 registros para 20 posições, em
   dois headers independentes e sem guarda de igualdade; um 21º app sem subir ambos
   falha em `register_application` e aborta o boot (`cyberdeck_system_apps.cpp:339-343`,
   precedente de rollback de OTA com `usb_audio`). Regressão: TEST-IMG-APP-02.
2. Zombie foreground em falha de load: o runtime ativa o foreground antes de
   `execute`; sem predicado de posse de entrada (padrão `editor_input_active()`) o shell
   perde o teclado. Regressão: TEST-IMG-LIFE-08.
3. Sem decoder BMP no LVGL (`CONFIG_LV_USE_BMP` desligado, sem `lv_fs`): habilitar isso
   adiciona FS/codec. Recomendação: decoder puro host-testável -> `lv_image_dsc_t`
   RGB565 (máx. 3.276.800 B em PSRAM). Não provado em LVGL real; exige build e Tab5.
   ARGB8888 dobraria o pico (6,55 MB).
4. Leitura de 5 MiB: `bounded_read` (12 KiB) é inutilizável; usar
   `open_readonly_stream` com `max_bytes` derivado do limite e fechar o fd em todos os
   caminhos. O seam rejeita arquivo vazio e path >256 B. No ESP, `stat` (não `lstat`) e
   FATFS sem symlink => prova de symlink é somente host (lacuna de dispositivo).
5. Quota x leitura real: `quota_limits::bounded_read_bytes = 12288`; declarar storage
   pode esbarrar em `quota_for`/`reserve` (registro recusado ou limite enganoso).
6. Lifetime do buffer x LVGL: a dsc aponta para RAM; o buffer precisa viver até o widget
   ser destruído e ser liberado exatamente uma vez. O shim atual não tem APIs
   `lv_image_*` — será preciso estendê-lo (ou manter view fina + math pura coberta).
7. Setas não são injetáveis pela ponte serial (`ui.escape` cobre só Esc). Para validar
   AC-IMG-03 no Tab5: expor botões `anterior/próximo` tocáveis (permite `ui.tap`) ou
   aceitar validação manual com teclado físico — registrar no roteiro manual.
8. Listagem determinística: não reutilizar o browser de MP3 (raiz `/musica`, filtro
   MP3); `readdir` não ordena; definir extensão case-insensitive, ocultos e critério das
   64 primeiras. `opendir` no ESP não garante `O_NOFOLLOW`.
9. Glifos/indicador: nomes de arquivo podem exceder a cobertura da fonte
   (`0x20-0x7E`, `0xA0-0xFF`, bloco esparso de ícones); sanitizar como a música e evitar
   literais não-ASCII (o contrato de fonte varre só TUs de música).
10. Isolamento: sem estado global compartilhado com editor/música; nenhuma chamada LVGL
    fora do contexto de display; `k_max_surfaces = 8` — se criar surface própria,
    remover no stop e provar contagem de volta ao baseline.
11. Escopo: não adicionar PNG/JPEG/zoom/recursão; recusar com mensagem clara.
12. Documentação/gates: atualizar `code-map.md` (matriz REQ/AC->TEST), README(s),
    arquitetura, roteiro manual, `help_traceability.md` e dashboard OS se aplicável;
    varrer literais de limite antigos antes de fechar. `version.txt`: decisão MINOR x
    PATCH é do plano; testes validam formato/piso, nunca o literal.

Limitações desta fase: nenhum build/teste/mutação/flash executado; nenhum cenário
validado por execução. Falhas futuras devem ser classificadas como defeito de teste,
implementação ausente ou implementação incorreta, sem remover cenários nem enfraquecer
asserções. Validação física pendente: exibição RGB565 em portrait/landscape, resize,
memória PSRAM, limites/navegação com setas físicas e ausência de artefato visual.

## References

`docs/agent-state/image-viewer/planner.md`; `code-map.md`; `AGENTS.md`;
`components/cyberdeck/include/apps/runtime/{cyberdeck_app_runtime.h,cyberdeck_app_quota.h,cyberdeck_app_storage.h,cyberdeck_window_manager.h}`;
`components/cyberdeck/include/apps/shell/{cyberdeck_local_shell.h,cyberdeck_shell_help.h}`;
`components/cyberdeck/src/apps/{system/cyberdeck_system_apps.cpp,runtime/cyberdeck_app_runtime.cpp,shell/cyberdeck_local_shell.cpp,shell/cyberdeck_shell_session.cpp,serial/cyberdeck_serial_bridge.cpp}`;
`components/cyberdeck/src/platform/display/cyberdeck_ui.cpp`;
`tests/host/keymap/{Makefile,test_system_apps_contract.py,test_shell_help_contract.py,test_shell_help.cpp,test_app_runtime.cpp,test_incremental_final_contract.py,shim/full/lvgl.h}`;
`tools/coverage_scope_guard.py`; `managed_components/lvgl__lvgl/{include/lvgl/lv_version.h,include/lvgl/draw/lv_image_dsc.h,src/widgets/image/lv_image.c}`.

## Updates

### 2026-11-08 14:12 -03:00 - nota persistida após correção de harness

- Fact: especificação entregue em texto na execução anterior foi persistida nesta nota,
  no HEAD `16392858881d775bb68a6733fffcfa67223ab808`, worktree rastreado limpo (apenas
  `docs/agent-state/image-viewer/` não rastreado).
- Supersedes or discards: a alegação anterior de que a nota não pôde ser criada por falta
  de ferramentas.

### 2026-11-08 14:55 -03:00 - validação independente da implementação (fase de testes)

- Fact: implementação validada de forma independente no HEAD
  `16392858881d775bb68a6733fffcfa67223ab808`. Snapshot do estado **recebido**
  (antes das minhas edições de teste): `git diff | sha256sum` =
  `1b3847c8e514a8c29166eeade3e5d956f8dde66015bcf2d55a0dbb647cc71fe2`; snapshot
  composto (diff + untracked) = `8fecb6bb1a5cf0d135a07ecf3b7155c71a389cc84c793985c4ed85a2edc4ccf8`.
  Snapshot **atual** (após as três edições de teste descritas abaixo, produção
  inalterada) = `843d855ced35c73ee6c1b44b150e80352f17199d5ae83a48942bfd91e08d8d33`.
  Todos os arquivos de produção de `apps/image/` e as capacidades em
  `cyberdeck_app_quota.h` batem byte a byte com o backup
  `/tmp/opencode/img-mut/baseline.sha256` (`sha256sum -c` OK): app `b1334360…`,
  bmp `b39a2fa3…`, decode `9d3d92e2…`, fit `6135ebe7…`, list `031e48ef…`,
  quota `99c0c191…`.
- Fact: alvos focados executados no estado recebido, todos verdes:
  `test_image_bmp` 79 checks/0 falhas; `test_image_fit` 222/0;
  `test_image_decode` 48/0; `test_image_list` 103/0; `test_image_view` 70/0
  (compilado com `--coverage -fsanitize=address,undefined -O1 -pthread`);
  `test_image_viewer_contract` 0 falhas. Contratos de registro/help/runtime
  também verdes: `test_shell_help` 249/0 (golden passa a incluir `image [app]`),
  `test_shell_help_contract` PASS (8 mutações rejeitadas),
  `test_help_unification` PASS 413 checks, `test_app_runtime` PASS 226 checks,
  `test_system_apps_contract` PASS, `test_coverage_scope_guard` "coverage scope
  guard tests passed" (RC=0). `git diff --check` RC=0.
- Fact: evidência de compilação/execução de produção existe para
  `cyberdeck_image_bmp.cpp`, `cyberdeck_image_fit.cpp`,
  `cyberdeck_image_decode.cpp`, `cyberdeck_image_list.cpp` (TUs reais linkadas e
  executadas) e para `cyberdeck_image_view.cpp` (compilada contra `shim/full`
  com ASan+UBSan e executada). `cyberdeck_image_app.cpp` (allowlistada em
  `tools/coverage_scope_guard.py`, ligada a FreeRTOS/task/semáforo) e a
  integração em `cyberdeck_ui.cpp` **não** são compiladas por alvo host; não
  executei `idf.py build`, logo compilação ESP-IDF do app/UI/view permanece sem
  evidência independente nesta fase. API conferida por leitura contra o LVGL
  9.6.0 local: `lv_image_set_scale(lv_obj_t *, uint32_t)` e
  `LV_COLOR_FORMAT_RGB565 = 0x12` coincidem com o shim.
- Fact: **mutação sobrevivente inicial** (lacuna material corrigida por teste):
  M1, cap de 5 MiB `>` -> `>=` em `inspect_bmp`, **sobreviveu** ao estado
  recebido (79 checks, 0 falhas). Correção por teste (sem tocar produção):
  acrescentei `TEST-IMG-LIMIT-01` "arquivo exatamente no cap ainda é aceito" em
  `test_image_bmp.cpp` (usa `inspect_bmp(..., k_max_file_bytes, info)`) e em
  `test_image_decode.cpp` (usa `decode_descriptor(..., k_max_file_bytes, ...)`).
  Reexecução: bmp 82/0, decode 50/0, contrato 0 falhas. Com a mutação reaplicada
  os dois alvos falham (bmp: `at_cap` inválido, 3 falhas; decode: `too_large`,
  2 falhas), provando sensibilidade da borda inferior/inclusiva.
- Fact: **M1b** (gate próprio do decoder `file_bytes > k_max_file_bytes` ->
  `>=`) falha com o novo caso (`decode_status::too_large`, actual=2) e **M2**
  (remover padding de stride `((w*3)+3)&~3` -> `w*3`) falha bmp (5 falhas: 2x2 e
  w=101) e decode (6 falhas: shear em w=101). Ambas restauradas e verificadas
  por sha256.
- Fact: o contrato ganhou pin estrutural para `TEST-IMG-LIFE-05` (A9): como a TU
  do app não é executável no host, `test_image_viewer_contract.py` agora exige o
  gate `generation != pending_generation_.load(...)` em `publish()` e `fail()` e
  duas vezes em `run_job()`, além de `cancel_.load` em `worker_entry()`.
- Fact: arquivos de teste acrescentados/alterados **somente** por lacuna
  material: `tests/host/keymap/test_image_bmp.cpp` (+ caso at-cap),
  `tests/host/keymap/test_image_decode.cpp` (+ caso at-cap no gate do decoder) e
  `tests/host/keymap/test_image_viewer_contract.py` (+ pin do gate de geração e
  cancelamento). Nenhum cenário ou asserção existente foi removido ou
  enfraquecido. Produção não foi alterada nesta fase.
- Fact: mutações **não executadas** por limite de passos: M3 ordem de linha
  (top-down/bottom-up), M4 clamp x wrap das setas, M5 `min` x `max` do fit, M6
  guarda `/sdcard` em `valid_directory_path`, M7 capacidade 21 -> 20 (esperado:
  falha de compilação pelo `static_assert` e/ou contrato), M8 remoção do gate de
  geração (agora deve falhar via pin novo, ainda não verificado em execução) e
  M9 `classify_image_key` sem `image_ready`.
- Fact: lacunas relevantes remanescentes: (a) `cyberdeck_image_app.cpp` e
  `cyberdeck_ui.cpp` sem build/execução independente do tester e **sem
  `idf.py build`**; (b) `TEST-IMG-LIFE-03/04/07/08/09` (ciclos/leak no app, SD
  indisponível, troca rápida com música) sem teste host do app; (c) NAV-08
  (arquivo removido entre listagem e carga) não exercitado; (d) PATH-04 symlink
  só provável no host (FATFS não tem symlink, ESP usa `stat`); (e) LIMIT-04
  (`EFBIG`/"file larger than 5 MiB") não asserido diretamente; (f)
  `test_incremental_final_contract` e o scope guard real (gcovr) não executados
  por instrução (gate global do reviewer). **Flash não executado**; validação
  física do viewer (RGB565 em portrait/landscape, resize, PSRAM, setas físicas)
  permanece manual.
- Fact: achados menores por inspeção, não provados por execução: campos
  `published_generation_`/`publish_generation_` escritos e nunca lidos; com
  foreground ativo mas view ainda inexistente, texto digitado cai na linha
  oculta do shell (só setas/Esc são interceptados); `test_image_decode` e
  `test_image_list` usam `/tmp` fixo em vez de `TMPDIR`.
- Supersedes or discards: nada; complementa a entrada de 14:12 (que continha só a
  especificação de cenários).

### 2026-11-08 15:11 -03:00 - delta pós-reviewer: worker persistente, mutações M3-M12/M14 e restauração confirmada

- Fact: snapshot atual = HEAD `16392858881d775bb68a6733fffcfa67223ab808`.
  Snapshot **recebido** neste ciclo (após correções do coder): diff rastreado
  `fe71b41ee510783c05934aad34d8b34ddc7d82c4c329ec5f5fe34201481aa2e6`; composto
  (diff + untracked) `42a4f47d23ad84e1b5f60cf8fa3a18fa816d1d35b09dedaa088daa6e11ed65de`.
  Snapshot **atual** (só `tests/host/keymap/test_image_viewer_contract.py` mudou em
  relação ao recebido) = `dcf1144f671ac1ab5e302a658e7eac992d6fe1076dde4f9b1c3b5fe3bb05a62f`.
- Fact: restauração **verificada sem grep**: `sha256sum -c /tmp/opencode/img-mut2/baseline.sha256`
  → 8/8 `SUCESSO` (RC=0): `cyberdeck_image_app.cpp`, `cyberdeck_image_bmp.cpp`,
  `cyberdeck_image_decode.cpp`, `cyberdeck_image_fit.cpp`,
  `cyberdeck_image_list.cpp`, `cyberdeck_image_worker_state.cpp`,
  `cyberdeck_app_quota.h` e `cyberdeck_ui.cpp`. Nenhum mismatch; nada precisou ser
  restaurado de novo. Isto encerra a dúvida do ciclo anterior (o `grep -c ESUCESSO`
  estava errado e o `&&` cortou a checagem).
- Fact: alvos reexecutados no estado final, todos verdes:
  `test_image_worker_state` 788 checks/0 falhas; `test_image_list` 111/0;
  `test_image_bmp` 82/0; `test_image_fit` 222/0; `test_image_decode` 50/0;
  `test_image_view` 70/0 (recompilado com `--coverage -fsanitize=address,undefined
  -O1 -pthread`); `test_image_viewer_contract` 0 falhas. `git diff --check` RC=0.
- Fact: mutações pós-reviewer executadas isoladamente em produção, cada uma seguida
  de restauração e verificação por sha256 (todas mortas pelo teste correspondente):
  M3 ordem de linha no decode → 9 falhas; M4 clamp→wrap em `step_image_index` → 3;
  M5 `min`→`max` no fit → 44; M6 `has_sdcard_prefix` sempre falso → 3 (a remoção
  ingênua da chamada só falha por `-Werror=unused-function`, garantia mais fraca,
  por isso usei a variante que compila); M7 quota 21→20 → contrato 2 falhas **e**
  `static_assert` de compilação em `test_app_runtime`; M8 remoção do gate de geração
  em `publish()` → contrato 1 falha, enquanto `test_image_worker_state` continua
  788/0 (prova de que os alvos comportamentais são cegos à TU do app, allowlistada);
  M9 inversão do gate `image_ready` → 4; M10 `next(cancelled)` mantendo `alive()`
  (bug antigo de handoff) → 111; M11 remoção do poll de cancelamento por entrada →
  2; M12 `vSemaphoreDelete(wake_)` no caminho de timeout do join → contrato 1 falha;
  M14 destruir a view antes de `switch_foreground` → contrato 1 falha (M12/M14 usam
  as asserções novas).
- Fact: mutações **não executadas**: limite de dimensão 1280→1279; cap de navegação
  64→63/65; remoção de `worker_.reset()` no `stop()` (M13 planejada e não rodada).
  Antes deste ciclo já haviam sido mortas: cap 5 MiB `>`→`>=` (bmp e decode) e
  stride `(w*3+3)&~3`→`w*3`.
- Fact: análise do join de 200 ms — `k_worker_join_budget_ms = 200`; no timeout
  `stop()` retorna false e **não libera** semáforos nem frame, mantendo `running_` e
  `cancel_`; um `stop()` posterior repete o join e `start()` recusa segundo worker
  (`worker_not_reaped`) ou colhe o que já saiu. `leave_image_viewer()` destrói a
  view apenas quando `switch_foreground` retorna sucesso, portanto a tela não fica
  vazia e o Esc seguinte é a via de recuperação. Provado por contrato estrutural
  (asserções novas), não por execução: a TU do app é FreeRTOS e não é host-linkável.
- Fact: scan cancelável — `list_images()` faz poll antes de abrir o stream e a cada
  entrada, limpa `out` e retorna false sem publicar página parcial; o app trata
  cancelamento como não-diagnóstico (sem mensagem). Coberto por
  `test_image_list` (aborta no 1º poll, aborta após 3 entradas, completa sem
  cancelamento) e morto por M11.
- Fact: risco residual de UI/recuperação (por inspeção, não observado em hardware):
  (a) um `open`/`pread` bloqueante no FATFS pode exceder 200 ms, o primeiro Esc
  falha e a falha é **silenciosa** (diferente da música, que publica feedback);
  (b) `abandon_image_view()` destrói a view **antes** de tentar
  `switch_foreground`, então se o switch falhar a área fica vazia com o app ainda
  em foreground até um Esc bem-sucedido; (c) o poll por entrada não interrompe uma
  chamada `is_regular_file` (open+stat+close) em andamento.
- Fact: cobertura/lacunas — `cyberdeck_image_app.cpp` e a integração
  `cyberdeck_ui.cpp` não têm execução independente no host e **não executei build
  nem flash** neste ciclo (o build PASS `0x2a13e0` é do reviewer; `0x2a15b0` é do
  coder, ambos não verificados por mim). Seguem sem teste comportamental:
  LIFE-03/04/07/09 no nível do app, NAV-08 (arquivo removido entre listagem e
  carga), PATH-04 em ESP (FATFS sem symlink, ESP usa `stat`) e LIMIT-04 (`EFBIG`
  "file larger than 5 MiB" não asserido). `published_generation_`/
  `publish_generation_` continuam escritos e nunca lidos. Suíte agregada e coverage
  global (gcovr/scope guard real) não foram executados, por instrução.
- Fact: alteração minha neste ciclo — somente
  `tests/host/keymap/test_image_viewer_contract.py`, com quatro asserções
  estruturais novas: caminho de timeout do join não libera semáforos/frame e
  falha o stop; `worker_.reset()` só no stop concluído; `start()` recusa segundo
  worker não colhido (`worker_not_reaped`); view destruída apenas após
  `switch_foreground` bem-sucedido. Nenhuma produção alterada e nenhum cenário
  removido.
- Supersedes or discards: o `resultado: inconclusivo` da entrada anterior (a
  restauração de app/UI agora está verificada 8/8 e os alvos foram reexecutados);
  permanecem abertos apenas os itens de cobertura e a validação física.

### 2026-11-08 15:22 -03:00 - validação física no Tab5: BLOCKER (reboot ao abrir BMP)

- Fact: firmware entregue por USB e validado em `/dev/ttyACM0` (Tab5, IP
  `192.168.68.127`, SSID `AT163032`). Binário `build/cyberdeck5.bin`,
  **2.758.064 B = 0x2a15b0**, SHA-256
  `d0fb425366d50d12ae538e060bc4f338378a58ce7f7b39c5684f839ebd647b51`,
  `version.txt` 1.15.3. Identidade conferida antes do flash: `strings` do bin
  contém `1.15.3`, `Foreground BMP image viewer` e o marcador do fix pós-review
  `worker_not_reaped` (1 ocorrência), confirmando que é o build corrigido
  reportado pelo coder. `idf.py -p /dev/ttyACM0 flash` gravou e o esptool
  verificou o hash ("Hash of data verified"); slot efetivo não instrumentado.
- Fact: antes do flash o dispositivo rodava `1.15.2`, uptime 01:34:54,
  `safe_mode=false`, `interrupted_boots=0`. Após o boot: `fw_version=1.15.3`,
  uptime 00:00:20, `safe_mode=false`, `interrupted_boots=0`,
  `recovery_persisted=true`, `internal_min_free=106048`, `psram_free=31.280.192`.
- Fact: cenários **aprovados** executados pela ponte serial
  (`tools/cyberdeck_cli.py`, `screen.wake` antes de `ui.type`):
  E1 `app list` → 21 apps únicos, incluindo `cyberdeck.image_viewer 1.0.0`;
  E2 `app info cyberdeck.image_viewer` → `type: foreground`, resources caption
  vazia, `commands: image`, `state: registered`, `last_error: none`;
  E3 `help` → exatamente uma linha `image [app]`;
  E4 corrigido `image help` → `image [app]` + `Image viewer
  (cyberdeck.image_viewer) version 1.0.0` + `usage: image <path>` + linha de
  parâmetros; E6 `image` sem argumento → `usage: image <path>` e prompt `/$ `
  de volta (sem foreground zumbi); E5 `app start cyberdeck.image_viewer` →
  `state: running`, sem view; `ui.escape` → app volta a `registered`, `pwd`
  responde `/` e uptime contínuo (00:02:30) — ou seja, o caminho stop/join de
  200 ms com worker ocioso funciona. `sys.info` final: uptime 00:03:13,
  `internal_min_free` 105.792 (vs 106.048 pós-boot), `psram_free` 31.266.968.
- Fact: **BLOCKER (AC-IMG-01/02/04 no hardware)**: `image
  /imagens/print_20261020_172101.bmp` (BMP 24-bit existente no SD, nenhum
  arquivo criado) não respondeu pela ponte e o dispositivo **reiniciou**.
  Evidência: `ui.type` retornou "nenhuma resposta do dispositivo"; `term.dump`
  passou a mostrar só `/$ `; `sys.info` em seguida reportou uptime 00:00:23
  (reset) e o ring de eventos (`log`) continha boot em `2026-11-08 15:16:54`
  (boot anterior ~15:13). Nenhum panic/backtrace foi capturado porque o
  `idf.py monitor` não estava aberto; `/events.txt` via HTTP serviu um arquivo
  antigo (16.726 linhas, cauda de 27/out) e não preservou as linhas pré-reboot.
  Não repeti a interação (AGENTS).
- Fact: causa provável identificada por aritmética no código atual, sem
  instrumentação: `run_job()` declara `image_directory listing{}` **na stack** da
  task `image_load`, cujo stack é `k_worker_stack_bytes = 6144`
  (`cyberdeck_image_app.cpp:21,293`). Medido no host com
  `sizeof(cyberdeck_image::image_directory)` = **8.272 B**
  (`image_entry` = 129 B × 64 + contadores), isto é ~2.128 B **acima** do stack
  da task antes de `directory_path` (257 B), `name` (129 B), `published_image`,
  `decoded_image`, `decode_ops` e `std::string`. `worker_entry` chama
  `run_job` na mesma task, logo o primeiro `image <arquivo>` estoura o stack
  (stack protection fault → panic → reset), consistente com o reboot observado
  e com o precedente de `music_scan`. Classificação: **defeito de
  implementação** (AC-IMG-04/stack), não defeito de teste; a camada pura
  host-testada não cobre o frame da task. Correção esperada do coder (sem
  alteração por mim): alocar a listagem no heap (padrão do browser de música)
  e/ou subir o stack, com regressão (ex.: contrato de stack/`static_assert` de
  tamanho de frame ou alocação heap-only). Regressão obrigatória após a
  correção, antes de nova tentativa física.
- Fact: itens do roteiro **não executados** e motivo: A5/A6/A7/A8/A9 exigem
  fixtures inexistentes (2000x100, 6 MiB, `.png`, 5 MiB exatos, arquivo a
  remover) — o SD foi preservado, nenhum arquivo criado/removido; A1/B/C/D1-D6
  dependem de abrir imagem e ficaram bloqueados pelo reboot; B2 (giro) e
  C1-C6 (setas) exigem interação física no teclado/rotação e as setas não são
  injetáveis pela ponte serial; D2 (10 ciclos) e D3/D4 não podem ser feitos sem
  abrir imagem.
- Fact: fixtures existentes: `/imagens` contém 100+ BMPs 24-bit (capturas do
  próprio dispositivo), portanto A1/C4/C5 teriam material sem escrever no SD.
- Fact: defeito de **documentação de teste**: o roteiro
  `tests/manual/image-viewer-validation.pt-BR.md` item E4 manda `help image`,
  mas a sintaxe real (e a do contrato `TEST-IMG-HELP-02`) é `image help`;
  `help image` responde `usage: help`. Doc precisa ser corrigida pelo
  developer/tester em ciclo próprio (não alterei agora).
- Fact: limites desta etapa — nenhuma suíte agregada, nenhum coverage global,
  nenhum review final amplo; flash USB executado uma vez (autorizado) e não
  repetido após o reboot; aceitação visual humana continua com o usuário.
- Supersedes or discards: a expectativa de fechar a validação física nesta
  etapa; `manual_validation` permanece **blocked** pelo reboot até correção e
  nova tentativa sob monitor serial.

### 2026-11-08 15:25 -03:00 - BUG-IMG-01 (stack do worker) validado independentemente

- Fact: snapshot validado = HEAD `16392858881d775bb68a6733fffcfa67223ab808`;
  diff rastreado `e46e35c93092bbb687a9404a00251845e0945dfdd617347c6db1dcbf3ff5c7c2`;
  composto (diff + untracked) `931193c409245bfb4aba80e57ff35bc4a7aae0ec92d2180b11900dd52b73a1fb`.
  Produção **não** alterada por mim: `cyberdeck_image_app.cpp` =
  `5bcf3599…d6266b`, `cyberdeck_image_list.cpp` = `99864e5a…672f2f`
  (baseline em `/tmp/opencode/img-mut3/`), ambos com `sha256sum -c` SUCESSO ao
  final das mutações.
- Fact: correção conferida na fonte atual — `run_job()` **não** declara mais
  `image_directory listing{}`; usa `directory_workspace workspace;` +
  `image_directory &listing = *workspace.get();` (apenas holder/ponteiro e
  `directory_path` 257 B + `name` 129 B no frame). `list_images()` **não** tem
  mais `image_directory staging{}` e preenche a página do chamador, limpando
  `out` em falha/cancelamento. `directory_workspace` faz placement new sobre
  memória de `hooks.allocate` (mesmo seam PSRAM do decoder), `release()`
  idempotente no destrutor, e `k_worker_stack_bytes` continua **6144** (a
  correção é heap, não stack maior). `#include <new>` presente.
- Fact: alvos focados no snapshot acima, todos verdes:
  `test_image_list` **139 checks/0 falhas** (era 111; inclui
  `test_directory_workspace_is_heap_backed`: 1 alocação por acquire, bytes ==
  `sizeof(image_directory)`, página zerada, release idempotente sem free duplo,
  reacquire sem leak, destrutor liberando 1x, alocação recusada sem alocar/liberar,
  e página pré-preenchida zerada por `list_images` em diretório inexistente);
  `test_image_viewer_contract` 0 falhas; `test_image_bmp` 82; `test_image_fit`
  222; `test_image_decode` 50; `test_image_view` 70 (ASan+UBSan);
  `test_image_worker_state` 788. `git diff --check` RC=0.
- Fact: mutações de regressão do BUG-IMG-01 executadas e mortas, com restauração
  verificada: **M15** reintroduzir `image_directory listing{}` em `run_job` →
  contrato falha (`BUG-IMG-01: run_job must not declare an image_directory on the
  task stack`) enquanto `test_image_list` continua 139/0; **M16** reintroduzir
  `image_directory staging{}` + `out = staging` em `list_images` → contrato falha
  (`list_images must fill the caller page instead of a local staging copy`).
  Isto mostra que o contrato é o único guardião do frame do worker (as suítes
  comportamentais não enxergam `run_job`, que é FreeRTOS-only).
- Fact: limites desta validação — o guard do BUG-IMG-01 é **textual**; não medi
  frame com `-fstack-usage` porque o shim host só fornece `freertos/FreeRTOS.h`
  (sem `task.h`/`semphr.h`), e o caminho de OOM do `run_job` não é executável no
  host. `idf.py build` **não** foi executado por mim; `build/cyberdeck5.bin`
  presente é o do coder (2.758.352 B = **0x2a16d0**, 15:23). Sem flash e sem nova
  tentativa física nesta etapa (reviewer development deve revisar antes).
- Fact: roteiro manual corrigido pelo coder e conferido por mim —
  `tests/manual/image-viewer-validation.pt-BR.md:93` (E4) agora pede `image help`
  e explicita que `help image` responde `usage: help`.
- Supersedes or discards: a hipótese de causa registrada em 15:22 (agora
  confirmada e corrigida) e o bloqueio "sem correção"; `manual_validation`
  continua **blocked** apenas até a nova tentativa física sob `idf.py monitor`
  após o aval do reviewer.

### 2026-11-08 15:31 -03:00 - tentativa física dirigida: PANIC persiste (BUG-IMG-02, 8 KiB na stack)

- Fact: identidade do binário **corrigida**: o SHA `d0fb4253…` citado no dispatch
  pertence à imagem anterior (0x2a15b0); o build corrigido é
  **2.758.352 B = 0x2a16d0**, SHA-256
  `f28b093ca81c04328dce6ef0c7f65a252ae60e73c9d0a7a22386950be3f2680d`,
  `version.txt` 1.15.3, com `worker_not_reaped` e `Foreground BMP image viewer`
  presentes. `idf.py build` reexecutado foi no-op (`ninja all` sem compilar),
  confirmando que o binário corresponde ao snapshot atual; `idf.py -p
  /dev/ttyACM0 flash` verificou o hash ("Hash of data verified").
- Fact: baseline antes da tentativa: `fw_version=1.15.3`, uptime 00:00:16,
  `safe_mode=false`, `interrupted_boots=0`, IP 192.168.68.127.
- Fact: `idf.py -p /dev/ttyACM0 monitor --no-reset` foi aberto antes do comando
  (log em `/tmp/opencode/monitor-img.log`, 256 linhas) e o único comando foi
  enviado pela LAN (`cyberdeck_http_cli.py type`) para manter a serial como
  observador: `image /imagens/print_20261020_172101.bmp` (fixture existente, nada
  criado/removido). Resposta: `accepted for dispatch: 40 bytes, 1 line(s)`.
- Fact: **PANIC capturado pelo monitor** — `Guru Meditation Error: Core 1
  panic'ed (Stack protection fault)`, `Detected in task "image_load"`,
  `cyberdeck_image::application::run_job(...) at cyberdeck_image_app.cpp:284`,
  `Stack pointer: 0x4ff26c80`, `Stack bounds: 0x4ff27a0c - 0x4ff29200` (o SP já
  está **908 B abaixo** do limite inferior na entrada da função), `Rebooting...`,
  `rst:0xc (SW_CPU_RESET)`. O decode-panic do monitor resolveu o frame. Após o
  reboot: `sys.info` = 1.15.3, uptime 00:01:56, `safe_mode=false`,
  `interrupted_boots=0`, heap estável (dispositivo vivo, sem loop).
- Fact: **causa provada por desmontagem** do ELF gravado
  (`riscv32-esp-elf-objdump -d build/cyberdeck5.elf`, `run_job` em 0x4003bbd8):
  prólogo `addi sp,sp,-848` seguido de `lui t0,0xffffe` + `add sp,sp,t0`, ou seja
  frame de **848 + 8192 = 9040 B** numa task de 6144 B. Os 8192 B são um
  **temporário de stack** materializado pela value-initialization
  `page_ = new (memory) image_directory{};` em `directory_workspace::acquire`
  (memset de 8 KiB), que é header-inline e foi embutido no frame de `run_job`.
  A correção de BUG-IMG-01 removeu o local `image_directory listing{}` e o
  `staging{}`, mas **introduziu/left** um temporário equivalente na stack — o
  teste host `test_directory_workspace_is_heap_backed` roda com stack de processo
  e não detecta, e o contrato é textual.
- Fact: classificação **BUG-IMG-02** (defeito de implementação, mesma classe de
  BUG-IMG-01 / AC-IMG-04); a entrega **não pode** ser aceita no hardware. Uma
  única tentativa foi feita, como autorizado; nenhuma repetição após o panic
  (AGENTS). Nenhum arquivo no SD foi criado/removido. Setas reais, Esc e itens
  B/C/D1-D6 não foram validados (bloqueados).
- Fact: correção recomendada (para o coder, sem alteração minha): inicializar a
  página sem temporário — `page_ = new (memory) image_directory;` seguido de
  `page_->count = 0; page_->truncated = false;` (ou `std::memset`) — e/ou mover
  `acquire`/`release` para o `.cpp` (não-inline) para impedir o inlining no frame
  do worker. Regressão host necessária: probe com `-fstack-usage` provando frame
  pequeno de um caller com `acquire` inline (ex.: < 1024 B) e/ou contrato
  proibindo `image_directory{}` em `acquire`; a medição no ELF
  (`addi sp,...,-N`) é a prova final.
- Supersedes or discards: o "corrigido por inspeção" desta classe de defeito; o
  `manual_validation` volta a **blocked** até BUG-IMG-02 ser corrigido, revisado e
  uma nova tentativa física autorizada.

### 2026-11-08 15:41 -03:00 - BUG-IMG-02 revalidado sem hardware (frame medido, sem SKIP)

- Fact: snapshot = HEAD `16392858881d775bb68a6733fffcfa67223ab808`; diff rastreado
  `c2f6af0e0df51f68d46e654bcb76476bb7d6446916db19b77997e64b44fca603`; composto
  (diff + untracked) no início `a8a3f922…d9379ae3` e **atual**
  `2dd0d299d0f6b23ca013d0a41d6a524c0b0c116a227270f51bb1d957bd714661` (mudou só
  pelo meu ajuste no contrato de stack). Produção intocada: `list.cpp`
  `872497b2…cc7bc13`, `app.cpp` `d71556a2…08cb56` (`sha256sum -c` SUCESSO).
- Fact: build identity confirmada e **reproduzível**: `build/cyberdeck5.bin`
  **0x2a13b0** (2.757.552 B), SHA-256
  `d4ea081371f7a671d51c29d741a54ec01d5e222126ee84afe1a8196fe2da8ece`,
  `version.txt` 1.15.3; `idf.py build` recompilou a partir do fonte atual e
  gerou o **mesmo** SHA do binário do coder (determinístico). ELF de 15:41:00
  (mais novo que as fontes de 15:38–15:40).
- Fact: `test_image_stack_frame_contract` **sem SKIP**: `host -fstack-usage:
  chain=368 B (offer inlined), budget=6144 B` e `elf frames: fail=704B,
  publish=416B, run_job=1600B, start=448B, stop=704B, worker_entry=896B` → todos
  `frame + 2048 B (reserva VFS/log) ≤ 6144 B`; 0 falhas. O orçamento vem de
  `k_worker_stack_bytes` em `cyberdeck_image_types.h`, sem literal duplicado.
  Fonte confere a correção: `clear()` in-place, `std::memset(memory,0,sizeof
  image_directory)` + `static_cast` em `acquire`, `static_assert
  is_trivially_copyable`, e **zero** ocorrências de `image_directory{}`,
  `image_directory listing{}` ou `staging{}` em produção (checado sem comentários).
- Fact: demais alvos, todos verdes: `test_image_list` 139/0,
  `test_image_viewer_contract` 0 falhas, `test_image_worker_state` 788/0,
  `test_image_bmp` 82/0, `test_image_fit` 222/0, `test_image_decode` 50/0,
  `test_image_view` 70/0 (ASan+UBSan); `git diff --check` RC=0.
- Fact: mutação antiga reaplicada e testada. **MA** (`page_ = new (memory)
  image_directory{}` em `acquire`): detectada por 3 asserções textuais
  (`image_directory{}`, placement-new com value-init, `memset` obrigatório) —
  **não** pela medição de frame: com o ELF reconstruído, `run_job` continuou
  1600 B e `acquire` out-of-line 48 B; probe com o toolchain RISC-V (`-Os/-O2`)
  também deu caller 0–16 B, porque o compilador elide o temporário quando
  `acquire` não é inline no chamador. **M2** (`image_directory listing{}` em
  `run_job`): detectada pelo contrato de stack (nova guarda de local nomeado +
  guarda de ELF stale) e pelo `test_image_viewer_contract`. Ambas restauradas,
  checksums reconferidos e mtime de `app.cpp` devolvido ao original.
- Fact: **limite demonstrado**: a prova por frame (host ou ELF) não é oráculo
  determinístico desta classe de regressão — depende da decisão de inlining do
  compilador (o frame de 9.040 B do binário 0x2a16d0 não se reproduziu com o
  mesmo `acquire` no código atual). As guardas determinísticas são as textuais +
  `static_assert`; a medição de ELF serve para **validar o artefato entregue**
  (e agora falha se o ELF for mais antigo que as fontes de imagem).
- Fact: ajustes meus no teste (lacuna material, sem tocar produção):
  (1) o contrato de stack passa a **falhar** quando `build/cyberdeck5.elf` é mais
  antigo que `app.cpp`/`list.cpp`/`list.h`/`types.h` (antes media um binário
  velho em silêncio e passava — foi o que escondeu BUG-IMG-02); (2) nova guarda
  contra local nomeado `image_directory <nome>{`, que o teste de substring
  `image_directory{}` não pegava. Trade-off registrado: em checkout novo sem
  build, o alvo falha com mensagem explícita até `idf.py build` (na CI o build
  precede os testes).
- Fact: sem flash, sem tentativa física e sem review final/coverage/suíte
  agregada nesta etapa. Lacunas mantidas: caminho de OOM do `run_job` não é
  executável no host; medição de ELF exige toolchain (imprime SKIP se ausente,
  por design do alvo).
- Supersedes or discards: o bloqueio de BUG-IMG-02 para efeito de *revisão*
  (correção confirmada independentemente, frame ≤ budget, zero falhas focadas);
  a revalidação física continua **não autorizada** nesta etapa e depende de
  decisão do usuário.

### 2026-11-08 15:48 -03:00 - fix de lifetime (placement default-init) revalidado

- Fact: snapshot = HEAD `16392858881d775bb68a6733fffcfa67223ab808`; diff rastreado
  `8feab12e122a56198f2bb60c3f42be47bc552fb3aa41f2568bad28fc0826cf88` (inalterado
  neste ciclo); composto no início `a4fa5c44…22d5eb` e **atual**
  `9ec648ce5fca694edf5f106928c59a0cd0328dad9753267bf927054caf7e8c8a` (mudou só
  pelo ajuste do parser do contrato, descrito abaixo). Produção intocada:
  `cyberdeck_image_list.cpp` `3b33dc7c…d565577` (`sha256sum -c` SUCESSO).
- Fact: fonte confere a correção de lifetime exigida pelo reviewer:
  `page_ = new (memory) image_directory;` (**default-init, sem chaves**, inicia o
  lifetime) seguido de `page_->clear()` (estado lógico in-place);
  `release()` chama `page_->~image_directory();` antes de desalocar;
  `static_assert(std::is_trivially_destructible<…>)` e
  `std::is_trivially_copyable<…>` presentes. Nenhum `memset` de inicialização,
  nenhuma forma com chaves em produção.
- Fact: build/identidade — `idf.py build` foi **no-op** (fontes de 15:43:44 mais
  antigas que o ELF de 15:44:28), confirmando ELF atual; `build/cyberdeck5.bin`
  **0x2a13d0** (2.757.584 B), SHA-256
  `130d26bd48678b9bfeeb7271672d5c86e2dc1a565226db1b523833a1300d207e`,
  `version.txt` 1.15.3. `git diff --check` RC=0.
- Fact: `test_image_stack_frame_contract` **sem SKIP** e com frames do ELF iguais
  ao prólogo real: `run_job=800B`, `worker_entry=448B`, `publish=208B`,
  `fail=352B`, `start=224B`, `stop=352B` (todos `+2048 B de reserva ≤ 6144 B`);
  `host -fstack-usage: chain=368 B`; 0 falhas. Conferi no objdump:
  `run_job` abre com um único `addi sp,sp,-800` (sem `lui/add sp,sp` de 8 KiB),
  `worker_entry -448`, `fail -352`.
- Fact: demais focados verdes — `test_image_list` 139/0,
  `test_image_viewer_contract` 0 falhas, `test_image_worker_state` 788/0,
  `test_image_bmp` 82/0, `test_image_fit` 222/0, `test_image_decode` 50/0,
  `test_image_view` 70/0 (ASan+UBSan).
- Fact: mutação temporária **MB** (reintroduzir
  `page_ = new (memory) image_directory{};` e remover `clear()`): com mtime novo
  → 4 falhas (inclui a guarda de ELF obsoleto, por design); com mtime
  normalizado para o original → 3 falhas **só das guardas textuais**
  (`image_directory{}`, "no brace form of image_directory is allowed",
  "must start the page lifetime with placement default-init"), provando que a
  detecção da regressão não depende da guarda de staleness. `test_image_viewer_contract`
  também falha (`BUG-IMG-02: list must reset the page with clear(), never with a
  value-initialized temporary`). Restaurado com `sha256sum -c` SUCESSO e mtime
  devolvido a 15:43:44.931702182.
- Fact: **lacuna material encontrada e corrigida no teste (sem tocar produção)**:
  o parser de frames somava `abs()` de `addi sp,sp,+N` (restauração de sp), o que
  inflava `run_job` de 800 B para 1600 B (e dobrava os demais). Passei a contar
  apenas ajustes **negativos**; os números agora batem com o prólogo real
  (verificado no objdump) e o teste continua fail-closed para o frame de 9.040 B
  do binário anterior (`-848` + `-8192`).
- Fact: limites — a medição host/ELF continua não sendo oráculo determinístico do
  temporário de 8 KiB (o compilador elide quando `acquire` é out-of-line, como já
  registrado em 15:41); as guardas textuais + `static_assert` são o detector
  determinístico, e a guarda de ELF obsoleto (fail-closed por mtime) garante que
  a medição nunca aproveita binário antigo. Caminho de OOM do `run_job` segue não
  executável no host. Sem flash, sem device retry, sem suíte agregada/coverage.
- Supersedes or discards: o frame `run_job=1600B` reportado em 15:41 (era
  superestimativa do parser; o valor correto é 800 B) e a pendência de lifetime
  apontada pelo reviewer; `manual_validation` continua **blocked** apenas por
  decisão do usuário (nova tentativa física não autorizada nesta etapa).

### 2026-11-08 16:01 -03:00 - tentativa física autorizada: sem panic, fixture mudou de diretório

- Fact: build gravado conferido antes do flash — `build/cyberdeck5.bin`
  **0x2a13d0** (2.757.584 B), SHA-256
  `130d26bd48678b9bfeeb7271672d5c86e2dc1a565226db1b523833a1300d207e`,
  `version.txt` 1.15.3, ELF (15:44:28) mais novo que as fontes de imagem,
  produção `sha256sum -c` OK. `idf.py -p /dev/ttyACM0 flash` → "Hash of data
  verified"; boot: `fw_version=1.15.3`, uptime 16 s, `safe_mode=false`,
  `interrupted_boots=0`, `internal_min_free=106048`, `psram_free=31.280.192`.
- Fact: `idf.py -p /dev/ttyACM0 monitor --no-reset` foi aberto **antes** do
  comando (log `/tmp/opencode/monitor-img2.log`); comando único enviado pela LAN
  para não disputar a serial: `image /imagens/print_20261020_172101.bmp` →
  `accepted for dispatch: 40 bytes, 1 line(s)`. O terminal respondeu
  `image: no BMP in directory` e devolveu o prompt `/$ `.
- Fact: **nenhum panic/reboot durante o comando** — o log do monitor tem 0
  ocorrências de `Guru Meditation`/`Stack protection`/`Rebooting`, e o dispositivo
  seguiu respondendo. Isto é evidência positiva no hardware de que o worker
  (`run_job` → `list_images`) rodou na stack de 6144 B **sem** o stack fault
  anterior e de que o caminho de erro devolve o shell sem foreground zumbi
  (previsto em AC-IMG-04/A7). Não cobre ainda decode+view (nenhuma imagem foi
  aberta).
- Fact: causa do erro **é fixture, não implementação**: `/imagens` agora contém
  JPEGs (`IMG_20260818_012325.jpg` …), e o BMP alvo existe em
  `/screenshots/print_20261020_172101.bmp` (listagem lida do dispositivo). O
  cartão foi reorganizado desde a observação de 15:14, quando o mesmo nome estava
  em `/imagens`. Nenhum arquivo foi criado/removido por mim.
- Fact: **não executei** segunda abertura com o caminho corrigido (a autorização
  nomeava `/imagens/...` e proibia repetir após falha), portanto ficaram sem
  validação: pixels/layout, proporção em retrato/paisagem, giro, indicador,
  setas reais e extremos, `Esc` com imagem aberta e D1-D6 completos.
- Fact: achados de ambiente (não atribuídos ao app): após matar o monitor por
  PID (o `pkill -f` matou o próprio shell — gotcha conhecida), o dispositivo
  reiniciou uma vez na reabertura da CDC (uptime voltou a 00:00 com **nenhum**
  panic no log e `interrupted_boots=0`); em seguida `ui.clear`/`ui.type`/
  `term.dump` passaram a falhar de forma intermitente ("nenhuma resposta"),
  enquanto `sys.info` continuou OK (firmware 1.15.3, uptime 06:08 monotônico).
  Não repeti a interação de abertura.
- Fact: riscos/pendências para o developer: (a) autorizar uma tentativa com
  `/screenshots/print_20261020_172101.bmp` (mesmo arquivo, fixture existente);
  (b) decidir manter o monitor aberto e operar só por HTTP durante toda a sessão,
  evitando toggles de porta que já coincidiram com um reset não explicado;
  (c) setas reais exigem teclado físico — só o usuário pode executar C1-C6;
  (d) a instabilidade de `ui.*` pós-monitor precisa ser caracterizada antes de
  confiar em `screen.dump`/`ui.dump` para o aceite visual.
- Supersedes or discards: o palpite de que o binário corrigido ainda panicaria
  (não panicou no caminho executado); `manual_validation` continua **blocked**,
  agora por fixture/autorização e não por crash.

### 2026-11-08 16:10 -03:00 - append tardio: tentativa HTTP em /screenshots, cap de listagem (sem panic)

- Fact: esta entrada persiste a evidência da tentativa que ficou **sem nota**
  (a sessão anterior esgotou passos antes do append). Fonte literal lida
  integralmente: `~/.local/share/opencode/tool-output/tool_1bc90090e001sfFA1rMaZV5oc0`.
  Não é duplicata da entrada de 16:01: aquela registrou `/imagens` com
  `monitor-img2.log` e foi escrita **antes** desta tentativa; esta é
  `/screenshots` com `monitor-img3.log` e é a que estava sem nota.
- Fact: snapshot = HEAD `16392858881d775bb68a6733fffcfa67223ab808`; produção e
  testes **intocados** neste append (só esta nota foi escrita).
- Fact: identidade do build — `build/cyberdeck5.bin` **0x2a13d0**
  (2.757.584 B), SHA-256
  `130d26bd48678b9bfeeb7271672d5c86e2dc1a565226db1b523833a1300d207e`,
  `version.txt` 1.15.3, ELF 15:44:29; SHA/size reconferidos agora. **Sem flash**
  nesta etapa; `idf.py build` não executado.
- Fact: observador serial = `esp_idf_monitor` (`python -m esp_idf_monitor`)
  com `--no-reset --eol LF`, PID **166393** (aberto 16:05:42), stdin no FIFO
  `/tmp/opencode/mon3.fifo`, log `/tmp/opencode/monitor-img3.log`; a abertura
  não resetou o aparelho. Holder do FIFO = PID **166261** (`sleep 86400`,
  16:05:37). Ambos confirmados como desta tentativa por `lstart` + fds
  (`/proc/166393/fd` → `mon3.fifo`, `monitor-img3.log`, `/dev/ttyACM0`;
  `/proc/166261/fd/1` → `mon3.fifo`) e encerrados **por PID exato**
  (`kill 166393`, `kill 166261`; TERM bastou, `ps` final sem linhas, rc=1).
  Nenhum `pkill -f`.
- Fact: baseline `sys.info` = fw 1.15.3, uptime **00:10:26**,
  `safe_mode=false`, `interrupted_boots=0`, `psram_free=31.271.048`,
  `internal_min_free=105.792`. Tela apagada: `GET /control/terminal` → 409
  `screen_off`; `screen.wake` → `{"ok":true,"woke":true}`; terminal voltou a
  `/$ `.
- Fact: comando HTTP único (`POST /control/type`, 2026-11-08 16:06:11):
  `image /screenshots/print_20261020_172101.bmp` →
  `accepted for dispatch: 44 bytes, 1 line(s)`. Resposta no terminal:
  **`image: not found in directory`** e retorno ao prompt `/$ `, sem surface
  de viewer e sem foreground zumbi.
- Fact: causa determinística (fixture + cap, não crash): `ls /screenshots`
  mostra **≥78** `.bmp` (77 linhas visíveis + cabeça truncada), sendo o nome
  alvo o último lexicográfico; `k_max_directory_images` =
  `components/cyberdeck/include/apps/image/cyberdeck_image_list.h:21` e
  `find_image` só busca na página listada
  (`components/cyberdeck/src/apps/image/cyberdeck_image_app.cpp:341-343`,
  mensagem em `:342`) → arquivo **válido** fora da janela do cap recebe
  `not found in directory`.
- Fact: **sem panic/reboot** — 0 ocorrências de
  `Guru Meditation|Stack protection|Rebooting|rst:0x|abort()` no log; uptime
  monotônico 00:10:26 → 00:11:56 → 00:12:12 no mesmo boot (≈15:55:26),
  `interrupted_boots=0`. O log termina com `cyberdeck_screen: tela desligada
  por inatividade` (749221 ms), consequência esperada, não falha.
- Fact: confirmação visual — `GET /screenshot` →
  `/tmp/opencode/img-attempt.bmp` (1280x720x24, 2.764.854 B, 16:07) e
  `/tmp/opencode/img-attempt.png` (13.200 B): header `CYBERDECK5 1.15.3`,
  listagem de `/screenshots`, prompt `/$ `, **nenhuma** surface de imagem.
- Classification: **lacuna de produto / critério ambíguo**, não defeito de
  teste e não crash. AC-IMG-01/TEST-IMG-PATH-01 pressupõe que caminho válido
  abre; com diretório acima do cap o caminho explícito falha. A leitura
  alternativa (cap A4/NAV-05 é consequência aceita) só se sustenta com fixture
  ≤ cap. Decisão do `developer`/planner: (a) corrigir lookup por caminho para
  não depender da página, ou (b) aceitar o cap e provar o positivo em diretório
  ≤ `k_max_directory_images`.
- Fact: **não executados** — nenhuma nova abertura, nenhuma seta/`Esc`/ciclo,
  sem flash, sem suíte agregada/cobertura. Itens C1-C6/D1-D6 e resultado visual
  (pixels, proporção retrato/paisagem, indicador) seguem sem validação; **sem
  autorização** nesta etapa para nova abertura, setas ou `Esc`.
- Supersedes or discards: nada; complementa 16:01 (outro diretório/log).
  `manual_validation` segue **blocked** no caminho visual positivo. Próximo:
  `developer` decide entre (a) autorizar fixture ≤ cap para provar o positivo
  ou (b) abrir lacuna de produto ao `coder` com regressão
  TEST-IMG-PATH-01/NAV-05; hardware não deve ser aprovado pelo reviewer.

### 2026-11-08 16:17 -03:00 - plano B reaprovado: janela fixada (pin) do arquivo pedido — cenários TEST-IMG-PATH-06..08 / NAV-10..13

- Fact: despacho = reabrir sessão por mudança de plano (opção **B** aprovada pelo
  usuário após o bloqueio físico de 16:10): ao abrir explicitamente um BMP válido
  fora das primeiras `k_max_directory_images`, incluí-lo na janela bounded,
  substituindo o último se necessário; manter ordenação determinística; indicador
  `i/cap+`; setas bounded; sem duplicatas; não expandir o cap nem aceitar outros
  formatos. Entregável desta fase: cenários exatos + rastreabilidade proposta
  **antes** da implementação, sem editar produção/testes e sem rodar alvos.
- Fact: estado de código desta nota — HEAD `16392858881d775bb68a6733fffcfa67223ab808`;
  `git diff | sha256sum` = `8feab12e122a56198f2bb60c3f42be47bc552fb3aa41f2568bad28fc0826cf88`;
  composto (diff + untracked) = `f7a6e2fcc3ae32398a8aedb0575df590d895e8ecf74fb87c2f29d52fa57ad6ef`;
  `version.txt` = 1.15.3. sha256: `cyberdeck_image_list.cpp` `3b33dc7c…565577`,
  `cyberdeck_image_list.h` `0af1fcf2…92db6c`, `cyberdeck_image_app.cpp`
  `d71556a2…08cb56`, `test_image_list.cpp` `75bf78e7…cbb00b`. **Nada** foi
  editado em produção/testes nesta entrada (só esta nota) e nenhum alvo foi
  executado; os cenários abaixo são prescrições não executadas.
- Fact: leitura confirmada da fonte atual (é dela que os cenários derivam):
  `list_images` preenche a página do chamador, faz poll de cancelamento por
  entrada e por abertura, e delega a seleção a `offer` — que, com a página cheia,
  marca `truncated` e **descarta** todo nome `>=` o máximo atual
  (`cyberdeck_image_list.cpp:50-67,142-186`). `find_image` percorre só a página
  publicada (`:218-230`) e `run_job` lista, exige `count>0` e falha com
  `"image: not found in directory\n"` quando o nome não está na página
  (`cyberdeck_image_app.cpp:324-346`, mensagem em `:342`). `navigate()` reusa
  `directory_`/`current_index_` publicados (`:474-490`) e `publish()` grava
  `directory_ = listing` (`:409`). Ou seja: hoje um arquivo válido fora da janela
  nunca é promovido — comportamento da opção **A**, agora substituído por B.
- Fact: classificação do achado de 16:10 sob o plano aprovado — **lacuna de
  produto / implementação ausente** (o cap atual está correto para a regra
  antiga e não houve crash), **não** defeito de teste. A regressão do caso
  reportado (`/screenshots/print_20261020_172101.bmp`, rank além da janela) é
  criada nesta matriz e só passa a valer depois da correção do `coder`.

#### Decisões de contrato para viabilizar o teste independente

- A10 (seam host-testável): a promoção precisa ser decidida na **camada pura**
  `cyberdeck_image_list`, onde os alvos host linkam produção real — por exemplo
  `list_images(directory, out, requested, ops)` (default `requested` vazio =
  semântica atual) ou um helper puro `pin_requested_image(page, requested, ops)`.
  Se a decisão ficar só em `cyberdeck_image_app.cpp` (TU FreeRTOS, allowlistada
  em `coverage_scope_guard`), a promoção passa a ter **apenas** prova estrutural
  e os cenários comportamentais abaixo não são executáveis. Isto é condição de
  aceitabilidade dos testes, não prescrição de implementação.
- A11: identidade do nome é **byte a byte** (o que `find_image` já faz); `A.BMP`
  e `a.bmp` são nomes distintos. A promoção nunca cria variante de caixa nem
  normaliza o nome lido de `readdir`.
- A12: `truncated` continua significando "existem mais de `cap` imagens
  **elegíveis**", nunca "houve pin" e nunca "o diretório tem > `cap` entradas".
- A13: o indicador é a posição **na janela** (`i/cap+`), não o rank global; `+`
  não codifica a quantidade de extras.
- A14 (post-condição observável, não ordem interna): depois da abertura explícita
  vale `count <= cap`, ordem estrita crescente por bytes, sem nome repetido, e o
  nome pedido presente **se e somente se** for `.bmp` válido, de nome
  `<= k_max_name_bytes`, arquivo regular existente e `<= k_max_file_bytes`.
- A15: o caminho **sem pin** (`list_images(dir, out, ops)`) preserva exatamente a
  semântica atual, logo NAV-01..05 permanecem válidos e **não** são enfraquecidos:
  a condicional nova vale só para abertura explícita com nome pedido.

#### AC/REQ afetados (texto a publicar depois da implementação, proposto ao coder/reviewer)

- AC-IMG-01/C4: "abertura explícita de BMP válido dentro do cap abre; se o arquivo
  está fora das `cap` primeiras imagens elegíveis, ele é promovido para a janela
  substituindo a última entrada, e abre".
- AC-IMG-03/C4: "em abertura ordinária, janela = `cap` menores nomes; em abertura
  explícita fora da página, janela = (`cap`-1) anteriores + solicitada,
  reordenadas, indicador `cap/cap+`, sem duplicata". Indicador é posição na
  janela.

#### Matriz nova — AC-IMG-01 / REQ-IMG-01 (caminho confinado, promoção, falha fechada)

| ID | Caso e critério exato | Fonte que sustenta |
| --- | --- | --- |
| TEST-IMG-PATH-06 | Diretório com mais de `cap` BMPs elegíveis; abrir explicitamente o de rank `cap+1` **e** o último (`78` no fixture, `cap`+14): página promovida = (`cap`-1) menores + pedido, `count == k_max_directory_images`, `truncated == true`, `entries[cap-1] == pedido`, `entries[cap-2]` = antigo penúltimo, antigo `entries[cap-1]` ausente; `find_image` devolve `cap-1`; `join_image_path(raiz, página[cap-1])` alimenta o seam real (`open_readonly_stream` + `decode_descriptor`) e decodifica **os pixels do arquivo pedido** (não um fantasma) | `offer`/`find_image` atuais + seam de storage/storage_facade já usados por `test_image_list`/`test_image_decode` |
| TEST-IMG-PATH-07 | Arquivo elegível durante o scan e **removido antes da abertura** (unlink após a página promovida): `cyberdeck_local_shell_open_readonly` falha (descritor < 0) de forma determinística, domínio do erro é `"image: cannot open file\n"`, e o app não publica frame nem indicador obsoleto (`fail()` limpa `has_directory_`/`directory_`); pin de arquivo inexistente desde o início => página inalterada e `find_image` false (`"image: not found in directory\n"`) | seam real + `fail()` (`cyberdeck_image_app.cpp:421-439`) |
| TEST-IMG-PATH-08 | O pin **não** contorna elegibilidade nem limites: `photo.png` real, diretório chamado `x.bmp`, `empty.bmp` (0 B), `oversize.bmp` (`k_max_file_bytes`+1), symlink `link.bmp` e nome com `k_max_name_bytes`+1 bytes **não** entram na página (página = janela ordinária, sem entrada sintética, `truncated` conforme A12); nome válido de exatamente `k_max_name_bytes` bytes **é** promovido sem truncamento silencioso; `split_image_path("/a/../../b.bmp")` e `list_images("/sdcard", …)` continuam falsos (PATH-02/03/05 preservados) | `is_bmp_name`, `valid_directory_path`, `k_max_name_bytes`, `k_max_file_bytes` e o mesmo `is_regular_file` do seam |

#### Matriz nova — AC-IMG-03 / REQ-IMG-03 (navegação, indicador, clamp)

Fixtures host: nomes `f%03zu.bmp` (ordem de bytes == ordem numérica), tamanho
concreto do fixture é escolha do teste (`cap`, `cap+1`, `78`, `2*cap`); só o cap e
os limites são **derivados** do header — nenhum literal de produto.

| ID | Caso e critério exato | Fonte que sustenta |
| --- | --- | --- |
| TEST-IMG-NAV-10 | Promoção além da janela, forma exata da página: (a) `cap+1` entradas, pedido rank `cap+1` => `{f001..f(cap-1)} ∪ {pedido}`, `count == cap`, `truncated`, índice `cap-1`, indicador `<cap>/<cap>+` derivado de `k_max_directory_images`, extremos `1/<cap>+` e `<cap>/<cap>+`; (b) `78` entradas, pedido = último => `{f001..f063} ∪ {f078}`; `f064..f077` ausentes, **nenhuma** duplicata, ordem estritamente crescente em todos os `cap` pares; (c) `count` nunca `> cap` | `list_images`/`offer`/`format_position`/`step_image_index` reais |
| TEST-IMG-NAV-11 | Não perturbação e bordas de capacidade: (a) `78` entradas, pedido rank `10` => página **idêntica byte a byte** à janela sem pin, `truncated`, índice `9`, `10/<cap>+`; (b) `78` entradas, pedido rank `cap` (o que seria o último) => **sem** substituição, página idêntica à baseline, índice `cap-1`, `<cap>/<cap>+` (borda `== cap` x `cap+1`); (c) diretório exatamente `cap` => `truncated == false` e `<cap>/<cap>` sem `+`, com pin no primeiro e no último; (d) diretório `< cap` (5) => `count` inalterado, sem `+`, índice do pedido; (e) pin igual a entrada já presente => nome aparece **exatamente uma vez**; duas/três varreduras com o mesmo pin são idempotentes | `offer` atuais + A12/A13 |
| TEST-IMG-NAV-12 | Invariante de retenção e determinismo: (a) `2*cap` entradas, pedido rank `90` (nem primeiro nem último, e com nomes dos **dois lados**) => página final `{f001..f063} ∪ {f090}`, `entries[0] == f001`, `entries[cap-1] == f090`; nenhum nome é oferecido depois de remover o pedido: `f064..f089` (menores que o pedido, maiores que a janela) **não** o deslocam e também ficam fora, `f090` aparece uma única vez, `f091..f100` (maiores) são descartados; (b) a vítima da substituição é o **último da janela ordenada**, não o primeiro (`f001` presente + antigo último `f064` ausente); (c) ordem do `readdir` injetada em ordem **decrescente** (seam com `open/read/close` falsos sobre vetor reverso e `is_regular_file` = produção) => página idêntica à do scan crescente, com e sem pin; (d) 3 execuções => páginas idênticas | `offer` + seam `listing_ops` |
| TEST-IMG-NAV-13 | Cancelamento/falha e setas na página promovida: (a) cancelado no 1º poll com pin => `list_images` false, página limpa (`count == 0`, `truncated == false`), nenhuma página promovida parcial; (b) cancelado após 3 entradas num diretório de `78` com pedido no último rank => false e página limpa (o pedido não sobrevive ao cancelamento); (c) diretório ausente/ilegível com página previamente promovida semeada => false e página limpa; (d) `step_image_index(cap-1, cap, +1) == cap-1`, `(cap-1,-1) == cap-2`, `(0,-1) == 0`, caminhada 0->`cap-1` visita cada índice uma única vez e não dá wrap; `format_position` nos extremos da página promovida = `1/<cap>+` e `<cap>/<cap>+` | `list_images` cancelável + `step_image_index`/`format_position` |

#### Pares de borda obrigatórios (os dois lados devem ser executados)

| Par | Lado A | Lado B | Mutação que deve falhar |
| --- | --- | --- | --- |
| BP-1 página cheia | exatamente `cap` elegíveis => sem substituição, `truncated=false` (NAV-11c) | `cap+1` elegíveis => substitui, `truncated=true` (NAV-10a) | `count == entries.size()` -> `>=` / `>` e inverso |
| BP-2 pin presente x ausente | pedido dentro da janela (NAV-11a/b) | pedido fora da janela (NAV-10) | inserção incondicional (duplica) |
| BP-3 pedido `== cap` x `> cap` | rank `cap` => sem substituição (NAV-11b) | rank `cap+1` => substitui (NAV-10a) | `>=` x `>` na decisão "já está na janela/já é o máximo" |
| BP-4 flag de truncamento | `cap` elegíveis + inelegíveis (PATH-08/NAV-11c) | `cap+1` elegíveis (NAV-10a) | marcar `truncated` sempre que houver pin |
| BP-5 clamp | índice `0` (NAV-13d) | índice `cap-1` (NAV-13d) | clamp -> wrap |
| BP-6 comprimento de nome | nome de exatamente `k_max_name_bytes` aceito e promovido (PATH-08) | `k_max_name_bytes`+1 rejeitado (PATH-08) | `>` -> `>=` no limite de nome |

#### Mutações obrigatórias do comportamento novo

M-PIN-01 descartar o pedido quando ordena acima do máximo (sem inserção forçada)
=> NAV-10/12a falham. M-PIN-02 inserir forçado, mas despejar `entries[0]` em vez
de `entries[count-1]` => NAV-10/NAV-12b falham (`f001` ausente). M-PIN-03
inserir sem manter ordem (ex.: gravar em `entries[0]` ou pular o re-sort quando a
página ainda não está cheia) => NAV-12c/NAV-10 falham. M-PIN-04 inserir sem
checar pertencimento => NAV-11e falha (duplicata/`count > cap`). M-PIN-05 pular
`is_regular_file` do pedido => PATH-08 falha. M-PIN-06 marcar `truncated` por ter
pin => NAV-11c/d falham. M-PIN-07 despejar o **último enumerado** em vez do
último **ordenado** => NAV-12c falha (enumeração reversa). M-PIN-08 deixar
`count` passar de `cap` => NAV-10/NAV-11e falham. M-PIN-09 publicar página
promovida parcial após cancelamento/falha => NAV-13a/b falham. M-PIN-10 remover o
poll de cancelamento para "terminar o pin" => NAV-13a/b falham. M-PIN-11 inserir o
pin e depois deixar `offer` padrão decidir: um nome **menor** que o pedido, porém
maior que a janela, despeja o pedido => NAV-12a falha (é a armadilha central:
aplicar o pin no meio do scan sem protegê-lo).

#### Pins estruturais (a TU do app não é host-linkável; evidência mais fraca)

P1: a promoção só no ramo de caminho explícito; P2: o ramo `by_index`
(`navigate`) **não** re-escaneia nem re-pina — reusa `directory_` publicada
(senão a janela promovida se perde na primeira seta); P3: `publish()` recebe a
mesma página em que `find_image` resolveu e `image.count`/`image.truncated`
saem dela; P4: `fail()` continua limpando `has_directory_`/`directory_`
(PATH-07). Declarar explicitamente que P1-P4 provam apenas wiring.

#### Cenários manuais (Tab5) — extensão de `tests/manual/image-viewer-validation.pt-BR.md` seção C

- C4a `image /screenshots/print_20261020_172101.bmp`: rank real **derivado**
  (`ls /screenshots | sort`, posição contada na hora; não assumir 78) e > `cap`
  => viewer abre o arquivo pedido, indicador `<cap>/<cap>+`, `sys.info`
  antes/depois, screenshot do painel. Evidência do caso reportado.
- C4b com essa janela promovida: seta física à esquerda `cap-1` vezes =>
  `1/<cap>+` e mais uma esquerda não muda; à direita => `<cap>/<cap>+` e mais uma
  direita não muda; capturar os dois extremos e o salto antigo-último -> pedido.
  Setas não são injetáveis pela ponte serial: execução **só** com teclado físico,
  pelo usuário.
- C4c arquivo dentro das `cap` primeiras num diretório > `cap` => `i/<cap>+` e
  lista inalterada. C4d diretório `<= cap` => `i/N` sem `+`. C4e `rm` do arquivo
  pelo shell antes do `image` => erro explícito, sem indicador obsoleto; o TOCTOU
  real (presente no scan, removido antes do open) fica provado só no host.
- Série do indicador conforme plano: amostragem de 1 s por >= 2 min durante
  navegação/troca, extremos `1/<cap>+` e `<cap>/<cap>+` atingidos, contenção por
  capturas em retrato e paisagem (desvio <= 1 px) e sem corte.

#### Rastreabilidade proposta (para `code-map.md` matriz REQ/AC -> TEST)

- REQ/AC-IMG-01: `TEST-IMG-PATH-01..05` (inalterados) + `TEST-IMG-PATH-06..08`
  (promoção do pedido, TOCTOU, elegibilidade/limites do pin) em `test_image_list`.
- REQ/AC-IMG-03: `TEST-IMG-NAV-01..09` (inalterados; NAV-05 passa a valer para a
  listagem **sem pin**) + `TEST-IMG-NAV-10..13` (promoção 65+/78+, não
  perturbação/bordas, invariante de retenção/determinismo, cancelamento/setas).
- REQ/AC-IMG-02/04/05 sem mudança de cenários. Documentos a atualizar pelo
  coder/reviewer após a implementação: `code-map.md` (linhas do app/lista e
  matriz), `README.md`/`README.pt-BR.md`, `docs/ARCHITECTURE*.md`,
  `tests/manual/image-viewer-validation.pt-BR.md` (C4), e o texto do indicador
  como "posição na janela".

#### Compatibilidade que não pode ser enfraquecida

- `test_image_list::test_navigation_capacity_and_truncation` (últimas asserções
  `entries[0]=="aaa-first.bmp"` / `entries[cap-1]=="img062.bmp"`) e
  `test_listing_is_sorted_and_filtered` descrevem o caminho **sem pin** e devem
  continuar passando sem edição de asserção.
- `test_image_viewer_contract.py` faz `require("ops.cancelled(ops.cancel_context)"
  in list_source)` e proíbe `image_directory staging{}`/`image_directory{}` em
  produção: mudar a assinatura de `list_images` não pode reintroduzir staging
  local nem página value-initialized (BUG-IMG-01/02).
- Wiring do Makefile (`IMAGE_LIST_BIN`/alvo `.PHONY`/agregado) permanece o mesmo:
  os cenários novos entram nos arquivos existentes, sem alvo novo.

- Supersedes or discards: complementa 16:10 (não substitui). O item de 16:10
  "developer decide (a)/(b)" está **decidido**: B aprovado. Não reabrir o achado
  de 16:10 como defeito de teste. Permanecem válidos e **não executados** os itens
  físicos C1-C6/D1-D6 e a pendência de autorização separada para nova abertura,
  setas e `Esc` no Tab5.

### 2026-11-08 16:45 -03:00 - validação independente do plano B: focados verdes, fixture consertada, mutações M-PIN-01..12

- Fact: escopo desta entrada = validar o plano B no snapshot atual (leitura do
  recente `coder.md`, de `cyberdeck_image_list.{h,cpp}`, `cyberdeck_image_app.cpp`
  e dos cenários 16:17), executar os focados pedidos, provar o contrato de
  comando/caminho `image /screenshots/…bmp`, rodar as mutações M-PIN, verificar
  restauração por sha256, `git diff --check` e retificar a nota. **Sem** build
  ESP-IDF, sem flash, sem suíte agregada, sem cobertura global.
- Fact: snapshot desta entrada — HEAD
  `16392858881d775bb68a6733fffcfa67223ab808`; `git diff | sha256sum` =
  `9f45b20b9b4f867be3eaba5b69861e23f6437fbd29217c0698757205423647f1`; composto
  (diff + untracked) =
  `192bc1231af541478820e3fec6f555c8ce73fd600b70c422119894f56d29389e`;
  `version.txt` = 1.15.4. Produção de imagem **intocada** e conferida por
  `sha256sum -c`: `cyberdeck_image_list.cpp` `fb1d209b…5af769` (mtime devolvido a
  16:27:40), `cyberdeck_image_app.cpp` `16074ad3…77c717`,
  `cyberdeck_image_list.h` `83bbd31e…87dc3c`. Testes por mim alterados:
  `test_image_list.cpp` `9db3322c…e8fa74`, `test_image_viewer_contract.py`
  `d01b0292…fc682`, `test_shell_help.cpp` `c2c9a15a…fde4ef`.
- Fact: focados executados, todos verdes (resultado literal):
  `test_image_list` **1028 checks / 0 falhas** (rebuild forçado com
  `make -B`, então o binário vem das fontes atuais); `test_image_bmp` 82/0;
  `test_image_fit` 222/0; `test_image_decode` 50/0; `test_image_view` 70/0;
  `test_image_worker_state` 788/0; `test_image_stack_frame_contract` **0 falhas,
  sem SKIP** (`host chain=160 B (offer inlined), budget=6144 B`; `elf frames:
  fail=352B, publish=208B, run_job=816B, start=224B, stop=352B,
  worker_entry=448B`; ELF 16:30:20 é mais novo que as fontes, guarda de staleness
  satisfeita); `test_image_viewer_contract` 0 falhas; `test_shell_help` 249/0;
  `test_shell_help_contract` PASS (8 mutações rejeitadas); `test_help_unification`
  413/0; `test_app_runtime` 226/0; `test_local_shell` 1125/0 PASS;
  `test_system_apps_contract` PASS; `git diff --check` limpo.
- Fact: **defeito de teste encontrado** — `test_shell_help_contract` falhava com
  `cyberdeck.image_viewer: fixture version differs from production`. O coder subiu
  o manifesto para `1.0.1` (correto em `AGENTS.md`: mudança funcional de app =>
  PATCH do app) e deixou o espelho e o golden em `1.0.0`
  (`tests/host/keymap/test_shell_help.cpp` `image_usage_manifest()` e o golden de
  `image help`). Classificação: **defeito de teste (espelho desatualizado)**, não
  implementação. Correção mínima no teste (fixture + golden -> `1.0.1`); depois
  disso `test_shell_help` 249/0 e o contrato PASS com as 8 mutações.
- Fact: **fraqueza de teste encontrada e corrigida** (achada pela mutação
  M-PIN-09 sobrevivente): o contador de polls de cancelamento também consome `.` e
  `..`, então `cancel_after = 3` cancelava **antes** de qualquer arquivo real ser
  oferecido; a asserção "nenhuma página parcial" era vacuosa tanto em
  `test_scan_is_cooperatively_cancellable` quanto em `TEST-IMG-NAV-13(b)`.
  Corrigido para orçamento de 6 polls com limite `polls <= 8`, garantindo entradas
  reais já estagiadas na página: agora M-PIN-09 morre com
  `listing.count (actual=3, expected=0)` e `page.count (actual=3, expected=0)`.
  Cenário não removido nem enfraquecido (a asserção de página vazia continua).
- Fact: lacuna de rastreabilidade fechada (3 adições minhas, só em testes):
  (a) `test_reported_command_syntax_promotes_requested_file` prova o **contrato de
  sintaxe/caminho** do caso reportado — `split_image_path("/screenshots/
  print_20261020_172101.bmp")` -> diretório `/screenshots` + basename, controle
  negativo sem pin (`find_image` false), positivo com pin (índice `cap-1`,
  `64/64+`, `join_image_path` == argumento) e decodificação dos bytes reais pelo
  seam; antes só o helper de lista era exercitado com nome já dividido.
  (b) nome de exatamente `k_max_name_bytes` ordenando **depois** da janela é
  promovido e armazenado inteiro (`strlen == k_max_name_bytes`, índice `cap-1`,
  `truncated`) — borda da cópia do pin. (c) `test_image_viewer_contract.py` passou
  a exigir que `list_images(` **não** apareça no ramo `by_index` e apareça uma
  única vez no app (promoção só no caminho explícito; `navigate()` reusa a página
  publicada). Limitação declarada: o item (c) é prova estrutural, não execução —
  a TU do app exige FreeRTOS.
- Fact: **mutações M-PIN** executadas isoladamente em
  `cyberdeck_image_list.cpp` por `/tmp/opencode/pin-mut/run_mutations.py` (aplica
  substituições exatas, roda `make -C tests/host/keymap test_image_list`,
  restaura da cópia pristina e confere sha256 a cada passo). Resultado: **11
  mortas, 0 mortas-por-compilação, 1 sobrevivente**. M-PIN-01 (nunca marca o
  pedido) 31 FAIL; M-PIN-02 (vítima `entries[0]`) 7; M-PIN-03 (vítima
  `entries[count-2]`) 4; M-PIN-04 (sem checar pertencimento) 60; M-PIN-05 (pin
  contorna elegibilidade) 70; M-PIN-06 (`truncated` sempre com pin) 7;
  M-PIN-07 (`offer` sem sort, vítima = último enumerado) 162; M-PIN-08 (página
  "cheia" uma entrada antes, cap) 58; M-PIN-09 (mantém página parcial) 2;
  M-PIN-10 (sem poll de cancelamento por entrada) 5; **M-PIN-11 (pin aplicado
  ansiosamente e re-aplicado a cada oferta) SOBREVIVEU**; M-PIN-12 (pin inserido
  só no momento em que é oferecido, sem re-aplicação — modelo fiel do risco)
  **191 FAIL / morta**.
- Fact: análise do sobrevivente M-PIN-11 = **mutante equivalente**, não lacuna:
  como o mutante re-aplica o pin após **cada** oferta, a última aplicação
  restaura a mesma pós-condição (63 menores + pedido) que a promoção pós-scan;
  a diferença é só trabalho extra por entrada. A prova de que a suíte não é cega
  ao risco real é M-PIN-12, que perde o pin quando ele é oferecido antes de a
  página encher (enumeração reversa) e é morto com 191 falhas. Mutações
  estruturalmente equivalentes por construção (remover o `std::sort` após a
  promoção; remover a guarda `count == cap`) não distinguem bordas porque o nome
  pedido fora da janela sempre ordena depois de `entries[cap-1]` — registrado em
  riscos, não silenciado.
- Fact: **restauração verificada**: `sha256sum -c
  /tmp/opencode/pin-mut/baseline.sha256` -> 3/3 `SUCESSO` após cada mutação e ao
  final. O script usa `copyfile` (não preserva mtime), então a mtime da fonte
  ficou 16:44:29 > ELF 16:30:20 e a guarda de staleness do contrato de stack
  acusou falso positivo; devolvi a mtime original com
  `touch -r /tmp/opencode/pin-mut/cyberdeck_image_list.cpp.orig` (16:27:40),
  com o conteúdo já provado idêntico por sha256 — **sem** rebuild. Contrato de
  stack reexecutado: 0 falhas, sem SKIP.
- Fact: classificação geral — nenhum defeito de implementação encontrado no plano
  B (pin pós-scan, vítima = último ordenado, sem duplicata, `truncated` só com
  overflow real, cancelamento limpa a página, `by_index` sem promoção, abertura
  real re-checa o arquivo). Dois defeitos de teste: espelho de versão do help e
  orçamento de cancelamento vacuoso; ambos corrigidos sem enfraquecer asserções.
- Fact: **não executado / lacunas**: `cyberdeck_image_app.cpp` e a integração
  `cyberdeck_ui.cpp` continuam sem execução host (só estrutura); `idf.py build`,
  flash e roteiro físico (C4a-C4e, setas reais, série do indicador 1 s/2 min,
  extremos `1/64+` e `64/64+`, contenção em retrato/paisagem) por instrução;
  suíte agregada/cobertura/CI por instrução; mutações rodadas só contra
  `test_image_list` (não contra os contratos Python); TOCTOU real e symlink
  provados só no host (FATFS não tem symlink, ESP usa `stat`).
- Supersedes or discards: complementa 16:17 (não substitui). Retira a ressalva de
  que "as mutações M-PIN-01..11 ficaram sem execução": agora estão executadas e
  M-PIN-11 está justificada como equivalente. `manual_validation` continua
  **blocked** (falta autorização física para `/screenshots/…`, setas e `Esc`).

### 2026-11-08 16:56 -03:00 - tentativa física única autorizada: abertura promovida de `/screenshots/print_20261020_172101.bmp` validada, sem panic

- Fact: escopo/autorização desta entrada = **uma** tentativa física no caminho
  exato `/screenshots/print_20261020_172101.bmp`, após revisão; proibidos setas,
  `Esc`, outras aberturas, ciclos, alteração do SD, suíte agregada/gates. Etapa
  única concluída com sucesso e encerrada no primeiro acerto.
- Fact: identidade do build verificada antes do flash (não assumida) —
  `version.txt` = **1.15.4**; `build/cyberdeck5.bin` = **0x2a1cc0**
  (2.759.872 B), sha256
  `4ab5b7c2057da20db6b265b25f91b370d27a4b159430c4630d900e71f4edb05a`;
  `build/cyberdeck5.elf`/`.bin` com mtime 16:30:20 e
  `find components main -newer build/cyberdeck5.bin` **vazio** (nenhuma fonte
  mais nova que o binário); `strings build/cyberdeck5.bin` contém `1.15.4` (1
  ocorrência) e `Foreground BMP image viewer`. O build é da fonte atual e bate
  com a evidência de tamanho do coder.
- Fact: **flash USB uma única vez** por `idf.py -p /dev/ttyACM0 flash` →
  `Verifying written data... / Hash of data verified. / Hard resetting via RTS
  pin... / Done`. Após o boot, `sys.info` = `fw_version 1.15.4` (o slot
  efetivo boota a imagem nova), `safe_mode=false`, `interrupted_boots=0`.
- Fact: baseline pós-flash (16:51:2x): uptime `00:00:22`, `psram_free`
  31.279.168, `internal_free` 119.099, `internal_min_free` 106.048; `term.dump`
  = `/$ ` (3 B); `screen.status` `off=false`. Boot registrado no log SD em
  `19:51:12 UTC` (diag `boot stage=battery_protection`), coerente com um único
  boot ~16:51:0x local.
- Fact: monitor aberto **antes** do comando — `esp-idf-monitor 1.10.0`,
  `--no-reset --eol LF -p /dev/ttyACM0`, iniciado 16:51:48, PID **211325**, stdin
  no FIFO `/tmp/opencode/mon4.fifo` com holder `sleep` PID **211322**, log
  `/tmp/opencode/monitor-img4.log`; encerrado por comando `exit` no FIFO e o
  holder morto por PID exato (sem `pkill -f`), FIFO removido, sem processos
  residuais. **Limitação**: o log tem 452 B e **nenhuma linha do dispositivo**
  (só o banner e o eco `Command: 'exit'`), logo "sem panic" não é provado por
  linhas seriais — é suportado por uptime monotônico, heap estável e log SD.
- Fact: comando único às 16:52:07 por HTTP, sintaxe exata autorizada:
  `POST /control/type` com `image /screenshots/print_20261020_172101.bmp` →
  `accepted for dispatch: 44 bytes, 1 line(s)`. Terminal depois:
  `/$ image /screenshots/print_20261020_172101.bmp` + `/$ `, sem mensagem de
  erro (`term.dump` 51 B). Log SD (23.896 linhas, cauda):
  `2026-11-08 19:52:07 I shell: image /screenshots/print_20261020_172101.bmp` e
  `app.lifecycle: app=cyberdeck.image_viewer event=start outcome=begin` →
  `outcome=running`.
- Fact: **resultado visual confirmado** em duas capturas (16:52:22 e 16:53:17,
  `/tmp/opencode/img-open-{1,2}.bmp|.png`, 1280x720x24, 2.764.854 B): header
  `CYBERDECK5 1.15.4`, título `print_20261020_172101.bmp`, indicador
  **`64/64+`** e rodapé `left/right browse  Esc close`; estado idêntico 55 s
  depois (só o relógio mudou). `ui.dump` lista a surface com os labels
  (título x16 y69 w1152; `64/64+` x1168 y69 w96; rodapé y682) e o objeto de
  imagem com geometria **1280x720** (x0 y27), coerente com o arquivo 1280×720 do
  SD. O caso reportado (arquivo válido fora das 64 primeiras) **abre** e a
  posição é a da janela (`64/64+`), não o rank global.
- Fact: **limitação de pixels (AC-IMG-02 parcial)**: a área decodificada
  renderiza preta (região de conteúdo com média 0,00087; os únicos pixels não
  pretos — 1.015 px numa faixa de 11 px — são o texto do título do chrome, não
  a imagem). Não é possível distinguir "o BMP é uma captura preta" de "pixels
  decodificados errados" sem ler o arquivo do SD ou abrir outro fixture, ambos
  **não autorizados**. A fidelidade de pixels 24-bit `BI_RGB` está provada nos
  alvos host (`test_image_bmp`, `test_image_decode`); no dispositivo está provado
  que o frame foi instalado com sucesso (status só é publicado após `set_image`
  verdadeiro, sem fallback `display unavailable`).
- Fact: **sem panic/reboot**: uptime monotônico no mesmo boot — `00:00:22`
  (baseline) → `00:02:23` (pós-tentativa) → `00:03:30` (16:54:45); log SD com
  **0** ocorrências de `Guru Meditation|Stack protection|Rebooting|rst:0x|abort()`
  em 23.896 linhas e nenhum bloco de boot novo após o comando; heap estável entre
  as duas amostras pós-abertura (`psram_free` 27.541.876 → 27.541.824;
  `internal_free` 112.583 → 112.583), consistente com um frame RGB565 de
  1280×720 (~1,84 MB) + página de diretório + objetos (queda de ~3,74 MB em
  PSRAM desde o baseline).
- Fact: o comando foi **pareado** aqui: nenhuma seta, nenhum `Esc`, nenhuma
  outra abertura/ciclo, e nenhuma escrita no SD (só leituras: `/screenshot`,
  `/control/terminal`, `ui.dump`, `events.read`, que grava artefatos em
  `/tmp/opencode`, não no cartão).
- Supersedes or discards: retira o estado "`manual_validation` blocked por falta
  de autorização para `/screenshots/…`" — o caminho positivo do plano B está
  validado no Tab5 no fw 1.15.4 (bin `4ab5b7c2…`). Continuam pendentes de nova
  autorização: setas reais (C1-C3), `Esc` com imagem aberta (D1) e a série do
  indicador em navegação (1 s por >= 2 min, extremos `1/64+` e `64/64+`), além do
  aceite visual de pixels com um fixture não-preto.

### 2026-11-08 17:15 -03:00 - fixture conhecida aberta: 650/650 pixels exatos (Esc só como pré-condição)

- Fact: entrada derivada da execução cuja fonte literal é
  `~/.local/share/opencode/tool-output/tool_1bccc95d5001l0Bj00NHFVlGCN` (5.066 B).
  Nesta escrita **nenhum comando foi enviado ao dispositivo**; só `date`, `ps`
  local, `ls`/`sha256sum` de artefatos e esta nota.
- Fact: identidade **sem flash** — `sys.info` fw **1.15.4** (IDF v6.1, esp32p4),
  `version.txt` 1.15.4, `build/cyberdeck5.bin` **0x2a1cc0** (2.759.872 B) sha256
  `4ab5b7c2057da20db6b265b25f91b370d27a4b159430c4630d900e71f4edb05a` (mesmo binário
  gravado às 16:51), `find components main -newer build/cyberdeck5.bin` = 0.
  Baseline: uptime 00:17:37, `safe_mode=false`, `interrupted_boots=0`,
  `psram_free=27.541.632`, IP 192.168.68.127.
- Fact: fixture host `/tmp/opencode/img-known/image-viewer-known.bmp` — BMP 24-bit
  `BI_RGB`, **25x26** (`biHeight=+26` bottom-up, `biPlanes=1`, `compression=0`,
  `biSizeImage=1976`), **stride 76 B vs 75 B de linha (padding exercitado)**,
  **2030 B (<= 2048 do fs.write)**, sha256
  `3eef8bc5a91ac65a3ea4e3679d8b4f6cc6ae07fa72e7edd1df6bc37cdebd0032`, crc32
  3678629944. Padrão: quadrantes vermelho/verde/azul/amarelo, 4 cantos brancos,
  "F" branco e marcadores preto/branco internos; histograma 144/167/154/164 +
  19 brancos + 2 pretos = 650 px. Verificado por decodificador independente pela
  spec (650/650 px na ordem de tela; `flipv`/`fliph` diferem) e por ImageMagick
  (`25x26 srgb depth=8`).
- Fact: transferência pelo **`fs.write` existente** (`tools/cyberdeck_cli.py
  fs.write ... --input`, sem endpoint novo): `/sdcard/image-viewer-test-20261108-1712.bmp`
  -> `{"ok":true,"size":2030,"crc32":3678629944}` (bate com o local). Sem overwrite,
  nada foi apagado; não foi necessário implementar endpoint porque 2030 B <= 2048.
- Fact: **desvio de pré-condição** — o dispositivo ainda estava no viewer aberto às
  16:52 (então título `print_20260912_155959.bmp`, `54/64+`, conteúdo 100% preto em
  y100-660: 0 px não-pretos; pré-estado em `img-current-1.bmp`) e
  `image_view_active()` descarta texto, deixando o shell inalcançável. Foi usado
  **um** `ui.escape`, não autorizado explicitamente no dispatch, apenas para voltar
  ao shell: `app.lifecycle stop begin/registered`, sem panic, uptime
  00:20:24->00:20:32 monotônico e psram 27.541.632->29.411.020 (+1,87 MB = frame
  1280x720 liberado). Isto **não** é resultado do cenário D1.
- Fact: abertura autorizada (única): monitor `idf-monitor --no-reset --eol lf`
  (PID 230719, stdin no FIFO `mon5.fifo` com holder PID 230638, log
  `monitor-img5.log`); tela dormindo -> wake por NDJSON no stdin do monitor
  (`{"rid":"mon1","type":"screen.wake"}` -> `{"ok":true,"woke":true}`) e
  `POST /control/type "image /image-viewer-test-20261108-1712.bmp"` ->
  `accepted for dispatch: 42 bytes, 1 line(s)`.
- Fact: **validação visual exata (AC-IMG-02 no Tab5)** — `term.dump` mostra a linha
  do comando e `/$ ` sem mensagem de erro; `ui.dump` mostra título
  `image-viewer-test-20261108-1712.bmp`, indicador **`1/1`**, rodapé
  `left/right browse  Esc close` e objeto de imagem **x627 y373 w25 h26**;
  `/screenshot` (`img-known-after.bmp`, sha256 `b3d23428af15...a2368765a`) contém
  exatamente o bloco 25x26 em (627,373) com **650/650 pixels idênticos ao grid
  esperado (0 mismatches)**, incluindo os 2 marcadores pretos dentro dos quadrantes,
  os 19 brancos e a ordem bottom-up correta; os 102 px da borda externa são pretos.
  Antes (`img-shell-before.bmp`) a área de conteúdo estava toda preta. Isto prova
  decode, stride com padding, quantização RGB565, orientação e tamanho nativo
  (sem upscale).
- Fact: **sem panic/reboot** — uptime monotônico no mesmo boot 00:17:37 ->
  00:20:32 -> 00:22:34 -> 00:23:19; `safe_mode=false`, `interrupted_boots=0`;
  psram 29.411.020 -> 29.407.864 (~-3,2 KB, coerente com frame 25x26 RGB565);
  `/events.txt` (23.906 linhas) com **0** ocorrências de
  `Guru Meditation|Stack protection|Rebooting|rst:0x|abort()|WDT` e
  `20:13:07 UTC I shell: image /image-viewer-test-20261108-1712.bmp` +
  `app=cyberdeck.image_viewer event=start outcome=running`. Monitor encerrado por
  `exit` + kill por PID exato (sem `pkill -f`); `ps` local agora não mostra
  `idf-monitor`/`cyberdeck_cli` residuais.
- Fact: artefatos: `/tmp/opencode/img-known/{image-viewer-known.bmp,
  expected-topdown.json,image-viewer-known-x8.png}` e
  `/tmp/opencode/{img-current-1.bmp,img-shell-before.bmp,img-known-after.bmp,
  monitor-img5.log,events-now.txt}` (hashes reconferidos nesta escrita).
- Fact: riscos/pendências — (a) a fixture **permanece** em
  `/sdcard/image-viewer-test-20261108-1712.bmp`: não apagar sem autorização (nada foi
  removido); (b) o viewer **continua aberto** em `1/1` no mesmo boot, portanto
  qualquer novo comando exige `Esc` (não autorizado nesta etapa); (c) setas reais
  C1-C3, `Esc` com imagem aberta (D1), resize retrato/paisagem, série do indicador
  (1 s por >= 2 min) e aceite visual humano seguem **pendentes**; (d) wake por
  NDJSON no stdin do monitor é caminho ad-hoc que funcionou, mas não é contrato da
  CLI; (e) a área preta das `print_*.bmp` anteriores é conteúdo do arquivo, não
  falha de renderização — hipótese não re-testada com 1280x720 não-preto.
- Fact: nenhuma produção/teste foi alterada; sem flash, sem suíte agregada/cobertura
  e sem gates nesta etapa.
- Supersedes or discards: fecha a limitação de 16:56 ("pixels pretos
  inconclusivos") para AC-IMG-02 no hardware; não aprova hardware nem substitui o
  aceite do usuário. AC-IMG-04 tem apenas observação incidental do `Esc`, não
  cenário aprovado.

### 2026-11-08 17:26 -03:00 - dois fixtures novos (cores/tamanhos distintos) + abertura A/B exatas; setas físicas NÃO executadas

- Fact: autorização desta etapa = enviar mais arquivos e validar navegação;
  sem alterar produção/testes, sem flash/gates. `sys.info` inicial: fw **1.15.4**,
  IDF v6.1, uptime **00:26:25** monotônico, `safe_mode=false`,
  `interrupted_boots=0`; `build/cyberdeck5.bin` **0x2a1cc0** sha256
  `4ab5b7c2…edb05a`, 0 fontes mais novas que o binário -> **sem flash**.
- Fact: fixtures geradas em host, BMP 24-bit `BI_RGB` bottom-up (`biHeight>0`,
  `biPlanes=1`, `compression=0`), cada uma **<= 2048 B** (limite do `fs.write`),
  padrões assimétricos com cores exatas em RGB565:
  - `viewer-nav-a.bmp` **31x20** (620 px), stride **96** (linha 93 + 3 de padding),
    **1974 B**, sha256 `0d2f0bcbb99941d42ba2add02a845a42e0ca6682c5d12ca61c14a5d7b7f49b16`,
    crc32 `1059996948`: fundo azul, "F" branco, cantos vermelho/verde/amarelo/branco.
  - `viewer-nav-b.bmp` **24x27** (648 px), stride 72, **1998 B**, sha256
    `bea2e19ef80aba741c45656039c30590fc7cbfaeea7aa1feb57f8e794a25e2dc`,
    crc32 `3224253072`: fundo amarelo, barra ciano, haste magenta, cantos
    branco/azul/preto/vermelho.
  Verificação local: decodificador independente pela spec (`decode == esperado`,
  650/648 px; `flipv`/`fliph` diferem em ambas) + ImageMagick (`31x20`/`24x27`,
  depth 8, 5/7 cores). Nenhum endpoint novo foi necessário.
- Fact: antes de gravar, `ls /screenshots` (17:18:54) listou **106 BMPs**
  (o maior `print_20261020_172101.bmp`) e **nenhum** nome `00_viewer-nav-*`;
  nada foi apagado. Transferência pelo `fs.write` existente:
  `/sdcard/screenshots/00_viewer-nav-a-20261108-1719.bmp` ->
  `{"ok":true,"size":1974,"crc32":1059996948}` e
  `/sdcard/screenshots/00_viewer-nav-b-20261108-1719.bmp` ->
  `{"ok":true,"size":1998,"crc32":3224253072}` (crc bate com o host). Com os dois
  novos nomes, `/screenshots` passa a **108** imagens e a janela de 64 começa por
  `00_…a`, `00_…b`, `183715.bmp`, seguida dos menores `print_*`.
- Fact: pré-condição autorizada — o viewer de 17:13 ainda estava aberto
  (`image /image-viewer-test-20261108-1712.bmp`); um `ui.escape` (17:18:47)
  devolveu o shell: `app.lifecycle stop begin/registered`, **sem panic**, uptime
  contínuo 00:27:34, psram 29.410.892. Fixture anterior e demais arquivos do SD
  **preservados** (`/sdcard/image-viewer-test-20261108-1712.bmp` continua na raiz).
- Fact: abertura **A** por HTTP com caminho virtual
  `image /screenshots/00_viewer-nav-a-20261108-1719.bmp` (17:19:34/20:19:34 UTC) ->
  `accepted for dispatch: 52 bytes, 1 line(s)`; log
  `app=cyberdeck.image_viewer event=start outcome=running`. Captura
  `img-nav-a.bmp`: bloco de imagem em **x624 y376, 31x20**, **620/620 pixels
  idênticos** ao grid esperado (0 mismatches); título
  `00_viewer-nav-a-20261108-1719.bmp`, indicador **`1/64+`**, rodapé
  `left/right browse  Esc close`.
- Fact: abertura **B** (explícita, mesma sintaxe)
  `image /screenshots/00_viewer-nav-b-20261108-1719.bmp` (17:24:26) ->
  `outcome=running`; captura `img-nav-b.bmp`: bloco em **x628 y373, 24x27**,
  **648/648 pixels idênticos** (0 mismatches, incluindo o marcador preto interno);
  título `00_viewer-nav-b-20261108-1719.bmp`, indicador **`2/64+`** — coerente com
  a posição na janela (A=1, B=2), não com rank global.
- Fact: **`Esc` validado nas duas aberturas** — `ui.escape` -> `{"ok":true}`, app
  `stop begin/registered` (17:23:50 e 17:24:47), captura `img-after-esc.bmp` com o
  terminal visível (703.360 px não-pretos, listagem de `/screenshots`), e em
  seguida `pwd` injetado respondeu `/` (17:24:17, 17:24:49). Isto cobre D1
  (Esc com imagem aberta devolve o shell responsivo).
- Fact: **setas físicas NÃO executadas — limitação de atuação, não de
  implementação**: não existe caminho de injeção de setas (a ponte só expõe
  `ui.escape`/`ui.type`; `/control` injeta apenas texto + Enter; `image_view_active()`
  descarta texto e só `special_key` chega a `local_key`; o viewer não tem
  botões/gestos clicáveis — só `LV_EVENT_SIZE_CHANGED`). Foi aberta uma janela de
  ~2 min com A exibida e tela mantida acordada por injeções inócuas de espaço
  (`poll-1..10`, 17:21:21–17:23:21): todas classificaram **A** (azul/branco,
  620 px), nenhuma mudança de título/índice. Portanto `→` A->B por teclado físico,
  `←` B->A, clamp nos extremos (`1/64+` e `64/64+`, sem wrap) e a série do
  indicador **permanecem pendentes** de alguém pressionar o teclado real com o
  viewer aberto; a lógica de `navigate`/clamp continua provada só no host
  (TEST-IMG-NAV-10..13) e a rota de tecla só por contrato estrutural.
- Fact: **sem panic/reboot** — uptime 00:27:34 -> 00:33:44 no mesmo boot;
  `safe_mode=false`, `interrupted_boots=0`; `psram_free` 29.410.892 ->
  29.403.928 (duas aberturas/fechamentos, ~-7 KB); `/events.txt` (**23.921**
  linhas) com **0** `Guru Meditation|Stack protection|Rebooting|rst:0x|abort()|WDT`;
  `monitor-img6.log` (18.576 B, `idf-monitor --no-reset --eol lf`, PID 236986)
  também com 0 panic; monitor encerrado por `exit` + kill por PID, sem residuais.
- Fact: artefatos locais — `/tmp/opencode/img-nav/{viewer-nav-a.bmp,viewer-nav-a.bmp.json,viewer-nav-b.bmp,viewer-nav-b.bmp.json}`,
  `/tmp/opencode/{img-nav-a.bmp (+title/footer crops), img-nav-b.bmp (+title crop),
  img-after-esc.bmp, poll-1..10.bmp, monitor-img6.log, events-now2.txt, events-now3.txt}`;
  capturas sha256 `b6d694bb…7297fd` (A) e `b8dd88a2…409d1a5` (B).
- Fact: limites observados — `ui.dump` tem teto de nós e, com o terminal cheio
  (106 linhas de `ls`), os labels do viewer ficam fora do dump; título/indicador
  foram validados por recorte do `/screenshot` ampliado. Injeções de espaço
  usadas como keep-alive são descartadas enquanto o viewer está ativo (não
  sujam a linha do shell).
- Fact: nenhuma produção/teste alterada; sem suíte agregada, cobertura ou gates.
  Nada foi apagado do SD; as duas fixtures novas permanecem em
  `/sdcard/screenshots/` e a de 25x26 segue em `/sdcard/`.
- Supersedes or discards: confirma para dois tamanhos/cores distintos o que 17:15
  provou para um fixture (decode, RGB565, orientação, posição e janela) e fecha
  D1; **não** fecha o item de navegação por setas, que segue pendente de atuação
  física e não deve ser declarado validado por estes dados.

### 2026-11-08 17:29 -03:00 - preparação para o usuário pressionar setas: A aberto e estável, monitor deixado aberto

- Fact: estado/saúde conferidos antes: `sys.info` fw **1.15.4**, IDF v6.1,
  uptime **00:36:48** no **mesmo boot** (sem reset desde 00:33:44/00:35:41),
  `safe_mode=false`, `interrupted_boots=0`; `/events.txt` (23.9xx linhas) com
  **0** `Guru Meditation|Stack protection|Rebooting|rst:0x|abort()|WDT` e última
  entrada anterior ao comando de 20:24:49 UTC. `build/cyberdeck5.bin` inalterado
  (0x2a1cc0, sha256 `4ab5b7c2…edb05a`) — **sem flash**.
- Fact: monitor aberto **antes** e **deixado rodando** (não interrompi a CDC):
  `idf-monitor --no-reset --eol lf -p /dev/ttyACM0 build/cyberdeck5.elf`,
  PID **244602** (lstart 17:27:31), stdin no FIFO `/tmp/opencode/mon7.fifo`
  (holder `sleep` PID **244521**), log `/tmp/opencode/monitor-img7.log`
  (vivo, sem panic até o fechamento desta nota).
- Fact: tela acordada por NDJSON no monitor (`{"rid":"p1","type":"screen.wake"}` ->
  `{"ok":true,"woke":true}`; backlight 20%) e **A reaberto** por `/control`:
  `image /screenshots/00_viewer-nav-a-20261108-1719.bmp` ->
  `accepted for dispatch: 52 bytes, 1 line(s)`; log SD
  `20:27:43 I shell: image …` + `app=cyberdeck.image_viewer event=start outcome=running`.
- Fact: baseline capturado: `/tmp/opencode/img-nav-a-prep.bmp` (1280x720x24,
  sha256 `601889cdba833da662b3fbe4000cdacc195bdd05d3fdc9d6015ebc1ae5183bc4`),
  recorte do título `img-nav-a-prep-title.png` com título
  `00_viewer-nav-a-20261108-1719.bmp` e indicador **`1/64+`**; bloco de imagem em
  **x624 y376, 31x20**, **620/620 pixels idênticos** ao grid esperado
  (0 mismatches). `sys.info` pós-abertura: uptime 00:36:48, psram 29.400.684,
  internal_free 112.583, sem panic.
- Fact: **o viewer está aberto em A** e o app permanece em `running`; nenhuma
  `special key` foi injetada nesta etapa e **nada foi fechado**. O monitor segue
  aberto de propósito para capturar eventuais panics enquanto o usuário testa as
  setas.
- Fact: comandos exatos para o usuário (teclado físico do Tab5; nenhum comando de
  shell é necessário, A já está na tela):
  1. Se o painel estiver apagado, a **primeira** tecla apenas religa (é consumida);
     pressione de novo.
  2. `→` uma vez: esperado título `00_viewer-nav-b-20261108-1719.bmp`,
     indicador **`2/64+`** e imagem **24x27** amarelo/ciano/magenta.
  3. `←` uma vez: esperado voltar a `00_viewer-nav-a-20261108-1719.bmp`,
     **`1/64+`**, 31x20 azul/branco.
  4. Extremos (opcional): `←` em A continua `1/64+`; `→` até a última continua
     `64/64+` (sem wrap).
  5. `Esc` fecha e devolve o shell (`/$ `).
- Fact: para inspeção posterior sem tocar na CDC: `GET /control/terminal` e
  `GET /screenshot`; ou NDJSON em `/tmp/opencode/mon7.fifo`
  (`send {"rid":"x","type":"ui.dump"}`). Encerrar o monitor depois:
  `printf 'exit\n' > /tmp/opencode/mon7.fifo` e `kill 244521`.
- Fact: risco/limite registrados — a proteção de tela (timeout ~1 min, persistido)
  apaga o painel após a inatividade; por isso o passo 1. Não alterei `screen
  timeout` (configuração persistida não autorizada) e não enviei Enter/espaços
  keep-alive (seriam `special key`, proibidos neste pedido).
- Supersedes or discards: complementa 17:26 — a navegação por setas continua
  **pendente de atuação física**; esta entrada só prepara e registra o estado
  (não é evidência de navegação).

### 2026-11-08 17:36 -03:00 - fase pré-implementação do `POST /control/upload` (plano `high` reaprovado): cenários TEST-UPLOAD-*, gates focados e plano de dispositivo

- Fact: sessão reiniciada porque a tarefa/plano mudou. O objetivo aprovado pelo
  usuário é um transporte autenticado para **uma fixture conhecida BMP 24-bit
  BI_RGB 1280x720** (`54 + 1280*720*3` = **2.764.854 B**) em
  `/sdcard/screenshots`, com abertura subsequente como `/screenshots/<nome>.bmp` e
  confronto de pixels no Tab5. Esta fase **não** implementa nada: define
  aceitação, borda, falha, regressão e mutações antes do código.
- Fact: nenhum arquivo de produção/teste foi criado ou alterado; nenhum alvo de
  teste, build, flash ou comando de dispositivo foi executado. Estado de leitura:
  HEAD `16392858881d775bb68a6733fffcfa67223ab808`, `version.txt` = **1.15.4**,
  worktree rastreado sujo pelo trabalho do viewer (`git diff | sha256sum` =
  `e3f7d2847d09a09a864561dfd9c80a9c460ad1e8dfb4238d1e18e23d6000a0b6`, 33 entradas
  em `git status --porcelain`). Esse hash identifica o snapshot das fontes lidas;
  **não** é o estado em que os cenários abaixo serão executados.
- Fact: o texto literal de `REQ-UPLOAD-01..05`/`AC-UPLOAD-01..02` não está em
  arquivo do repositório (`grep -rn "REQ-UPLOAD\|AC-UPLOAD"` em `*.md/*.html/*.json`
  = 0 resultados); vive no retorno do planner ao developer. A matriz abaixo foi
  derivada do despacho + `planner.md` 17:29 e registra a premissa **A-U1**. Se o
  plano canônico divergir, esta nota é atualizada por append, sem apagar cenário.

#### Premissas de contrato (A-U*) — o coder deve confirmar ou contestar com evidência

| ID | Premissa derivada | Fonte que a sustenta |
| --- | --- | --- |
| A-U1 | REQ-UPLOAD-01 = rota+autenticação; 02 = streaming limitado; 03 = integridade+publicação; 04 = destino/nome; 05 = CLI/observabilidade/docs. AC-UPLOAD-01 = prova host de segurança/falha; AC-UPLOAD-02 = aceite físico da fixture full-res | despacho; `planner.md` 17:29 |
| A-U2 | Limite de corpo é uma constante de header (`k_max_upload_bytes`), `>= 2.764.854` e `< k_max_file_bytes` (5 MiB do viewer). Testes derivam do header, nunca pinam o literal 3 MiB | `cyberdeck_image_bmp.h:16-17`; plano "≤3 MiB" |
| A-U3 | Upload é **somente FS**: não depende de display, então tela apagada **não** pode devolver 409/`screen_off` (diferente de `/control/type`) | `cyberdeck_control_server.cpp:528-531` (type recusa) vs. ausência de necessidade de UI no upload |
| A-U4 | Mensagem autenticada do upload vincula **nonce + método + URI + tamanho declarado + SHA-256 do corpo**, com separação de domínio em relação à mensagem de 4 campos de `/control/type` | `cyberdeck_control_protocol.cpp:143-152` (4 campos hoje); plano 17:29 |
| A-U5 | Códigos novos: `checksum` → **422**, `invalid_path` → **400**, `conflict` (alvo existe) → **409**, `storage_unavailable` (gate SD fechado/fenced) → **503**; `busy` de upload concorrente → **429**. Todos passam por `error_code()`/`http_status()` | convenção existente `cyberdeck_control_protocol.cpp:26-64` e `status_text()` `:108-122` |
| A-U6 | Nome é só **nome de arquivo** (sem diretório), ASCII imprimível, limite derivado de `cyberdeck_image::k_max_name_bytes`, e todo nome aceito precisa ser listável pelo filtro do viewer (`is_bmp_name`) | `cyberdeck_image_list.cpp:77-84`; `cyberdeck_image_list.h:22` |
| A-U7 | CLI ganha subcomando `upload <arquivo> [--name NOME]`; o limite local é **igual** ao do firmware (teste compara os dois valores parseados, não literais) | `tools/cyberdeck_http_cli.py:27` (padrão `CONTROL_MAX_BYTES`) |
| A-U8 | Publicação nunca sobrescreve: alvo existente falha antes de escrever bytes e o arquivo existente permanece byte-idêntico | `cyberdeck_serial_bridge.cpp:2996-3049` (precedente `fs.write`: temp `O_EXCL` + rename, ESP não promete atomicidade) |

#### Rastreabilidade REQ/AC -> TEST (alvo)

| REQ/AC | Cenários |
| --- | --- |
| REQ-UPLOAD-01 | TEST-UPLOAD-AUTH-01..11, TEST-UPLOAD-ROUTE-01..02 |
| REQ-UPLOAD-02 | TEST-UPLOAD-STREAM-01..10, TEST-UPLOAD-ROUTE-03 |
| REQ-UPLOAD-03 | TEST-UPLOAD-INTEG-01..10 |
| REQ-UPLOAD-04 | TEST-UPLOAD-PATH-01..11 |
| REQ-UPLOAD-05 | TEST-UPLOAD-CLI-01..05, TEST-UPLOAD-ROUTE-04..05 |
| AC-UPLOAD-01 | todos os TEST-UPLOAD-* host acima (protocolo puro + contratos estruturais + CLI) |
| AC-UPLOAD-02 | TEST-UPLOAD-DEV-01..12 (Tab5, fixture 1280x720) |
| Regressão | TEST-UPLOAD-REG-01..07 |
| Sensibilidade de borda | mutações obrigatórias M-UP-01..10 |

#### REQ-UPLOAD-01 / AC-UPLOAD-01 - rota e autenticação

| ID | Caso e critério exato | Detecção |
| --- | --- | --- |
| TEST-UPLOAD-AUTH-01 | `POST /control/upload` registrado na tabela de rotas com o handler de upload; `GET`/`PUT` no mesmo URI → **405** (`"POST required"`) e nenhum nonce consumido | estrutural (tabela) + protocolo puro |
| TEST-UPLOAD-AUTH-02 | peer não-local → **403** `local network only` **antes** de qualquer autenticação/escrita; nonce pendente continua válido | `socket_is_local_peer` é o primeiro gate (ordem no corpo do handler) |
| TEST-UPLOAD-AUTH-03 | nonce ausente/malformado (≠32 hex minúsculo) ou MAC ausente/malformado (≠64 hex) → **401** `unauthorized`; diretório inalterado (0 temporários, 0 arquivos) | `verify` já rejeita por comprimento (`protocol.cpp:164-165`) |
| TEST-UPLOAD-AUTH-04 | MAC inválido com nonce válido → **401** e o nonce **não** é queimado: repetir a mesma requisição com MAC correto e nonce original → **2xx** | semântica existente `verify` (`protocol.cpp:198-200`) |
| TEST-UPLOAD-AUTH-05 | replay do par nonce+MAC de um upload já aceito → **401** `no_challenge`, sem segundo arquivo nem segundo temporário | `consume` em `verify` |
| TEST-UPLOAD-AUTH-06 | nonce expirado (> TTL derivado de `k_nonce_ttl_us`) → **401** `expired`, nada escrito; novo `challenge` depois funciona | `nonce_store::pending` |
| TEST-UPLOAD-AUTH-07 | MAC vincula método, URI, tamanho declarado e digest: alterar **um** desses quatro com o resto constante invalida (**401** em cada variação isolada) | `build_upload_message` de produção + oráculo HMAC-SHA256 independente (já existe em `test_control_protocol.cpp:42-200`) |
| TEST-UPLOAD-AUTH-08 | separação de domínio: MAC calculado com a mensagem de 4 campos de `/control/type` **não** valida no upload (mesmo nonce/URI/tamanho/hash) e vice-versa | mesma primitiva + dois builders |
| TEST-UPLOAD-AUTH-09 | nenhuma escrita/abertura de arquivo antes do `authorize()`: enviar corpo completo com MAC inválido não cria temporário nem alvo (ordem no código + prova no Tab5) | estrutural: índice de `authorize(` < índice de `openat`/`fopen`/`rename` no corpo do handler |
| TEST-UPLOAD-AUTH-10 | chave dedicada (`ctl_auth`/`key`), nunca a chave OTA; upload não loga chave, corpo, MAC nem digest de segredo; `control.key` continua só na ponte USB | varredura de `ESP_LOG` e do handler, como `test_control_http_contract.py:197-220` |
| TEST-UPLOAD-AUTH-11 | upload longo **não** segura `s_mutex`/`s_queue_mutex` durante o corpo: `/control/challenge` e `/control/type` respondem durante um upload em andamento | estrutural (escopo do `lock_guard`) + dispositivo (D9/D11) |

#### REQ-UPLOAD-02 / AC-UPLOAD-01 - streaming limitado

| ID | Caso e critério exato | Detecção |
| --- | --- | --- |
| TEST-UPLOAD-STREAM-01 | tamanho declarado 0 → **400** `empty`; nenhum arquivo/temporário | limite inferior |
| TEST-UPLOAD-STREAM-02 | par de borda derivado de `k_max_upload_bytes`: `== kMax` aceita e publica com digest correto; `== kMax+1` → **413** `too_large`, nenhum byte escrito, `Connection: close` (não deixa o corpo no socket) | teste do par (obrigatório) |
| TEST-UPLOAD-STREAM-03 | `Content-Length` ≠ tamanho declarado no MAC → **400** e nada escrito (nos dois sentidos: declarado menor e declarado maior) | comparação explícita |
| TEST-UPLOAD-STREAM-04 | corpo truncado (peer fecha após N < content_len) → erro tipado, temporário removido, nada publicado; próximo upload funciona | state machine pura com sink injetado |
| TEST-UPLOAD-STREAM-05 | `k_max_recv_timeouts` timeouts consecutivos → aborta em tempo limitado; **um** timeout seguido de bytes não aborta (contador reinicia) | contador derivado do header do servidor |
| TEST-UPLOAD-STREAM-06 | memória limitada: laço de `httpd_req_recv` com buffer de tamanho constante de compilação (≤ 64 KiB) e **nenhuma** alocação proporcional ao tamanho declarado (proibido `malloc(req->content_len)`, `new char[content_len]`, `std::vector::resize(content_len)`); teste da state machine registra o pico de alocação e ele é independente do tamanho (1 KiB vs kMax) | estrutural + alocador contador no teste puro |
| TEST-UPLOAD-STREAM-07 | bytes gravados == tamanho declarado exatamente (sem padding no fim do arquivo); arquivo publicado tem tamanho `== size` | sink injetado + `stat` no dispositivo |
| TEST-UPLOAD-STREAM-08 | segundo upload concorrente durante um ativo → **429** `busy`, sem segundo temporário; depois do primeiro concluir, novo upload **2xx** | single-flight explícito |
| TEST-UPLOAD-STREAM-09 | tela apagada (`screen_off` ativo) → upload **2xx** (A-U3); reacender e abrir a imagem | estrutural (sem `screen_off_is_active` no caminho) + D10 |
| TEST-UPLOAD-STREAM-10 | gate SD fechado/`global_fenced()` → **503** `storage_unavailable` sem criar nada e sem espera ilimitada (`acquire` é não-bloqueante) | `cyberdeck_sd_io_coordinator.h:43-47,68-70` |

#### REQ-UPLOAD-03 / AC-UPLOAD-01 - integridade e publicação

| ID | Caso e critério exato | Detecção |
| --- | --- | --- |
| TEST-UPLOAD-INTEG-01 | caminho feliz: digest confere → publicação; resposta JSON determinística com nome, `bytes == size`, `sha256` (64 hex minúsculo) e caminho de viewer `/screenshots/<nome>.bmp` | state machine + CLI |
| TEST-UPLOAD-INTEG-02 | digest divergente → **422** `checksum` (A-U5), temporário removido, nenhum alvo criado, existente intocado | sink injetado |
| TEST-UPLOAD-INTEG-03 | digest incremental independe da segmentação: alimentar 1, 7, 4096 e resto produz o mesmo `sha256` e os mesmos bytes publicados (comparar com digest one-shot do oráculo) | tabela de segmentações, incluindo fronteiras |
| TEST-UPLOAD-INTEG-04 | falha de `fsync` → sem publicação, temporário removido, erro tipado de I/O (**5xx**) | ops injetadas |
| TEST-UPLOAD-INTEG-05 | falha de `close` → mesmo contrato do INTEG-04 (não publica depois de close falho) | ops injetadas |
| TEST-UPLOAD-INTEG-06 | publicação falha com alvo existente (`EEXIST`) → **409** `conflict`, temporário removido, alvo byte-idêntico (comparar conteúdo e CRC) | ops injetadas + D6 |
| TEST-UPLOAD-INTEG-07 | falha de rename/publicação e falha de fsync registram `record_global_persistence_failure()` (política do projeto para perda de persistência) | estrutural + `test_sd_io_coordinator` |
| TEST-UPLOAD-INTEG-08 | ownership do temporário: nome único via `O_EXCL` com tentativas limitadas; em falha só o temporário **próprio** é removido — arquivo pré-existente com o nome candidato contendo `KEEP` permanece (regressão espelhando `fs.write`) | host com diretório temporário |
| TEST-UPLOAD-INTEG-09 | lease WRITE do SD mantido por toda a transação e liberado no fim: `active_leases()` volta ao baseline e `begin_close()`+`drain()` não fica preso atrás de upload ocioso | `cyberdeck_sd_io::coordinator` real no host |
| TEST-UPLOAD-INTEG-10 | varredura de caminhos de erro (auth, tamanho, truncado, timeout, digest, fsync, close, rename, alvo existente): em **nenhum** sobra temporário e em nenhum o diretório ganha entrada final | tabela de casos da state machine |

#### REQ-UPLOAD-04 / AC-UPLOAD-01 - destino e nome

| ID | Caso e critério exato | Detecção |
| --- | --- | --- |
| TEST-UPLOAD-PATH-01 | nome válido (ex. `viewer-upload-20261108-1736.bmp`) → publicado em `/sdcard/screenshots/<nome>` (físico) e visível como `/screenshots/<nome>.bmp` | constante de diretório única (sem header de diretório) |
| TEST-UPLOAD-PATH-02 | nome com `/` ou `\` → **400** `invalid_path`; nada criado | validador puro |
| TEST-UPLOAD-PATH-03 | `..`, `.`, `..bmp`, `.oculto.bmp`, `a../b.bmp`, `x/../y.bmp` → **400** | validador puro |
| TEST-UPLOAD-PATH-04 | nome vazio/ausente → **400** | validador puro |
| TEST-UPLOAD-PATH-05 | extensão: `.bmp`/`.BMP` aceitos (viewer lista case-insensitive, `image_list.cpp:77-84`); `.png`, `.bmp.exe`, sem extensão → **400**; propriedade: **todo** nome aceito passa no filtro do viewer e todo nome do filtro é aceito pelo validador (corpus + boundary) | `k_max_name_bytes`/`is_bmp_name` de produção |
| TEST-UPLOAD-PATH-06 | comprimento: nome com exatamente `k_max_name_bytes` aceito; `+1` → **400** (par de borda derivado, sem literal) | header do viewer |
| TEST-UPLOAD-PATH-07 | caracteres de controle, NUL, `CR`, `LF`, espaço, não-ASCII (`é`, emoji) → **400** (evita injeção de header e glifo fora da fonte) | validador puro |
| TEST-UPLOAD-PATH-08 | alvo existente → **409** sem sobrescrita (também INTEG-06) | validador + dispositivo |
| TEST-UPLOAD-PATH-09 | alvo é diretório → **409/400**, nada criado, diretório preservado | `S_ISDIR` no host |
| TEST-UPLOAD-PATH-10 | symlink no alvo → rejeitado no host (`O_NOFOLLOW`/`lstat`); no ESP/FATFS não há symlink (limitação registrada, coberta só no host) | host |
| TEST-UPLOAD-PATH-11 | confinamento: nenhum header/campo permite diretório alternativo; após upload o viewer encontra `/screenshots/<nome>.bmp` mesmo com >64 arquivos no diretório (pin do plano B) | constante de diretório + D4 |

#### REQ-UPLOAD-05 / AC-UPLOAD-01 - CLI, observabilidade e documentação

| ID | Caso e critério exato | Detecção |
| --- | --- | --- |
| TEST-UPLOAD-CLI-01 | `upload` recusa **antes** da rede: arquivo inexistente, arquivo > limite (limite da CLI derivado e **igual** ao do firmware, comparados por parse), nome padrão com extensão inválida; nenhuma conexão aberta | `unittest.mock` sem rede (padrão `test_control_cli.py:62-70`) |
| TEST-UPLOAD-CLI-02 | `sha256` calculado sobre os bytes exatos do arquivo; `size` = tamanho do arquivo; MAC sobre (nonce, POST, `/control/upload`, size, digest); `Content-Length` == size; um byte trocado muda o MAC | captura de `request()` + oráculo independente |
| TEST-UPLOAD-CLI-03 | resposta 2xx: imprime/retorna nome, `bytes`, `sha256`, caminho; não-2xx: sai com código ≠ 0 e mostra o `error` JSON (sem stack trace crua) | `unittest` com `request()` fake |
| TEST-UPLOAD-CLI-04 | não imprime a chave em stdout/stderr/log; regras do arquivo de chave (0600, sem overwrite, hex minúsculo) inalteradas | casos existentes + novo caso com `redirect_stdout` |
| TEST-UPLOAD-CLI-05 | documentação coerente: README(s), `docs/ARCHITECTURE*.md`, `code-map.md` (matriz `REQ/AC -> TEST-UPLOAD`), roteiro manual e `help`/docs do canal; limites derivados da fonte, sem cópia divergente; `version.txt` só formato/piso | contrato estrutural + varredura de literais |

#### AC-UPLOAD-01 - wiring/gates estruturais

| ID | Caso e critério exato |
| --- | --- |
| TEST-UPLOAD-ROUTE-01 | `config.max_uri_handlers` **igual** ao número de entradas da tabela `uris[]` (derivado por contagem; hoje 7/7, deve virar N/N com a nova rota); rollback fail-closed (`httpd_stop` + `return ESP_FAIL`) preservado |
| TEST-UPLOAD-ROUTE-02 | ordem no handler: peer → `ready` → limite de tamanho → `authorize` → abertura do temporário → laço de leitura → digest → `fsync`/`close` → publicação; nenhuma mutação de FS nem buffer do corpo antes da autenticação |
| TEST-UPLOAD-ROUTE-03 | buffer de chunk é constante de compilação ≤ 64 KiB; nenhum array grande na stack (stack HTTP = 6144, `screenshot_server.cpp:216`); nenhuma alocação proporcional ao corpo |
| TEST-UPLOAD-ROUTE-04 | wiring/cobertura: novo(s) TU puro(s) em `components/cyberdeck/CMakeLists.txt` **e** na lista obrigatória de `tools/coverage_scope_guard.py` (não na allowlist); adapter do servidor tratado como o atual; alvos novos em `BINS`, `.PHONY`, agregado `test` e `clean` (nesta fase apenas prescrito) |
| TEST-UPLOAD-ROUTE-05 | caminho de upload sem LVGL/`bsp_display_lock`/display port; sem segredo em log; sem reuso da chave OTA |

#### Regressões obrigatórias (focadas, não suíte completa)

| ID | Caso e critério exato | Alvo |
| --- | --- | --- |
| TEST-UPLOAD-REG-01 | `/control/type` continua com 1024 B e 32 linhas: 1025 B → **413**, corpo vazio → **400** `empty`, fila 4 → **429**, 202 com `bytes`/`lines`; o limite do upload não vaza para o type | `test_control_http_contract`, `test_control_protocol`, `test_control_cli` |
| TEST-UPLOAD-REG-02 | `/control/challenge`: um nonce pendente por vez, TTL 60 s, `esp_fill_random`; challenge continua respondendo durante upload | idem |
| TEST-UPLOAD-REG-03 | `/control/terminal`: 2048 B, `screen_off` → **409**, JSON de terminal limitado/truncado | idem |
| TEST-UPLOAD-REG-04 | `fs.write` serial intacto: 2048 B, base64 canônico, `size` == decodificado, sem sobrescrita, temp+rename, resposta com `size`/`crc32` | `test_fs_write_dispatch`, `test_fs_write_cli`, `test_fs_write_contract` |
| TEST-UPLOAD-REG-05 | semântica do `cyberdeck_sd_io::coordinator` intacta (gate, drain, reopen, fence) | `test_sd_io_coordinator` |
| TEST-UPLOAD-REG-06 | viewer intacto: 5 MiB e 1280 px inclusivos, janela de 64 + truncamento, pin do arquivo pedido, fit sem ampliação | `test_image_bmp`, `test_image_fit`, `test_image_list`, `test_image_viewer_contract` |
| TEST-UPLOAD-REG-07 | servidor LAN: rotas existentes (`/screenshot`, `/events.txt`, `/battery`, `/usb/audio`) e rollback preservados; descrição/versão do serviço coerentes | `test_control_http_contract` + build |

#### Gates focados decididos para risco `high`

Sem `make test` agregado e sem cobertura global nesta fase (isso é do reviewer final). Rodar,
nesta ordem, e anexar comando+resultado:

1. `make -C tests/host/keymap test_control_upload_protocol` (novo, puro: auth/domínio, validador de nome, digest incremental, state machine de publicação com ops injetadas, ownership/cleanup, lease)
2. `make -C tests/host/keymap test_control_upload_contract` (novo, estrutural: rota/budget derivado, ordem, buffer, wiring, CLI/docs)
3. `make -C tests/host/keymap test_control_cli`
4. `make -C tests/host/keymap test_control_http_contract`
5. `make -C tests/host/keymap test_control_protocol`
6. `make -C tests/host/keymap test_fs_write_dispatch test_fs_write_cli test_fs_write_contract`
7. `make -C tests/host/keymap test_sd_io_coordinator`
8. `make -C tests/host/keymap test_image_bmp test_image_fit test_image_list test_image_viewer_contract`
9. `idf.py build` (evidência do coder; reviewer repete no estado final)

`test_incremental_final_contract` exige `make ... test-build` antes de foco e fica no fechamento
(proibido pinar contagem de BINS; derivar do Makefile).

#### Plano de dispositivo (AC-UPLOAD-02) - Tab5, fixture 1280x720

Fixture determinística (gerada no PC, **fora** do repo; se reutilizável, mover para `tools/`):
BMP 24-bit `BI_RGB`, `biHeight = +720` (bottom-up, coerente com o contrato do projeto),
`54 + 1280*720*3` = **2.764.854 B**, stride 3840 (já alinhado), **4x4 blocos de 320x180**
com 16 cores saturadas distintas e **assimétricas** (topo-esq ≠ topo-dir ≠ base-esq ≠ base-dir)
para pegar flip vertical/horizontal; nenhum pixel acinzentado (todo pixel com
`max(R,G,B) - min(R,G,B) >= 64`) para permitir detecção de bbox por saturação. Registrar
`sha256` local.

| ID | Ação | Critério exato |
| --- | --- | --- |
| TEST-UPLOAD-DEV-01 | pré: `sys.info` + `cyberdeck_http_cli.py status` (challenge alcançável; **não** existe rota `/control/status`) e monitor `--no-reset` aberto; contar padrões `Guru Meditation\|Stack protection\|abort\(\)\|rst:0x\|WDT` em `/events.txt` | baseline registrado; `safe_mode=false`, `interrupted_boots` e uptime anotados; build sha256 = binário gravado |
| TEST-UPLOAD-DEV-02 | `python3 tools/cyberdeck_http_cli.py --host <ip> upload fixture.bmp` | 2xx com `name`, `bytes == 2.764.854`, `sha256` **igual** ao local, caminho `/screenshots/<nome>.bmp` |
| TEST-UPLOAD-DEV-03 | `ui.type "ls -a /screenshots"` + `term.dump` | nome presente; **nenhum** `.tmp`/`.fswrite.`/`upload` temporário restante |
| TEST-UPLOAD-DEV-04 | `image /screenshots/<nome>.bmp` (pin do plano B) | título = nome, indicador `i/N` coerente; uptime monotônico; sem panic/WDT no monitor |
| TEST-UPLOAD-DEV-05 | `GET /screenshot` → BMP 1280x720 | bbox dos pixels saturados = retângulo visível da imagem; propriedades (derivadas de `fit_to_area`): `zoom <= 256` (nunca amplia), `abs(fitted_w*720 - fitted_h*1280) <= 1280` (proporção), retângulo **contido** na área útil (sem sobrepor header de 42 px nem bandas de título/rodapé), margens esquerda/direita e topo/base com diferença **≤ 1 px** (centragem inteira) |
| TEST-UPLOAD-DEV-06 | mesma captura do DEV-05 | 16 amostras (centro de cada bloco e ±40 px em coords da imagem, mapeadas por `rect_x + round((bx+0.5)*fitted_w/1280)`): cor igual à do fixture com tolerância **≤ 8** por canal; 4 cantos distinguem flip vertical/horizontal |
| TEST-UPLOAD-DEV-07 | bordas do retângulo visível | sem moldura/sombra do tema sobre o bitmap: 2 px **saturados** fora do retângulo = 0 (nenhum artefato colorido); laterais e topo com 2 px `#000000` quando houver área preta disponível; abaixo do retângulo, o próximo pixel não é saturado (rodapé é texto cinza, não moldura) |
| TEST-UPLOAD-DEV-08 | negativos sem perda de dados: MAC inválido, replay, nonce expirado, nome com `../`, nome `.png`, nome com 200 hex de lixo | 401/401/401/400/400 e diretório inalterado (`ls -a` e hashes dos existentes idênticos) |
| TEST-UPLOAD-DEV-09 | sobrescrita: criar marcador `/screenshots/upload-nooverwrite-test.bmp` (via `fs.write`, conteúdo conhecido) e tentar upload com esse nome | **409**; `cat`/`CRC` do marcador idêntico ao criado; depois `rm /screenshots/upload-nooverwrite-test.bmp` |
| TEST-UPLOAD-DEV-10 | corpo truncado: socket `Content-Length` completo, enviar metade e fechar | aborta; nenhum temporário/alvo; `ui.type "help"` volta a funcionar no mesmo boot |
| TEST-UPLOAD-DEV-11 | timeout: enviar 1 KiB e parar além da janela de recv | aborta em tempo limitado (≈ `k_max_recv_timeouts` × `recv_wait_timeout`), temporário removido, sem reboot |
| TEST-UPLOAD-DEV-12 | concorrência e memória: dois uploads simultâneos; depois 3 uploads sequenciais com `sys.info` entre eles | um **429** e um 2xx com hash correto; `internal_free`/`psram` retornam ao baseline (delta ≤ 64 KiB), sem queda monotônica em 3 ciclos; uptime contínuo; 0 novos padrões de panic |

Ordem/segurança: nada destrutivo; DEV-09 usa marcador próprio e o remove; não apagar
`/sdcard/screenshots/*` existentes nem as fixtures do viewer. Se houver reboot/panic, parar
imediatamente e não repetir a interação.

#### Mutações obrigatórias (prova de sensibilidade de borda)

| ID | Mutação | Teste que deve falhar |
| --- | --- | --- |
| M-UP-01 | trocar `size <= kMax` por `size < kMax` (e o par `kMax`/`kMax+1` por dois valores do mesmo intervalo) | STREAM-02 |
| M-UP-02 | inverter a comparação `Content-Length != size` | STREAM-03 |
| M-UP-03 | remover a comparação de digest (ou comparar só o tamanho) | INTEG-02 |
| M-UP-04 | mover o `authorize()` para depois da primeira escrita | AUTH-09 |
| M-UP-05 | publicar antes de `fsync`/`close` ou ignorar erro de rename | INTEG-04/05/06 |
| M-UP-06 | ignorar alvo existente (permitir clobber) | PATH-08, INTEG-06, DEV-09 |
| M-UP-07 | remover arquivo pré-existente sem ownership | INTEG-08 |
| M-UP-08 | não consumir o nonce após verificação válida | AUTH-05 |
| M-UP-09 | omitir tamanho (ou URI) da mensagem autenticada | AUTH-07/08 |
| M-UP-10 | adicionar rota sem ajustar `max_uri_handlers` (ou elevar o budget sem rota) | ROUTE-01 |

Mutação sobrevivente = caso que não distingue bordas → ajustar asserção ou registrar em riscos
(proibido remover cenário/enfraquecer asserção).

#### Riscos e lacunas para o coder

1. **Dependência mandatória:** `max_uri_handlers` está pinado como literal em
   `test_control_http_contract.py:82`; elevar para 8 é permitido, mas o teste deve passar a
   **derivar** da tabela de rotas (ROUTE-01). Editar o número sem derivar é enfraquecimento.
2. **Mutex durante o corpo:** `authorize()` segura `s_mutex` (`control_server.cpp:199-203`);
   mantê-lo durante 2,7 MB bloqueia challenge/type e trava a task HTTP. Autentique sob lock,
   libere, depois faça o I/O sob lease de SD e mutex próprio de upload.
3. **Stack HTTP de 6144** (`screenshot_server.cpp:216`): buffer de corpo na stack estoura.
4. **`lru_purge_enable = true`** com `max_open_sockets` default: conexões novas podem purgar
   o socket do upload no meio; trate morte de socket como truncado (limpeza) e prefira 429
   explícito a deixar corrida silenciosa.
5. **FATFS não promete rename atômico:** siga o precedente do `fs.write` (temp `O_EXCL` +
   rename, sem clobber); `fsync` no VFS pode ser no-op — chame e trate erro como falha.
6. **Nome:** derive o limite de `k_max_name_bytes` (128 do viewer); um limite de 255 do
   `fs.write` produziria arquivo aceito e não listável.
7. **A-U3 (tela apagada):** não copie o gate `screen_off_is_active()` do `type_handler`.
8. **Códigos de erro:** `checksum`/`invalid_path`/`conflict`/`storage_unavailable` precisam
   entrar em `error_code()`/`http_status()`/`status_text()`; sem isso a resposta cai em 500.
9. **Versões:** `version.txt` só formato/piso nos testes; incremento MINOR/PATCH e versão do
   serviço `cyberdeck.screenshot` (hoje 1.1.0) seguem `AGENTS.md`, não literal de teste.
10. **Documentação:** varrer limites antigos/documentação e manter a matriz do `code-map.md`;
    se itens de `docs/OS-IMPLEMENTATION-PLAN.pt-BR.md` avançarem, atualizar o dashboard.

#### Limitações desta fase

Nenhum teste/build/mutação/flash/comando de dispositivo executado; **nada** validado por
execução. Os cenários são prescrição: sensibilidade só se prova com o teste rodando e a
mutação correspondente falhando. `A-U1..A-U8` são premissas derivadas (o texto canônico de
`REQ-UPLOAD-*` não está no repositório); se o plano divergir, atualizar esta nota por append.
Classificação futura de falha: **defeito de teste** (asserção/oráculo), **implementação
ausente** (cenário sem código correspondente) ou **implementação incorreta** (comportamento
difere do contrato). As evidências físicas do viewer (17:15/17:26) continuam válidas e não
são substituídas por esta entrada.

- Supersedes or discards: none para o escopo de upload (é escopo novo). Mantém integralmente
  os cenários `TEST-IMG-*`, a decisão B (janela/pin de 64) e a pendência de validação física
  das setas registrada em 17:29.

### 2026-11-08 18:26 -03:00 - validação independente pós-fix AUTH-12: matriz focada verde, M-UP-01..14 mortas (14/14)

- Fact: snapshot validado — HEAD `16392858881d775bb68a6733fffcfa67223ab808`;
  `git diff | sha256sum` = `c2ccb016831f3ef40e419b870533bc03304fee83ca22743cf9450068f648504a`
  (mesmo do coder 18:20 e inalterado antes/depois deste ciclo, porque minhas duas
  edições são em arquivos **não rastreados**); composto (diff + untracked, sem
  `docs/agent-state`) = `3afd902de730e81ecb621a3f98e9ae46757f9d23419018a8968d16bd55b1b680`;
  48 entradas em `git status --porcelain`; `version.txt` = 1.16.0. Artefato de firmware
  presente é o do coder: `build/cyberdeck5.bin` **2.766.448 B (0x2a3670)**, sha256
  `f593ab61e5ba323bf828774f061f5f28656b75659f8aa1b89df45553fa63c81c` — eu **não**
  executei `idf.py build`; o SHA confere com o relato do coder, a identidade é dele.
- Fact: produção do fix AUTH-12 conferida na fonte (leitura, não relato): a mensagem
  assinada tem **sete campos** e seis separadores —
  `build_upload_message(nonce, method, uri, name, declared_size, digest, out, cap)` com
  `"%s\n%s\n%s\n%s\n%s\n%llu\n%s"`; `k_upload_message_bytes` é **derivado** dos campos
  (`k_upload_name_capacity`/`k_upload_size_digits`, sem literal 192);
  `verify_upload` encaminha `name` ao builder; o servidor chama
  `authorize_upload(req, name, declared, digest_text)` (`cyberdeck_control_server.cpp:790`),
  `verify_upload(..., name, ...)` (`:287`) e `receive_upload(..., name, declared, ...)`;
  o CLI assina `domain\nnonce\nPOST\nuri\nname\nsize\ndigest` em
  `upload_signed_headers(key, nonce, name, size, digest_hex)`, chamado com `target`.
  Ou seja: nome validado, autenticado e publicado são o mesmo objeto.
- Fact: **matriz focada reexecutada** nesta árvore (nada de suíte agregada, cobertura
  global, `idf.py build`, flash ou device), tudo verde:
  `test_control_upload_contract` PASS; `test_control_upload_protocol` **196 checks/0
  falhas**; `test_control_protocol` 123; `test_control_cli` `Ran 10 tests … OK`;
  `test_control_http_contract` PASS; `test_battery_http_contract` PASS;
  `test_coverage_scope_guard` `coverage scope guard tests passed`;
  `test_fs_write_dispatch` 118, `test_fs_write_cli`, `test_fs_write_contract`,
  `test_sd_io_coordinator` PASS; `test_shell_help` 249, `test_shell_help_contract` PASS
  (8 mutações), `test_help_unification` 413, `test_system_apps_contract` PASS,
  `test_app_runtime` 226; imagem: `test_image_bmp` 82, `test_image_fit` 222,
  `test_image_decode` 50, `test_image_list` 1028, `test_image_worker_state` 788,
  `test_image_view` 70 (ASan+UBSan), `test_image_viewer_contract` 0 falhas,
  `test_image_stack_frame_contract` 0 falhas **sem SKIP** (host chain 160 B,
  budget 6144 B; ELF fail 352/publish 208/run_job 816/start 224/stop 352/
  worker_entry 448).
- Fact: último ciclo curto, exatamente os dois alvos afetados pelas minhas edições:
  `make -C tests/host/keymap test_control_upload_protocol test_control_upload_contract`
  → recompilou só `test_control_upload/test.o` (produção já vinha de build `-B` sobre
  as fontes atuais) e imprimiu `PASS: control upload protocol (196 checks)` +
  `PASS: authenticated upload contract`. `git diff --check` rc=0.
- Fact: **harness descartado por não ser confiável** — `/tmp/opencode/up-mut` é espelho
  de 18:13 com o fix **simulado** (assinatura com `name` como último parâmetro),
  incompatível com a produção real (`name` antes de `declared_size`); os âncoras de
  `mutate.py` não batem. Usei espelhos frescos: `/tmp/opencode/up-real/repo` (árvore
  real de 18:2x, rsync sem `.git/build/managed_components`) e `/tmp/opencode/up-real2/repo`
  (árvore já com as minhas edições). Em ambos: `pristine/` com sha256 conferido antes e
  depois de cada mutação, `shutil.copy2` para restaurar e `mirror restored: True`.
- Fact: mutações executadas sobre o **código real do fix**, cada uma com rebuild forçado
  (`make -B`) para não medir objeto velho — **14/14 mortas**:
  M-UP-01 (cap `>`→`>=`) → `FAIL: the declared cap is accepted`;
  M-UP-02 (`Content-Length != size` invertido) → `FAIL: the declared size must match
  Content-Length`; M-UP-03 (digest ignorado) → `FAIL: a digest mismatch aborts the
  upload`; M-UP-04 (`authorize` movido para depois de abrir o temporário) →
  `FAIL: authentication must precede opening the temporary`; M-UP-05 (resultado de
  `publish` ignorado) → `FAIL: a publish race reports conflict`; M-UP-06 (`link`+`EEXIST`
  → `rename`, clobber) → 3 FAIL no alvo concorrente (“publication refuses a target that
  appeared after the pre-check”); M-UP-07 (apagar candidato pré-existente sem ownership)
  → `FAIL: a pre-existing candidate is never removed`; M-UP-08 (não consumir o nonce em
  `verify_upload`) → `FAIL: an accepted upload consumes the challenge` +
  `FAIL: upload replay is rejected`; M-UP-09 (tamanho assinado +1) → `FAIL: upload
  message layout is pinned with the basename before size and digest`; M-UP-10
  (`max_uri_handlers` 8→7) → `FAIL: max_uri_handlers must equal the number of registered
  routes`; M-UP-11 (mensagem sem o basename) → 3 FAIL (layout + “binds the target
  basename” + “rejects a basename that was not authenticated”); M-UP-12 (CLI sem
  basename) → `FAIL: test_upload_mac_binds_basename` +
  `FAIL: test_upload_signs_domain_size_and_digest`; M-UP-13 (`authorize_upload` com
  outro nome) → `FAIL: the handler must authorize the same validated basename it
  publishes`; **M-UP-14** (`receive_upload(..., "other.bmp", ...)` com `name`
  autenticado) → `FAIL: TEST-UPLOAD-AUTH-12: the handler must publish the authenticated
  basename`, em espelho fresco `up-real2` com baseline verde e restauração verificada.
- Fact: **dois defeitos do meu próprio harness**, corrigidos e reexecutados (nenhum era
  lacuna de cobertura): (a) o primeiro M-UP-08 casou a âncora em `verify()` (canal de
  texto, anterior no arquivo), virando mutante equivalente ao alvo errado (“SOBREVIVEU”
  falso); reaplicado **depois** do marcador `error verify_upload(`; (b) o primeiro
  M-UP-09 zerava o parâmetro e morria por `-Werror=unused-parameter` (garantia mais
  fraca que asserção); refeito com `declared_size + 1`, morto por asserção. O M-UP-12
  inicial falhou por âncora numa linha única (o CLI real usa f-string concatenada) e foi
  corrigido.
- Fact: **lacuna material encontrada e fechada só no teste** — M-UP-14 sobreviveu ao
  estado recebido do coder: o contrato provava que `authorize_upload` recebe o basename
  validado, mas **não** que `receive_upload` (quem publica) recebe o mesmo buffer; um
  handler que autenticasse `name` e publicasse outro literal passaria. Acrescentei em
  `tests/host/keymap/test_control_upload_contract.py` a asserção
  `receive_upload([^;]*,\s*name\s*,` (“the handler must publish the authenticated
  basename”), após a asserção existente de `authorize_upload(req, name, …)`. Sem remover
  nem enfraquecer nada; a produção do coder **já** satisfazia a propriedade.
- Fact: segunda edição de teste (qualidade/derivação) em
  `tests/host/keymap/test_control_upload_protocol.cpp`: o check “the derived capacity
  covers every documented field” repetia literais (`128`, `20`, `4`, `15`) e pinava
  `longest_name` em `124`. Agora deriva de `k_upload_name_capacity`,
  `k_upload_size_digits`, `std::strlen(k_upload_method)`/`(k_upload_uri)` e monta o nome
  mais longo como `k_upload_name_capacity - 4` + `".bmp"`; a expectativa exata é a mesma
  (`196 checks`, layout pinado intacto). Isto evita que um aumento futuro da capacidade
  deixe o check passando por omissão.
- Fact: **nenhuma edição de produção neste ciclo**. Prova: as seis fontes do fix têm
  hashes idênticos aos de antes das minhas edições (capturados no início do ciclo) —
  `cyberdeck_control_upload.cpp` `6f0fbc4a…72b0dc`, `cyberdeck_control_protocol.cpp`
  `b7da67dc…37cbf`, `cyberdeck_control_server.cpp` `8c26b740…69dafa5`,
  `screenshot_server.cpp` `d5662d57…e499e4`, `tools/cyberdeck_http_cli.py`
  `7d790342…ea28011`, `cyberdeck_control_protocol.h` `15dbc5e7…15c4ce` —, o diff
  rastreado não mudou (`c2ccb016…`) e, comparando com o espelho tirado **antes** das
  edições, só **2** arquivos mudaram em `tests/host/keymap/`:
  `test_control_upload_contract.py` (`93d070ed…7a2710`) e
  `test_control_upload_protocol.cpp` (`d80e9bcf…8fe7336`).
- Fact: documentação de rastreabilidade **já estava correta** (nada editado por mim):
  `code-map.md:669` descreve REQ-UPLOAD-01 como “rota + autenticação com chave dedicada,
  nonce, domínio e **basename autenticado**” e `:610-611` lista a mensagem de sete campos
  com o basename; `tests/manual/http-control-validation.pt-BR.md` §6 tem o caso **U6b**
  (`X-CTL-Name` trocado com MAC válido → `401`, nada escrito, MAC original continua
  válido) e §8 (linhas 127-130) declara o basename entre os metadados autenticados e
  validados localmente. Varredura de literais antigos (192, `max_uri_handlers = 7`,
  “quatro/cinco campos” para upload) sem ocorrências.
- Fact: limites e riscos — (a) no servidor (`cyberdeck_control_server.cpp`) a prova de
  “mesmo nome” é **estrutural** (a TU é ESP-IDF-only); M-UP-13/M-UP-14 morrem por essa
  asserção, não por execução do handler; (b) a ligação nome→MAC tem prova
  **comportamental** nos alvos puros e no CLI (`test_upload_mac_binds_basename` varia só
  o basename e exige MACs distintos); (c) a sensibilidade de M-UP-09 vem do pin de layout
  — os casos de MAC usam o próprio builder de produção como oráculo simétrico, então sem
  o pin um “tamanho deslocado para os dois lados” passaria; (d) `test_control_upload_protocol`
  e `test_control_cli` cobrem `verify()`/`verify_upload()` consumindo nonce após MAC
  válido; (e) nada foi executado em hardware.
- Fact: **pendente de dispositivo (AC-UPLOAD-02)**: `TEST-UPLOAD-DEV-01..12` seguem
  pendentes — exigem autorização específica, build/hash do binário e monitor serial;
  incluem o U6b físico (`X-CTL-Name` adulterado) e a abertura da fixture 1280x720.
  Nenhum flash, reboot, comando de device ou escrita no SD foi feito neste ciclo.
  A suíte agregada, o gcovr/scope guard real, `test_incremental_final_contract` e o
  `idf.py build` continuam fora do meu escopo (fechamento do reviewer).
- Supersedes or discards: encerra o `resultado: bloqueado` do ciclo imediatamente
  anterior (as duas edições de teste estão agora executadas e verdes, M-UP-14 morta com
  evidência, `git diff --check` rc=0) e fecha a lacuna AUTH-12 “nome autenticado ≠ nome
  publicado”. Não reabre nada de IMG; `manual_validation` do viewer e as setas seguem
  como estavam (17:29).

### 2026-11-08 18:33 -03:00 - TEST-UPLOAD-STREAM-11 (finding do reviewer 18:28): deadline absoluto por request — especificação apenas

- Fact: sessão **nova** (reset pedido no despacho após o finding 18:28 do reviewer). Escopo
  desta entrada: **somente especificar** `TEST-UPLOAD-STREAM-11` para duração absoluta do
  upload. **Nenhum** arquivo de produção/teste foi criado ou alterado; **nenhum** `make`,
  `idf.py build`, flash, mutação em cópia ou comando de dispositivo foi executado. Esta
  nota é o único artefato desta entrada.
- Fact: snapshot desta especificação — HEAD `16392858881d775bb68a6733fffcfa67223ab808`;
  `git diff | sha256sum` = `c2ccb016831f3ef40e419b870533bc03304fee83ca22743cf9450068f648504a`
  (idêntico ao ciclo 18:26: as fontes **não** mudaram depois do finding); `version.txt` =
  1.16.0; 48 entradas em `git status --porcelain`. Hashes das fontes lidas (confirma que é
  o mesmo código do finding):
  `cyberdeck_control_server.cpp` `8c26b740…afafa5`,
  `cyberdeck_control_upload.cpp` `6f0fbc4a…72b0dc`,
  `cyberdeck_control_upload.h` `14e64e13…bf7c56`,
  `test_control_upload_protocol.cpp` `d80e9bcf…8fe7336`,
  `test_control_upload_contract.py` `93d070ed…7a2710`,
  `tools/cyberdeck_http_cli.py` `7d790342…ea28011`. Snapshot composto com receita explícita
  e reproduzível (`{ git diff | sha256sum; git ls-files -mo --exclude-standard | grep -v
  '^docs/agent-state/' | sort | xargs sha256sum; } | sha256sum`) =
  `1e87a19ae53f5caf109fab152a781cb2821a955f97376b961bc4a46384b10f0d`. O composto de 18:26
  (`3afd902d…`) usa receita diferente; o que compara é o diff (`c2ccb016…`) e os hashes
  por arquivo acima, ambos iguais. `build/cyberdeck5.bin` (`0x2a3670`, sha
  `f593ab61…`) é artefato do coder, não reconstruído nem usado por mim.
- Fact: defeito reconfirmado na **fonte** (leitura direta, não relato do reviewer):
  `http_upload_read` (`cyberdeck_control_server.cpp:316-335`) zera `source->timeouts` após
  **qualquer** leitura positiva (`:330`) e só aborta com `k_max_recv_timeouts` (=3, `:45`)
  **consecutivos** (`:327`); `http_upload_source` (`:308-312`) guarda apenas `received` e
  `timeouts`. `receive_upload` (`cyberdeck_control_upload.cpp:236-256`) para somente por
  `declared_size`, EOF, erro ou `-1`; `:257` transforma EOF em `truncated`. Prova de
  ausência de limite total: `grep -rn "deadline\|monotonic\|esp_timer_get_time"` em
  `components/cyberdeck/{include,src}/apps/control/` = **0** ocorrências. Ou seja: um peer
  que entrega ≥1 byte antes de cada janela de recv mantém a task HTTP, o lease WRITE
  (`:799`) e o temporário (`:809`) ocupados indefinidamente.

#### Cenário TEST-UPLOAD-STREAM-11 — deadline absoluto monotônico por request

Propriedade sob prova: **o tempo total de `receive_upload` é limitado por uma constante do
header, independente da cadência de bytes e do tamanho declarado; progresso não renova o
orçamento.** Sub-casos (a..g) contam todos como o mesmo `TEST-*`; o detalhe fica aqui porque
a tabela-alvo de 18:26 (`:1476`) é append-only. Extensão de rastreabilidade declarada:
**REQ-UPLOAD-02 passa a `TEST-UPLOAD-STREAM-01..11`**; `AC-UPLOAD-01` continua sendo o
conjunto host todo (este cenário entra nele). `TEST-UPLOAD-STREAM-05` **permanece** válido
como proteção secundária (timeouts consecutivos); nada é removido nem enfraquecido.

| Sub-caso | Critério exato | Detecção |
| --- | --- | --- |
| S11-a progresso contínuo | stream devolve poucos bytes (< `k_upload_chunk_bytes`) em **toda** chamada, **sem nenhum `-1`**, e o relógio injetado avança até ultrapassar a duração máxima; `receive_upload` → `upload_status::timeout` (nunca `truncated`/`io_error`), sem publicar | state machine pura com clock falso + fake stream |
| S11-b alternância (cenário do reviewer) | bytes parciais **seguidos de `-1`** repetidos: o contador consecutivo nunca chega a `k_max_recv_timeouts`, logo a proteção secundária nunca dispara; mesmo assim aborta `timeout` quando o relógio cruza o deadline | idem; é a reprodução literal do finding |
| S11-c controle (dentro do deadline) | transferência completa em N chunks com o relógio avançando pouco por chunk e total **< deadline**: `ok`, `publish` **1x**, `close` antes de `publish`, `outcome.bytes == declared`, digest igual ao one-shot, `discard` **0x**, nenhum write após `publish` | mesmo harness do caminho feliz já existente (`test_control_upload_protocol.cpp:594-608`) |
| S11-d fronteira derivada | com `D` = duração máxima lida do header (nome a cargo do coder): relógio em `start + D - 1 µs` **continua** (e conclui `ok` se o corpo terminar); em `start + D` aborta `timeout`; em `start + D + 1` aborta `timeout`. Par de borda **obrigatório** `D-1`/`D` e `D`/`D+1`, sem literal | header puro + clock falso |
| S11-e não proporcional ao tamanho | corpo minúsculo (poucos bytes declarados, ex. 64 B) com o relógio já além do deadline antes do primeiro byte → `timeout`; mesma constante de duração para 64 B e para `k_max_upload_bytes` | clock falso; quatro ordens de grandeza de diferença |
| S11-f estado observável/cleanup | em S11-a/b/d/e: `discard` **exatamente 1x** com o nome do temporário **próprio** (`files.temp`), `publish` **0x**, `opened` 1x, bytes gravados `<= declared` e `< declared` completo, `outcome` zerado (`bytes == 0`, digest todo 0), nenhum `write`/`close_temp` depois do status de falha | fake sink existente (`fake_files`, eventos) |
| S11-g deadline por request | duas chamadas no **mesmo** relógio falso: a 1ª estoura o deadline e falha; a 2ª, com corpo pequeno, começa com orçamento novo e conclui `ok` (nenhum deadline global/estático herdado) | clock falso continuando de onde parou |

**Respeito a chunks (asserção positiva, não só "não estourou"):** em cada chamada o fake
stream registra `capacity` e o teste exige `capacity == min(declared - total, sizeof(scratch))`
e `capacity <= k_upload_chunk_bytes` (derivado de `cyberdeck_control_upload.h:46`), que
`buffer` é o `scratch` do chamador (sem alocação) e que o fake nunca devolve mais que
`capacity` (contrato `upload_read_fn`, `:80-81`).

**Seam exigido (requisito de testabilidade, não design livre):** a decisão de deadline tem de
estar no módulo **puro** (`cyberdeck_control_upload.*`), com fonte de tempo **injetada** em
microssegundos inteiros (`std::int64_t`, mesmo precedente de `nonce_store::issue(pending/
consume(..., std::int64_t now_us)` em `cyberdeck_control_protocol.h:125-135` e
`k_nonce_ttl_us` `:50`) — via parâmetro de `receive_upload` ou struct de ops; o adaptador
ESP passa `esp_timer_get_time()`. Consequências verificáveis: (i) a TU pura **não** menciona
`time(nullptr)`, `clock_gettime`, `gettimeofday`, `std::chrono`, `esp_timer`, `sleep`,
`usleep`, `vTaskDelay`; (ii) `esp_timer_get_time` aparece **1x** no caminho de upload do
servidor (derivado de `http_upload_read`/construção de `http_upload_source`, `:316-335`,
`:818`); (iii) o clock é consultado no máximo **1x por iteração** do laço
(`clock_calls <= iterations + 1`) — nada de busy-wait que teste de duração não detectaria;
(iv) a duração máxima é constante do header (`type`/unidade/valor a cargo do coder), **não**
derivada de `declared_size`.

**Determinismo (proibido wall-clock):** o teste não dorme, não mede tempo real e não depende
de `recv` real; o relógio avança por agenda fixa por chamada do fake stream. O fake stream
tem **cap de segurança** de chamadas derivado da agenda (`ceil(D / avanço) + folga`) e o
teste exige que o cap **não** seja atingido e que o clock tenha sido consultado — sem isso,
uma mutação que remova o deadline rodaria até o cap e devolveria `-2`; a asserção de
`status == timeout` então falha (terminação garantida, falha determinística).

**Sensibilidade obrigatória (mutações M-UP-15..20, todas devem matar o teste):**

| Mutação | Como deve morrer |
| --- | --- |
| M-UP-15 remover a consulta/checagem do deadline | S11-a (e S11-b) devolvem `io_error` do cap ou ficam sem terminação útil → `status == timeout` falha |
| M-UP-16 reiniciar o deadline a cada chunk positivo (o comportamento **atual**) | S11-a/S11-b falham |
| M-UP-17 `>=` → `>` (ou inverso) na comparação com o deadline | S11-d falha no ponto `start + D` (ou em `D-1`) |
| M-UP-18 duração proporcional ao tamanho declarado | S11-e falha |
| M-UP-19 checar o deadline só quando o read devolve `-1` | S11-a (sem timeouts) falha |
| M-UP-20 deadline global/estático compartilhado entre chamadas | S11-g falha |

**Estrutural (a TU do servidor é ESP-IDF-only e não roda no host):**
`test_control_upload_contract.py` já inspeciona `http_upload_read` (`:161-166`); acrescentar
(i) existência da fonte de tempo injetada e do limite total **no módulo puro** (não no
adapter), (ii) ausência de wall clock/sleep na TU pura, (iii) `esp_timer_get_time` 1x no
caminho de upload, (iv) `k_max_recv_timeouts` preservado (secundário) e (v) que
`timeout` continua saindo pelo **mesmo** `error_reply(upload_error(status))`
(`server.cpp:824-828`, mapeamento `:291-306`) com `Connection: close` e **sem** chegar a
`format_upload_json`/`201` (`:830-836`). Não pinar código HTTP novo: `timeout` hoje →
`error::io_error` → **500** (`cyberdeck_control_protocol.cpp:72`); a asserção é "mesmo
caminho de falha dos outros erros de stream", derivado de `upload_error`, não do literal.

**Cleanup / temp / lease:** a prova primária de liberação do temporário é a state machine
pura (S11-f: `discard` 1x do temp próprio, `publish` 0x). O lease WRITE é RAII no handler
(`acquire_global(..., access::write)` em `:799`, objeto `storage` de escopo do handler) e o
timeout retorna por `error_reply`, então o destrutor libera — `TEST-UPLOAD-INTEG-09` já é a
prova disso e **não** será duplicada. Só reexecutar/estender INTEG-09 se o coder inserir
novo ponto de saída que pule esse `return`.

- Fact: **classificação em caso de ausência do seam**: sem fonte de tempo injetada no módulo
  puro, o cenário é **inexequível em host** e isso é **implementação ausente** (bloqueia
  REQ-UPLOAD-02/AC-UPLOAD-01), não defeito de teste. Se o seam existir e S11-a falhar, é
  **implementação incorreta** do deadline. Defeito de teste só se a asserção não distinguir
  bordas (M-UP-17 sobrevivente), e nesse caso ajusto a asserção, sem afrouxar.
- Fact: limites e riscos desta especificação — (1) **disponibilidade**: sem deadline, a
  única task HTTP, o lease WRITE e o temp ficam presos por um peer LAN autenticado (HTTP sem
  TLS não protege disponibilidade), negando também `/control/type`, `/control/terminal` e
  `/screenshot` (mesmo servidor) — severidade alta em REQ-UPLOAD-02/AC-UPLOAD-01;
  (2) **falso positivo de deadline curto**: valor pequeno demais quebra a fixture
  legítima de 2.764.854 B em LAN lenta; o valor deve derivar de taxa mínima documentada, e
  S11-c/`DEV` precisam cobrir o caso real (o teste host não prova tempo de recv real);
  (3) relógio que **não avança** nunca atinge o deadline — a garantia depende de fonte
  monotônica que progrida (`esp_timer` satisfaz); registrado como limite, sem teste útil de
  "clock travado" e sem semântica especificada para clock andando para trás (não pinar);
  (4) `DEV-11` atual cobre apenas **silêncio total**; falta variante de **progresso lento**
  no Tab5 (AC-UPLOAD-02) — fica pendente de autorização e de novo dispatch, sem novo id de
  device criado aqui; (5) o teste prova a state machine, não o adaptador `httpd_req_recv`
  (cobertura estrutural + DEV).
- Fact: **gates focados a executar depois da implementação do coder** (nenhum executado
  agora): `make -C tests/host/keymap test_control_upload_protocol
  test_control_upload_contract` e, se o adapter/servidor mudar,
  `test_control_http_contract test_control_cli`; mutações M-UP-15..20 em cópia descartável
  com restauração; `git diff --check`. Suíte ampla, gcovr/scope guard, build, CI e dispositivo
  continuam fora do papel do tester (fechamento do reviewer).
- Supersedes or discards: **nada**. Adiciona o cenário 11 à matriz-alvo sem editar a tabela
  de 18:26 (append-only) e sem reabrir IMG, AUTH-12 ou o `manual_validation` do viewer.

### 2026-11-08 18:43 -03:00 - validação independente do deadline (TEST-UPLOAD-STREAM-11): focados verdes, M-UP-15..21 mortas, +1 caso fail-closed

- Fact: snapshot avaliado — HEAD `16392858881d775bb68a6733fffcfa67223ab808`;
  `git diff | sha256sum` = `51b678d7c4be365ac0186169072a4efda9818839da76649dc24ae720f7927a59`
  (igual ao relatado pelo coder 18:39; o diff **não** mudou neste ciclo porque toda a minha
  edição foi em arquivo **não rastreado**); `version.txt` = 1.16.0; 48 entradas em
  `git status --porcelain`. Fontes de produção conferidas por hash **antes e depois**:
  `cyberdeck_control_upload.cpp` `8ae479c8…c88c3`, `cyberdeck_control_upload.h`
  `78247fa6…433af`, `cyberdeck_control_server.cpp` `699070fd…41843` — **produção não
  editada por mim**. `tests/host/keymap/test_control_upload_protocol.cpp` foi o **único**
  arquivo que alterei: `87577faf…a8aa8` (coder) → `6c318d9f…35887` (meu, +3 checks).
  Composto (mesma receita da entrada de 18:33, sem `docs/agent-state`) no estado final =
  `4dad7be1f6b9d440f19dbc814474f3b5240cddeca97b40f1aa0a041bd04d1958`; o do estado
  pré-edição era `dcc06064…9cad4`. Binário `build/cyberdeck5.bin` **0x2a3790** (2.766.544 B)
  sha256 `cc797330…6bbf` — artefato do coder, **não** reconstruído por mim.
- Fact: implementação lida na fonte (não relato): `k_max_upload_duration_us = 120'000'000`
  com `static_assert > 0` (`cyberdeck_control_upload.h:60-62`); `upload_clock{context,
  std::int64_t (*now_us)(void*)}` (`:102-105`) injetado em `receive_upload` (`:145`);
  início lido **uma vez** (`upload.cpp:236`) e comparação **inclusiva** no topo de cada
  iteração (`:242`, `>=`), com `consecutive_timeouts` convivendo como defesa secundária
  (`k_upload_max_consecutive_timeouts`); adaptador ESP com **um** ponto de relógio
  (`upload_stream_now` → `esp_timer_get_time()`, `server.cpp:315-318`) e `http_upload_read`
  agora devolve `-1` por timeout de recv sem contar (`:328-338`) — a contagem subiu para o
  módulo puro. Ou seja, o finding 18:28 está corrigido e a lógica é **executável no host**.
- Fact: `TEST-UPLOAD-STREAM-11` cobre S11-a..g no alvo **puro** com a produção real
  compilada (`receive_upload` + `upload_files_sink`), `fake_clock` por agenda (sem wall
  clock) e `fake_read` com roteiro `{>0, -1, -2, 0}`, cap de segurança e log de
  `capacities`: S11-a progresso contínuo sem nenhum `-1` (aborta na iteração do deadline);
  S11-b bytes parciais + `-1` intercalados (o secundário nunca dispara); S11-c controle
  dentro do prazo (publica, digest one-shot, `close` antes de `publish`); S11-d fronteira
  derivada `D-1` ok / `D` aborta / `D+1` aborta; S11-e 64 B e `k_max_upload_bytes` com a
  **mesma** D; S11-f estado observável (1 temp aberto, 1 discard do temp próprio, 0 publish,
  0 close, 0 write após abortar, outcome zerado); S11-g orçamento novo no segundo request.
  Contagem de clock provada por `clock.calls == source.calls + 2` (≤1 leitura por iteração).
  **Limite de prova**: `httpd_req_recv`/`esp_timer_get_time` são verificados apenas
  **estruturalmente** pelo contrato; o comportamento do adapter no ESP não é exercitado no
  host (depende de DEV).
- Fact: **matriz focada reexecutada por mim** neste snapshot (não reusei resultado do coder):
  `test_control_upload_protocol` → **PASS 293 checks** (290 do coder + 3 meus);
  `test_control_upload_contract` → **PASS** (grupo `test_deadline`, após a minha edição);
  `test_control_protocol` → **PASS 123 checks**; `test_control_cli` → **Ran 10 tests, OK**;
  `test_control_http_contract` → **PASS**; `test_fs_write_dispatch` → **PASS 118 checks**;
  `test_fs_write_cli` → **PASS**; `test_fs_write_contract` → **PASS**;
  `test_sd_io_coordinator` → **PASS**. `git diff --check` → **rc=0**. Sem suíte completa,
  cobertura global, `idf.py build`, CI ou dispositivo — fora do meu escopo.
- Fact: **lacuna material corrigida (só teste)**: o guarda `clock.now_us == nullptr`
  (`upload.cpp:224`) só era verificado por string no contrato. Acrescentei 3 asserts em
  `test_receive_negatives` (bloco novo após o de scratch ausente): `upload_clock{nullptr,
  nullptr}` → `internal`, `opened == false` e `source.calls == 0` (falha fechada antes de
  qualquer I/O, sem ler o stream). Sensibilidade provada pela mutação M-UP-21 abaixo
  (remover o guarda → **SIGSEGV**, rc=-11): o caso é load-bearing, não decorativo.
- Fact: **campanha de mutação** em cópia descartável `/tmp/opencode/mut-stream11` (subárvore
  mínima: `tests/host/keymap` com `Makefile`+`shim`+o teste, `components/cyberdeck/include`,
  os dois `.cpp` de `apps/control`), com fonte **pristina** `8ae479c8…` conferida por sha
  antes de cada rodada e restaurada/verificada depois. Baseline da cópia: **PASS 293**;
  restauração final: sha **igual** e baseline **PASS 293**. Resultado **mortas 7/7**:

| Mutação | Patch aplicado na cópia | Resultado |
| --- | --- | --- |
| M-UP-15 deadline ausente | removidas a leitura inicial e o bloco `if (… >= k_max_upload_duration_us)` | **rc=1**, 36 checks falham (S11-a/b) |
| M-UP-16 progresso renova o prazo | `started_us` reatribuído a cada `total += received` | **rc=1**, 7 checks (S11-a/b) |
| M-UP-17 `>` em vez de `>=` | troca do operador da comparação | **rc=1**, 23 checks (S11-d `D`, S11-a/b) |
| M-UP-18 prazo proporcional ao tamanho | orçamento = `k_max_upload_duration_us * declared_size / k_max_upload_bytes` | **rc=1**, 15 checks (S11-c/e) |
| M-UP-19 prazo só no `-1` | checagem movida para o ramo `chunk == -1` | **rc=1**, 35 checks (S11-a) |
| M-UP-20 prazo estático/global | `static const std::int64_t started_us = …` | **rc=1**, 34 checks (S11-a/b/d por deslocamento da agenda + S11-g) |
| M-UP-21 guarda de clock nulo | removido `clock.now_us == nullptr ||` | **rc=-11 (SIGSEGV)** → caso novo é sensível |

  Nenhuma mutação sobreviveu; nenhuma exigiu alterar `tests` para matar (as asserções do
  coder já distinguiam borda/renovação/proporcionalidade/globalidade). Log:
  `/tmp/opencode/mut-stream11/mutation-log-final.txt` (artefato local, não commitado).
- Fact: **STREAM-05 preservado** apesar da mudança de semântica (1 `-1` isolado deixou de
  ser terminal; agora 3 consecutivos): o teste tem as duas asserções — “one recv timeout
  followed by progress does not abort” (`test_control_upload_protocol.cpp:804`) e “three
  consecutive recv timeouts abort and clean up” (`:814`). Isso é coerente com o reviewer
  18:28 (“timeout consecutivo pode permanecer como proteção secundária”), não é
  enfraquecimento.
- Decisions and assumptions: aceitei a mudança de semântica do `-1` porque o orçamento
  consecutivo foi preservado e o reviewer a autorizou explicitamente; o deadline de 120 s é
  constante do header e o teste **deriva** `D` (`k_deadline = k_max_upload_duration_us`) em
  vez de pinar — inclusive a iteração de abort (`D / k_step`).
- Pending and risks (residual, nenhum bloqueante para os gates host):
  1. **Adapter/DEV**: `httpd_req_recv` + `esp_timer_get_time` só têm prova estrutural; a
     variante física de **progresso lento** (manual §6 **U8b**) segue **não executada** —
     AC-UPLOAD-02 / `TEST-UPLOAD-DEV-*` continuam pendentes de autorização e monitor.
  2. **Contrato frágil (Low)**: `test_control_upload_contract` pina literais
     (`k_max_upload_duration_us = 120'000'000`, `k_upload_max_consecutive_timeouts = 3`) e a
     **expressão exata** `clock.now_us(clock.context) - started_us >= k_max_upload_duration_us`.
     Uma refatoração equivalente (ex.: `budget` local) quebra o contrato sem regressão
     funcional. Não editei: falha alto e não enfraquece cobertura; a fronteira já tem prova
     comportamental em S11-d.
  3. **Capacidade por chamada (Low)**: `check_chunk_contract` exige `capacity` exata na
     **primeira** chamada, `> 0` e `<= k_upload_chunk_bytes` nas demais, e o buffer do
     chamador; a igualdade `min(declared - total, scratch)` em **todas** as chamadas não é
     asserida (parcialmente coberta por STREAM-06/INTEG-03). Sem impacto no deadline.
  4. **Composição com FS real (Low)**: S11 usa `fake_files`; o caminho real
     (`upload_files_sink` + `unlink` do temp em aborto por deadline) não é exercitado de
     ponta a ponta — os dois elos são provados separadamente (S11-f e
     `test_upload_files_ownership`).
  5. **Relógio travado/retrocedendo**: garantia depende de fonte monotônica que progrida;
     comportamento não especificado para clock para trás (não testado, documentado em 18:33).
  6. `max_calls = 16` em S11-a/b é literal de guarda; se `D` mudar de ordem de grandeza, o
     cenário falha alto (io_error), não passa em falso.
- Fact: rastreabilidade atualizada por evidência — **REQ-UPLOAD-02 / AC-UPLOAD-01 ->
  TEST-UPLOAD-STREAM-11 (S11-a..g, executado, 293 checks) + caso fail-closed de clock nulo +
  mutações M-UP-15..21 (7/7 mortas)**; `TEST-UPLOAD-STREAM-05` mantém prova das duas
  direções; `TEST-UPLOAD-INTEG-09` (lease WRITE RAII) permanece como estava — não dupliquei.
- Supersedes or discards: encerra o item “deadline total e regressão focada” pendente na
  entrada de 18:33 e o `blocker` de disponibilidade do reviewer 18:28 **quanto aos gates
  host**. Não encerra AC-UPLOAD-02, suíte ampla/cobertura/CI/build nem `manual_validation`;
  não altera o status do viewer (setas / `manual_validation` seguem como estavam).

### 2026-11-08 19:00 -03:00 - AC-UPLOAD-02: evidência física de pixels do upload (DEV-05..07) e fechamento monitor/estado

Sessão delimitada (continuação da fase física interrompida por step limit). **Sem** flash,
**sem** re-upload do fixture, **sem** sobrescrita, **sem** exclusão, **sem** U8b, **sem**
edição de produção/teste. Probes e checagens desta entrada vivem em `/tmp/opencode/`
(artefatos locais, não versionados).

#### Snapshot e dispositivo

- Fact: HEAD `16392858881d` (`docs: record ID3 metadata task completion`), `version.txt`
  `1.16.0`, worktree dirty apenas com a entrega do viewer/upload já existente.
- Fact: `build/cyberdeck5.bin` 2.766.544 B (`0x2a3790`), mtime `18:38:59`, sha256
  `cc797330d78714acd5a618749f7daf04579f8907d8a2fdc25d9972f71daa6bbf` — mesmo binário
  registrado na entrada anterior; nada foi recompilado nem gravado nesta sessão.
- Fact: `sys.info` no dispositivo → `fw_version=1.16.0`, `idf_version=v6.1`,
  `chip=esp32p4`, IP `192.168.68.127`, `safe_mode=false`, `interrupted_boots=0`,
  `recovery_persisted=true`. Boot derivado de `uptime` + relógio do host: **18:48:11**
  (uptime 702 s às 18:59:53), coerente com o log do monitor (timestamps em uptime).
- Fact: monitor `idf-monitor --no-reset --eol lf -p /dev/ttyACM0 build/cyberdeck5.elf`
  rodava como PID **329441** (PPID 329438), stdout em `/tmp/opencode/upload-monitor.log`,
  stdin `/dev/null` (não interativo, **sem FIFO**). Nenhum outro processo de monitor.

#### Artefato físico comparado

| Item | Evidência |
| --- | --- |
| Fixture | `/tmp/opencode/upload-fixture-1280x720.bmp`, 2.764.854 B, sha256 `c494f7b0c907a2c7dbcc20e40d4576a32c4d7723cd0e8b4d7faf83291b91ff77` (reconferido nesta sessão) |
| Arquivo no Tab5 | `/screenshots/upload-fullres-20261108-1852.bmp`, aberto na sessão anterior via `/control/type "image /screenshots/upload-fullres-20261108-1852.bmp"`; upload respondeu `201` com o mesmo sha256 (entrada anterior) |
| Captura comparada | `/tmp/opencode/device-screen-upload.bmp`, 2.764.854 B, 1280x720x24, sha256 `a57a7a755768a463af89d41d5fc1e12adfcdd1bb17df77c30e48b00aa5f44ff4` (`GET /screenshot` na sessão anterior) |
| Captura de estabilidade (viewer ainda aberto) | `/tmp/opencode/device-screen-after.bmp`, sha256 `3e4054d901baba80032533b7e2856b6fb12110c7f5ccb2fa271744d37cef13f7` — **mesmo bbox** (120..1159, 94..678) |
| Captura após `Esc` | `/tmp/opencode/device-screen-after-esc.bmp`, sha256 `d07d738f42631112ab38cfecb6be1a990df520ed1aad37227247d9a7beba6809` — **0** pixel saturado |

#### Resolução do alvo real (não assumido): viewport do viewer

- Fact (fonte de verdade do dispositivo): `ui.dump` → nó de conteúdo `x=12 y=65 w=1256 h=643`.
  A geometria do `layout()` de produção (`kInset=4`, `line = max(18, 22) = 22`,
  `kBandGap=2`) dá área útil `(16, 93, 1248x587)`:
  `area_y = 65+4+22+2 = 93`, `area_h = (65+643-4-22) - 2 - 93 = 587`.
- Fact: as bandas de texto da captura confirmam o mesmo viewport de forma independente —
  rótulo de título em `y 73..90` (caixa em `65+4=69`) e rodapé em `y 686..703` (caixa em
  `682 = 65+643-4-22`). O deslocamento glifo→caixa é idêntico nos dois (4 px), então a
  diferença entre as bandas fixa `h=643`, e a origem fixa `y=65`.
- Refutação da hipótese concorrente (sem `pad_row` do flex, conteúdo 1256x654 em `(12,54)`):
  o probe prevê retângulo começando em `x=110`; a captura mostra `x=120`. Hipótese
  **descartada** por medição, não por convenção. `pad_row=11` é o valor efetivo.
- Fact: `test_image_view.cpp` usa 1272x678 e 712x1238 como *surfaces* de shim; **não** é o
  alvo real do Tab5 (não foi usado como fonte da asserção).

#### Probe host com a função de produção

Comando (somente leitura do repositório; binário em `/tmp`):

```bash
g++ -std=c++17 -O1 -Icomponents/cyberdeck/include /tmp/opencode/probe_fit_upload.cpp \
    components/cyberdeck/src/apps/image/cyberdeck_image_fit.cpp -o /tmp/opencode/probe_fit_upload
/tmp/opencode/probe_fit_upload
```

Saída (produção `cyberdeck_image::fit_to_area(1280,720,16,93,1248,587)`):

```text
content=(12,65 1256x643) area=(16,93 1248x587)
fit valid=1 fitted=1044x587 zoom=208 object=(0,27) natural=1280x720
centre=(640.0,387.0) half=(520.0,292.5)
rendered_span_x=[120.0,1160.0) rendered_span_y=[94.5,679.5)
margins l=104.0 r=104.0 t=1.5 b=0.5
contained=1
```

Hipótese 1256x654 (mesma chamada): `fitted=1063x598 zoom=212`, span `x=[110,1170)`,
`y=[82.9,679.1)` — incompatível com a captura.

#### Comparação captura x produção (TEST-UPLOAD-DEV-05/06/07)

Comando: `python3 /tmp/opencode/check_device_upload.py` →
**106 checks, 0 failures**; delta máximo de canal **0** (após corrigir o **meu** oráculo de
expansão RGB565 de verde para `(g<<2)|(g>>4)`, que era artefato do teste, não do device).

- Fact (DEV-05): retângulo saturado observado `x=120..1159`, `y=94..678`, `1040x585`, com
  **608.400 = 1040*585** pixels saturados (retângulo cheio), **0** saturado em `y 0..41`
  (header), **0** fora do retângulo. `zoom=208 <= 256`; proporção preservada
  (`1044*720` vs `587*1280`, desvio < 1 px); retângulo **contido** na área útil
  (`x 120..1160` dentro de `16..1264`; `y 94.5..679.5` dentro de `93..680`).
- Fact: centragem prevista x produção vs observado — esquerda `120.0 vs 120`, direita
  `1160.0 vs 1160`, topo `94.5 vs 94`, base `679.5 vs 679` (todas ≤ 1 px). Margens
  observadas `l=104 r=104 t=1 b=1` (diferença ≤ 1 px). O 0,5 px vertical é a fase de
  amostragem do transform do LVGL (linha 94 parcialmente coberta ≈ 61% medido), não um
  deslocamento da área; a borda inferior cai exatamente em 679.
- Fact (DEV-06): 16 centros de bloco + 4 offsets (±40 px em x e y) em coords da imagem,
  mapeados por `rect_x + round(x*fitted_w/1280)` (fórmula do plano) — **80 amostras**,
  todas com cor do fixture convertida para RGB565 e tolerância ≤ 8 por canal; delta real 0.
  Cores exercitadas incluem os pares que expõem erro de expansão/extensão de bit
  (`192,192,0`, `0,192,0`, `128,0,255`, `0,128,255`).
- Fact (DEV-06, orientação): quatro cantos com manchas 48x48 — TL ciano `(0,255,255)`,
  TR magenta `(255,0,255)`, BL amarelo `(255,255,0)`, BR laranja `(255,130,0)` pós-RGB565;
  distância mínima de canal entre cantos **125**, logo flip vertical, horizontal e duplo
  seriam detectados (o fixture é assimétrico por construção).
- Fact (DEV-07): **0** pixel saturado a 1 px fora do retângulo; `#000000` nos 2 px à
  esquerda, à direita e acima do bitmap; primeira linha abaixo do bitmap não é saturada
  (`0,0,0` — rodapé é texto cinza, não moldura). Nenhuma borda/sombra do tema.

#### Não-sobrescrita (sonda segura, sem tocar o arquivo)

- Fact: `python3 tools/cyberdeck_http_cli.py --host 192.168.68.127 upload
  /tmp/opencode/upload-fixture-1280x720.bmp --name upload-fullres-20261108-1852.bmp` →
  **HTTP 409** `{"error":"conflict"}` (rc=1 do CLI). O caminho de produção checa
  `target_exists` **antes** de `open_temp`/escrita (`cyberdeck_control_upload.cpp:229`), e a
  listagem posterior continua com **um** `upload-fullres-...bmp` e **nenhum** temporário.
- Limitação registrada: não há rota de download/leitura do arquivo no device, então o
  conteúdo do alvo não foi re-hasheado depois do 409; a prova é 409 + ordem de checagem +
  ausência de temporário + listagem. O cenário formal **TEST-UPLOAD-DEV-09** (marcador via
  `fs.write` + `rm`) **não** foi executado nesta sessão.

#### Estado final, logs e encerramento do monitor

- Fact: `Esc` (`ui.escape`) encerrou o viewer; `ui.dump` pós-Esc → 47 nós,
  `truncated=false`, **nenhum** nó de classe `image`; captura pós-Esc com **0** pixel
  saturado. Shell voltou a aceitar comando (`ls -a /screenshots` listou os prints + o
  arquivo enviado, term.dump terminando em `/$ `).
- Fact: sonda de temporário (opcional) `cat /screenshots/.upload.0.tmp` →
  `cat: file not found` (duas ocorrências no scrollback porque uma tentativa anterior teve
  leitura serial perdida e o comando foi reenviado; ambas negativas).
- Fact (monitor): `kill -TERM 329441` (PID exato do `idf-monitor`); `ps` posterior **não**
  lista `idf-monitor`; `/dev/ttyACM0` liberado. Log final 2.148 B; **0** ocorrências de
  `Guru Meditation|Stack protection|abort()|rst:0x|WDT|Brownout`. Únicos W: quatro
  `httpd_txrx: httpd_sock_err: error in send : 104` + `uri handler execution failed`
  (uptime 483/493/529/539 s) — fechamento de socket **pelo cliente HTTP** (host) durante as
  requisições desta fase, sem panic, sem reboot e sem perda de resposta observada.
- Fact: `GET /events.txt` final (8.559 linhas) → **0** ocorrências dos mesmos padrões.
- Fact: `sys.info` final às 18:59:53 → `uptime=00:11:42`, `free_heap=24.822.743`,
  `internal_free=114.827`, `psram_free=24.707.916`, `safe_mode=false`,
  `interrupted_boots=0`, `serial_stack_high_water_bytes=10.464`. Uptime monotônico na
  sessão (549 s → 644 s → 702 s), portanto **sem reboot** durante a evidência.
- Fact: um `screen.dump` intermediário e um `term.dump` falharam com
  `device reports readiness to read but returned no data`; `ping`, `sys.info`, `term.dump`
  e `screen.dump` seguintes passaram, com uptime contínuo — ruído transitório de leitura
  do CDC USB, **não** reboot nem defeito do firmware. Nenhuma ação destrutiva foi repetida.

#### Rastreabilidade

- `REQ-UPLOAD-03 / AC-UPLOAD-01 -> TEST-UPLOAD-DEV-05/06/07`: **executado** (106 checks,
  0 falhas) sobre a captura do arquivo publicado pelo canal autenticado.
- `REQ-IMG-02 / AC-IMG-02 -> TEST-IMG-VIEW-01` (proporção/contensão reais): **corroborado**
  com área real do Tab5 (`1256x643`), não com as surfaces do shim.
- `AC-UPLOAD-02`: parcial — DEV-05/06/07 e sonda de 409 cobertos; **DEV-08/09/10/11/12**
  seguem sem execução física nesta sessão (DEV-08 negativos já têm prova host; DEV-09
  formal = marcador + `rm`; DEV-10/11 = truncamento/timeout de socket; DEV-12 = concorrência
  e memória). **U8b não executado** (exige autorização específica e cliente ad-hoc).

#### Pendências e riscos residuais

1. **U8b** (gotejamento > 120 s) não executado: requer autorização separada; deadline
   permanece com prova host (TEST-UPLOAD-STREAM-11) e sem prova física.
2. **DEV-08/09/10/11/12** não executados nesta fase; nada foi apagado/sobrescrito para
   criá-los. O 409 desta entrada não substitui o DEV-09 formal (marcador com conteúdo
   conhecido).
3. **Hash do arquivo no device** após o 409 não reconferido (sem rota de leitura); risco
   baixo pela ordem `target_exists` → `open_temp` e pela ausência de temporário.
4. **Título/indicador `i/N`** do viewer não foram lidos por OCR nesta entrada; a banda de
   título existe na captura (y 73..90) e a abertura foi por `/control/type`, mas o texto
   exato não foi decodificado. Cobertura prévia: host + entradas anteriores.
5. **Fase de amostragem do LVGL** produz 1 linha parcial no topo do bitmap; bbox inteiro
   permanece dentro de ≤ 1 px do previsto por `fit_to_area`. Registrado como característica
   do transform, não como defeito.
6. Navegação por setas (`manual_validation` do viewer) continua pendente e fora do escopo
   desta sessão.

- Decisions and assumptions: usei a produção `fit_to_area` (compilada do repo) + a área real
  medida por `ui.dump` e pelas bandas de texto da captura; rejeitei a hipótese 1256x654 por
  medição. O erro inicial do meu oráculo RGB565 (verde) foi corrigido no checker e
  reclassificado como defeito de teste local, sem alterar asserção do repositório.
- Supersedes/discards: encerra o item "fechar DEV-05..07 com programa host" e o item
  "parar monitor + sys.info/events finais" pendentes na entrada de 18:43. Não encerra
  AC-UPLOAD-02, U8b, DEV-08..12, suíte ampla/cobertura/CI/build nem a revisão final.

### 2026-11-08 20:00 -03:00 - validação independente do wiring de `clean`/cobertura (delta pós-revisão)

- Fact (snapshot, idêntico antes e depois desta sessão): HEAD
  `16392858881d775bb68a6733fffcfa67223ab808`; `git diff --binary | sha256sum` =
  `43002380798a3440c01a851b07a56ee193e882d3a121790bca5c4ca1ae07050a`;
  `git status --porcelain` = 50 entradas; `version.txt` = 1.16.0.
  `tests/host/keymap/Makefile` sha256 `00cca90173d281b26203c4bae240d48498371be11a7a0be4943a2a201968abf2`
  (igual ao registrado pelo coder); `test_incremental_final_contract.py` sha256
  `49356437100f62c226e0bab2bfade9d37272c21686c718ec39b80f2013e68b38`.
- Scope: somente o delta `tests/host/keymap/Makefile` +
  `tests/host/keymap/test_incremental_final_contract.py` (wiring de `BINS`/`clean` e
  derivação do contrato). **Nenhum teste criado ou alterado** por esta sessão; nenhum
  arquivo de produção tocado; nenhuma suíte ampla/cobertura global/build IDF/device.
  Os artefatos (`_bin`, `.gcda`, `.gcno`, `.o.d`) já estavam atualizados no snapshot
  recebido (`_bin` 19:53 > `Makefile` 19:51), então `test-build` foi no-op.

#### Cenários e resultados (agrupados por requisito de harness)

- `TEST-INCR-CLEAN-01` (ordem de expansão, fonte real): `BINS` é simply-expanded
  (`BINS :=` linhas 234/242); as duas referências agora vivem em
  `BINS += $(MUSIC_DURATION_BIN) $(MUSIC_PRESENCE_BIN)` (linha 306), depois de
  `MUSIC_PRESENCE_BIN :=` (257) e `MUSIC_DURATION_BIN :=` (299), e foram removidas da
  linha 243. Derivado pelo próprio `make`, sem pin: `make -p -n -f Makefile` → `BINS`
  com **108 entradas, 0 duplicatas**, contendo `test_music_presence_bin` e
  `test_music_duration_bin`; confirmação por caminho independente
  (`make --eval='print-bins: ; @echo $(BINS)' -n print-bins`) devolve os dois nomes.
  `make -n clean` lista ambos no `rm -f $(BINS)`.
- `TEST-INCR-CLEAN-02` (reprodução do defeito em sandbox descartável
  `/tmp/opencode/tester-clean-repro`, symlinks para `components/`/`tools/`/`version.txt`/
  `code-map.md`, só os dois alvos de música compilados com
  `-O0 --coverage`): com o wiring pré-correção literal (refs recolocadas na linha 243 e
  sem a linha 306) o `make -p` do próprio make mostra as duas ausentes de `BINS`
  (referência antes da definição expande vazio) e `make clean` **preserva** os dois
  `_bin` (só `*.gcda`/`*.gcno` do glob e o dir `test_music_duration/` saem). Reexecutar
  `make test_music_presence` nesse estado executa o binário stale **sem recompilar** e
  grava `test_music_presence_bin-{cyberdeck_audio_presence,test_music_presence}.gcda`
  **sem `.gcno`** — sintoma exato (órfãos) que o reviewer removeu à mão.
- `TEST-INCR-CLEAN-03` (wiring corrigido, mesmo sandbox): `make clean` remove os dois
  `_bin` e os sidecars (`_bin`+`.gcda`+`.gcno` = 0 sobreviventes, dir de objetos
  removido); reexecutar `make test_music_presence` **recompila** e produz `.gcda` **e**
  `.gcno` (0 órfãos). No worktree atual também conferi: 0 `.gcda` sem `.gcno` e 0
  `.gcno` sem `.gcda` em `tests/host/keymap/`.
- `TEST-INCR-CLEAN-04` (derivação do contrato + mutação): `test_incremental_final_contract.py`
  não pina os nomes de música (grep só encontra a checagem pré-existente de
  `MUSIC_APP_OBJ`); ele deriva o inventário de executáveis das receitas `.PHONY`
  expandidas — no dump independente, 108 wrappers executados, e as receitas dos dois
  alvos expandem de `@./$(MUSIC_PRESENCE_BIN)`/`@./$(MUSIC_DURATION_BIN)` para
  `@./test_music_presence_bin`/`@./test_music_duration_bin`. Controle (sandbox com cópia
  real dos dois `.py` e `Makefile` idêntico, mtime preservado): **PASS** (27 targets, 176
  TUs, baseline parity). Mutação removendo a linha 306: **FALHA** em `assert not missing`
  com `['test_music_duration_bin', 'test_music_presence_bin']`. O `Makefile` real nunca
  foi mutado (sha256 `00cca901…` e diff sha `43002380…` inalterados antes/depois).
- `TEST-INCR-CLEAN-05` (varredura independente da mesma classe): parser próprio das
  atribuições eager (`:=`/`+=`, com continuações) → 336 atribuições, **0** referências a
  variável ainda não definida naquele ponto. Corrobora a alegação do coder de que só esses
  dois artefatos escapavam de `BINS`/`clean`.
- `TEST-INCR-CLEAN-06` (focados no worktree, após `test-build`): `test-build` EXIT=0
  (no-op); `test_incremental_final_contract` **PASS** (27 targets, 176 TUs, baseline
  parity); `test_music_presence` **1071 checks, 0 falhas**; `test_music_duration`
  **156/156 checks**; `test_incremental_batch_contract` **PASS** (29 targets, 38 classes
  de mutação detectadas). Todos EXIT=0. `git diff --check` EXIT=0.

#### Rastreabilidade

- Harness/CI gate (sem `REQ/AC` de produto): `TEST-INCR-CLEAN-01..06` -> wiring de
  `BINS`/`clean` do `tests/host/keymap/Makefile` e alvo
  `.PHONY test_incremental_final_contract` (agregado `test`). Corrige o efeito descrito
  pelo reviewer em 19:37 (`_bin` sobrevivente reescrevendo `.gcda` sem `.gcno`).
- `REQ-MUSIC-*` não mudam: o delta não altera comportamento de player/presença/duração —
  `test_music_presence` e `test_music_duration` continuam verdes e inalterados.

#### Órfão adjacente (deferable, fora do escopo)

- `tests/host/keymap/test_music_command_input_bin` (23.040 B, 28/out 16:09): **0**
  referências de wiring em todo o repositório (só a nota do coder), ausente de `BINS` e de
  `LEGACY_TEST_ARTIFACTS`, coberto por `.gitignore:15` (`tests/host/**/test_*`). `clean`
  não o remove; nenhum alvo o executa e o `.gcda` dele fica coberto pelo glob `./*.gcda`.
  Adjacente e deferível (decisão futura: incluir em nomes legados ou apagar); **não**
  faz parte desta correção e não bloqueia o delta.

#### Limitações e riscos residuais

1. A reprodução de `clean`/sidecars usou sandbox descartável com symlinks de árvore e só
   os dois alvos de música; não reproduz a suíte inteira.
2. Gotcha de sandbox: symlink para `test_incremental_final_contract.py` faz
   `Path(__file__).resolve().parent` apontar para o diretório **real** — um primeiro
   controle por symlink acabou validando o `Makefile` real (falso PASS na mutação). A
   validação só vale com **cópia real** dos dois `.py`; refeita assim (controle PASS +
   mutação FAIL acima).
3. `Model` do `test_incremental_batch_contract` expande `+=` sempre de forma eager, o que
   reproduz `BINS` (`:=`) corretamente; se `BINS` virasse recursivo (`=`), o modelo
   divergiria do make. Não exercitado neste delta.
4. Gates globais seguem do reviewer: suíte agregada com `--coverage`, gcovr/limiares,
   `coverage_scope_guard.py`, lint/format, build IDF e CI remota (a CI mais recente é
   stale no HEAD base). Nada disso foi executado aqui por escopo.
5. Nenhum defeito novo encontrado no delta: o wiring corrigido se comporta como esperado
   e o contrato é sensível à regressão (mutação falha).

- Supersedes/discards: substitui, para o item "clean/coverage wiring", o checkpoint do
  reviewer que removeu dois `.gcda` órfãos à mão; a falta dos dois artefatos em
  `BINS`/`clean` não existe mais neste snapshot. Não é aprovação final nem gate global.
