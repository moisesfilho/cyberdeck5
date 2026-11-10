# coder - image-formats

- Role: coder
- Updated: 2026-11-08 (hora local; não aferida com `date` nesta sessão) -03:00
- Code state: HEAD `0f4ab3a3dd43` ("docs: record image viewer completion"),
  worktree sujo (fase 1 em andamento). `git diff | sha256sum` =
  `d16e6df142e8ad89b025df10247853cd4f62c92a5a3b54ea15d7c9fae5c769d4`;
  `git status --porcelain | sha256sum` =
  `206dba941c8215b520b3d3f5b313846bffccc7fc1c25e16588f9bf6dc2846fbc`.
  `version.txt` = 1.16.0 (não bumpado nesta fase; ver Decisions).

## Dispatch objective

Fase 1 da expansão do viewer `image` (BMP -> JPEG/PNG): implementar os decoders
puros e a inspeção/dispatch comum em `components/cyberdeck/{include,src}/apps/image/`,
com vendorização standalone estritamente necessária, licenças/proveniência e
integração CMake. APIs host-linkáveis, sem LVGL/ESP-IDF nos TUs puros, sem
biblioteca externa no host (C7); entrada por descriptor/`read_at`, decode por
MCU/linha, saída RGB565, alocação bounded injetável, cancelamento, nada parcial em
falha. Listagem/app/UI/help/documentação ampla **fora** desta fase.

Critério de saída: módulos puros testados (variantes JPEG/PNG aprovadas, erros,
memória, cancelamento essenciais), integração build host e licenças verificadas.
Nunca declarar pronto sem rodar os testes focados.

## Work done

### Vendorização (licença conferida na cópia exata)

| Lib | Origem exata | sha256 da cópia | Licença |
| --- | --- | --- | --- |
| miniz 3.0.2 | `https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip` (zip `2e5f4e3c…`? ver nota abaixo) | `miniz.c 0fcdc9888cb3a29ca8f176bac087e5fe6c7258a6ab06b1c271c1e109a11d3740`, `miniz.h 295d1a0041aea09609598c0f1f35c1977ca05ad662acbadcfdaac44c140af37b`, `LICENSE 0115478d567121238cf6cc1c0c361926cf07a49d9e4c9e66da97fac6a01646b3` | MIT (RAD Game Tools / Valve / Rich Geldreich): permissiva, uso pessoal e comercial, exige manter copyright+notice |
| TJpgDec R0.03 | `https://elm-chan.org/fsw/tjpgd/arc/tjpgd3.zip` (zip sha256 `052fe3efbc9a8be29f31597ad009c5b51a4f6905878eb28569e0ab3d46d0c013`) | `tjpgd.c 3a0091b6e1e6407f18633c17c19b5791b878d3ede4282005222bc41050e347a9`, `tjpgd.h 12d7b17c55929224d878b2b801750e32d437ce58c89bc87bd3f5c6d4151337a2` | ChaN (2021): "No restriction on use… personal, non-profit or commercial… Redistributions of source code must retain the above copyright notice" |

Achado relevante: a cópia dentro de `managed_components/lvgl__lvgl/src/libs/tjpgd`
está **modificada** (gating `LV_USE_TJPGD`, headers LVGL, reformatação; ~2294
linhas de diff contra o upstream). Não foi usada como fonte de licença/conteúdo.

Arquivos vendorizados: `components/cyberdeck/third_party/{miniz,tjpgd}/`.
`tjpgdcnf.h` vendorizado é uma **cópia modificada** (documentada em
`components/cyberdeck/THIRD_PARTY.md`): `JD_FORMAT 0→1` (RGB565),
`JD_USE_SCALE 1→0`, `JD_FASTDECODE 0→1`, `JD_SZBUF 512→1024`.

`miniz.c` é compilado com `MINIZ_NO_STDIO`, `MINIZ_NO_ARCHIVE_APIS` e
`MINIZ_NO_DEFLATE_APIS`: só o inflate (`tinfl`) e o CRC-32 entram no binário.

### Código (estado no worktree)

- `include/apps/image/cyberdeck_image_formats.h` + `src/.../cyberdeck_image_formats.cpp`
  (novos): limites únicos do viewer (`k_max_file_bytes`, `k_max_dimension`,
  `k_max_name_bytes`, `k_max_frame_bytes`), `image_format`,
  `format_from_name` (tabela única `.bmp/.jpg/.jpeg/.png`), `format_name`,
  `frame_bytes` (produto em 64 bits).
- `include/apps/image/cyberdeck_image_bmp.h` e `.../cyberdeck_image_list.h`:
  constantes compartilhadas passam a vir de `formats.h` (sem duplicação).
- `src/apps/image/cyberdeck_image_list.cpp`: `is_bmp_name` delega a
  `format_from_name` (uma única tabela; caminho de upload preservado).
- `include/apps/image/cyberdeck_image_decode.h`: `decode_status::unsupported_format`,
  `decode_result::reason`, `decode_status_reason`, overload
  `decode_descriptor(..., image_format, ...)` (o overload BMP continua para o
  chamador atual) e bloco de comentário com a pendência explícita da fase 2.
- `include/apps/image/cyberdeck_image_jpeg.h` + `src/.../cyberdeck_image_jpeg.cpp`
  (novos): `inspect_jpeg` com walker de marcadores (SOI/segmentos/SOF0/SOS),
  `jpeg_status` tipado, orçamento derivado `k_jpeg_pool_bytes` (5.504 B),
  `jpeg_decode_peak_bytes`, `decode_jpeg` com `jd_prepare`/`jd_decomp`, frame +
  pool em um bloco alinhado, cancelamento por MCU, sonda de trailer
  (EOI obrigatório; segundo SOS recusado) antes de publicar.

## Evidence

Nenhum teste, build ou flash executado nesta sessão. Os hashes acima foram
obtidos com `sha256sum` sobre os arquivos vendorizados; `git diff`/`git status`
com `sha256sum` para o snapshot. Todo o restante é inspeção de código.

## Decisions and assumptions

1. **4:1:1 recusado.** O TJpgDec R0.03 aceita apenas `0x11` (4:4:4), `0x21`
   (4:2:2) e `0x22` (4:2:0) — `jd_prepare` devolve `JDR_FMT3` para os demais e o
   upsampling é fixo em MCU de 8/16 px. Em vez de patch em terceiro, a
   implementação rejeita 4:1:1 com `jpeg_status::unsupported_sampling`. Isso
   **estreita o recorte aprovado** e precisa de aceite do developer/humano.
   Nenhum `TEST-IMGF-JPG-0x` depende de 4:1:1.
2. **`a.jpg.png`** resolve por extensão final (PNG), como `a.jpg.bmp` resolve
   BMP hoje. `TEST-IMGF-FMT-02` lista esse nome entre os rejeitados; implementar
   isso exigiria uma regra de "ponto único" que muda o comportamento BMP atual.
   Pendente de decisão do tester; divergência registrada na matriz.
3. **Trailing após IEND** aceito (`TEST-IMGF-PNG-08`), o que conflita com
   `NEG-IMGF-PNG-18` ("IDAT após IEND"); seguirei PNG-08.
4. **`version.txt` não bumpado** na fase 1: nenhum comportamento é alcançável
   pelo usuário até a fase 2 (app/listagem/UI). Incremento (MINOR + PATCH do
   manifesto) fica para a entrega funcional.
5. `JD_SZBUF` subiu para 1024 para aceitar um segmento DHT combinado de 4
   tabelas (716 B > 512). O pool derivado acompanha (`static_assert` no TU).

## Pending and risks

Pendente (fase 1, em ordem):

1. `cyberdeck_image_png.h/.cpp`: inflate incremental (`tinfl_decompress`, janela
   de 32 KiB) via `read_at`, reconstrução por linha (filtros 0..4), CRC de
   IHDR/PLTE/IDAT/IEND, `PLTE`/`tRNS`, limites (bomba/truncado), cancelamento
   por linha, scratch declarado.
2. `cyberdeck_image_decode.cpp`: dispatch por `image_format`, `reason` sempre
   preenchido, `decode_status_reason`.
3. `components/cyberdeck/CMakeLists.txt`: novos `.cpp` + os dois `.c`
   vendorizados com os defines de poda do miniz.
4. `tests/host/keymap/Makefile`: alvos focados (`test_image_formats`,
   `test_image_jpeg`, `test_image_png`), regra de compilação C dos vendorizados
   (sem `--coverage`, para não entrar no gcovr), `BINS`/`.PHONY`/agregado/`clean`.
5. Fixtures + oráculos independentes (host sem PIL; `cjpeg` libjpeg-turbo 3.2.1
   e `magick` 7.1.2-35 disponíveis; PNG pode ser gerado em Python com `zlib`).
6. Testes focados e execução; build de firmware só se o escopo fechar.
7. `components/cyberdeck/THIRD_PARTY.md` (notices/proveniência) e `code-map.md`.

Risco residual: nada foi compilado nem executado; o TU de JPEG é o primeiro corte
e pode revelar erros de compilação/interação com o TJpgDec.

## References

- `docs/agent-state/image-formats/{planner,tester}.md`
- `components/cyberdeck/include/apps/image/` e `src/apps/image/`
- `components/cyberdeck/third_party/{miniz,tjpgd}/`
- `tests/host/keymap/Makefile`, `tests/host/keymap/test_image_{bmp,decode}.cpp`
- `agent-toolkit/workflows/developer-workflow.md`, `templates/task-note.md`

## Updates

### 2026-11-08 (hora local) -03:00 - checkpoint de fase 1 (worktree parcial)

- Fact: vendorização concluída e licenças conferidas na cópia exata; módulos
  `formats` e `jpeg` criados; `decode.h` preparado para dispatch multi-formato.
  PNG, dispatch, CMake, Makefile, fixtures, testes focados, `THIRD_PARTY.md` e
  `code-map.md` ainda pendentes. Nenhum teste/build/flash executado.
- Supersedes or discards: substitui o retorno anterior sem nota; não há resultado
  de execução a herdar.

### 2026-11-09 13:5x -03:00 - fase 1 concluída em host (PNG, dispatch, wiring, testes focados)

- Fact: PNG incremental implementado (`cyberdeck_image_png.{h,cpp}`) com
  `tinfl`+janela de 32 KiB, staging de entrada comprimida, remontagem por linha
  (filtros 0..4), CRC de IHDR/PLTE/IDAT/IEND, PLTE/tRNS, bomba limitada e
  cancelamento por linha. `decode_descriptor(..., image_format, ...)` roteia
  BMP/JPEG/PNG. CMake e Makefile compilam as TUs novas e os dois `.c`
  vendorizados (com `$(CC)`, sem `--coverage`). Fixtures (33 arquivos + manifest)
  e gerador `tools/gen_image_fixtures.py` criados. `THIRD_PARTY.md` e
  `code-map.md` atualizados.
