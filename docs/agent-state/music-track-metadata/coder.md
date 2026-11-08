# coder - music-track-metadata

- Role: coder
- Updated: 2026-11-07 (ciclo 11: correção documental do estado do aceite `1.15.2`
  em `docs/03-audio-player-plan.pt-BR.md`)
- Code state: `develop` HEAD `ff47b9a31c8d8a1fdb5746c60013619cf75c7481` + worktree
  sujo não commitado. Snapshot do ciclo 10 (delta **gate + docs**, sem tocar
  produção ID3/USB):
  - `git diff | sha256sum` = `b50bb288625625e23d213880a49ee0e31df9881fc5022a9bd998ce60c07f9292`;
  - `git status --porcelain | sha256sum` =
    `a139ace2a7c2a309ac87c74c7eb7dd6a51dd08855d2c3ee0fd8f50f9e51af944`
    (28 entradas: 24 `M` + 4 untracked, incluindo esta nota e os testes novos);
  - arquivos alterados neste ciclo (sha256 do conteúdo, 16 hex):
    `tools/coverage_scope_guard.py` `201a1c21aad218f9`,
    `tests/host/keymap/test_coverage_scope_guard.py` `23c600fb72a95a99`,
    `tests/manual/music-player-validation.pt-BR.md` `1dd3c3b251c9c034`,
    `tests/host/keymap/music_traceability.md` `9e64baef5270c0ea`,
    `code-map.md` `3ab7efb592d630eb`;
  - delta do ciclo 9 (correção do padding) preservado: snapshot anterior
    `fc54a8dd…2885a09` (26 entradas); `version.txt` = `1.15.2` e manifesto
    `cyberdeck.music` = `1.0.4` (não tocados neste ciclo).
- Snapshot do ciclo 11 (delta **somente documental**, um parágrafo do plano;
  nenhuma TU, teste ou manifesto tocado):
  - `git diff | sha256sum` =
    `76dd091f9126a352012f3387d280874338152c12bd65d9eb094458d23ab66605`;
  - `git status --porcelain | sha256sum` = `a139ace2…51af944` (mesmas 28
    entradas do ciclo 10, sem arquivo novo);
  - `docs/03-audio-player-plan.pt-BR.md` sha256 `98f488d88dc13436`.
- Snapshots anteriores: `30530d23…828025` (ciclo 5, entrega de álbum/UI),
  `3e4cbfa1…d1566b58` (ciclo 6, antes do bump de versão e dos documentos),
  `f6c2d78a…a875b239` (ciclo 7) e `8743eb38…6154c5` (recebido do tester).


## Reset e continuação registrados

- **Ciclo 10 (este): sessão coder reaberta pelo developer** depois de o
  reviewer bloquear o fechamento por (a) o gate de cobertura falhar num TU de
  hardware preexistente e (b) o manual/rastreabilidade afirmarem que `1.15.2`
  ainda não tinha build/flash/aceite. **Autorização humana registrada**: o
  usuário autorizou explicitamente uma exceção de coverage estreita, limitada ao
  TU preexistente `components/cyberdeck/src/platform/usb/cyberdeck_usb_audio_host.cpp`,
  com justificativa e teste que garanta o conjunto exato — sem harness amplo, sem
  mexer nos limites/thresholds e sem alterar a produção ID3 ou a USB. O aceite
  físico foi **reportado pelo usuário** em `1.15.2`; não há medição independente
  do custo de scan (`TEST-MUSIC-META-PHYS-SCAN-COST`).
- **Ciclo 9 (anterior): sessão coder reiniciada pelo developer** para a correção
  pós-reviewer do blocker `AC-MUSIC-META-27` (padding após zero inicial não
  validado). O reset evita o viés das hipóteses dos ciclos 1–8; esta nota mantém
  o histórico anterior como referência, mas só o delta do ciclo 9 é evidência
  corrente. Escopo fechado pelo despacho: produção dos dois parsers, casos
  negativos/positivos nos dois alvos, `test_music_model` e
  `test_music_metadata_io` apenas; sem suíte, build, flash ou commit.
- A sessão inicial deste coder foi reiniciada pelo developer porque o plano foi
  **reaprovado** pelo usuário: a hipótese de que a tag do MP3 real do usuário
  cabia na janela de 64 KiB foi refutada no ensaio físico, e o novo envelope
  aprovado é ID3v2 com até **1 MiB totais** (header + payload) lido por offsets
  no mesmo `fd`, pulando APIC/frames desconhecidos sem carregar artwork.
- A nota anterior refletia a entrega de identificação/álbum (fw `1.15.1`, app
  `cyberdeck.music` `1.0.3`). Ela ficou **stale** para este plano e foi reescrita
  no ciclo 6; o resumo histórico ficou em "Histórico (ciclo 5)".
- Ciclo 6 (continuação após **teto de passos**): a produção streaming e os testes
  de parser/IO já existiam, `test_music_model` passava (361/361) e
  `test_music_metadata_io` falhava com 175/182 (7 asserts); o delta corrigiu só
  essas falhas e fechou o critério "io verde + model verde".
- Ciclo 8 (continuação anterior, docs-only): o developer informou que o tester
  adicionou `test_music_id3_contract.py` (contrato estrutural, PASS), que as
  mutações M1'–M12 estão todas detectadas/restauradas, que houve correção
  test-only no wiring de dependência do Makefile e que C1/C2 foram detectadas.
  O escopo aqui é **só documentação** (`code-map`, `music_traceability`,
  `player_traceability`, plano e manual) e esta nota; produção e testes não foram
  tocados e nada foi executado além da leitura e do próprio contrato (Python,
  read-only).
- Ciclo 7 (continuação anterior): o delta verificado pelo tester liberou o
  fechamento (`test_music_metadata_io` 190/190, `test_music_model` 361/361,
  `test_music_player` 598/598, `test_music_app` 438/438, M2–M6 detectadas e
  fontes restauradas). O escopo passou a ser **somente** versões, fixture/golden
  do shell help e documentação; nenhuma alteração de produção além do manifesto.

## Dispatch objective

REQ-MUSIC-META-06 / AC-MUSIC-META-23..30 (nota do tester, ciclo 5): ler o ID3v2
até 1 MiB por offsets no mesmo descritor, decodificar apenas TIT2/TPE1/TALB (e
`TT2/TP1/TAL` em v2.2) com buffers por payload limitados, e rejeitar a tag
inteira (all-or-nothing) quando inválida, acima do teto, com short read/erro de
I/O ou falha de alocação — mantendo o fallback de basename no player. Sem alterar
playback, formatos aceitos, comandos, layout ou dependências.
Ciclo 6: `test_music_metadata_io` verde e `test_music_model` continua verde.
Ciclo 7 (este): aplicar `version.txt` `1.15.2` e manifesto `1.0.4`, reconciliar
fixture/golden do shell help, atualizar `code-map.md`,
`docs/03-audio-player-plan.pt-BR.md`,
`tests/manual/music-player-validation.pt-BR.md`, `music_traceability.md` e
`player_traceability.md`, varrer `1.15.1`/`1.0.3`/`64 KiB`/`65536` classificando
histórico vs superado; sem suíte, build, flash ou playback.
Ciclo 9: corrigir o blocker `AC-MUSIC-META-27` apontado pelo reviewer em
`development` — padding só pode ser aceito se todo o resto da área de frames
declarada for zero, nos dois caminhos (parser puro e leitor streaming), mantendo
I/O bounded, teto e janela; reproduzir antes de corrigir e adicionar negativos
(zero + lixo → rejeita e preserva `out`) e positivo (padding zero) em
`test_music_model` e `test_music_metadata_io`. Executar **somente** esses dois
alvos; sem suíte/build/flash/commit.
Ciclo 10 (este): fechar os dois blockers do reviewer sem reabrir o ID3 — (a)
incluir **apenas** o TU de hardware preexistente `cyberdeck_usb_audio_host.cpp`
na allowlist do `tools/coverage_scope_guard.py`, com justificativa explícita da
dependência BSP/ESP-IDF/FreeRTOS/USB Host/UAC, e ajustar
`test_coverage_scope_guard.py` para garantir o conjunto exato (nenhum outro TU
`platform/usb/` na exceção; serializador puro segue coberto no host); (b)
corrigir o estado obsoleto de
`tests/manual/music-player-validation.pt-BR.md` §3.3.1 e o espelho em
`music_traceability.md` para registrar o aceite **reportado pelo usuário** em
`1.15.2`, sem alegar observação independente e mantendo o custo de scan como
risco residual não medido. Atualizar `code-map.md`. Rodar **somente** o alvo do
scope guard, o contrato de documentação ID3 e `git diff --check`; sem suíte,
build, flash ou commit.