- Bug encontrado e corrigido no caminho: `tinfl_decompress` devolve em
  `*pIn_buf_size` a contagem **consumida** (não o resto); tratá-la como resto
  descartava bytes comprimidos e corrompia qualquer imagem maior que a janela.
  Regressão: `test_image_png` 1280x1280 + multi-IDAT.
- Segundo bug corrigido: `palette_entries` era copiado do IHDR (antes do PLTE),
  então todo PNG de paleta falhava com `invalid_palette_entry`.
- Commands | exit | head:
  - `make -C tests/host/keymap test_image_formats` | 0 | `48 checks, 0 failures`
  - `make -C tests/host/keymap test_image_jpeg` | 0 | `401 checks, 0 failures` (ASan+UBSan)
  - `make -C tests/host/keymap test_image_png` | 0 | `431 checks, 0 failures` (ASan+UBSan)
  - `make -C tests/host/keymap test_image_decode` | 0 | `50 checks, 0 failures`
  - `make -C tests/host/keymap test_image_list` | 0 | `1028 checks, 0 failures`
  - `make -C tests/host/keymap test_image_view` | 0 | `70 checks, 0 failures`
  - `make -C tests/host/keymap test_image_bmp` | 0 | `82 checks, 0 failures`
  - `make -C tests/host/keymap test_image_worker_state` | 0 | `788 checks, 0 failures`
  - `make -C tests/host/keymap test_image_decoders_contract` | 0 | `196 checks, 0 failures`
  - `make -C tests/host/keymap test_image_viewer_contract` | 0 | `0 failures` (após apontar os limites para `formats.h`)
  - `make -C tests/host/keymap test_image_stack_frame_contract` | 1 | falha
    esperada: ELF mais antigo que as fontes ("run idf.py build"); não é defeito
    de wiring.
- Decisions this round:
  - `a.jpg.png` resolve para PNG pela última extensão, como `a.jpg.bmp` resolve
    BMP hoje; endurecer para "ponto único" mudaria nomes BMP aceitos, logo NÃO
    altero o requisito nem o comportamento: fica registrado como pendência de
    decisão do tester (FMT-02 lista esse nome entre os rejeitados).
  - Trailing após IEND: bytes são ignorados e **não** processados (PNG-08);
    `NEG-IMGF-PNG-18` (IDAT após IEND) e PNG-08 não podem valer juntos quando os
    bytes finais formam um chunk. Coberto por
    `test_trailing_bytes_after_iend_are_ignored` (lixo e um IDAT completo após
    IEND mantêm o frame do arquivo que termina em IEND).
  - 4:1:1: NÃO estreitei em silêncio. O TJpgDec R0.03 aceita apenas
    `0x11/0x21/0x22` (`jd_prepare`) e o upsampling é fixo em MCU de 8/16 px, então
    suportar 4:1:1 exigiria patch em código de terceiro. O decoder rejeita com
    `unsupported_sampling` (teste fixa o comportamento) e a mudança de requisito
    está escalada no retorno ao developer; nada foi vendorizado fora das duas
    bibliotecas aprovadas.
- Pending: build de firmware (`idf.py build`) em execução para validar as TUs
  novas e desbloquear `test_image_stack_frame_contract`; fase 2 (listagem
  multiformato, app, UI, help, docs, upload) fora desta entrega; `version.txt`
  continua 1.16.0.

### 2026-11-09 14:3x -03:00 - build de firmware verde + pico de memória medido

- Fact: `idf.py build` (ESP-IDF, esp32p4) EXIT=0; `cyberdeck5.bin` 0x2a74a0
  (2.784.928 B), 34% livre na partição. Isso desbloqueia
  `test_image_stack_frame_contract` (rodado depois: 0 falhas; frames do ELF
  fail=352B, publish=208B, run_job=816B, start=224B, stop=352B,
  worker_entry=448B).
- Correções necessárias para o firmware compilar (todas mínimas e dentro da
  fase 1):
  - `cyberdeck_image_app.cpp`: o `switch (result.state)` existente não conhecia
    `decode_status::unsupported_format` (`-Werror=switch` no target). Adicionado
    o caso com `image: unsupported format` e a mensagem de `format` passou a usar
    `result.reason` (fonte única por formato).
  - miniz: o wrapper compatível com zlib (`mz_uncompress2`) compara `mz_ulong`
    com 0xFFFFFFFF, constante-falsa no alvo 32-bit (`-Werror=type-limits`).
    Resolvido incluindo `MINIZ_NO_ZLIB_APIS` nos defines de poda (CMake e
    Makefile), sem tocar no código vendorizado — os hashes seguem os mesmos.
- Memória/pico medidos (host, `-O2`, `size`): miniz `.text` 13.136 B (só inflate
  + CRC-32), TJpgDec `.text` 9.770 B, `cyberdeck_image_png.cpp` 10.438 B,
  `cyberdeck_image_jpeg.cpp` 6.168 B, `cyberdeck_image_formats.cpp` 543 B.
  Orçamentos declarados e derivados no código: JPEG frame + `k_jpeg_pool_bytes`
  (5.504 B); PNG frame + `png_scratch_bytes` (janela 32.768 + inflater 8.448 +
  staging 4.096 + 2 linhas + paleta 768 = 53.768 B no pior caso 1280 px),
  ambos independentes da altura e alocados em um único bloco via `decode_ops`.
- Commands | exit | head (após as correções):
  - `source ~/esp/esp-idf/export.sh && idf.py build` | 0 |
    `cyberdeck5.bin binary size 0x2a74a0 … (34%) free` / `Project build complete`.
  - `make -C tests/host/keymap test_image_stack_frame_contract` | 0 | `0 failures`.
  - `make -C tests/host/keymap test_image_{formats,jpeg,png,decode,list,view,bmp,worker_state}`
    | 0 | 48 / 401 / 431 / 50 / 1028 / 70 / 82 / 788 checks, 0 failures.
  - `make -C tests/host/keymap test_image_decoders_contract` | 0 | `197 checks, 0 failures`.
  - `make -C tests/host/keymap test_image_viewer_contract` | 0 | `0 failures`.
  - `git diff --check` | 0 | limpo.
- Not run: suíte host completa e gates globais de cobertura (fora do despacho da
  fase 1); nenhum flash; nenhuma validação física.
- Snapshot final desta rodada: HEAD `0f4ab3a3dd43`;
  `git diff | sha256sum` = `823afb4107f43faf31ef0383248dad8cde29c2aead3e53ece6fc91954cd7f08a`;
  `git status --porcelain | sha256sum` = `7483ecdb38836e4f89c6683dc3f047da7501fa156b22580f52ca204fded08a28`.
- Supersedes or discards: substitui o parágrafo "Pending" da entrada anterior
  quanto ao build (agora executado) e o valor do snapshot.
- Pronto para validação independente da fase 1 nos pontos: decoders puros,
  dispatch por formato, orçamentos declarados, cancelamento/nada-parcial,
  proveniência/licenças. Continua **fora** desta entrega (fase 2): listagem
  multiformato, app/UI/help/docs, upload HTTP, `version.txt`, fixtures
  adicionais do tester (fuzz maior, suíte ASan dos alvos de decode como gate de
  CI) e a decisão sobre 4:1:1.

### 2026-11-09 15:2x -03:00 - fase 2: integração app/listagem/help/docs (checkpoint)

- Fact: aplicados os deltas vigentes das notas do tester/planner antes de agir —
  última extensão decide o formato (`.jpg.png` → PNG), bytes após `IEND`
  ignorados e nunca interpretados, `IEND` ausente falha, JPEG 4:1:1 recusado
  (`NEG-IMGF-JPG-16`, já coberto). Nenhum caso superseded foi reintroduzido.
- Código:
  - `cyberdeck_image_list`: o filtro da listagem passa a ser `is_viewer_name`
    (tabela única de `format_from_name`); `is_bmp_name` fica só como predicado do
    upload HTTP (inalterado, `validate_upload_name` intacto).
  - `cyberdeck_image_app`: `execute()` aceita `.bmp/.jpg/.jpeg/.png` via
    `format_from_name` e recusa o resto com `image: invalid path` sem I/O;
    `run_job()` resolve o formato do nome listado e o passa ao novo overload de
    `decode_descriptor`; arquivo de 0 byte vira `image: empty file` (achado F1 do
    tester, antes do gate de tamanho); toda falha é composta de `result.reason`
    (fonte única por formato), eliminando os textos duplicados por `switch`.
  - Manifesto do app: versão `1.0.2` (PATCH), descrição "Foreground image viewer
    (BMP, JPEG, PNG)" e help citando as três extensões.
  - `version.txt`: `1.16.0` → `1.17.0` (MINOR de firmware; MAJOR intocado).
- Testes/contratos harmonizados:
  - `test_image_list`: listagem aceita o conjunto reconhecido (`.jpg/.jpeg/.png`
    incluídos, `.gif/.webp`/diretório com nome de imagem excluídos), pin com
    extensão suportada promovido como qualquer BMP, e novo teste de integração
    `test_mixed_directory_resolves_and_decodes_each_format` (listar → resolver
    formato pelo nome → decodificar BMP/JPEG 4:4:4/JPEG 4:2:0/PNG e recusar
    `.bmp` com bytes PNG). 1218 checks, 0 falhas.
  - `test_image_viewer_contract`: inclui os alvos novos no agregado, exige
    `format_from_name`/`image_format::none`/`image: empty file` no app,
    `is_viewer_name` na listagem, upload BMP-only, READMEs citando JPEG/PNG e
    roteiro manual com seção F e 4:1:1; pisos elevados (firmware 1.17.0, app
    1.0.2) sem fixar a versão exata.
- Docs: README.md/README.pt-BR.md (subconjunto suportado e recusas, upload
  BMP-only), docs/ARCHITECTURE{,.pt-BR}.md (envelope/dispatch/pool por formato),
  `tests/manual/image-viewer-validation.pt-BR.md` (seção F, F1..F20),
  `code-map.md` (linhas de app/listagem/dispatch e matriz REQ-IMGF).
- Pendente nesta checkpoint: `idf.py build` da fase 2 e a rodada final dos alvos
  focados depois do bump; nota final e snapshot.

### 2026-11-09 15:5x -03:00 - fase 2 concluída: integração, versionamento, docs e build

- Fact: fase 2 implementada e validada em host + build de firmware. Nenhum
  commit, nenhum flash, nenhuma suíte completa/gate amplo executados.
- Integração:
  - listagem multiformato por `is_viewer_name` (tabela única de
    `format_from_name`); `.gif/.webp`/diretório homônimo continuam fora;
  - app resolve o formato pelo nome listado, chama
    `decode_descriptor(..., format, ...)`, recusa 0 byte como `image: empty
    file` e compõe toda falha de `result.reason`;
  - help/manifesto do app atualizados (três extensões, versão `1.0.2`);
  - upload HTTP permanece BMP-only (`validate_upload_name` intocado, coberto por
    `test_control_upload_contract`);
  - `version.txt` `1.16.0` → `1.17.0`; MAJOR intocado; nenhum teste fixa a
    versão exata (pisos: firmware 1.17.0, app 1.0.2).
- Harmonizações exigidas pelos deltas vigentes: última extensão decide o formato
  (`a.jpg.png` → PNG, teste em `test_image_formats`), trailing após `IEND`
  ignorado e não interpretado (`test_image_png`), `IEND` ausente falha
  (`truncated_segment`), 4:1:1 recusado (`NEG-IMGF-JPG-16`).
- Documentação: README.md/README.pt-BR.md, docs/ARCHITECTURE{,.pt-BR}.md,
  `tests/manual/image-viewer-validation.pt-BR.md` (seção F, F1..F20; A7/E4
  corrigidos), `code-map.md` (visão geral, app, listagem, dispatch, matriz) e
  `components/cyberdeck/THIRD_PARTY.md`. Nenhuma menção obsoleta a "só BMP"
  restou nos documentos principais (varredura por `only .bmp`/`BMP file` vazia).
- Commands | exit | result:
  - `make -C tests/host/keymap test_image_formats` | 0 | 51 checks
  - `... test_image_jpeg` | 0 | 406 checks (ASan+UBSan)
  - `... test_image_png` | 0 | 467 checks (ASan+UBSan)
  - `... test_image_decode` | 0 | 50 checks
  - `... test_image_list` | 0 | 1218 checks (inclui TEST-IMGF-LIST-01/03/09)
  - `... test_image_view` | 0 | 70 checks (ASan+UBSan)
  - `... test_image_bmp` | 0 | 82 checks
  - `... test_image_worker_state` | 0 | 788 checks
  - `... test_image_decoders_contract` | 0 | 197 checks
  - `... test_image_viewer_contract` | 0 | 0 failures
  - `... test_image_stack_frame_contract` | 0 | 0 failures (ELF novo)
  - `... test_shell_help` | 0 | 249 checks; `... test_shell_help_contract` | 0 | PASS
  - `... test_system_apps_contract` | 0 | PASS
  - `... test_control_upload_contract` | 0 | PASS
  - `... test_app_runtime` | 0 | 226 checks
  - `source ~/esp/esp-idf/export.sh && idf.py build` | 0 |
    `cyberdeck5.bin binary size 0x2a7410 (34% free)`, `Project build complete`
  - `git diff --check` | 0 | limpo
- Snapshot final: HEAD `0f4ab3a3dd43` (sem commit, conforme despacho);
  `git diff | sha256sum` = `727d0353c6e6c9363b08a80c47d3419a3eea37cded1f62d7fa36ede4dec36903`;
  `git status --porcelain | sha256sum` = `09755ae04def41adb4ee1d4f2eda1fab798102e61f21d655f2556be2eea71390`;
  `version.txt` = `1.17.0`; manifesto image = `1.0.2`.
- Não executado (fora do despacho): suíte host completa, gates globais de
  cobertura/scope guard, fuzz ampliado, validação física/flash e atualização dos
  blocos `.opencode/memory/` (sem ferramenta de memória nesta sessão).
- Ajustes em testes que apenas espelham o manifesto (não enfraquecem asserção):
  `test_shell_help.cpp` (golden/fixture do help do `image`),
  `test_shell_help_contract.py` (fixture `IMAGE_USAGE` e descrição),
  `test_image_viewer_contract.py` (pisos e novas propriedades),
  `test_image_list.cpp` (conjunto reconhecido + integração lista→formato→decode;
  helper de fixture corrigido para um aviso real de `-Warray-bounds` do GCC 16).
- Supersedes or discards: substitui o parágrafo "Pendente nesta checkpoint" da
  entrada anterior e os valores de snapshot/versão de todas as entradas
  anteriores.

### 2026-11-09 16:2x -03:00 - D3 corrigido (claim morto removido) + F6/F7

- Fact (defeito D3 do tester): `image: empty file` era **inalcançável** —
  `cyberdeck_local_shell_open_readonly` rejeita `st_size <= 0` (EFBIG) e a
  listagem/pin usa o mesmo gate via `is_regular_file`, então um `.bmp/.png`
  vazio nunca entra na página. O contrato só exigia a substring no fonte
  (evidência fraca). Corrigi pela alternativa conservadora aprovada no despacho,
  **sem** tocar permissões/comportamento do shell genérico:
  - removido o branch `file_bytes == 0` e a mensagem dedicada do app; o seam de
    storage segue sendo a única autoridade sobre "não é arquivo regular/é vazio";
  - comportamento real contratado: vazio é inelegível na listagem e no pin;
    `image /dir/empty.png` com outras imagens → `image: not found in directory`;
    pasta só com o vazio → `image: no supported image in directory`;
  - comentário do `run_job()` junto do `listing.count == 0` registra por que a
    branch não existe.
- Regressão no nível mais baixo testável (listagem) — novo
  `test_image_list::test_empty_files_are_not_listed`: pasta só com
  `empty.bmp/.png/.jpg` → `count == 0`; pin de `empty.png` não promove; pasta
  mista → só a imagem real, `find_image` falso para os vazios; e o seam recusa
  `/empty.png` com `EFBIG`. `test_image_list` = 1231 checks, 0 falhas.
- Contrato reforçado (`test_image_viewer_contract.py`): proíbe a mensagem morta e
  o branch `file_bytes == 0`, exige `info.st_size <= 0` no seam (a razão real da
  inelegibilidade), exige `image: no supported image in directory`, e o roteiro
  manual precisa documentar o desfecho de 0 byte sem repetir a mensagem.
- F6 (achado): `is_bmp_name` não tem chamador de produção — upload valida o nome
  com `validate_upload_name` próprio. Mantida a API (usada por testes), mas os
  comentários em `cyberdeck_image_list.h` e `cyberdeck_image_decode.h` agora
  dizem a verdade (não é o gate do upload).
- F7: comentários de `run_job()` trocados de "eligible BMP"/"path/BMP/regular-size"
  para o conjunto de extensões reconhecidas.
- Docs: `tests/manual/image-viewer-validation.pt-BR.md` ganhou o caso **A10**
  (vazio inelegível, mensagens reais) e `code-map.md` registra o comportamento
  contratado em vez do claim morto.
- Commands | exit | result:
  - `make -C tests/host/keymap test_image_list` | 0 | 1231 checks
  - `... test_image_viewer_contract` | 0 | 0 failures
  - `... test_image_decoders_contract` | 0 | 197 checks
  - `... test_image_formats|jpeg|png|decode|bmp|view|worker_state` | 0 | 51/406/467/50/82/70/788 checks
  - `... test_shell_help` | 0 | 249 checks; `... test_shell_help_contract` | 0 | PASS
  - `... test_image_stack_frame_contract` | 0 | 0 failures (decode_png 784 B, decode_jpeg 288 B)
  - `source ~/esp/esp-idf/export.sh && idf.py build` | 0 | `cyberdeck5.bin` 0x2a73e0 (34% free), `Project build complete`
  - `git diff --check` | 0 | limpo
- Snapshot: HEAD `0f4ab3a3dd43` (sem commit);
  `git diff | sha256sum` = `3da6266e9f12cc95b2dba74c3e8915352a1a2eba1857cfffcccf0870cae86483`;
  `git status --porcelain | sha256sum` = `09755ae04def41adb4ee1d4f2eda1fab798102e61f21d655f2556be2eea71390`
  (o conjunto de caminhos untracked/modificados não mudou; só conteúdo);
  `version.txt` = `1.17.0`; manifesto image = `1.0.2` (formatos/limites/versionamento
  preservados).
- Pendente/limitação: mensagens e estado do app continuam sem prova executável no
  host (não linkável); a única prova observável do caso de 0 byte é o nível de
  listagem/seam, como o tester pediu. Sem flash, sem commit, sem suíte completa.
- Supersedes or discards: substitui o claim "0 byte → `image: empty file`" das
  entradas anteriores desta nota e encerra D3/F6/F7; snapshot/diff atualizados.

### 2026-11-09 16:5x -03:00 - blocker do reviewer corrigido (cancelamento precoce)

- Fact (blocker, `reviewer.md:29-33`): JPEG/PNG pre-cancelados faziam parse do
  header, leituras e alocação de frame+scratch antes do primeiro poll. Isso viola
  `NEG-IMGF-CANCEL-01` (cancelado desde o início ⇒ `cancelled`, nada alocado,
  `out` vazio) e gastava até ~3,3 MiB por pedido morto.
- Correção mínima: poll de `cancelled` no **começo** de `decode_jpeg` e
  `decode_png`, antes do gate de tamanho, de qualquer `read_at` e de qualquer
  `allocate`, devolvendo `decode_status::cancelled` com `reason` da tabela e
  `out` zerado. Aplicado também em `decode_bmp` (mesmo dispatcher/família de
  teste), porque a matriz é por item e não por formato — sem mudar semântica de
  sucesso, limites ou mensagens.
- Regressões determinísticas (seam testemunha):
  - `test_image_jpeg` `test_limits_and_seams`: cancelado antes do início →
    `cancelled`, `pixels == nullptr`, `pixel_bytes == 0`, **`g_reads == 0`**,
    **`g_allocations == 0`**, `g_freed == 0` (antes afirmava 1 alocação);
  - `test_image_png` `test_limits_seams_and_cancellation`: mesmas assertivas
    (antes só comparava frees com allocs);
  - `test_image_decode` (BMP) mantém `cancelled` + `pixels == nullptr` +
    `g_freed == g_allocations` e agora passa com zero alocação, sem mudar a
    asserção existente;
  - cancelamento **durante** o decode continua coberto
    (`g_cancel_after_reads`), e o caso de alocação recusada segue
    `allocation_failed` com zero alocações.
- `code-map.md`: linha do dispatcher registra o poll precoce nos três caminhos e
  aponta os testes que o provam.