## Work done

### Gate de cobertura (allowlist USB) e registro do aceite reportado (ciclo 10)

Autorização humana: o usuário aprovou uma exceção de coverage **estreita** para
o TU de hardware preexistente, com justificativa e teste que garanta o conjunto
exato; sem harness amplo, sem relaxar limites e sem tocar produção ID3/USB.

- `tools/coverage_scope_guard.py`: **uma** entrada nova em `ALLOWLIST` para
  `components/cyberdeck/src/platform/usb/cyberdeck_usb_audio_host.cpp`, com
  justificativa da dependência BSP ESP32-P4/USB Host/`usb_host_uac`/FreeRTOS,
  registro da autorização de 07/11/2026 e a informação de que o serializador
  puro (`cyberdeck_usb_audio_json.cpp`) continua coberto no host e o wiring é
  fixado por `test_usb_audio_host_contract.py`. Nenhuma outra entrada, nenhum
  threshold e nenhuma regra de descoberta foram alterados; o TU continua sendo
  descoberto (`--source-root`) e o guard segue fail-closed para qualquer outro
  TU ausente do JSON.
- `tests/host/keymap/test_coverage_scope_guard.py`: conjunto revisado ganhou
  `USB_AUDIO_ALLOWLIST = {USB_AUDIO_HOST}` (entra em `REVIEWED_ALLOWLIST` e na
  expectativa exata de TEST-COV-001). Novo **TEST-COV-013** deriva do próprio
  guard todos os allowlisted sob `platform/usb/` e exige igualdade com o
  conjunto de um TU, confirma que o TU existe e é descoberto, que não é
  forbidden, que o serializador puro **não** está na allowlist, que o alvo host
  continua compilando o serializador real e que a justificativa cita
  dependência de hardware. Novo **TEST-COV-014** prova, em fixture, que
  allowlistar o serializador coberto falha fechado. TEST-COV-008 passou a
  remover também a entrada USB (sem ela, o guard exige cobertura inexistente).
  Nenhum assert existente foi removido ou enfraquecido.
- `tests/manual/music-player-validation.pt-BR.md` §3.3.1: o parágrafo "ainda
  precisa de build/flash" foi substituído pelo estado real — `1.15.1`
  reproduziu o bug; `1.15.2` foi compilado/gravado por USB (imagem
  `f6c4ba7f…`, 2.740.496 B, app `0x29d110`) com `sys.info`
  `fw_version=1.15.2`, `safe_mode=false`, `interrupted_boots=0`, uptime
  monotônico — e o usuário **relatou** “funcionou tudo corretamente” na mesma
  faixa real. Registrei explicitamente que é aceite **reportado**, não
  observação independente (sem `screen.dump`/`ui.dump` arquivado, sem `ffprobe`
  lado a lado, sem repetição), que `-OVERCAP`/`-ORDER`/`-COMPACT` não têm
  relato individual e que `TEST-MUSIC-META-PHYS-SCAN-COST` segue **sem
  medição** (~5350 `pread` de 196 B no pior caso teórico de padding no teto =
  risco residual calculado). A frase final de §3.3.1 deixou de dizer que a
  entrega "permanece em `manual_validation`" e passou a listar o que falta como
  evidência independente.
- `tests/host/keymap/music_traceability.md`: o bloco stale "1.15.2 ainda
  precisa… não há aceite físico" foi substituído pelo mesmo registro do manual;
  removi a duplicação literal do parágrafo "Validação física permanece
  pendente" (aparecia duas vezes seguidas) mantendo uma única versão correta.
- `code-map.md`: linha do TU USB Host/UAC agora declara que ele é a **única**
  exceção allowlisted (autorização estreita de 07/11/2026, TEST-COV-013/014);
  linha dos testes USB cita os dois testes novos; linha do gate/workflow cita a
  exceção estreita; a frase que pinava "allowlist aprovada de 16 TUs" (já stale
  antes deste ciclo) passou a descrever **conjunto exato** sem contagem literal,
  incluindo o adaptador USB.

### Correção do blocker AC-MUSIC-META-27 (ciclo 9, pós-reviewer)

Defeito (achado do reviewer em development, reproduzido por mim): um byte `0x00`
no início da área restante do frame area encerrava a caminhada em ambos os
parsers sem validar o resto. Tag com frame de texto válido + `0x00` + lixo
não-zero publicava os metadados (violava o all-or-nothing).

- `cyberdeck_music_model.cpp::parse_tag`: removida a flag `padding` que pulava a
  validação; o `break` no byte zero agora só encerra a caminhada e a varredura
  `for (i = offset; i < end; ++i) if (p[i] != 0) return false;` roda **sempre**,
  cobrindo tanto o resto curto (< frame header) quanto a área aberta por um zero.
  É varredura do buffer já em memória — sem I/O, sem alocação.
- `cyberdeck_music_metadata.cpp::read_metadata`: `read_padding` (leitura única do
  resto) substituída por `read_zero_padding`, que valida `[offset, tag_bytes)` em
  blocos de no máximo `k_id3_read_window_bytes` usando a janela fixa já alocada.
  A área de padding passou a ser efetivamente lida e conferida; short read/erro
  continua rejeitando. Janela, teto e número de alocações não mudaram.
- `cyberdeck_music_metadata.h`: contrato documenta a rejeição por byte não-zero
  na área após o último frame (padding precisa ser todo zero).
- Testes — parser puro (`test_id3_frame_bounds_flags_and_all_or_nothing`): (a)
  frame `TIT2` válido + 1 zero + lixo que ainda cabe um frame header → rejeita
  com sentinelas preservadas; (b) lixo a `2 * k_id3_read_window_bytes` de
  profundidade → rejeita; (c) padding zero do mesmo tamanho → aceito com o
  título. Streaming (`test_read_padding_and_absent_tag`): os mesmos três casos
  em arquivo real (a/b com `out` preservado; c com `probe.highest_end ==
  tag_bytes`, `probe.largest <= k_id3_read_window_bytes` e `total` limitado, o
  que prova validação ponta a ponta em mais de um bloco e nenhuma leitura acima
  da janela). Nenhum assert existente foi removido ou enfraquecido.
- `code-map.md` (semântica do padding nas duas seções + descrição dos dois
  alvos), `tests/host/keymap/music_traceability.md` (linha AC-27) e
  `docs/03-audio-player-plan.pt-BR.md` (lista de rejeições) atualizados com a
  regra; contagens do tester preservadas (a nota não reescreve evidência alheia).

### Fechamento de versão e documentação (ciclo 7)

- `version.txt`: `1.15.1` -> `1.15.2` (PATCH do firmware; base inalterada, então
  `MINOR` intacto e `MAJOR` é controle do usuário).
- `components/cyberdeck/src/apps/music/cyberdeck_music_app.cpp`: `k_manifest` de
  `cyberdeck.music` `1.0.3` -> `1.0.4`; `api_version` continua `"1"` e
  recursos/capacidades/handles não mudaram. Foi a **única** mudança de produção
  deste delta.
- `tests/host/keymap/test_shell_help.cpp`: fixture `music_usage_manifest()` e o
  golden literal `TEST-HELP-APP-USAGE-MUSIC` para `1.0.4` — o contrato
  `test_shell_help_contract.py` reconcilia fixture x manifesto real
  (TEST-HELP-APP-MANIFEST-04) e exige golden literal
  (TEST-HELP-APP-MANIFEST-05).
- `code-map.md`: `decode_id3v2_text` no lugar do `copy_text` removido; framing
  compartilhado (`parse_id3v2_header`/`parse_id3v2_frame_header`, status
  `absent/accepted/rejected`, frame de 6/10 bytes, tamanho 0, resto final só como
  padding); descrição do leitor streaming com as três constantes, teto de 1 MiB
  total, skip de `APIC`/desconhecidos, janela fixa, sem `open`/`dup`/`close` e a
  matriz de flags aceitas/rejeitadas; lista de testes com
  `test_id3_large_tag_frame_walk`, `test_id3_frame_bounds_flags_and_all_or_nothing`
  e o alvo `test_music_metadata_io` (wrappers e `operator new`), incluindo o
  wiring de `MUSIC_METADATA_SRC`/`MUSIC_MODEL_SRC` no alvo novo.
- `docs/03-audio-player-plan.pt-BR.md`: nova seção "Envelope de metadados ID3v2
  até 1 MiB — 2026-11-07" com o relato, a mudança (fw `1.15.2`, app `1.0.4`),
  a semântica header+payload, o percurso por offsets, a matriz de flags, o
  fail-closed all-or-nothing, os testes e a pendência de custo no SD.
- `tests/manual/music-player-validation.pt-BR.md`: nova seção 3.3.1 com as
  fixtures a preparar (MP3 real, `real-shaped`, `apic-first`, `cap`, `cap+1`,
  `compact-64KiB`, `truncado`), pré-condições de flash/`sys.info` e os cenários
  `TEST-MUSIC-META-PHYS-LARGE`, `-OVERCAP`, `-ORDER`, `-COMPACT` e `-SCAN-COST`,
  marcando `manual_validation: pending`.
- `tests/host/keymap/music_traceability.md`: nova seção "Envelope de metadados de
  1 MiB, streaming por offsets (REQ-MUSIC-META-06)" com matriz AC-23..30 ->
  TEST e evidência por alvo, o registro do supersede parcial de
  AC-MUSIC-META-04/05, o estado da validação independente (190/361/598/438 e
  M2–M6) e a pendência física; linhas antigas `TEST-AUDIO-15` e
  `TEST-MUSIC-ID3-LARGE` reescritas para o teto de 1 MiB mantendo a menção
  histórica explícita ao antigo envelope de 64 KiB.
- `tests/host/keymap/player_traceability.md`: contagem `560` -> `598` (com a
  origem: 11 checks do tester + casos do envelope novo) e ponteiro para o alvo
  `test_music_metadata_io` nos cenários `-READ-SAME-FD`, `-SKIP-APIC`,
  `-SHORT-IO` e `-OOM`.
- Notas de tester/developer/reviewer **não** foram editadas.

### Documentação do contrato e do estado das mutações (ciclo 8)

- `code-map.md`: `test_music_id3_contract.py` entrou na lista de testes como
  contrato **estrutural** (alvo `.PHONY` no agregado, sem `_bin`), com o que ele
  deriva (constantes, `pread` por offset sem reopen, alocação única da janela,
  avanço por `header_bytes + payload_bytes`, cross-end/tamanho 0/syncsafe, flags,
  wiring `BINS`/`.PHONY`/agregado/`--wrap` e documentação do envelope). Registrei
  também a correção test-only do wiring do Makefile: `$(MUSIC_MODEL_BIN)` passou a
  declarar `$(MUSIC_METADATA_HDR)` (o teste inclui o header de metadata; sem isso
  mudança só no header não recompilava e o alvo dava verde falso).
- `tests/host/keymap/music_traceability.md`: linha
  `REQ-MUSIC-META-06 / wiring` do TEST-MUSIC-META-ID3-CONTRACT agora cita o
  contrato PASS, o que ele cobre e as mutações estruturais **C1** (remover
  `$(MUSIC_METADATA_HDR)` da regra de I/O ⇒ falha "must depend on
  $(MUSIC_METADATA_HDR)") e **C2** (`new (std::nothrow) unsigned char[tag_bytes]`
  ⇒ falha "the only allocation must be k_id3_read_window_bytes"), ambas
  restauradas. O parágrafo de estado passou a registrar 190/361/598/438 + contrato
  PASS, **M1'–M12 (12/12) detectadas e restauradas** (superando o placar parcial
  M2–M6 antes anotado), o detalhe de M10 exigir `-B` enquanto o header não era
  pré-requisito e M11/M12 no mesmo ciclo.
- `tests/host/keymap/player_traceability.md`: a seção de identidade da faixa
  aponta o contrato `test_music_id3_contract` (PASS) junto do alvo de I/O.
- `tests/manual/music-player-validation.pt-BR.md` (3.3.1) e
  `docs/03-audio-player-plan.pt-BR.md` (seção do envelope): acrescentado o
  **estado dispositivo x versão**, explicitamente separado de aceite físico — o
  ensaio com `1.15.1` mostrou só o nome do arquivo (reprodução do bug: tag de
  254209 B com `APIC` de 252112) e o `1.15.2`, que contém a correção, **ainda
  precisa** de build, flash autorizado e execução dos cenários manuais.
- Referências históricas ao envelope antigo foram preservadas com marcador
  explícito ("antigo"/"superada"/"legacy"), como o contrato estrutural exige
  (janela de ±2 linhas); nomes de fixture como `compact-64KiB` ficam isentos.

### Produção (ciclo 6, 4 arquivos com mudança de comportamento)

- `components/cyberdeck/include/apps/music/cyberdeck_music_metadata.h`: constantes
  únicas e exportadas `k_id3v2_header_bytes = 10`, `k_id3_max_tag_bytes =
  1024 * 1024` (bytes **totais**, header + payload) e `k_id3_read_window_bytes =
  2 * k_max_title_bytes + 4`; contrato documentado de `read_metadata` (mesmo fd,
  offsets explícitos, skip por tamanho, janela fixa no heap, fail-closed).