- Commands | exit | result:
  - `make -C tests/host/keymap test_image_jpeg` | 0 | 408 checks (ASan+UBSan)
  - `... test_image_png` | 0 | 469 checks (ASan+UBSan)
  - `... test_image_decode` | 0 | 50 checks
  - `... test_image_list` | 0 | 1231 checks
  - `... test_image_view` | 0 | 70 checks
  - `... test_image_formats|bmp|worker_state` | 0 | 51 / 82 / 788 checks
  - `... test_image_decoders_contract` | 0 | 197 checks
  - `... test_image_viewer_contract` | 0 | 0 failures
  - `... test_shell_help_contract` | 0 | PASS
  - `... test_image_stack_frame_contract` | 0 | 0 failures
    (`decode_png` host 928 B, ELF 784 B; `decode_jpeg` 288 B)
  - `source ~/esp/esp-idf/export.sh && idf.py build` | 0 |
    `cyberdeck5.bin` 0x2a7420 (34% free), `Project build complete`
  - `git diff --check` | 0 | limpo
- Snapshot: HEAD `0f4ab3a3dd43` (sem commit);
  `git diff | sha256sum` = `0d534e5605b95bd9f7f6b70e35e562f0ab2c0fbaeb78319722f807bdce179506`;
  `git status --porcelain | sha256sum` = `09755ae04def41adb4ee1d4f2eda1fab798102e61f21d655f2556be2eea71390`;
  `version.txt` = `1.17.0`; manifesto image = `1.0.2` — formatos, limites e
  versionamento inalterados.
- Pendente/limitações: mensagens/estado do app seguem sem prova executável no
  host (TU não linkável); validação física/flash não executadas; sem suíte
  completa/gates amplos, conforme despacho.
- Supersedes or discards: substitui o comportamento de cancelamento descrito nas
  entradas anteriores (alocação antes do primeiro poll) e o snapshot/diff.

### 2026-11-09 17:4x -03:00 - triagem física: orientação REFUTADA, PNG paleta = ROM hijack do tinfl

- Fact 1 (orientação AC-IMG-01): **não há defeito de código**. As capturas salvas
  pela triagem (`/tmp/opencode/cd5-*.bmp`, `http-shot.bmp`, `serial-shot.bmp`)
  foram relidas por um leitor conforme a especificação BMP (`biHeight > 0` =
  linhas de baixo para cima, triplo **B,G,R**):
  - `cd5-p-png.bmp` x=640: y=146..385 = (255,0,0) **vermelho**, y=386..625 =
    (0,0,255) **azul** → topo vermelho/base azul = exatamente o host;
  - `cd5-o-bmp.bmp`: topo azul (0,0,255), base vermelha (255,0,0) = host;
  - `cd5-b-jpg.bmp` x=608: topo vermelho/base azul; x=672: topo verde/base
    branca = host (TL/TR/BL/BR preservados → sem inversão vertical **nem**
    troca R/B).
  A conclusão anterior de "inversão vertical" veio de ler os triplos como R,G,B:
  com fixtures vermelho-em-um-extremo/azul-no-outro, uma troca R↔B imita
  inversão vertical, e as amostras de verde/branco (invariantes à troca) não
  distinguem. Dois caminhos de captura concordam porque ambos são corretos.
  Nenhum hack visual foi aplicado.
- Fact 2 (PNG paleta `f.png` → `image: PNG decode failed`): **causa raiz
  confirmada e corrigida** — o firmware não usava o miniz vendorizado.
  `esp_rom/esp32p4/ld/esp32p4.rom.ld:88` faz `tinfl_decompress = 0x4fc000f8`
  (idem `tinfl_decompress_mem_to_*`, `mz_adler32`, `mz_free`); uma atribuição
  direta no linker script vence a definição do objeto, e o `nm` do ELF anterior
  mostrava `A 4fc000f8 tinfl_decompress` (absoluto = ROM). O código ROM é miniz
  2.x (`MZ_VER_MAJOR 9`, `tinfl_huff_table` por valor embutido em
  `m_tables[3]`), enquanto nossos headers são 3.0.2 (tabelas embutidas
  diretamente no `tinfl_decompressor`, sem `m_tables`) — ABI incompatível:
  alguns streams decodificavam "por sorte" (p.png) e outros falhavam (f.png),
  com risco de corrupção dentro do bloco de scratch.
  Correção: `third_party/miniz/cyberdeck_miniz_names.h` prefixa os símbolos
  (`cyberdeck_tinfl_decompress`, `cyberdeck_mz_crc32`, `cyberdeck_mz_adler32`,
  `cyberdeck_mz_free`, `..._mem_to_*`) e `third_party/miniz/miniz_cyberdeck.c`
  é a única unidade de build (inclui o prefixo e depois `miniz.c`, que continua
  byte-idêntico). `cyberdeck_image_png.cpp` inclui o prefixo antes de `miniz.h`;
  CMake e Makefile compilam o shim. ELF novo: `T 400768c2
  cyberdeck_tinfl_decompress` (fora da faixa ROM 0x4fc0_xxxx).
- Regressões acrescentadas (sem enfraquecer oráculo):
  - `test_image_capture_orientation.cpp` (novo alvo no agregado): frame RGB565 →
    `screenshot_bmp_rgb565_to_bgr888` → BMP bottom-up → leitor independente
    conforme spec; linha 0 continua topo e R/G/B não trocam, inclusive para um
    frame vindo do decoder BMP real. Fixa o invariante que a triagem leu errado.
  - `test_image_png::test_palette_128_bands` + fixture `png_palette_128.png`
    (128x128, tipo 3, 4 faixas): reproduz a forma do arquivo que falhou no
    dispositivo e verifica ordem das faixas.
  - `test_image_decoders_contract.py`: proíbe compilar `miniz.c` cru, exige o
    header de prefixo com os 4 símbolos, o shim, a ordem de include no PNG e o
    alvo novo no agregado.
  - `test_image_stack_frame_contract.py`: lê o ELF (`riscv32-esp-elf-nm`) e falha
    se `cyberdeck_tinfl_decompress`/`cyberdeck_mz_crc32` não estiverem no nosso
    objeto **ou** se algum símbolo da lista ROM aparecer como código.
- Docs: `THIRD_PARTY.md` (hazard + prefixo + shim), `code-map.md` (linha do
  vendor e novo teste), roteiro manual (como ler capturas: BGR + `biHeight > 0`
  bottom-up; passo F4b do PNG paleta 128x128).
- Commands | exit | result:
  - `make -C tests/host/keymap test_image_png` | 0 | 500 checks
  - `... test_image_capture_orientation` | 0 | 50 checks
  - `... test_image_jpeg` | 0 | 408; `... test_image_decode` | 0 | 55
  - `... test_image_list` | 0 | 1231; `... test_image_view` | 0 | 70
  - `... test_image_formats` | 0 | 51; `... test_image_bmp` | 0 | 82;
    `... test_image_worker_state` | 0 | 788
  - `... test_image_decoders_contract` | 0 | 212
  - `... test_image_viewer_contract` | 0 | 0 failures
  - `... test_image_stack_frame_contract` | 0 | 0 failures
    (`elf miniz binding: vendored inflater linked, no ROM tinfl_decompress`)
  - `source ~/esp/esp-idf/export.sh && idf.py build` | 0 | `cyberdeck5.bin`
    0x2a89c0 (34% free), `Project build complete`
  - `git diff --check` | 0 | limpo
- Snapshot: HEAD `0f4ab3a3dd43` (sem commit);
  `git diff | sha256sum` = `2aa7816a1e37a91e198c508adb2dcac22e35f46fb5c0fda76df6ca82a196c2b0`;
  `git status --porcelain | sha256sum` = `a5e6d548fe448c50cf987c1cf668bd66427fcdb16af0b8f0393165d5dc266301`;
  `version.txt` = `1.17.0`; bin local `9cbc468500863beb…` (sha256 completo no
  comando acima). Formatos/limites/versionamento inalterados.
- Pendente: revalidação física (sem flash nesta sessão, conforme despacho) para
  confirmar no Tab5 que `f.png` e o PNG paleta 128x128 decodificam e que as
  capturas lidas corretamente mostram orientação/canais iguais ao host. Sem
  suíte completa/gates amplos.
- Supersedes or discards: substitui a hipótese de "inversão vertical no caminho
  device→lv_image" (refutada com as próprias capturas) e o diagnóstico em aberto
  do PNG paleta (agora ROM hijack, corrigido).

### 2026-11-09 15:44 -03:00 - RESET DE SESSÃO + checkpoint da navegação recuperável

- Fact (reset): a sessão anterior deste papel foi interrompida por limite de
  passos e **não persistiu nota** da fase de navegação recuperável. Nenhum
  resultado de execução da sessão anterior é herdado como evidência; a nota
  anterior (17:4x, ROM hijack) continua válida apenas para aquele delta.
- Code state medido nesta entrada: HEAD `0f4ab3a3dd43`;
  `git diff | sha256sum` = `84d18543f65568611d2700cdaa621feb9605427088d0652567eceebba9d08e8a`;
  `git status --porcelain | sha256sum` =
  `a90b19b8d9adcf30dd85bab95eb1d722b66ce7449f72a7f86906aba17e5b5af3`;
  `version.txt` = 1.17.0; manifesto `cyberdeck.image_viewer` = 1.0.2;
  `git tag` = 0 tags (nenhuma versão liberada por tag).
- Já presente no worktree (inspeção, não prova de execução):
  - `cyberdeck_image_navigation.{h,cpp}` puros (C8/C9): disposição
    origem×ok/failed/cancelled×current, `chrome_after_publish`,
    `chrome_after_error`, `chrome_after_accept`, `navigation_target`;
  - app integrado (`disposition`, `fail_initial`, `fail_recoverable`,
    `take_recoverable_error`, `navigate(direction, target*)` com D11/C12);
  - view com `error_`, `apply_chrome`, `drop_frame` (repoint antes de liberar),
    `clear_error`, acessores `position_text`/`error_text`/`has_error` (C10);
  - UI com `image_navigate`, `s_image_chrome`, consumo do canal recuperável sem
    switch/append, e `classify_image_key(..., s_image_view.exists(), ...)`;
  - testes `test_image_navigation` (221 linhas, 58 checks na execução anterior
    não reprovada nesta sessão) e acréscimos em `test_image_view` (102 checks na
    execução anterior);
  - contrato `test_image_viewer_contract.py` parcialmente ajustado (CMake com
    `cyberdeck_image_navigation.cpp`, checagem `by_index` sem `list_images`),
    MAS ainda sem `test_image_navigation` na lista de alvos/agregado, sem as
    asserções do canal tipado UI↔app e com a checagem antiga de stale gate em
    `fail()` — que agora delega a `fail_initial`/`fail_recoverable`, logo o
    contrato tende a falhar até ser atualizado.
- Pendente (escopo aprovado): completar matriz C8..C13 (list/key ownership,
  canal inicial vs recuperável, generation/stale, setas rápidas, cancel/stop no
  que é host-testável), atualizar `test_image_stack_frame_contract.py` para a TU
  nova, docs (`code-map.md`, READMEs, ARCHITECTURE{,.pt-BR}, manual F21..F27),
  versionamento (sem tag; D14 → MINOR firmware + PATCH do app), testes focados e
  build. Sem suíte completa, cobertura global, flash ou commit.
- Supersedes or discards: registra o reset e substitui a ausência de nota da
  sessão interrompida; não revoga nenhum achado anterior.

### 2026-11-09 16:04 -03:00 - F8/F9 fechados (comentário de posse + ordem do clear)

- Fact: correção apenas dos dois deltas aprovados pelo tester
  (`tester.md:1385-1400`, snapshot `9cf22da0…`). Versão **inalterada** no release
  ainda não taggeado: `version.txt` 1.18.0, manifesto image 1.0.3.
- F8 (documentação stale): comentário de `classify_image_key` em
  `cyberdeck_image_list.h` agora descreve a regra real — consumo enquanto o
  viewer é foreground **e a superfície existe** (frame publicado **ou** cromo de
  erro recuperável), sem superfície o teclado do shell fica livre, `Esc` segue
  consumido. Nenhuma mudança de código/comportamento.
- F9 (risco baixo, caminho degradado): em `application::request()` a limpeza de
  `recoverable_error_`/`has_recoverable_error_` saiu de antes do handoff e passou
  para **depois** do aceite real de `worker_.enqueue()`. Se o enqueue é rejeitado
  (worker não vivo), a função retorna `false` antes de qualquer limpeza, então o
  cromo de erro exibido continua com seu canal. Token/generation/worker
  preservados: `generation_counter_`/`pending_generation_`/`state_ = loading` e o
  `enqueue()` continuam na mesma seção crítica de `request_mutex_`; a limpeza
  segue sendo a única seção aninhada request→publication e ocorre antes de o
  worker poder consumir o job (ele precisa de `request_mutex_` em `next()`).
- Contrato reforçado (`test_image_viewer_contract.py`, C11/F9): exige exatamente
  uma limpeza de cada campo em `request()`, `worker_.enqueue()` **antes** do
  clear, e que o trecho da rejeição (`if (worker_.enqueue())`) contenha
  `return false;` sem limpar o canal. Mutação (clear antes/dentro do ramo de
  rejeição) falha o contrato. TU do app não é linkável no host, então a ordem é
  provada estruturalmente, como os demais gates dessa TU.
- `code-map.md`: linha do app registra que a limpeza só ocorre com pedido aceito
  pelo `enqueue` (rejeição preserva erro/cromo); linha da listagem explicita a
  posse de teclado pela superfície (com ou sem frame).
- Commands | exit | result (snapshot do cabeçalho desta entrada, `date` medido):
  - `make -C tests/host/keymap test_image_viewer_contract` | 0 | `0 failures`
  - `... test_image_navigation` | 0 | `78 checks, 0 failures` (ASan+UBSan)
  - `... test_image_list` | 0 | `1247 checks, 0 failures`
  - `... test_image_view` | 0 | `108 checks, 0 failures` (ASan+UBSan; inclui as
    asserções do tester)
  - `... test_image_formats|decode|bmp` | 0 | `51 / 55 / 82 checks`
  - `... test_image_decoders_contract` | 0 | `212 checks`
  - `source ~/esp/esp-idf/export.sh && idf.py build` | 0 | `cyberdeck5.bin`
    0x2a9570 (33% free), `Project build complete`
  - `... test_image_stack_frame_contract` | 0 | `0 failures` — host navigation
    largest 272 B; ELF `fail_initial=368B`, `fail_recoverable=384B`,
    `navigate=448B`, `run_job=800B`, budget 6144 B; miniz binding OK
  - `git diff --check` | 0 | limpo
- Snapshot final: HEAD `0f4ab3a3dd43` (sem commit);
  `git diff | sha256sum` = `cb017eac4028392852b15af22a3b384164a0a65efcbf9acee58dc3271a79cb01`;
  `git status --porcelain | sha256sum` =
  `a90b19b8d9adcf30dd85bab95eb1d722b66ce7449f72a7f86906aba17e5b5af3` (conjunto de
  caminhos inalterado; só conteúdo); `build/cyberdeck5.bin` sha256
  `68556364154dc0eb4d334392007356c653706e58aaf93da12dad5a0f7de50fcf` (2.790.768 B,
  16:03), sem fonte mais nova que o binário.
- Não executado (fora do despacho): suíte completa, gates globais de
  cobertura/scope guard, flash e validação física F21..F27; `test_image_jpeg`/
  `test_image_png` não foram reexecutados (TUs de decoder não tocadas nesta
  rodada). Sem commit.
- Supersedes or discards: substitui o estado "F8/F9 abertos" implícito no
  snapshot anterior desta nota e os hashes/diff anteriores; mantém aprovada a
  matriz host do tester e o versionamento 1.18.0/1.0.3. Nenhum outro escopo é
  revogado.

### 2026-11-09 16:24 -03:00 - gate de CI cobre os arquivos novos do viewer (preflight)

- Fact: fechado o blocker de preflight do reviewer (`reviewer.md:92-100`) —
  `.github/workflows/quality-gate.yml` não incluía os arquivos novos nas listas
  de clang-format/cppcheck. Correção é de cobertura de gate, sem mudança de
  comportamento de produção/teste e sem bump de versão (1.18.0 / app 1.0.3).
- Workflow (`.github/workflows/quality-gate.yml`):
  - `clang-format --dry-run --Werror` da lista do viewer ganhou
    `cyberdeck_image_formats.{h,cpp}`, `cyberdeck_image_jpeg.{h,cpp}`,
    `cyberdeck_image_png.{h,cpp}`, `cyberdeck_image_navigation.{h,cpp}`,
    `cyberdeck_image_source.h`, `test_image_formats.cpp`, `test_image_jpeg.cpp`,
    `test_image_png.cpp`, `test_image_navigation.cpp` e
    `test_image_capture_orientation.cpp`. Ordem alfabética dentro de
    headers/fontes/testes; entradas antigas preservadas.
  - Nova invocação `cppcheck --enable=warning,performance,portability
    --error-exitcode=1 --suppress=missingIncludeSystem
    --suppress=uninitMemberVarNoCtor` para
    `cyberdeck_image_{formats,jpeg,png,navigation}.cpp`. O suppression cobre só
    o falso positivo de agregados (`extension_entry`, `chunk_walk`), que têm
    membros inicializados em toda construção; a invocação existente
    (BLE/time-sync) e `bandit`/checks de segurança/status ficaram intactos.
  - Política de vendor documentada em comentário: `third_party/` miniz/TJpgDec
    não é formatado nem lintado (cópias upstream; proveniência/hashes seguem em
    `test_image_decoders_contract`).
  - Contratos Python (`test_image_viewer_contract.py`,
    `test_image_stack_frame_contract.py`) não entram em clang-format/cppcheck
    (não se aplicam) e permanecem cobertos pelo agregado `test` do CI, que já os
    executa; nenhum escopo Python existente foi ampliado/afrouxado.
- Formatação (somente whitespace, sem mudança de lógica): os arquivos novos
  estavam fora do padrão (ex.: jpeg.cpp 24 linhas de violação, test_image_png 99,
  test_image_jpeg 69, capture_orientation 12, jpeg.h/png.h 6/3), então
  `clang-format -i` foi aplicado só em código first-party. Também foi formatado o
  que a entrega deixou não conforme em arquivos **já gateados**
  (`cyberdeck_image_decode.{h,cpp}`, `test_image_list.cpp`, `test_image_view.cpp`),
  senão o step existente falharia. Nenhum arquivo em `third_party/` foi tocado.
- `code-map.md`: linha do workflow/CI documenta a cobertura nova (format integral
  do viewer, cppcheck dos decoders, exclusão deliberada de vendor, contratos
  Python no agregado) e a seção do viewer referencia o gate.
- Commands | exit | result (checagens focadas, sem suíte completa/gate amplo/CI):
  - `clang-format --dry-run --Werror` sobre a lista completa do viewer
    (headers/fontes/testes acima, incluindo os já gateados) | 0 | sem violações
    (clang-format local 23.1.4; CI fixa 23.1.3 — diferença de patch registrada)
  - `cppcheck --enable=warning,performance,portability --error-exitcode=1
    --suppress=missingIncludeSystem --suppress=uninitMemberVarNoCtor` nos 4
    decoders | 0 | sem achados (cppcheck local 2.22.0)
  - parse YAML do workflow via PyYAML do venv ESP-IDF | 0 | `Quality Gate`, 11
    steps (mesmo número de antes)
  - `bash -n` nos 9 blocos `run:` extraídos | 0 | sintaxe OK
  - checagem de membership/existência dos caminhos citados | 0 | nenhum ausente;
    nenhuma referência a `third_party` nas listas
  - `make -C tests/host/keymap test_image_list` | 0 | 1247 checks
  - `... test_image_view` | 0 | 108 checks (ASan/UBSan)
  - `... test_image_capture_orientation` | 0 | 50 checks
  - `... test_image_jpeg` | 0 | 408 checks (ASan/UBSan)
  - `... test_image_png` | 0 | 500 checks (ASan/UBSan)
  - `... test_image_decode` | 0 | 55 checks
  - `source ~/esp/esp-idf/export.sh && idf.py build` | 0 | `cyberdeck5.bin`
    0x2a9570 (33% free), `Project build complete`
  - `git diff --check` | 0 | limpo
- Observação de execução: duas rodadas de teste chegaram a rodar em paralelo e o
  gcov emitiu "overwriting an existing profile data with a different checksum"
  nos sidecars; os cenários foram reexecutados isoladamente e os resultados acima
  são os das execuções sequenciais. Nenhuma conclusão de cobertura foi tirada
  daqui.
- Snapshot final: HEAD `0f4ab3a3dd43` (sem commit);
  `git diff | sha256sum` = `b6d47ca8f2c0f5f1ad174dba3b0335cc591fcc2ece1396550c272d27b6ba1072`;
  `git status --porcelain | sha256sum` =
  `fd7a6c2344dfea738121c6336c85b51cc7b7421736fc7bc234e1e2b881ee5d6a` (mudou porque
  o workflow agora está modificado); `version.txt` 1.18.0; manifesto 1.0.3;
  `build/cyberdeck5.bin` sha256
  `1701f334cfc32834324dd9c534e2c55d2ee149172f1fa389e0f0dd40ea3107c3` (2.790.768 B),
  sem fonte mais nova que o binário.