- `components/cyberdeck/include/apps/music/cyberdeck_music_model.h`: API de
  framing compartilhada entre o parser em memória e o leitor streaming —
  `id3v2_header_status {absent, accepted, rejected}`, `id3v2_field`,
  `id3v2_header`, `id3v2_frame_header`, `parse_id3v2_header`,
  `parse_id3v2_frame_header`, `decode_id3v2_text`.
- `components/cyberdeck/src/apps/music/cyberdeck_music_model.cpp`: `frame_size` e
  `copy_text` (privados) substituídos por esses helpers; `parse_tag` reescrito
  sobre eles com: teto total novo, validação de flags de header (unsync 0x80 e
  extended 0x40 e bits reservados rejeitam; experimental 0x20 e footer 0x10 de
  v2.4 tolerados; v2.2 exige flags = 0), rejeição de frame-alvo com qualquer flag
  ≠ 0, e resto final menor que um frame header aceito só se for padding.
- `components/cyberdeck/src/apps/music/cyberdeck_music_metadata.cpp`:
  `read_metadata` virou leitor streaming: header 10 B com `pread` no offset 0,
  iteração de frames com um header por leitura, `min(payload, janela)` apenas para
  frames-alvo, frames não-alvo pulados pelo tamanho declarado (APIC nunca é
  lido), uma única alocação fixa (`new (std::nothrow)` da janela), e retorno sem
  publicar nada em header inválido/versão fora de 2..4/teto/estouro de tag/
  cross-end/size insuficiente/short read/erro/OOM. `pread` exigido exato
  (`== length`), então short read é rejeição; a posição do descritor não muda
  (nada de `lseek`, `open`, `dup`, `close`).

### Testes

- `tests/host/keymap/test_music_model.cpp` (parser puro): fronteiras do ciclo
  anterior substituídas pela derivada `k_id3_max_tag_bytes` (payload 64 KiB + 1
  agora aceito, com os três campos exatos); novos `test_id3_large_tag_frame_walk`
  (v2.2/2.3/2.4 com TPE2/TSSE intercalados, APIC de 252112 antes e depois dos
  textos, decoy `TIT2 "Fake"` embutido no payload do APIC, tag exatamente no teto
  com textos após artwork quase no teto, e teto + 1 rejeitado com sentinelas) e
  `test_id3_frame_bounds_flags_and_all_or_nothing` (frame de tamanho 0 após TIT2
  válido, APIC cruzando o fim, resto curto `xxxx` vs padding zero, matriz de flags
  de header v2.2/2.3/2.4, frame-alvo com flag rejeitando, APIC com flag sendo
  pulado e textos seguintes decodificados, entrada sem `ID3` como no-op).
- `tests/host/keymap/test_music_metadata_io.cpp` (**novo** alvo
  `test_music_metadata_io`): fixtures em `/tmp` geradas por descrição estrutural
  (APIC com 252112 bytes zerados, sem áudio/capa reais), com `--wrap=pread/open/
  openat/dup/close` e override local de `operator new`. Cobre: fixture com a
  geometria do arquivo relatado (payload 254209, frames TALB@10/69, TPE1@89/47,
  TPE2@146/47, TSSE@203/29, TIT2@242/59, TRCK@311/3, TYER@324/6,
  APIC@340/252112, padding 1757) e valores ffprobe (título com espaço inicial);
  variante APIC-primeiro; teto exato e teto + 1; posição do fd preservada em 1234,
  idempotência e nenhum `open/openat/dup/close` durante a leitura; bound de leitura
  (maior leitura ≤ janela, total < payload do APIC, offsets crescentes, zero
  sobreposição com o payload do APIC); TIT2 gigante (300 KiB) truncado em
  `k_max_title_bytes`; injeção de short read no header da tag, no header de frame
  e em payload, retorno 0 e `-1/EIO`, mais arquivo truncado com `size` antigo;
  falha de alocação rejeitando e igualdade de contagem/tamanho de alocação entre
  tag compacta e tag no teto; v2.2/v2.4 com APIC grande; flags de header e de
  frame; padding final vs lixo; arquivo sem `ID3` e arquivo menor que o header;
  `size` um byte menor que a tag.
- `tests/host/keymap/Makefile`: `MUSIC_METADATA_IO_TEST/BIN`,
  `test_music_metadata_io_bin` em `BINS`, alvo `.PHONY` novo com regra própria
  (`--wrap=…`) e **uma** entrada no agregado `test`, logo após `test_music_model`.

### Correção desta continuação (as 7 asserções)

Causas raiz, todas no teste novo (produção não mudou neste delta):

1. `reported_text_frames()` montava TRCK e TYER com
   `std::string(1, '\x01') + "1\0"`: o `operator+(std::string, const char*)`
   para no NUL, então os payloads saíam com 2 e 5 bytes em vez de 3 e 6. Isso
   deslocava a soma dos frames em 2 bytes e quebrava 4 asserts de
   `frames.size() == k_reported_payload - k_reported_padding` e o assert de
   sobreposição do APIC (offset calculado 348 em vez de 350). Corrigido com
   `std::string("\x01" "1\0", 3)` e `std::string("\x00" "2021\0", 6)`.
2. O bloco de arquivo truncado lia primeiro o fd do arquivo **completo** com o
   `size` completo (leitura válida, que publica os valores reais em `tags`) e em
   seguida o fd truncado reutilizando o mesmo `out`; as duas asserções
   `preserved(tags)` eram impossíveis. Removida a leitura redundante; a leitura
   truncada agora usa um `metadata truncated_tags{}` próprio com sentinelas.

Nada além disso foi alterado; nenhum assert foi removido ou enfraquecido.

## Evidence

Ciclo 10 (gate de cobertura + registro do aceite), na raiz do repositório,
**somente** os comandos do despacho (sem `clean`, sem suíte, sem build, sem
flash, sem commit):

| Comando | Resultado |
| --- | --- |
| `make -C tests/host/keymap test_coverage_scope_guard` | `coverage scope guard tests passed`, EXIT=0 (a linha `coverage scope error: invalid allowlist / forbidden eligible TU: cyberdeck_cat_worker.cpp` vem do cenário negativo intencional de allowlist forbidden, não é falha) |
| sensibilidade do TEST-COV-013/014 (execução em processo isolado, sem editar o repositório) | os dois testes passam; removendo `USB_AUDIO_HOST` de `ALLOWLIST` em memória, TEST-COV-013 falha com `the USB exception must cover exactly the reviewed set: missing=[…usb/cyberdeck_usb_audio_host.cpp]` → o conjunto exato é realmente verificado |
| `make -C tests/host/keymap test_music_id3_contract` | `PASS: 1 MiB ceiling (header + payload) and window are single exported constants; … the docs document 1 MiB and keep 64 KiB only as explicit history`, EXIT=0 — contrato read-only que faz lint das edições no manual/plano |
| `make -C tests/host/keymap test_usb_audio_host_contract` | `PASS` (open error path tolerates a missing interface / releases before deleting / never reads a deleted interface), EXIT=0 — contrato estrutural do TU allowlisted |
| `git diff --check` | limpo (EXIT=0) |
| `git rev-parse HEAD` + `git diff \| sha256sum` + `git status --porcelain \| sha256sum` | `ff47b9a…c7481`; `b50bb288…f9292`; `a139ace2…af944` (28 entradas: 24 `M` + 4 untracked) |