- Não executado (fora do despacho): suíte completa, gates globais de
  cobertura/scope guard, TSan, CI dispatch, flash/física. Sem commit.
- Supersedes or discards: encerra o blocker de preflight do reviewer para o
  snapshot anterior (`cb017eac…`) e substitui os hashes/diff. Não revoga a
  matriz aprovada, F8/F9, decisões de versão nem a pendência física F21..F27.

### 2026-11-09 17:07 -03:00 - CI: agregado host após o build (stack contract fail-closed)

- Fact: fechado o blocker de ordem do reviewer (`reviewer.md:114-128`, snapshot
  `b6d47ca8…`). Em checkout limpo, `make test` rodava antes do setup/build ESP-IDF
  e `test_image_stack_frame_contract` (incluído no agregado) falhava com
  `FAIL: build/cyberdeck5.elf or riscv32-esp-elf-nm not available`. Alteração é
  só de pipeline/documentação: nenhum código, teste ou versão mudou (1.18.0 /
  app 1.0.3).
- Workflow (`.github/workflows/quality-gate.yml`): ordem dos steps passou a
  `Format` → `Lint` → `TSan` → setup ESP-IDF → `Prepare ESP-Hosted CPU-staging` →
  `Verify LVGL symbol code point map` → `Build firmware` → `Host aggregate and
  coverage` (com gcovr/scope guard) → `Job summary`.
  - O agregado host agora roda depois do build, então o ELF e o toolchain RISC-V
    existem para o stack contract.
  - Para não reintroduzir o problema conhecido do `as` cruzado sombrear o
    binutils host (motivo pelo qual o passo host rodava antes), o próprio passo
    host filtra do `PATH` somente as entradas `*/.espressif/*`, `*/esp/esp-idf/*`
    e `*riscv32-esp-elf*`; mantém `/usr/bin`, `$HOME/.local/bin` (pipx/gcovr) e o
    python do sistema.
  - O contrato continua fail-closed e não foi enfraquecido/pulado: localiza
    `riscv32-esp-elf-nm`/objdump via glob em `~/.espressif` (independente de
    PATH) e exige o ELF por caminho; sem rebuild ele falha como antes.
  - TSan permanece antes do setup IDF; staging/LVGL/build mantiveram a ordem
    relativa e as dependências (`managed_components` pelo warm-up, patch do
    ESP-Hosted antes do build). Coverage/scope guard e thresholds inalterados.
  - O comentário do passo LVGL que dizia "o agregado roda antes do warm-up" foi
    corrigido; o restante dos checks de segurança/status não foi tocado.
- `code-map.md`: linha do workflow/CI documenta a nova ordem, o filtro de PATH e
  o motivo (stack contract exige ELF/nm; `as` cruzado quebraria o build host),
  incluindo a mensagem de fail-closed; a seção do viewer referencia a ordem.
- Commands | exit | result (wiring/foco, sem suíte completa/cobertura/CI):
  - parse YAML via PyYAML do venv ESP-IDF | 0 | `Quality Gate`, 11 steps
  - extração dos blocos `run:` + `bash -n` nos 9 blocos | 0 | sintaxe OK
  - asserção de ordem (format<lint<TSan<install<staging<build<host) e de que o
    `export PATH="$HOST_PATH"` precede `make clean`/`make test` | 0 | wiring OK
  - simulação do filtro com PATH sintético contendo `.espressif`/`esp-idf`/
    `riscv32-esp-elf` | 0 | PATH final manteve `/usr/bin` e `$HOME/.local/bin`,
    removeu o toolchain cruzado; `g++`/`make`/`python3` são os do host
  - `make -C tests/host/keymap test_image_stack_frame_contract` com PATH
    filtrado e ELF presente | 0 | `0 failures` (ELF `run_job=800B`,
    `fail_recoverable=384B`; miniz vendorizado)
  - mesmo alvo com `build/cyberdeck5.elf` temporariamente movido, PATH filtrado |
    2 | `FAIL: build/cyberdeck5.elf or riscv32-esp-elf-nm not available` —
    reproduz o finding e confirma fail-closed; ELF restaurado no mesmo comando
  - `make -C tests/host/keymap test_image_viewer_contract` | 0 | `0 failures`
  - `git diff --check` | 0 | limpo
- Snapshot final: HEAD `0f4ab3a3dd43` (sem commit);
  `git diff | sha256sum` = `aa815af2b80166a5a12e78fdc34c29ab68840a7f949de19f72804ad5fddea15c`;
  `git status --porcelain | sha256sum` =
  `fd7a6c2344dfea738121c6336c85b51cc7b7421736fc7bc234e1e2b881ee5d6a` (mesmo
  conjunto de caminhos da entrada anterior; só conteúdo do workflow/code-map);
  `version.txt` 1.18.0; manifesto 1.0.3; `build/cyberdeck5.bin` sha256
  `1701f334cfc32834324dd9c534e2c55d2ee149172f1fa389e0f0dd40ea3107c3`
  (inalterado, sem fonte mais nova); `build/cyberdeck5.elf` presente/restaurado.
- Não executado (fora do despacho do coder): suíte completa, cobertura/scope
  guard, TSan, gates de lint/format completos, CI dispatch, flash/física. Sem
  commit/push.
- Supersedes or discards: encerra o blocker de ordem do workflow apontado em
  `reviewer.md:123-127` e substitui o diff hash anterior; não revoga nenhum gate
  local já registrado pelo reviewer nem a exigência de CI aplicável e validação
  física antes do aceite final.

### 2026-11-09 17:57 -03:00 - commit+push autorizados do snapshot revisado (CI remoto)

- Decisão/override humano: o usuário autorizou explicitamente commit e push para
  executar o CI remoto do snapshot revisado. Escopo não reaberto: nenhum arquivo
  de produção/teste/versão foi alterado nesta etapa; apenas o commit do working
  tree já revisado e o push.
- Snapshot confirmado **antes** de publicar (nenhuma alteração posterior ao
  revisado): HEAD `0f4ab3a3dd43`; `git diff | sha256sum` =
  `aa815af2b80166a5a12e78fdc34c29ab68840a7f949de19f72804ad5fddea15c`;
  `git status --porcelain | sha256sum` =
  `fd7a6c2344dfea738121c6336c85b51cc7b7421736fc7bc234e1e2b881ee5d6a`;
  `git status --porcelain -z | sha256sum` =
  `1eb8fb4da459dce2b2070809283decb9da1fcc3d93a5f6f6f2ad69b781d8045f` (idêntico
  ao status medido pelo reviewer). `version.txt` 1.18.0; manifesto 1.0.3.
  Inventário preservado: 48 entradas / 92 arquivos staged (fontes, testes,
  fixtures, third_party vendorizado, docs, notas de agente, workflow); nenhum
  artefato de build/cobertura/vendor-objeto staged.
- Commit: `cf56d9005444e4893254232ac8c3f8b1844933a1` (short `cf56d90`), branch
  `develop`, mensagem
  `feat(image): add JPEG/PNG decoding and recoverable navigation` com corpo
  listando decoders/vendor, navegação recuperável, testes/contratos, gate e bump
  1.18.0/1.0.3. Push sem tag.
- Push: `git fetch origin develop` mostrou `origin/develop` em `0f4ab3a` e
  fast-forward (`git merge-base --is-ancestor` OK); `git push origin develop`
  | exit 0 | `0f4ab3a..cf56d90  develop -> develop`. Verificação posterior:
  `git ls-remote origin refs/heads/develop` = `cf56d9005444e4893254232ac8c3f8b1844933a1`
  (= HEAD local); `git status -sb` limpo e sem ahead/behind; `git tag --points-at
  HEAD` = 0. Nenhum force push.
- Estado após evidência: o worktree voltou limpo no commit; a **única** alteração
  local posterior é este próprio append em `docs/agent-state/image-formats/coder.md`
  (não commitado/não enviado nesta autorização).
- Não executado: acompanhamento/polling do GitHub Actions (sem credenciais/CLI CI;
  o run é disparado pelo push e deve ser verificado pelo reviewer/developer), novo
  build/flash, suíte/gates locais, commit/push adicional.
- Supersedes or discards: substitui o estado "sem commit/push" das entradas
  anteriores desta nota e registra o override humano. A validação física
  F21..F27 e o aceite final continuam pendentes; o CI remoto passa a ser a
  evidência a conferir para o commit `cf56d90`.

### 2026-11-09 18:20 -03:00 - CI run 42697867174: correção do write no teste de orientação

- Fact: o usuário obteve os logs com `gh` para o run **42697867174** (`Quality
  Gate`, event push, `head_sha` = `cf56d9005444e4893254232ac8c3f8b1844933a1`). A
  etapa `Host aggregate and coverage` falhou (exit 2) porque
  `tests/host/keymap/test_image_capture_orientation.cpp:207` fazia
  `(void)::write(descriptor, bmp_bytes.data(), bmp_bytes.size())`; com
  `-Werror=unused-result` o GCC do runner rejeita
  `ignoring return value of 'ssize_t write(...)' declared with attribute 'warn_unused_result'`.
  Não era defeito de produção: o mesmo teste passava no host local (GCC sem o
  mesmo diagnóstico) e o gate remoto só revelou a diferença.
- Correção (somente teste, sem produção/versão): novo helper `write_all()` no
  próprio TU grava o buffer inteiro em loop, retentando escrita curta e `EINTR`;
  erro real ou `write() == 0` imprime o motivo, o cenário registra `CHECK(false)`,
  fecha/remove o temporário e retorna sem decodificar um arquivo parcial. Nenhum
  cast para `void`, nenhum suppression/pragma e nenhum afrouxamento de asserção.
  O teste passou de 50 para 51 checks (o `CHECK(wrote_all)` novo).
- Commands | exit | result (focados, sem suíte/build/flash):
  - `make -C tests/host/keymap test_image_capture_orientation` | 0 | `51 checks,
    0 failures` (compila com `-Wall -Wextra -Werror` e ASan/UBSan)
  - `... test_image_viewer_contract` | 0 | `0 failures`
  - `... test_image_decoders_contract` | 0 | `212 checks, 0 failures`
  - `clang-format --dry-run --Werror tests/host/keymap/test_image_capture_orientation.cpp`
    | 0 | sem violações
  - `git diff --check` | 0 | limpo
- Commit/push da correção (autorização de commit+push já dada para obter CI;
  sem force): `7aa1187057614599244c302e5f4e94e258ca607a` (short `7aa1187`),
  `fix(test): handle partial writes in capture orientation test`, pai
  `cf56d900…`, branch `develop`. `git fetch origin develop` mostrou
  `origin/develop` em `cf56d90` e fast-forward; `git push origin develop` | exit
  0 | `cf56d90..7aa1187  develop -> develop`; `git ls-remote origin
  refs/heads/develop` = `7aa1187057614599244c302e5f4e94e258ca607a` (= HEAD
  local); `git tag --points-at HEAD` = 0. Nenhum force push.
- Preservação de alterações do usuário: as notas pós-commit
  `docs/agent-state/image-formats/{coder,reviewer}.md` (incluindo o registro do
  run 42697867174 feito pelo reviewer) permanecem **não commitadas** e não foram
  alteradas por esta correção; nenhum arquivo de produção/versão foi tocado.
- Snapshot (medido imediatamente antes deste append): HEAD
  `7aa1187057614599244c302e5f4e94e258ca607a`; `version.txt` 1.18.0;
  `git diff | sha256sum` =
  `a6af95f4f03acc656845da9e9468b74cb014a4ac5ea1e438b9b20e815972f45f`;
  `git status --porcelain | sha256sum` =
  `c414d7e93c3037ec845c6f9fd065fd51dd0974f9c2fe86c6d99a9b7fe1963cad`;
  `git status --porcelain -z | sha256sum` =
  `7426c35ec50154acf4078bcf1c95d6ec29bd225d9107e095aefe1caf68c0c452`. Depois
  deste append, a única diferença adicional é esta própria nota.
- Não executado: suíte completa, gates de cobertura, TSan, build/flash; o novo
  run remoto do commit `7aa1187` ainda precisa ser observado pelo
  reviewer/developer antes de qualquer conclusão de CI.
- Supersedes or discards: substitui o estado "correção do CI pendente" implícito
  na entrada anterior e registra o commit/push da correção; **não** declara o
  gate remoto verde nem resolve o blocker do run `42697867174` até novo run no
  commit `7aa1187`.

### 2026-11-09 18:32 -03:00 - CI run 42701467880: PATH do toolchain para o stack contract

- Causa (log via `gh`): run **42701467880**, workflow `Quality Gate`, `head_sha`
  exatamente `7aa1187057614599244c302e5f4e94e258ca607a`. Todos os testes de
  imagem passaram; a etapa `Host aggregate and coverage` falhou em
  `test_image_stack_frame_contract` porque o passo removia **todas** as entradas
  `*/.espressif/*` do PATH e o runner não tinha o toolchain em `~/.espressif`
  (instalação em outro root), então `shutil.which('riscv32-esp-elf-nm')` e o
  fallback por glob falhavam com `build/cyberdeck5.elf or riscv32-esp-elf-nm not
  available`. O contrato estava correto (fail-closed); o wiring do workflow era
  incompleto.
- Correção (somente pipeline/docs; nenhum código/teste/versão): a etapa host
  monta o PATH do host primeiro (sem ESP-IDF/`riscv32-esp-elf`), resolve os bin
  dirs do toolchain com `python3 "$IDF_PATH/tools/idf_tools.py" export --format
  key-value` (com fallback via `idf.py` e, por último, `command -v
  riscv32-esp-elf-nm/objdump`) e acrescenta esses dirs **depois** dos bins do
  host. Dois guards fail-closed no próprio passo: `command -v as` precisa ser
  `/usr/bin/as` (senão aborta com `host assembler is not first in PATH`) e
  `riscv32-esp-elf-nm` precisa resolver (senão `riscv32-esp-elf-nm not found
  after PATH setup`). O teste/stack contract e seus critérios não mudaram.
- `code-map.md`: linha do workflow/CI e seção do viewer atualizadas para a nova
  descoberta via `idf_tools.py`, ordem host→cross bins e guards fail-closed.
- Commands | exit | result (validação delimitada, sem suíte/build/flash):
  - parse YAML (PyYAML do venv ESP-IDF) | 0 | `Quality Gate`, 11 steps
  - `bash -n` nos 9 blocos `run:` extraídos + prefixo da etapa host | 0 | OK
  - prefixo real da etapa + `test_image_stack_frame_contract` (IDF_PATH real) |
    0 | `host as=/usr/bin/as`, `cross nm=/home/moises/.espressif/tools/riscv32-esp-elf/esp-15.2.0_20251204/riscv32-esp-elf/bin/riscv32-esp-elf-nm`,
    contrato `0 failures`
  - mesmo teste com `HOME` falso (sem `~/.espressif`) e `IDF_TOOLS_PATH` real |
    0 | reproduz o cenário do runner (toolchain fora do HOME): mesmos guards e
    contrato `0 failures`
  - controle negativo: `HOME` falso, sem `IDF_TOOLS_PATH` e sem cross no PATH |
    1 | `riscv32-esp-elf-nm not found after PATH setup` (fail-closed)
  - prefixo real + compilação de probe C++ com `g++` no PATH resultante (HOME
    falso, `IDF_TOOLS_PATH` real) | 0 | `host g++ compile OK (as=/usr/bin/as, nm
    resolved)` — prova que o `as` cruzado não sombreia o binutils do host
  - `git diff --check` | 0 | limpo
- Commit/push (autorização humana de commit+push continua válida; sem force):
  `5d2630e7cd4f39fa53e1f9fb47242f0c99185ebb` (short `5d2630e`),
  `fix(ci): keep the riscv toolchain discoverable for the stack contract`, pai
  `7aa1187`, branch `develop`. `git fetch origin develop` → `origin/develop` em
  `7aa1187`, fast-forward; `git push origin develop` | exit 0 |
  `7aa1187..5d2630e  develop -> develop`; `git ls-remote origin
  refs/heads/develop` = `5d2630e7cd4f39fa53e1f9fb47242f0c99185ebb` (= HEAD local);
  `git tag --points-at HEAD` = 0. Nenhum force push.
- Preservação: as notas `docs/agent-state/image-formats/{coder,reviewer}.md`
  continuam não commitadas, incluindo o registro do reviewer para o run
  `42697867174`; não foram editadas nesta correção.
- Snapshot (medido imediatamente antes deste append): HEAD
  `5d2630e7cd4f39fa53e1f9fb47242f0c99185ebb`; `version.txt` 1.18.0;
  `git diff | sha256sum` =
  `6d0650a789e56976f22fc88f3beb171a50c572eb2f06539e955907cc5a9ac1dc`;
  `git status --porcelain | sha256sum` =
  `c414d7e93c3037ec845c6f9fd065fd51dd0974f9c2fe86c6d99a9b7fe1963cad`;
  `git status --porcelain -z | sha256sum` =
  `7426c35ec50154acf4078bcf1c95d6ec29bd225d9107e095aefe1caf68c0c452`. Depois
  deste append, a única diferença adicional é esta própria nota.
- Não executado: suíte completa, cobertura/scope guard, TSan, build/flash e
  polling do novo run; o CI remoto do commit `5d2630e` precisa ser observado
  pelo reviewer/developer antes de qualquer conclusão.
- Supersedes or discards: substitui o estado "wiring do PATH pendente" e estende
  a entrada 18:20; **não** declara o gate remoto verde nem fecha o blocker do run
  `42701467880` até novo run no commit `5d2630e`.

### 2026-11-09 18:45 -03:00 - CI run 42703506289: exporter vira sondagem tolerante

- Causa (log via `gh`, salvo em `/tmp/opencode/image-ci-42703506289-failed.log`;
  reviewer `reviewer.md:165-173`): run **42703506289** no commit
  `5d2630e7cd4f39fa53e1f9fb47242f0c99185ebb`. A etapa `Host aggregate and
  coverage` saiu 1 imediatamente após o bloco `env`, antes do `echo host as=`,
  `make clean` e do agregado. O último comando registrado é a atribuição
  `IDF_EXPORT_PATH=$(python3 .../idf_tools.py export --format key-value ... |
  sed ...)`; com `bash -e` + `set -o pipefail`, o status não-zero do exporter
  (stderr suprimido em `/dev/null`) abortou o step antes dos fallbacks/guards.
  Ambiente do runner no log: `IDF_PATH=/tmp/esp/idf/v6.1/esp-idf`,
  `IDF_TOOLS_PATH=/tmp/esp`, `IDF_PYTHON_ENV_PATH=/tmp/esp/python/v6.1/venv`,
  `SYSTEM_PATH` sem bin dir do toolchain — logo o fallback por `command -v`
  sozinho também não bastaria.
- Correção (só workflow/docs): (1) o exporter virou **sondagem tolerante**,
  executado com `$IDF_PYTHON_ENV_PATH/bin/python` quando presente (senão
  `python3`), com `2>` para arquivo temporário e `|| IDF_EXPORT_STATUS=$?`; em
  falha imprime status e as primeiras linhas do stderr e zera a saída, sem
  abortar; (2) fallback de dirs passa a procurar
  `$IDF_TOOLS_PATH/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin` e
  `~/.espressif/tools/...`, antes do fallback final por `command -v`; (3) guards
  fail-closed inalterados: `/usr/bin/as` precisa vencer e `riscv32-esp-elf-nm`
  precisa resolver. Nenhum teste/produção/versão alterado.
- `code-map.md`: linha do workflow/CI e seção do viewer atualizadas para a
  sondagem tolerante + fallbacks + guards.
- Commands | exit | result (focados, sem suíte/build/flash; prefixo extraído do
  YAML e executado com o contrato):
  - parse YAML | 0 | `Quality Gate`, 11 steps; `bash -n` em todos os blocos
    `run:` | 0 | OK
  - caso 1, exporter real (`IDF_PATH` local) + contrato | 0 |
    `host as=/usr/bin/as`, cross nm resolvido, `test_image_stack_frame_contract:
    0 failures`
  - caso 2, tipo runner: `IDF_PATH` fake com exporter saindo 7, `HOME` fake,
    `IDF_TOOLS_PATH` fake contendo nm/objdump linkados | 0 |
    `idf_tools.py export failed (status=7); using installed tool dirs` + stderr
    diagnóstico, fallback de dirs, contrato `0 failures`
  - caso 3, exporter falha e cross só via PATH | 0 | fallback `command -v`,
    contrato `0 failures`
  - caso 4, sem nm (exporter falha, tools root vazio, HOME fake) | 1 |
    `riscv32-esp-elf-nm not found after PATH setup`
  - caso 5, `as` não-host primeiro | 1 |
    `host assembler is not first in PATH: /tmp/opencode/qg-fake-as/as`
  - `git diff --check` | 0 | limpo