Não executados neste ciclo (por decisão explícita do despacho): agregado
`make test`, `clean`, cobertura/gcovr, `verify`, ASan/UBSan/TSan, `idf.py build`,
flash, playback, `ruff`/`bandit` (o CI roda esses gates; alterei apenas dict e
teste Python). O gate de cobertura completo em si (gcovr + guard com JSON real)
**não** foi reexecutado aqui — quem fecha isso é o tester/reviewer.

Ciclo 9 (correção do blocker), na raiz do repositório, **somente** os dois alvos
do despacho (sem `clean`, sem suíte, sem build, sem flash):

| Comando | Resultado |
| --- | --- |
| `make -C tests/host/keymap test_music_model` (antes da correção, com os testes novos) | `363/367 checks passed`, 4 `FAIL` (471, 472, 481, 482 — as duas rejeições e os dois `preserved`) → bug reproduzido no parser puro |
| `make -C tests/host/keymap test_music_metadata_io` (antes da correção, com os testes novos) | `202/204 checks passed`, 2 `FAIL` (718, 729 — `preserved` nos dois negativos) → bug reproduzido no leitor streaming |
| `make -C tests/host/keymap test_music_model` (depois) | `367/367 checks passed`, recompilou `test_music_model_bin`, exit 0 |
| `make -C tests/host/keymap test_music_metadata_io` (depois) | `205/205 checks passed`, recompilou `test_music_metadata_io_bin`, exit 0 (o `+1` é a asserção de `highest_end`) |
| `git diff --check` | limpo |
| `git rev-parse HEAD` + `git diff \| sha256sum` + `git status --porcelain` | `ff47b9a…c7481`; `fc54a8ddd7a5c11c68383a72e8de800b2199ffca7e5bd83fc7ec63f732885a09`; 26 entradas de status (untracked não cobertos) |
| `grep -rn "read_padding\|padding = true"` em `components/cyberdeck/{src,include}/apps/music` | nenhuma ocorrência (flag/helper antigos removidos, sem comentário stale) |

Nenhum outro alvo foi executado neste ciclo: `test_music_player`,
`test_music_app`, `test_shell_help`, contrato estrutural, agregado, cobertura,
`verify`, build IDF, flash e playback ficam para tester/reviewer. Os dois alvos
rodados são os que linkam os TUs alterados.

Ciclo 8 (delta anterior) — só documentação de projeto/rastreabilidade, sem tocar
produção ou testes. Comandos e resultados:

| Comando | Resultado |
| --- | --- |
| `git rev-parse HEAD` + `git diff \| sha256sum` + `git status --porcelain` | snapshot conferido: `8743eb38…6154c5` no início do ciclo (25 entradas) e `aff8eda3…699b71` após este delta docs-only (26 entradas: 22 `M` + 4 untracked) |
| `python3 tests/host/keymap/test_music_id3_contract.py` | `PASS: 1 MiB ceiling (header + payload) … the docs document 1 MiB and keep 64 KiB only as explicit history`, EXIT=0 — contrato **read-only**, sem build/compilação, reexecutado após as edições para provar que plano/manual seguem válidos |
| `git diff --check` | limpo |
| `git diff --stat` | 22 arquivos, 2091 inserções / 160 remoções |
| `grep` de `1.15.1`/`1.0.3`/`64 KiB` no escopo do `AGENTS.md` | nenhum valor corrente sobrando; só menções históricas marcadas e o `CHANGELOG.md` vendorizado do `esp_codec_dev` |
| `grep` do Makefile | `test_music_id3_contract` com `.PHONY`, pré-requisitos (script, 2 headers, 2 fontes, plano, manual, Makefile) e **uma** entrada no agregado `test`; `$(MUSIC_MODEL_BIN)` depende de `$(MUSIC_METADATA_HDR)` |