- Commit/push (autorização vigente; sem force): `89ac382e8cf4dfb33e5bd1b908033185af4d6737`
  (short `89ac382`), `fix(ci): make the idf_tools export probe non-fatal`, pai
  `5d2630e`, branch `develop`. `git fetch origin develop` → `origin/develop` em
  `5d2630e`, fast-forward; `git push origin develop` | exit 0 |
  `5d2630e..89ac382  develop -> develop`; `git ls-remote origin
  refs/heads/develop` = `89ac382e8cf4dfb33e5bd1b908033185af4d6737` (= HEAD local);
  `git tag --points-at HEAD` = 0. Nenhum force push.
- Preservação: as notas `docs/agent-state/image-formats/{coder,reviewer}.md`
  continuam não commitadas (incluindo o registro do reviewer para
  `42703506289`), sem edição nesta correção.
- Snapshot (medido imediatamente antes deste append): HEAD
  `89ac382e8cf4dfb33e5bd1b908033185af4d6737`; `version.txt` 1.18.0;
  `git diff | sha256sum` =
  `763328bb93c97e0e0479fcc80a090b457531d874b84cd579e89c6729034816a9`;
  `git status --porcelain | sha256sum` =
  `c414d7e93c3037ec845c6f9fd065fd51dd0974f9c2fe86c6d99a9b7fe1963cad`;
  `git status --porcelain -z | sha256sum` =
  `7426c35ec50154acf4078bcf1c95d6ec29bd225d9107e095aefe1caf68c0c452`. Depois
  deste append, a única diferença adicional é esta própria nota.
- Não executado: suíte completa, cobertura/scope guard, TSan, build/flash e
  polling do novo run; o CI remoto do commit `89ac382` precisa ser observado
  pelo reviewer/developer antes de qualquer conclusão.
- Supersedes or discards: substitui o estado "exporter aborta o step" e estende
  as entradas 18:20/18:32; **não** declara o gate remoto verde nem fecha o
  blocker do run `42703506289` até novo run no commit `89ac382`.

### 2026-11-09 15:50 -03:00 - segundo reset (limite de passos) + checkpoint medido da navegação

- Fact (reset 2): a sessão anterior deste papel foi interrompida de novo por
  limite de `steps`; ela aplicou produção/app/view/UI/testes/docs/versão no
  worktree **depois** da entrada de 15:44, mas não gravou nota nem executou
  validação. Nada desta sessão anterior é herdado como evidência.
- Code state medido agora (`date` = `2026-11-09 15:50 -0300`), HEAD `0f4ab3a3dd43`:
  - `git diff | sha256sum` =
    `104fd042db0f3a14674f59104d7d2f9f5921a7de9dacde52f1612b8e3351da00`;
  - `git status --porcelain | sha256sum` =
    `a90b19b8d9adcf30dd85bab95eb1d722b66ce7449f72a7f86906aba17e5b5af3`
    (conjunto de caminhos igual ao de 15:44; só conteúdo mudou);
  - `version.txt` = `1.18.0` (era 1.17.0), `cyberdeck.image_viewer` = `1.0.3`
    (era 1.0.2), `git tag` = 0 (nenhuma versão liberada por tag, logo MINOR/PATCH
    automáticos são legítimos).
- Arquivos alterados nesta entrega (o que a sessão interrompida deixou pronto,
  verificado por inspeção): `cyberdeck_image_navigation.{h,cpp}` (92+73 linhas,
  C8/C9/C12), `cyberdeck_image_app.{h,cpp}` (`disposition`, `fail_initial`,
  `fail_recoverable`, `take_recoverable_error`, `navigate` + `navigation_target`),
  `cyberdeck_image_view.{h,cpp}` (`apply_chrome`, `drop_frame` com repoint,
  `clear_error`, `has_error`/`error_text`/`position_text`), `cyberdeck_ui.cpp`
  (`image_navigate`, `s_image_chrome`, consumo do canal recuperável,
  `classify_image_key(..., exists(), ...)`), `cyberdeck_image_list.{h,cpp}`,
  `CMakeLists.txt`, `Makefile` (`IMAGE_NAVIGATION_*`, `test_image_navigation`
  em `BINS`/`.PHONY`/agregado/`clean`), `test_image_navigation.cpp` (281 linhas),
  `test_image_view.cpp`, `test_image_list.cpp`, `test_image_decode.cpp`,
  `test_image_viewer_contract.py`, `test_image_stack_frame_contract.py`,
  `test_shell_help{,_contract}`, READMEs/ARCHITECTURE/code-map/THIRD_PARTY,
  manual com F21..F27, `version.txt`.
- Pendente: **execução** de todos os alvos focados do despacho e do build de
  firmware (era exatamente o que faltava), mais qualquer correção que as
  execuções exigirem. Sem suíte completa, cobertura global, flash ou commit.
- Supersedes or discards: substitui o valor de snapshot/versão da entrada de
  15:44 e o "Pendente" dela; não revoga nenhum achado anterior.

### 2026-11-09 15:53 -03:00 - execução focada da navegação: 12 alvos verdes + build (2 defeitos de contrato corrigidos)

- Fact: todos os alvos focados do despacho e o build de firmware passaram. Duas
  falhas iniciais eram **defeitos do contrato de teste** (texto de produção pinado
  de forma frágil), não da produção; corrigi só o texto do contrato, sem afrouxar
  a matriz.
- Defeitos corrigidos (todos em `tests/host/keymap/test_image_viewer_contract.py`):
  1. `process_image_status` era extraído com `ui.find("void process_image_status(")`,
     que casava a **declaração antecipada** (linha 325), não a definição (linha
     1178). O corpo ficava com 612 B e sem canal recuperável. Agora ancora na
     definição (`void process_image_status(lv_timer_t *) {`).
  2. `disposition()` exigia literalmente `generation != pending_generation_...`;
     a produção compara com `==` dentro de `current`. Trocado por regex de
     propriedade (`generation\s*(==|!=)\s*pending_generation_\.load\(`), sem
     aceitar ausência da comparação.
  3. O loop exigia `disposition(value` em `fail_initial`, mas a classificação
     vive em `fail()` (que roteia `fail_recoverable`/`fail_initial`). O loop
     passou a exigir em `publish`/`fail`/`fail_recoverable` e **acrescentei**
     asserção mais forte em `fail_initial`: re-checagem de geração **e**
     `cancel_.load(` (C13), que antes não era verificada nesse método.
  - `code-map.md`: a linha da matriz REQ-IMGF-06..08 agora registra "comparação de
    geração em `disposition()`/`fail_initial()` (com `cancel_`)".
- Commands | exit | result (rodada final consolidada, tudo incremental):
  - `make -C tests/host/keymap test_image_navigation` | 0 | 78 checks, 0 failures
  - `... test_image_view` | 0 | 102 checks, 0 failures (ASan+UBSan)
  - `... test_image_list` | 0 | 1247 checks, 0 failures
  - `... test_image_viewer_contract` | 0 | 0 failures
  - `... test_image_stack_frame_contract` | 0 | 0 failures (host chain 160 B,
    budget 6144 B; ELF fail=16B, fail_initial=368B, fail_recoverable=384B,
    navigate=448B, publish=368B, run_job=800B, start=240B, stop=368B,
    worker_entry=448B, decode_jpeg=288B, decode_png=784B; miniz vendorizado)
  - `... test_image_decoders_contract` | 0 | 212 checks, 0 failures
  - `... test_image_decode` | 0 | 55 checks
  - `... test_image_jpeg` | 0 | 408 checks (ASan+UBSan)
  - `... test_image_png` | 0 | 500 checks (ASan+UBSan)
  - `... test_image_bmp` | 0 | 82 checks; `... test_image_formats` | 0 | 51 checks
  - `... test_shell_help` | 0 | 249 checks; `... test_shell_help_contract` | 0 |
    PASS (parse: 8 mutações rejeitadas)
  - `source ~/esp/esp-idf/export.sh && idf.py build` | 0 |
    `cyberdeck5.bin` 0x2a9580 (2.790.784 B, 33% livre), `Project build complete`;
    sha256 = `f2d832cb358cfe3b23a2c7990398abd9c315f877f770f289afc01dcdddd755c9`
  - `git diff --check` | 0 | limpo
- Ordem importa: a primeira rodada de `test_image_stack_frame_contract` falhou
  (`stale build/cyberdeck5.elf: newer sources cyberdeck_image_app.cpp,
  cyberdeck_image_navigation.{cpp,h}` + `fail_initial`/`fail_recoverable`
  ausentes do disassembly); refeita **depois** do `idf.py build` e ficou 0
  falhas, como o próprio contrato de fail-closed exige.
- Correções de estado necessárias na rodada: gcov reclamou
  `overwriting an existing profile data with a different checksum` no binário de
  navegação (sidecar `.gcda` da sessão interrompida). Removi **apenas**
  `test_image_navigation_bin-*.gcda`; a reexecução ficou limpa. Nenhum `clean`
  global foi executado.
- Versionamento verificado (sem tag, conforme despacho): `git tag` = 0;
  `version.txt` = `1.18.0` (MINOR sobre 1.17.0), manifesto
  `cyberdeck.image_viewer` = `1.0.3` (PATCH sobre 1.0.2); contrato usa
  `MIN_VERSION = (1, 18, 0)` e piso de app `1.0.3` (formato + piso, sem pin da
  versão exata). Nenhuma menção obsoleta a 1.17.0/1.0.2 restou em READMEs,
  ARCHITECTURE, code-map, contrato ou manual.
- Snapshot final: HEAD `0f4ab3a3dd43` (sem commit, conforme despacho);
  `git diff | sha256sum` =
  `e9a4070af8d72d3b4457e5ed097fac7e87f18f8dd68a7b885832901085bf3546`;
  `git status --porcelain | sha256sum` =
  `a90b19b8d9adcf30dd85bab95eb1d722b66ce7449f72a7f86906aba17e5b5af3`
  (mesmo conjunto de caminhos do checkpoint de 15:50; só conteúdo mudou).
- Defeitos/limitações em aberto (nenhum bloqueante para revisão):
  - validação física MAN-01..07 / F21..F27 **não executada**: setas não são
    injetáveis pela ponte serial e não houve flash nesta sessão;
  - a TU `cyberdeck_image_app.cpp` continua não linkável no host, então
    página/índice preservados, ordem no `process_image_status` e ausência de
    terminal no erro navegacional têm prova **estrutural** (contrato) e não
    executável;
  - suíte host completa, gates globais de cobertura/scope guard e fuzz ampliado
    fora do despacho, não executados;
  - 4:1:1 continua recusado (`unsupported_sampling`) por limitação do TJpgDec
    R0.03 — decisão de requisito registrada na entrada de 13:5x, não reaberta.
- Supersedes or discards: substitui o "Pendente" do checkpoint de 15:50 (testes e
  build agora executados) e o valor de `git diff | sha256sum`; não revoga nenhum
  achado anterior.