Nenhum alvo C++, build, suíte, flash ou playback foi executado neste ciclo; os
números de teste citados nos documentos são os do tester (`190/190`, `361/361`,
`598/598`, `438/438`, contrato PASS, M1'–M12), não medições minhas.

Ciclo 7 (delta anterior) — só versões, fixture/golden e documentos; **nenhum teste,
build, suíte, flash ou playback executado**, conforme o despacho:

| Comando | Resultado |
| --- | --- |
| `git diff -- version.txt` | `-1.15.0/+1.15.2` (o `1.15.1` do ciclo 6 nunca foi commitado) |
| `grep -n "k_manifest" -A3 …/cyberdeck_music_app.cpp` | `"cyberdeck.music", "Music", "1.0.4"`, `api_version` `"1"` intacta |
| `git diff -- tests/host/keymap/test_shell_help.cpp` | fixture `1.0.2 -> 1.0.4` e golden `version 1.0.4` (as linhas de usage/explicação não mudaram) |
| `grep -rn "1\.15\.1"` (README, README.pt-BR, code-map, docs, tests, components, main) | 0 ocorrências fora de `docs/agent-state` e do `CHANGELOG.md` do fork `esp_codec_dev` (versão de outro componente, histórica) |
| `grep -rn "1\.0\.3"` (mesmo escopo) | 0 ocorrências fora do mesmo changelog vendorizado (`v1.0.3`) |
| `grep -rn "64 KiB"` (mesmo escopo) | 9 ocorrências, todas explicitamente históricas/superadas (plano, manual, `music_traceability` e comentários de fixture nos testes) |
| `git diff --check` | limpo |
| `git diff \| sha256sum` | `f6c2d78ad7f1cbe09b963cdd68630bef88274adc55084fc8e91857c0a875b239` |

Verificação estática do bump (sem rodar o alvo): `test_shell_help_contract.py`
exige fixture == manifesto real (ambos `1.0.4`) e o golden literal do C++ é
comparado com o texto renderizado a partir da fixture; `version.txt` só é lido
por contratos de bateria/control/OTA (formato e piso, não valor exato) e nenhum
`.py` fixa a versão do app `cyberdeck.music`. Nenhum alvo foi executado neste
delta; reexecutar `test_shell_help`/`test_shell_help_contract` fica para o tester.

Ciclo 6 (correção das 7 asserções), na raiz do repositório, sem suíte, `verify`,
cobertura, ASan/TSan, build IDF, flash ou playback:

| Comando | Resultado |
| --- | --- |
| `make -C tests/host/keymap test_music_metadata_io` (antes) | `175/182 checks`, 7 `FAIL` (linhas 324×4, 483, 568, 573), exit ≠ 0 |
| `make -C tests/host/keymap test_music_metadata_io` (depois) | `181/181 checks passed`, exit 0, recompilou `test_music_metadata_io_bin` |
| `make -C tests/host/keymap test_music_model` | `361/361 checks passed`, exit 0 |
| `make -C tests/host/keymap test_music_player` | `560/560 checks passed`, exit 0 |
| `make -C tests/host/keymap test_music_app` | `428/428 checks passed`, exit 0 |
| `make -C tests/host/keymap test_music_metadata_contract` | `PASS` (wiring por `playback.current`, glifo do placeholder, 64 + 15 ≤ 120) |
| `make -n test_music_metadata_io` (alvo atualizado) | só `./test_music_metadata_io_bin` (sem compilação; incremental no-op) |
| `git diff --check` | limpo |
| `git diff \| sha256sum` | `3e4cbfa1…d1566b58` (mesmo antes e depois do delta, pois as correções ficaram no arquivo untracked) |

As contagens de ciclo 6 para `test_music_metadata_io`/`test_music_player`/
`test_music_app` foram **superadas** pelo ciclo do tester, que acrescentou
checks: `test_music_metadata_io` 190/190, `test_music_player` 598/598,
`test_music_app` 438/438, `test_music_model` 361/361, com M2–M6 detectadas e
fontes restauradas (evidência do tester, não reexecutada por mim neste delta).

Wiring conferido por inspeção do Makefile: `test_music_metadata_io_bin` em `BINS`,
wrapper `.PHONY`, uma entrada no agregado e nenhum diretório temporário residual
em `/tmp/opencode/music-metadata-io-*`.

`test_music_player`/`test_music_app` foram rodados no ciclo 6 porque linkam
`cyberdeck_music_metadata.cpp`/`cyberdeck_music_model.cpp` e exercitam
`read_metadata` real (fixture de 70 KiB só com padding, `size` menor que a tag,
arquivo sem `ID3`); são alvos afetados pelo parser, não suíte.

## Decisions and assumptions

- **Exceção de cobertura (ciclo 10)**: allowlist de **um** TU, decidida pelo
  usuário, é a opção de menor custo e risco entre allowlist estreita, harness
  host com shims USB/FreeRTOS/BSP (alto custo, denominadores gcovr novos) e
  mover o TU para fora do source-root (rejeitado: mascararia o gate). Escolhi
  não excluir o TU da descoberta nem afrouxar thresholds: o guard continua
  exigindo cobertura de todo o resto e rejeita allowlistar TU coberto. O risco
  aceito é o TU de hardware seguir sem cobertura host **por este gate**;
  comportamento já validado fisicamente na entrega de áudio USB e pinado por
  `test_usb_audio_host_contract.py`.
- **Semântica do conjunto exato no teste**: a expectativa é a união de três
  conjuntos revisados (`PRE_EXISTING`, `NEWLY_ALLOWLISTED`, `USB_AUDIO_ALLOWLIST`),
  não um literal; TEST-COV-013 deriva do próprio guard os allowlisted de
  `platform/usb/` e exige igualdade ao conjunto de um TU. Assim, adicionar
  qualquer outro `platform/usb/` à exceção falha, e remover a entrada também
  (TEST-COV-008 estendido). Comprovei a sensibilidade removendo a entrada em
  memória (mutação local, repositório intocado).
- **Registro do aceite (ciclo 10)**: descrevi `1.15.2` como compilado/gravado e
  aceito **por relato do usuário**, com os limites explícitos (sem captura
  arquivada, sem `ffprobe` lado a lado, sem repetição) e `-SCAN-COST` ainda sem
  medição. Não converti relato em ensaio independente; os ~5350 `pread` são
  aritmética de `teto/janela` (1048576 / 196), não medição.
- **Escopo respeitado**: não editei produção ID3 nem USB, não toquei thresholds
  do CI nem `version.txt`/manifesto. No ciclo 10 o plano
  `docs/03-audio-player-plan.pt-BR.md` ainda tinha um parágrafo "1.15.2 ainda
  precisa de build/flash" e **não** foi alterado, porque aquele despacho limitou
  o registro a manual + rastreabilidade — reportei como achado adjacente. O
  reviewer final bloqueou por isso e o ciclo 11 corrigiu **só** esse parágrafo;
  a pendência abaixo está resolvida e o achado não é mais válido.

- **Semântica do teto**: `k_id3_max_tag_bytes` conta **header + payload** (o
  usuário escreveu "1 MiB inclusive maximum total tag bytes"; o tester fixou o
  mesmo). Aceita total == teto, rejeita teto + 1. Payload máximo aceito é
  `teto - 10`, derivado da constante. Implementado em
  `parse_id3v2_header`, que é usado pelos dois caminhos, então o parser em
  memória e o leitor streaming não divergem.
- **AC-28 aplicado também ao caminho de janela**: as flags de header/frame são
  validadas em `parse_id3v2_header`/`parse_id3v2_frame_header`, usadas tanto por
  `parse_id3v2` (buffer) quanto por `read_metadata`. Isso é mais restritivo que o
  parser antigo para tags compactas com flags, mas AC-30 enumera
  encodings/transliteração/duplicata/vazio/limites/`sanitize` como o que precisa
  ficar idêntico, e nenhum teste existente usava flags. Assumido como leitura
  correta do contrato; se o tester entender que AC-30 exige tolerar flags em tag
  ≤ 64 KiB, isso é decisão de contrato e volta ao developer.
- **Resto final e padding (revisado no ciclo 9)**: todo byte entre o último frame
  completo e o fim declarado da tag precisa ser zero, nos dois caminhos. Um resto
  curto não-zero é header de frame truncado; um zero só abre padding quando toda a
  área restante é zero (o caso "zero + lixo" era o blocker AC-27 e agora rejeita
  a tag inteira). A validação do streaming é feita em blocos de no máximo
  `k_id3_read_window_bytes` com a janela fixa já alocada: satisfaz "não ler
  payloads grandes"/teto sem aumentar alocação nem janela, ao custo de
  `ceil(padding / janela)` `pread`s extras por tag (≤ ~5,5 mil no teto de 1 MiB
  totalmente em padding), risco de custo no SD registrado como pendência de
  medição física junto do custo por frame.
- **Uma leitura por header de frame**: `pread` exige o count exato, então short
  read é rejeição (AC-27) e nenhuma leitura excede a janela (AC-25). O custo é um
  `pread` por frame (o risco de custo no SD já está registrado pelo planner/tester
  como pendência de medição física).
- **Alocação fixa única** (`k_id3_read_window_bytes` = 196 B, `2 * 96 + 4`):
  satisfaz "nenhuma alocação proporcional à tag/frame", mantém o caminho de falha
  de alocação exercitável (o teste de OOM do tester arma falha e espera `out`
  preservado — com alocação zero isso seria vácuo) e mantém 196 B fora da stack da
  task de scan.
- **Semanticamente nada muda no playback**: `read_metadata` continua `void` e
  apenas faz merge de campo não vazio em `out`; rejeição = `out` intocado, então
  player/app caem no fallback de basename como antes.
- **Anti-pin nos testes**: teto e janela derivados de `k_id3_max_tag_bytes`/
  `k_id3_read_window_bytes`; `254209`, `252112`, `1757` e os offsets do arquivo
  real ficam como constantes **nomeadas de proveniência do reprodutor do bug**,
  não como limite de produção. O envelope histórico de 64 KiB aparece como
  `k_legacy_tag_payload` documentado como superado.
- `jev`: não usado — nenhuma escolha deste delta dependia de desempate semântico;
  as decisões acima vêm do plano reaprovado e da nota do tester.

## Pending and risks

- **Gate de cobertura (ciclo 10) — validação independente pendente**: a
  allowlist de um TU passou no alvo focado do guard, mas o gate completo
  (`make clean` + agregado CI-style + `gcovr` 80/90/50 + `python3
  tools/coverage_scope_guard.py` com o JSON real) **não** foi reexecutado por
  mim. O tester/reviewer precisa rodar esse gate e confirmar que o TU sai de
  "missing" e não entra em "allowlisted and covered".
- **Risco residual da exceção**: `cyberdeck_usb_audio_host.cpp` continua sem
  cobertura host por este gate; o comportamento UAC segue sustentado por
  contrato estrutural + validação física anterior, não por teste de execução.
  Se o usuário quiser cobertura real, é um harness com shims USB/FreeRTOS/BSP —
  fora deste escopo autorizado.
- **Achado adjacente (resolvido no ciclo 11, não é mais bloqueante)**:
  `docs/03-audio-player-plan.pt-BR.md` dizia que `1.15.2` “ainda precisa de
  build, flash autorizado e execução dos cenários manuais”, contradizendo o
  aceite reportado. O ciclo 10 não editou por restrição de despacho; o ciclo 11
  recebeu despacho próprio e corrigiu **apenas** esse parágrafo, com os mesmos
  fatos e limites dos espelhos.
- **Risco residual de custo**: ~5350 `pread` de 196 B no pior caso teórico
  (padding todo-zero no teto) continua **sem medição**;
  `TEST-MUSIC-META-PHYS-SCAN-COST` é o único gate que mede o SD, e o aceite do
  usuário foi um relato geral, não uma série temporal de custo.
- **Correção do blocker (ciclo 9) pronta, validação independente pendente**:
  `test_music_model` 367/367 e `test_music_metadata_io` 205/205 na minha
  execução; o tester precisa reexecutar os dois alvos (e os demais que linkam os
  TUs, se entender necessário) e o reviewer fechar os gates. Não houve suíte,
  build, flash ou commit neste ciclo.
- **Custo novo de padding**: a validação agora lê a área de padding em blocos; um
  arquivo com tag no teto quase toda em padding gera ~5,5 mil `pread`s de 196 B.
  Só `TEST-MUSIC-META-PHYS-SCAN-COST` mede o impacto real no SD; é custo, não
  mudança de semântica. Se o custo for inaceitável no dispositivo, a alternativa
  (buffer maior só para padding) muda a janela e exige reavaliação de contrato.
- **Assinatura de sensibilidade**: os dois asserts novos de rejeição falharam
  antes da correção (parser e streaming) e passam depois, então não são vácuos.
  Não executei a matriz M1'–M12 neste ciclo (fora do despacho).
- **Documentação do contrato (ciclo 8)**: `code-map.md`, `music_traceability.md`,
  `player_traceability.md` e manual documentam
  `TEST-MUSIC-META-ID3-CONTRACT` (PASS), o placar de mutações M1'–M12 (12/12) com
  M10/M11/M12 nominadas, C1/C2 e a correção test-only do wiring de dependência do
  Makefile. O estado dispositivo x versão foi **atualizado no ciclo 10**: ensaio
  de `1.15.1` reproduziu o bug; `1.15.2` foi compilado/gravado e recebeu aceite
  reportado pelo usuário.
- **Estado físico de 1.15.2**: aceite **por relato** do usuário na faixa real;
  `-OVERCAP`/`-ORDER`/`-COMPACT` com fixtures caracterizadas e `-SCAN-COST`
  seguem sem evidência independente. Quem quiser prova objetiva deve executar o
  roteiro de §3.3.1 e arquivar `screen.dump`/`ui.dump` + oráculo `ffprobe`.
- **Gates de código ainda não executados**: agregado
  `make -C tests/host/keymap test`, cobertura/scope guard completo, `make …
  verify`, ASan/UBSan/TSan, `test_incremental_final_contract` (exige
  `test-build`) e CI remota; o reviewer é quem fecha esses gates.
- **Versionamento aplicado no ciclo 7**: `version.txt` `1.15.2` e manifesto
  `cyberdeck.music` `1.0.4` (`api_version` `"1"` preservada), com fixture/golden
  de `test_shell_help.cpp` reconciliados. Nada foi commitado e nenhuma tag
  `v1.15.2` foi criada (tag só com pedido explícito).
- **Documentação atualizada no ciclo 7**: `code-map.md`,
  `docs/03-audio-player-plan.pt-BR.md` (seção do envelope de 1 MiB),
  `tests/manual/music-player-validation.pt-BR.md` (3.3.1 com fixtures e cenários
  físicos), `tests/host/keymap/music_traceability.md` (matriz REQ-MUSIC-META-06)
  e `player_traceability.md`.
- **Varredura de valores antigos (ciclo 7)**: `1.15.1`/`1.0.3` não sobraram em
  README/docs/tests/manual/code-map/components (exceto changelog vendorizado do
  `esp_codec_dev`); as menções a `64 KiB` restantes são explicitamente históricas
  ("superada"/"antiga"/"legacy"), como exige o `AGENTS.md`. Notas de
  tester/developer continuam com `1.15.1`/`1.0.3` porque são snapshots daqueles
  ciclos e não foram editadas.
- **Reexecução pendente do bump**: `test_shell_help` e
  `test_shell_help_contract` não foram rodados neste delta (o despacho proibiu
  testes); a consistência foi verificada só estaticamente. O tester/reviewer deve
  reexecutar os focados do shell help junto dos demais gates.
- **Validação independente**: `test_music_metadata_io` 190/190,
  `test_music_model` 361/361, `test_music_player` 598/598 e
  `test_music_app` 438/438 no delta do tester, com M2–M6 detectadas e fontes
  restauradas; o reviewer roda os gates finais.
- **Gates não executados**: agregado `make -C tests/host/keymap test`,
  cobertura/scope guard, `make … verify`, ASan/UBSan/TSan, `idf.py build`,
  `test_incremental_final_contract` (exige `test-build`) e CI remota. Nenhum
  flash/playback — a validação física do envelope (`TEST-MUSIC-META-PHYS-LARGE`,
  `-OVERCAP`, `-ORDER`, `-COMPACT`, `-SCAN-COST`) segue pendente e depende do
  usuário.
- **Limitações de teste do que foi feito**: `--wrap` prova bytes/offsets/fd
  observados, não intenção; o override de `operator new` cobre o alocador C++
  (o leitor só usa `new (std::nothrow)`), então uma futura alocação via
  `malloc`/`std::string` não seria detectada pelo teste de OOM (o contrato
  estrutural do tester cobre esse caso). O host não prova o comportamento do
  FatFs com `pread` em offsets grandes nem o custo de muitos frames no SD.
- **Risco residual aceito**: tags com unsynchronisation/extended header (e
  frame-alvo com flags) passam a perder o metadado, com playback preservado; é
  perda de compatibilidade intencional, agora registrada em `code-map.md`,
  `docs/03-audio-player-plan.pt-BR.md` e `music_traceability.md`.
- **Risco aberto de custo**: o leitor faz um `pread` por frame; o número cresce
  com a quantidade de frames de uma tag no teto e só `PHYS-SCAN-COST` mede isso.
- **Adjacente/deferível (não bloqueante, fora do escopo deste delta)**: o
  comentário em `tests/host/keymap/test_music_model.cpp:49` ainda cita
  `copy_text`, símbolo renomeado para `decode_id3v2_text` no ciclo 6. É só
  comentário de teste; mantive inalterado (fora do despacho do ciclo 9).
  Corrigir no próximo ciclo que tocar esse arquivo.
- **Adjacente/deferível**: a nota `.opencode/memory/` não tem bloco do envelope
  ID3v2; o ciclo 9 não muda teto/janela/versão, então não criei bloco novo
  (fora do despacho). Se o developer quiser memória durável do contrato, é um
  passo separado.
- Nada foi commitado; nenhuma tag criada.

## Histórico (ciclo 5, entrega anterior, superada por este plano)

- Implementados exibição de título/artista/álbum na região NOW PLAYING alimentada
  por `playback.current` (`render_now_playing`), flag `title_from_filename`,
  placeholder `(not available)`, álbum no `metadata`/`track` e no scan; naquele
  ciclo as versões eram `1.15.1`/`cyberdeck.music` `1.0.3` (hoje **superadas**
  por `1.15.2`/`1.0.4`) e 10 alvos focados foram reexecutados pelo tester
  (`test_music_player` 560/560, contagem depois elevada a 598). Detalhes e
  evidências ficaram nas notas do tester/developer/reviewer.

## Ciclo 11 — correção documental do aceite `1.15.2` (este ciclo)

- Motivo: o reviewer final bloqueou porque `docs/03-audio-player-plan.pt-BR.md`
  ainda afirmava que o fw `1.15.2` "ainda precisa" de build, flash autorizado e
  cenários manuais, contradizendo o aceite já registrado em
  `tests/manual/music-player-validation.pt-BR.md` §3.3.1 e em
  `tests/host/keymap/music_traceability.md` (que o ciclo 10 corrigiu, mas o
  parágrafo do plano ficou de fora).
- Escopo seguido à risca: alterado **um único trecho** do plano (o parágrafo
  "Estado do dispositivo x versão", hoje linhas 1182–1202). Nada de produção,
  testes, manifesto, `version.txt`, `code-map.md` ou das notas de outros papéis.
- Novo conteúdo registra, na mesma formulação dos espelhos: `1.15.1` reproduziu o
  bug; `1.15.2` foi compilado e gravado por USB Serial-JTAG (imagem
  `build/cyberdeck5.bin` SHA-256 `f6c4ba7f…cc4e9e9`, 2.740.496 bytes, app
  `0x29d110`), com `sys.info` em `fw_version=1.15.2`, `safe_mode=false`,
  `interrupted_boots=0` e uptime monotônico; o aceite humano de
  `TEST-MUSIC-META-PHYS-LARGE` é **reportado pelo usuário** ("funcionou tudo
  corretamente"), não observação independente; `-OVERCAP`, `-ORDER` e
  `-COMPACT` seguem sem relato individual e `TEST-MUSIC-META-PHYS-SCAN-COST`
  continua **sem medição** (~5350 `pread`/faixa no pior caso teórico, risco
  residual calculado).
- Números conferidos contra `docs/agent-state/music-track-metadata/developer.md`
  (linha 17: exit 0 do `idf.py -p /dev/ttyACM0 flash`, SHA-256 completo, tamanho
  e `0x29d110`) antes de escrever; nada foi copiado de memória.
- Não há contrato host que pine a frase de estado deste parágrafo:
  `test_music_id3_contract.py` só exige "1 MiB" no plano/manual e "64 KiB" apenas
  como história explícita, então a edição não cria pin nem exige ajuste de teste.

**Evidência (comandos, resultado, estado):**

- `make -C tests/host/keymap test_music_id3_contract` → PASS (mensagem cobre
  teto 1 MiB, `pread` no mesmo descritor, wiring de `BINS`/`.PHONY`/agregado e a
  documentação); executado com o plano já editado.
- `make -C tests/host/keymap test_music_metadata_contract` → PASS (contrato da
  região NOW PLAYING, sem relação com o parágrafo, rodado como fumaça do
  diretório de testes).
- `git diff --check` → exit 0 (sem whitespace pendente). `sha256sum` do plano
  `98f488d88dc13436`; `git diff | sha256sum` =
  `76dd091f…ab66605`.
- **Não executado neste ciclo, por escopo**: agregado `make ... test`, cobertura,
  scope guard, ASan/UBSan/TSan, `idf.py build`, flash, qualquer cenário no Tab5.
  Nenhuma declaração de suíte completa aprovada.
- Validação independente ainda devida: reviewer conferir o parágrafo novo contra
  os espelhos (manual §3.3.1 e rastreabilidade) e seguir os gates locais/CI do
  fechamento — não é aprovação final.

## References

- Plano/requisitos: `docs/03-audio-player-plan.pt-BR.md`; REQ-MUSIC-META-06 e
  AC-MUSIC-META-23..30 na nota do tester (ciclo 5).
- `docs/agent-state/music-track-metadata/{planner,tester,developer,reviewer}.md`
  (não editadas por mim).
- Produção: `components/cyberdeck/{include,src}/apps/music/
  cyberdeck_music_{metadata,model}.{h,cpp}`; consumidores `cyberdeck_music_player.cpp`
  e `cyberdeck_music_app.cpp` (inalterados neste delta).
- Testes: `tests/host/keymap/{test_music_metadata_io.cpp (novo),
  test_music_model.cpp, test_music_player.cpp, test_music_app.cpp,
  test_shell_help.cpp, Makefile}`; rastreabilidade em
  `tests/host/keymap/{music_traceability,player_traceability}.md`.
- Documentos atualizados no ciclo 7: `code-map.md`,
  `docs/03-audio-player-plan.pt-BR.md`,
  `tests/manual/music-player-validation.pt-BR.md` (seção 3.3.1).
- Arquivo real (read-only, fora do repositório):
  `/run/media/moises/CC64B5CF64B5BC90/moises/musicas/Kid Cosmic/01 - The Kid (Kid
  Cosmic Theme).mp3`.
- Referências do ciclo 9 (correção do blocker): `reviewer.md` (achado
  `AC-MUSIC-META-27`), `components/cyberdeck/src/apps/music/cyberdeck_music_model.cpp`
  (`parse_tag`), `components/cyberdeck/src/apps/music/cyberdeck_music_metadata.cpp`
  (`read_zero_padding`/`read_metadata`), `cyberdeck_music_metadata.h`,
  `tests/host/keymap/test_music_model.cpp::test_id3_frame_bounds_flags_and_all_or_nothing`,
  `tests/host/keymap/test_music_metadata_io.cpp::test_read_padding_and_absent_tag`,
  `tests/host/keymap/music_traceability.md` (AC-27), `code-map.md`.
- Referências do ciclo 10 (gate + registro do aceite): `reviewer.md` (os dois
  blockers), `planner.md` (plano da allowlist estreita),
  `tools/coverage_scope_guard.py`, `tests/host/keymap/test_coverage_scope_guard.py`,
  `.github/workflows/quality-gate.yml` (step `Host aggregate and coverage`, linhas
  ~121-148), `components/cyberdeck/src/platform/usb/cyberdeck_usb_audio_host.cpp`
  (dependências de hardware) e
  `components/cyberdeck/src/platform/usb/cyberdeck_usb_audio_json.cpp` (TU puro
  coberto), `tests/host/keymap/test_usb_audio_host_contract.py`,
  `tests/manual/music-player-validation.pt-BR.md` §3.3.1,
  `tests/host/keymap/music_traceability.md`, `code-map.md`,
  `docs/agent-state/music-track-metadata/developer.md` (flash `1.15.2`, SHA-256,
  relato do usuário).
- Referências do ciclo 11 (correção documental do aceite):
  `docs/03-audio-player-plan.pt-BR.md` (parágrafo "Estado do dispositivo x
  versão", linhas 1182–1202), espelhos
  `tests/manual/music-player-validation.pt-BR.md` §3.3.1 e
  `tests/host/keymap/music_traceability.md` (§ "Estado do dispositivo x
  aceite"), `docs/agent-state/music-track-metadata/developer.md` (flash
  `1.15.2`, SHA-256, relato do usuário) e
  `tests/host/keymap/test_music_id3_contract.py` (único contrato que lê o plano).
- Índices: `code-map.md`, `AGENTS.md`.
