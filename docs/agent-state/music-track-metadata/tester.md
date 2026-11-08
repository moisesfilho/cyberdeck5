# tester - music-track-metadata

- Role: tester
- Updated: 2026-11-07 (ciclo 13: validação independente do blocker AC-MUSIC-META-27
  — padding all-zero nos dois caminhos; 367/367 e 205/205 verdes; mutações
  PAD-A/PAD-B detectadas e restauradas; custo de 5350 `pread` no pior caso
  classificado e pendente de medição física)
- Code state: develop HEAD `ff47b9a31c8d8a1fdb5746c60013619cf75c7481`;
  `version.txt` = `1.15.2` (app `cyberdeck.music` `1.0.4`); worktree com
  26 entradas de status, `git diff` sha256
  `fc54a8ddd7a5c11c68383a72e8de800b2199ffca7e5bd83fc7ec63f732885a09`
  (untracked excluídos; idêntico antes/depois deste ciclo). Produção da
  correção: `metadata.cpp 3141220b…649733`, `model.cpp 4f9683ec…5fbfd1b`,
  `metadata.h 44976885…61a61c3`; `player.cpp f5b7fe59…af36da` (inalterado).
  Testes: `test_music_model.cpp d4df9cde…f34d9453`,
  `test_music_metadata_io.cpp 52860cc2…1e82c2`.
- As seções "Especificação" abaixo são o ciclo 1 (pré-implementação,
  risk_profile: high) e foram mantidas como referência de rastreabilidade. Os
  resultados reais dos ciclos 2-4 estão em "Execução pós-implementação"; a
  reaprovação/cenários novos estão em "Ciclo 5"; a validação independente em
  "Ciclo 6"; M7–M9/M1' em "Ciclo 7"; M10–M12 em "Ciclo 8"; o wiring em
  "Ciclo 9"; o contrato estrutural em "Ciclo 10"; o checkpoint docs-only em
  "Ciclo 11"; a reexecução no snapshot anterior em "Ciclo 12"; a correção do
  padding em "Ciclo 13".

## Dispatch objective

Definir cenários independentes de aceite, fronteira, falha e regressão para
exibir título, artista e álbum da faixa **efetivamente em execução** no app
`music` (MP3 com ID3v2 existente), com rastreabilidade `REQ -> AC -> TEST` e
pré-condições de validação manual no Tab5. Sem editar produção e sem executar
testes/revisão ampla. Exit: especificação fixada + nota.

## Ciclo 5 — reaprovação do plano após bug físico (ID3v2 até 1 MiB)

### Reset registrado

O usuário reaprovou o plano depois de o ensaio físico refutar a hipótese de que
a tag do MP3 real cabia em 64 KiB. Registro o reset da sessão do tester por
reaprovação (o estado anterior ficou stale para este objetivo) e **não** reuso
como prova o flash nem os resultados do ciclo 4. Não editei produção nem
testes, não executei `make`, build, flash ou suíte; o entregável deste ciclo é
a especificação executável e rastreável abaixo.

Snapshot lido nesta sessão (read-only):

- `git rev-parse HEAD` = `ff47b9a31c8d8a1fdb5746c60013619cf75c7481`;
- `version.txt` = `1.15.1`;
- `git diff | sha256sum` = `30530d23…828025` (untracked excluídos);
- produção ainda rejeita tag grande: `cyberdeck_music_model.cpp:203-206`
  (`tag_bytes > 64 * 1024`) e `cyberdeck_music_metadata.cpp:13,23-24`
  (`k_id3_metadata_limit = 64 * 1024`, uma única leitura de até limite+10).

### Arquivo real que motivou a reaprovação (inspeção read-only, não copiado)

Fonte: `/run/media/moises/CC64B5CF64B5BC90/moises/musicas/Kid Cosmic/01 - The
Kid (Kid Cosmic Theme).mp3` (2.350.363 bytes), lida só para caracterizar a
fixture. Comandos: `ffprobe -show_entries format=size,duration
-format_tags=title,artist,album` e um dump estrutural do prólogo.

| Propriedade | Valor medido |
| --- | --- |
| ID3 | v2.3, flags do header `0x00` (sem unsync/extended/footer) |
| bytes do campo size (payload) | `254209`; bytes totais da tag `254219` |
| frames, em ordem de offset | TALB@10/69, TPE1@89/47, TPE2@146/47, TSSE@203/29, TIT2@242/59, TRCK@311/3, TYER@324/6, APIC@340/`252112` |
| padding | 1757 bytes `0x00`; fim dos frames/padding = 254219 |
| encoding dos textos | `0x01` UTF-16 com BOM `FF FE` e terminador `00 00` |
| `title` (ffprobe) | ` The Kid (Kid Cosmic Theme)` — **espaço inicial preservado** |
| `artist` (ffprobe) | `Dr. Fang and The Gang` |
| `album` (ffprobe) | `Kid Cosmic and the Sonic Courage` |
| offset do áudio | 254219 (sem footer) |

Consequências para a especificação: o parser precisa pular TPE2/TSSE/TRCK/TYER e
o APIC de 252 KiB **por tamanho de frame**, chegar aos textos antes do APIC
(TALB/TPE1/TIT2) e continuar funcionando quando os textos vierem **depois** do
APIC. Não versionar áudio nem capa: a fixture host reproduz os tamanhos/offsets
exatos com payload de APIC zerado e os textos factuais curtos do oráculo.

### Contrato novo (supersede parcial explícito)

- Somente as cláusulas de `AC-MUSIC-META-04`/`AC-MUSIC-META-05` que fixavam
  a rejeição acima de 64 KiB e a leitura limitada a esse prefixo ficam
  **superadas** por REQ-MUSIC-META-06 / AC-MUSIC-META-23..30. O restante
  desses ACs (fail-closed de header/versão/frame, frame de tamanho 0 ou que
  ultrapassa o fim, `size` menor que a tag, só sobrescrever campo não vazio) e
  todos os demais REQ/AC 01..22 continuam válidos.
- A expectativa existente em `test_music_model.cpp`
  (`test_id3_versions_and_malformed_tags`) que aceita payload `65536` e rejeita
  `65537` é substituída por: payload `65537` passa a ser aceito e a fronteira
  passa a ser o novo teto, derivada da constante (ver TEST-MUSIC-META-ID3-CAP).
  Isso é atualização de contrato aprovado, **não** enfraquecimento: as
  asserções de conteúdo e de fail-closed continuam e ficam mais fortes.
- Semântica do teto: **bytes totais da tag** (header de 10 bytes + payload do
  campo size) ≤ 1 MiB. Aceito com total exatamente `cap`; rejeitado com
  `cap + 1`. O payload máximo aceito é `cap - 10`, derivado da constante; o
  antigo limite de 64 KiB contava só o payload. A diferença de 10 bytes é
  registrada para o coder não escolher a semântica em silêncio.

**REQ-MUSIC-META-06 — ID3v2 até 1 MiB por offsets, bounded e fail-closed, sem
alterar playback/comandos/formatos/layout**

- `AC-MUSIC-META-23` (percurso): v2.2/2.3/2.4 decodificam TIT2/TPE1/TALB (e
  TT2/TP1/TAL) com os mesmos valores do caminho compacto,
  independentemente de os frames de texto virem antes ou depois de um APIC
  grande e de frames desconhecidos intercalados. O percurso é dirigido pelo
  header de cada frame; bytes dentro do payload de um frame não-alvo nunca são
  interpretados como frame nem como texto.
- `AC-MUSIC-META-24` (mesmo fd/offsets): `read_metadata` usa somente o `fd`
  recebido, lê por offset explícito (sem `open`/`dup`/`fopen`), não fecha o fd,
  não altera a posição corrente do descritor e é idempotente em chamadas
  repetidas no mesmo fd.
- `AC-MUSIC-META-25` (I/O e payload bounded): nenhuma leitura única excede uma
  janela derivada das constantes exportadas; o total lido não cresce com o
  tamanho de APIC/payload ignorado; o payload de APIC/frame não-alvo não é
  carregado (no máximo a janela de leitura cruza o início de um frame que será
  pulado); um frame de texto gigante é lido só até a janela necessária para
  preencher a capacidade de saída; nenhuma alocação tem tamanho proporcional ao
  tamanho da tag/frame.
- `AC-MUSIC-META-26` (teto): constante única e exportada com a semântica do
  contrato acima; total `cap` aceito, `cap + 1` rejeitado; um tag no teto com
  textos depois de um APIC quase do tamanho do teto ainda decodifica os três
  campos. Footer v2.4, quando tolerado, são 10 bytes adicionais fora do teto e
  não são lidos pelo metadado.
- `AC-MUSIC-META-27` (fail-closed all-or-nothing): header inválido, versão
  fora de 2..4, byte reservado `0xff`, size syncsafe com bit `0x80`, frame de
  tamanho 0, header de frame truncado, frame que cruza o fim da tag, tag
  declarada maior que o teto, short read/`pread` com erro/arquivo truncado,
  falha de alocação ⇒ tag rejeitada inteira; `out` preserva os valores
  anteriores e o overload `track` não publica título/artista/álbum parciais.
  Um arquivo sem prólogo `ID3` continua não alterando `out`.
- `AC-MUSIC-META-28` (flags): header v2.3/v2.4 com unsynchronisation (`0x80`)
  ou extended header (`0x40`) e bits reservados ≠ 0 ⇒ rejeitar; experimental
  (`0x20`) e footer (`0x10`, v2.4) são tolerados porque não deslocam o payload;
  v2.2 com compression (`0x40`) ou unsync (`0x80`) ou bits reservados ⇒
  rejeitar. Frame-alvo com qualquer flag de frame ≠ 0 (compressão, criptografia,
  grouping, unsync, data-length indicator) ⇒ rejeitar a tag; frame não-alvo
  com flags é pulado pelo campo de tamanho.
- `AC-MUSIC-META-29` (rejeição não é erro de playback): tag rejeitada mantém
  `state=playing`, `failure_stage=none`, contadores
  `generations_queued/started/retired/failures` e a sequência
  open→id3→probe→decode→codec→write inalterados; o título cai no basename
  sanitizado com `title_from_filename=true` e artista/álbum ficam vazios; uma
  única abertura segura por play; sem novo `error_stage`.
- `AC-MUSIC-META-30` (regressão compacta e não ampliação): toda tag com
  payload ≤ 64 KiB (o envelope antigo) mantém resultados idênticos aos atuais
  (encodings 0/1/2/3, transliteração,
  duplicata last-wins, frame vazio limpa, limites de saída, `sanitize`), e
  `music status`/`music list`/`music help`, formatos aceitos e a view NOW
  PLAYING não mudam.

### TEST — cenários executáveis (alvos)

Legenda: alvo `make -C tests/host/keymap <alvo>`. Novos alvos propostos:
`test_music_metadata_io` (binário focado, linka
`cyberdeck_music_metadata.cpp` + `cyberdeck_music_model.cpp`, sem LVGL/player)
e, se o coder preferir, subcasos distribuídos em `test_music_model`/
`test_music_player`; a rastreabilidade vincula ao nome real. Wiring do alvo
novo: `.PHONY`, `_bin` em `BINS`, inclusão uma vez no agregado `test`, e o
contrato deriva a contagem de `BINS`/agregado do Makefile (nunca literal).

**Grupo parser puro (`test_music_model`)**

| TEST | Prova e cenários |
| --- | --- |
| TEST-MUSIC-META-ID3-ORDER | v2.2/2.3/2.4 com TIT2+TPE1+TALB (ids v2.2 TT2/TP1/TAL) antes e depois de APIC de ~252 KiB e com TPE2/TSSE/TRCK/TYER/TXXX intercalados ⇒ os três valores exatos. Variante adversarial: payload do APIC começa com um frame TIT2 válido embutido (id+size+flags+encoding+"Fake") ⇒ título deve ser o TIT2 real, nunca "Fake". Mutação: localizar texto varrendo bytes por "TIT2" em vez de usar o size do frame deve falhar. |
| TEST-MUSIC-META-ID3-ACTUAL | Fixture sintética com a **geometria exata** do arquivo real (tabela acima): v2.3/flags 0, payload 254209, frames TALB@10/69, TPE1@89/47, TPE2@146/47, TSSE@203/29, TIT2@242/59, TRCK@311/3, TYER@324/6, APIC@340/252112, padding 1757; textos UTF-16/BOM iguais ao oráculo (título com espaço inicial); APIC com MIME `image/jpeg`, tipo 3, descrição vazia e bytes de imagem **zerados**; áudio de frames válidos. Asserts: `parse true`, três valores byte-a-byte, `valid_utf8`, offsets/somas conferidos. Mutação: manter o teto 64 KiB (ou ler só o prefixo) ⇒ parse false/campos vazios, teste falha — é o reprodutor do bug físico. |
| TEST-MUSIC-META-ID3-CAP | Fronteira derivada de `k_id3_max_tag_bytes` exportada: total `cap` com textos após APIC(~cap) ⇒ decodifica; total `cap+1` com textos completos antes e depois ⇒ `false` e nada publicado; tag compacta de 64 KiB e 64 KiB+1 ⇒ ambas aceitas com o mesmo conteúdo (supersede registrado); contrato assegura `cap == 1024*1024` e a semântica header+payload. Mutação: trocar o teto por `cap-1` ou `cap+1` (ou aceitar `>` em vez de `>=`) deve falhar os dois lados. |
| TEST-MUSIC-META-ID3-BOUNDS | Matriz de frames: v2.3 32-bit BE; v2.4 syncsafe com bit `0x80` ⇒ rejeita; v2.2 header 6 bytes/24 bits; size 0 ⇒ rejeita; header de frame truncado (restam < header) ⇒ rejeita; payload que cruza o fim ⇒ rejeita (inclusive APIC depois de um TIT2 válido ⇒ nada publicado); padding (`0x00`) encerra e bytes depois do fim declarado são ignorados; tag de size 0 ⇒ `true` sem campos; tag cujo size declarado termina exatamente no fim do arquivo ⇒ aceita; tag com ~90k frames mínimos (1 byte de payload) termina e não publica lixo (loop avança ≥ header+1). Mutação: remover a checagem de cruzamento/avanço ⇒ falha. |
| TEST-MUSIC-META-ID3-FLAGS | Matriz do AC-28: unsync/extended/reservados rejeitam; experimental/footer tolerados e o conteúdo decodifica; v2.2 compression/unsync rejeitam; TIT2 com flag de frame ≠ 0 rejeita; APIC com grouping é pulado e os textos seguintes decodificam. Mutação: ignorar flags de header/frame ⇒ teste falha. |
| TEST-MUSIC-META-ID3-ALLOR-NOTHING | Sentinela pré-carregada em `metadata` e `track`; TIT2 válido seguido de APIC malformado/tamanho excessivo/short data ⇒ `false` e sentinelas intactas (nenhum campo parcial); entrada sem `ID3` ⇒ `true` e sentinelas intactas. Mutação: publicar em `out` durante o loop ⇒ falha. |
| TEST-MUSIC-META-COMPACT-REGRESSION | Todos os testes atuais de título/artista/álbum, encodings, duplicatas, vazio, limites `k_max_*`, `sanitize_display_text`, path policy e fila permanecem com as mesmas expectativas (só a linha do antigo teto de 64 KiB muda, conforme o contrato); nenhuma asserção removida. |

**Grupo I/O real (`test_music_metadata_io`; arquivo temporário + `-Wl,--wrap=pread/open/openat/dup/close`)**

| TEST | Prova e cenários |
| --- | --- |
| TEST-MUSIC-META-READ-SAME-FD | Escreve a fixture real-shaped no /tmp, abre `O_RDONLY`, move a posição para 1234 (`lseek`), chama `read_metadata(fd,size,out)` ⇒ campos exatos; depois `lseek(fd,0,SEEK_CUR)==1234` (posição intacta), chamada repetida idempotente. Wrappers armados registram que nenhum `open/openat/dup/close` ocorreu durante a leitura e que todo `pread` usou exatamente o fd passado. Mutação: usar `read()`/reabrir o arquivo ⇒ posição muda/`open` aparece e o teste falha. |
| TEST-MUSIC-META-READ-SKIP-APIC | Com a fixture real-shaped e com a variante APIC-primeiro: total de bytes lidos ≤ `janela * (frames_escaneados + 2)` (janela derivada das constantes exportadas), estritamente menor que o payload do APIC; nenhuma leitura única > janela; sobreposição com o payload do APIC ≤ janela (só o possível read-ahead do header); offsets estritamente crescentes e dentro de `[0, min(size, 10+cap))`; textos depois do APIC decodificados. Variante TIT2 gigante (300 KiB): saída truncada em `k_max_title_bytes` e bytes lidos para esse frame ≤ janela de texto derivada. Mutação M1 (voltar a ler a tag inteira em um buffer) e M2 (carregar o payload do APIC) ⇒ asserções de total/sobreposição falham mesmo com os campos corretos. |
| TEST-MUSIC-META-READ-SHORT-IO | O wrapper de `pread` injeta: retorno curto no header inicial, no header de um frame, no meio de um payload de texto; retorno 0; `-1`/`EIO`; e arquivo truncado depois do `stat` (o `size` recebido continua o antigo). Em todos: `out` preservado, sem crash/loop, nenhuma publicação parcial. Mutação: aceitar short read e parsear o buffer parcial ⇒ falha. |
| TEST-MUSIC-META-READ-OOM | Override local de `operator new(std::nothrow)`/`new[]` no binário focado: com falha armada, `read_metadata` não publica nada e não crasha (fail-closed). Medição de tamanho/número de alocações na janela de leitura: idêntica entre tag compacta (≤64 KiB) e tag no teto, provando que a alocação não cresce com a tag. Mutação: alocar buffer do tamanho da tag/frame ⇒ as duas asserções falham. |

**Grupo player/app (reais, áudio gated; fakes de armazenamento existentes)**

| TEST | Prova e cenários |
| --- | --- |
| TEST-MUSIC-META-PLAYER-LARGE | Play da fixture real-shaped (com áudio válido): `snapshot.current.title/artist/album` exatos (título com espaço inicial), `title_from_filename=false`, `state=playing`, `failure_stage=none`, `size_bytes` = tamanho do arquivo; PCM escrito; exatamente uma abertura segura. Variante APIC-primeiro também. Mutação: ler metadado do índice da fila/path errado ou tratar rejeição como `error_stage=id3` ⇒ falha. |
| TEST-MUSIC-META-PLAYER-OVERCAP | Tag `cap+1` com TIT2/TPE1/TALB completos: playback normal (`playing`, `failure_stage=none`, `failures==0`, PCM > 1 write), título = basename sanitizado com extensão, `title_from_filename=true`, artista/álbum vazios; nenhuma linha de erro; contadores/sequência e uma abertura segura iguais ao play sem tag. Mutação: mapear rejeição para erro/parar playback ⇒ falha. |
| TEST-MUSIC-META-APP-SCAN-LARGE | Scan da fixture real-shaped preenche `track.title/artist/album` exatos; tag `cap+1` gera item com basename e artista/álbum vazios; exatamente uma abertura segura por arquivo; `music list` continua `n. <title>` e `music status`/`help` sem álbum. Mutação: scan usar prefixo de 64 KiB ⇒ campos vazios, falha. |
| TEST-MUSIC-META-COMPACT-REGRESSION (player/app) | Fixture de 70 KiB só com padding (teste atual `write_large_id3_mp3`): aceita, sem campos ⇒ basename + flag; playback e contadores idênticos; nenhuma abertura extra. |

**Contrato estrutural (Python, agregado; complementa, não substitui, os C++)**

| TEST | Prova |
| --- | --- |
| TEST-MUSIC-META-ID3-CONTRACT | Deriva do header de metadata: constante única `inline constexpr` do teto com semântica de bytes totais e valor `1024*1024` (requisito aprovado, comparado com a expressão, não com um literal duplicado da implementação); constantes de janela exportadas e usadas; `read_metadata` sem `open/openat/dup/fopen`, usando `pread` com offset; nenhuma alocação cujo tamanho contenha `size/tag/frame` (nem `std::string/vector` crescendo com payload); loop com avanço `>= header_bytes + 1` e rejeição de size 0; máscaras de flags do AC-28 presentes; documentação (`docs/03-audio-player-plan.pt-BR.md`, README/manual) atualizada de 64 KiB para 1 MiB sem pinar cópia independente; wiring do novo alvo derivado do Makefile (sem contagem literal). Limite: contrato textual não prova comportamento; os TESTs C++ acima é que provam. |

### Fixtures — regras (sem áudio/capa commitados)

- Toda fixture é gerada em código/temporário a partir de uma descrição estrutural
  (header + frames + padding + frames MP3 válidos sintéticos). **Nenhum** áudio
  real, capa, binário de MP3 ou BMP do usuário entra no repositório; o payload
  de APIC é `0x00` com o tamanho exato.
- Os números `254209`/`252112` e os offsets da tabela do arquivo real são
  **proveniência de reprodutor de bug** e podem aparecer como constantes
  nomeadas com comentário; não são derivados de firmware nem substituem o teto
  (`k_id3_max_tag_bytes`).
- Valores de texto da fixture real-shaped = oráculo ffprobe acima (título com
  espaço inicial). As variantes `APIC-primeiro`, `TEXT-HUGE`, `CAP`, `OVERCAP`,
  `MALFORMED`, `FLAGS` e `COMPACT` reutilizam o mesmo gerador por parâmetros.
- O áudio sintético usa o padrão já existente de frames silenciosos válidos
  (`write_valid_silent_mp3`), suficiente para exercitar decode/PCM; duração e
  probe não são o alvo destes testes.

### Mutações obrigatórias (sensibilidade)

M1 ler a tag inteira num buffer (implementação atual) ⇒ TEST-READ-SKIP-APIC
falha no total lido. M2 carregar/pular lendo o payload do APIC ⇒ falha de
sobreposição. M3 achar texto varrendo bytes em vez de usar o size ⇒ falha em
ID3-ORDER (TIT2 embutido no APIC). M4 teto `cap-1`/`cap+1` e M5 aceitar `>cap`
⇒ ID3-CAP falha nos dois lados. M6 publicar parcialmente antes de falhar ⇒
ALL-OR-NOTHING falha. M7 ignorar flags de header/frame ⇒ ID3-FLAGS falha. M8
usar `read()`/reabrir ⇒ READ-SAME-FD falha. M9 rejeição virar erro de playback
⇒ PLAYER-OVERCAP falha. M10 restaurar 64 KiB ⇒ ID3-ACTUAL/PLAYER-LARGE/APP-SCAN
falham. M11 alocar por frame/tag ⇒ READ-OOM (tamanho idêntico) e READ-SKIP-APIC
falham. M12 truncar a saída de texto em vez de preencher a capacidade ⇒
ID3-BOUNDS/TEXT-HUGE falham.

### Gate físico (obrigatório antes da revisão final; não executado aqui)

Pré-condições: firmware buildado a partir do snapshot revisado (registrar
`fw_version` + SHA-256 do `build/cyberdeck5.bin`), flash USB autorizado,
`sys.info` com `safe_mode=false`, `interrupted_boots=0`, uptime monotônico e
checkpoint no log. Uma sessão serial por vez; não abrir `idf.py monitor`
durante o playback.

Arquivos no SD (via leitor de cartão; `fs.write` só aceita 2 KiB): o MP3 real
do usuário (não copiar para o repositório) e fixtures sintéticas
`real-shaped`, `APIC-primeiro`, `cap` (total exatamente 1 MiB), `cap+1`,
`compact-64KiB` e `truncado`, geradas pelo helper de teste/tool e registradas
com SHA-256 no relatório.

- `TEST-MUSIC-META-PHYS-LARGE`: `music queue /musicas/Kid Cosmic` →
  `music list` (títulos vêm do TIT2, não do basename) → `music play 1` → abrir
  `music` → `ui.dump`/`screen.dump`. Esperado: título/artista/álbum exatamente
  iguais ao oráculo ffprobe (incluindo o espaço inicial), em portrait e
  landscape; sem sobreposição/truncagem; amostrar a cada 1 s por 2 min de
  play/troca para detectar stale/flicker; uptime monotônico, sem panic/WDT.
- `TEST-MUSIC-META-PHYS-OVERCAP`: tocar o fixture `cap+1` ⇒ playback normal,
  título = nome do arquivo, artista/álbum indisponíveis, sem erro/reboot.
- `TEST-MUSIC-META-PHYS-ORDER`: tocar `APIC-primeiro` ⇒ três campos corretos.
- `TEST-MUSIC-META-PHYS-COMPACT`: tocar `compact-64KiB` (sem frames de texto)
  ⇒ basename; e um MP3 compacto com tags ⇒ três campos corretos (regressão
  visível do caminho antigo).
- `TEST-MUSIC-META-PHYS-SCAN-COST`: medir o tempo de `music queue` num
  diretório com 5 arquivos de tag grande (contagem/tempo registrados) e o
  stack/high-water do scan se exposto; sem nova task/buffer de stack grande.
  Aceite: sem panic, sem reboot, tempo monotônico e coerente com leitura
  bounded (não provar wall-clock por host).
- Nenhum ensaio destrutivo; não repetir interação que já causou reboot/panic.
  Sem esses resultados, a entrega permanece em `manual_validation`.

## Ciclo 6 — validação independente após reset por orçamento

### Reset registrado

A sessão anterior do tester encerrou por **limite de passos** depois de edições
somente em testes, deixando M1 inconclusivo (o mutante não compilou por
`-Werror=unused-function`) e M2..M12 pendentes. Registro o reset por orçamento;
não reuso como prova os resultados anteriores além do que foi reconfirmado
abaixo. Nenhuma produção foi mantida alterada: os quatro hashes de produção
foram conferidos contra os do coder e não há marcador `MUTANT` no código.

Escopo deste ciclo (restrito): (1) conferir estado/hashes e ausência de resíduo
de mutação; (2) reexecutar os quatro alvos focados atuais; (3) executar M2, M3,
M4, M5 e M6 individualmente, restaurando byte-a-byte e confirmando detecção.
Fora do escopo: suíte agregada, `verify`, cobertura, ASan/TSan, build ESP-IDF,
flash e playback; M7..M12 continuam pendentes.

### Snapshot e ausência de resíduo

- `git rev-parse HEAD` = `ff47b9a31c8d8a1fdb5746c60013619cf75c7481`;
  `version.txt` = `1.15.1`; `git status --porcelain` = 24 entradas
  (21 rastreadas modificadas + `docs/agent-state/music-track-metadata/`,
  `test_music_metadata_contract.py` e `test_music_metadata_io.cpp` não
  rastreados); `git diff | sha256sum` =
  `8caf74a2…6036096e` (idêntico antes e depois deste ciclo).
- Produção (idêntica aos hashes do coder, conferida antes e depois):
  `cyberdeck_music_metadata.cpp` `964fb918d7f17d1caa6b45812253a86f5252674d22474876bd9a469da72ae001`,
  `cyberdeck_music_model.cpp` `5f5b4a3b82370126a2a45e6de3620d3842fa8a12b4b085dad1e99b0951e5042c`,
  `cyberdeck_music_metadata.h` `10c82bad0462e318f07f2a12e2bf1e25140a00109bf92e2f25ef8855bbe98673`,
  `cyberdeck_music_player.cpp` `f5b7fe59ce0577b50805af889b2fc280b051e0cdda3ee077378b2c506eaf36da`.
- `grep -rn "MUTANT"` em `components/cyberdeck/{src,include}/apps/music` →
  nenhum marcador. Cópias pristinas desta rodada em
  `/tmp/opencode/tester-mut-c6/{metadata.cpp,model.cpp,metadata.h,player.cpp}.orig`
  com os mesmos sha256 acima.

### Testes test-only já presentes (da sessão anterior; revalidados aqui)

Contagens medidas nesta sessão, sem edição de teste neste ciclo:

| Alvo | Comando | Resultado |
| --- | --- | --- |
| `test_music_metadata_io` | `make -C tests/host/keymap test_music_metadata_io` | `190/190 checks passed`, EXIT=0 |
| `test_music_model` | `make -C tests/host/keymap test_music_model` | `361/361 checks passed`, EXIT=0 |
| `test_music_player` | `make -C tests/host/keymap test_music_player` | `598/598 checks passed`, EXIT=0 |
| `test_music_app` | `make -C tests/host/keymap test_music_app` | `438/438 checks passed`, EXIT=0 |

Logs: `/tmp/opencode/base-<alvo>.log`. `grep -c FAIL` = 0 nos quatro. Esses
números incluem as inclusões test-only do ciclo anterior: requisito
`k_id3_max_tag_bytes == 1024*1024` e suficiência da janela UTF-16 no alvo de
I/O; PLAYER-LARGE (textos antes/depois do APIC de 252112), PLAYER-OVERCAP
(cap+1 ⇒ basename + flag, `state=playing`, `failure_stage=none`, PCM > 1
write) e reforço do fixture legado de 70 KiB no player; APP-SCAN-LARGE
(tag grande preenche a tripla; cap+1 cai no basename) no app.

### Mutações executadas (uma por vez, restauração + sha256)

Cada mutação: editar produção → `make -C tests/host/keymap <alvo>` → capturar
EXIT/FAIL → `cp /tmp/opencode/tester-mut-c6/<arquivo>.orig <arquivo>` →
conferir sha256. Nenhuma falha de compilação foi contada como sensibilidade.

| Mut. | Mutante aplicado | Alvo | Detecção observada | Restauração |
| --- | --- | --- | --- | --- |
| M2 | `metadata.cpp`: ler também o payload de frames não-alvo (`read_exact` de `min(payload, janela)` antes do `if (frame.field != none)`) | `test_music_metadata_io` | EXIT=2, `186/190`; FAIL 484 (`!probe.overlaps(artwork_begin, artwork_end)`), 497 (APIC-primeiro) e 646 (v2.2/v2.4) — o APIC passou a ser lido | `964fb918…e5042c` |
| M3 | `model.cpp`: avançar `offset += 1` em frames não-alvo (percorrer o payload em vez de pular pelo size) | `test_music_model` | EXIT=2; FAIL 354–357 (textos antes do APIC) e 371–374 (APIC com decoy `TIT2 "Fake"` + textos) nas três versões — o percurso por tamanho é o que sustenta a ordem/decoy | `5f5b4a3b…bb1085` |
| M4 | `metadata.h`: `k_id3_max_tag_bytes = 1024 * 1024 - 1` | `test_music_metadata_io` | EXIT=2, `189/190`; FAIL 728 (`k_id3_max_tag_bytes == 1024 * 1024`) — o requisito de 1 MiB não é derivável de si mesmo | `10c82bad…699867` |
| M5 | `model.cpp`: aceitar total `cap+1` (`payload + header > cap + 1`) | `test_music_metadata_io` | EXIT=2, `188/190`; FAIL 531 (`preserved(rejected)` — publicou “Over title”) e 532 (`probe.calls <= 1` — continuou lendo a tag acima do teto) | `5f5b4a3b…bb1085` |
| M6 | `model.cpp`: publicar `out.title/artist/album` já decodificados antes de `return false` na falha de frame | `test_music_model` | EXIT=2, `360/361`; FAIL 435 (`preserved(target)` no caso TIT2 válido + frame de tamanho 0) — all-or-nothing detecta publicação parcial | `5f5b4a3b…bb1085` |

Após cada restauração o sha256 foi conferido (coluna “Restauração”); ao final,
`test_music_metadata_io` e `test_music_model` foram reexecutados no estado
restaurado: `190/190` e `361/361`, EXIT=0 (`/tmp/opencode/final-*.log`).

### Mutações não executadas neste ciclo

- **M1** (ler a tag inteira num buffer): tentativa da sessão anterior gerou
  **apenas falha de compilação** (`read_padding` ficou não usado sob
  `-Werror`), o que **não** conta como sensibilidade. Continua inconclusivo;
  um mutante compilável precisa remover/marcar `read_padding`.
- **M7** (ignorar flags de header), **M8** (`read()`/reabrir em vez de
  `pread`), **M9** (rejeição virar erro de playback), **M10** (restaurar
  64 KiB), **M11** (alocar por frame/tag), **M12** (truncar a saída em vez de
  preencher a capacidade): pendentes por orçamento deste ciclo; cada um tem
  alvo e asserção previstos na especificação do ciclo 5.

### Limitações do ciclo 6

- As mutações M2–M6 provam sensibilidade das asserções existentes, não
  correção de produção: a produção não foi revisada de novo além da inspeção
  do ciclo anterior.
- `--wrap`/contadores observam syscalls e alocações do binário de teste; não
  provam o comportamento do FatFs no Tab5 nem custo de muitos frames no SD.
- Continua sem prova de hardware/playback real; o gate físico do ciclo 5 não
  foi executado.

## Ciclo 7 — mutações M7, M8, M9 e M1 equivalente

### Contexto e snapshot

O coder fechou versão/documentação (`version.txt` `1.15.2`, app `1.0.4`) no
snapshot `git diff` sha256 `f6c2d78ad7f1cbe09b963cdd68630bef88274adc55084fc8e91857c0a875b239`;
o delta não alterou código de produção nem testes (hashes abaixo conferidos).
Este ciclo executou M7, M8, M9 e uma versão compilável de M1, uma por vez, com
restauração byte-a-byte e sha256 conferido. Nenhuma edição permanente em
produção; só esta nota foi escrita. Sem suíte/build/verify/flash/playback.

Estado inicial e final (idêntico):

- `HEAD ff47b9a31c8d8a1fdb5746c60013619cf75c7481`, `version.txt 1.15.2`,
  `git status --porcelain` 25 entradas, `git diff | sha256sum` `f6c2d78a…`.
- Produção: `metadata.cpp` `964fb918d7f17d1caa6b45812253a86f5252674d22474876bd9a469da72ae001`,
  `model.cpp` `5f5b4a3b82370126a2a45e6de3620d3842fa8a12b4b085dad1e99b0951e5042c`,
  `metadata.h` `10c82bad0462e318f07f2a12e2bf1e25140a00109bf92e2f25ef8855bbe98673`,
  `player.cpp` `f5b7fe59ce0577b50805af889b2fc280b051e0cdda3ee077378b2c506eaf36da`.
- Testes inalterados: io `5f9f826e…`, player `6f03dca5…`, app `0ed05be4…`.
  `grep -rn MUTANT` em `components/cyberdeck/{src,include}/apps/music` →
  nenhum marcador. Cópias limpas em `/tmp/opencode/tester-mut-c6/*.orig`.

### Mutações executadas

| Mut. | Mutante aplicado | Alvo | Detecção observada | Restauração |
| --- | --- | --- | --- | --- |
| M7 | `model.cpp`: mascarar a validação de flags de header para zero (não rejeitar unsync/extended/reservados) | `test_music_model` | EXIT=2, `347/361`, 14 FAIL: 470/471 (flags v2.3 0x80/0x40/0x01), 489/490 (v2.4 reservado), 498/499 (v2.2) — sem a rejeição o parser aceita a tag estruturalmente inválida | `5f5b4a3b…bb1085` |
| M8 | `metadata.cpp`: `read_exact` com `lseek`+`read` em vez de `pread` no offset (mesmo fd, mas posição consumida) | `test_music_metadata_io` | EXIT=2, `182/190`: 447 (`probe.calls > 0`, nenhum `pread`), 448/464 (posição do fd deixou de ser 1234) e 557 ×5 (injeção de short read deixa de valer) — o contrato “mesmo fd, offsets, posição intacta” é observável | `964fb918…e5042c` |
| M9 | `player.cpp`: tag com prober `ID3` e metadado vazio vira `publish_error(req, error_stage::id3)` em vez de fallback | `test_music_player` | EXIT=2, 20 FAIL; primeiro bloco afetado é PLAYER-OVERCAP: 1294 (`begin_gated_playback`), 1297 (`state==playing`), 1298 (`failure_stage==none`); depois 1405+ no fixture legado de 70 KiB. Binário encerra com SIGFPE após a cascata de falhas (só ocorre no estado mutado; caminho verde passa) | `f5b7fe59…af36da` |
| M1' | `metadata.cpp`: carregar a tag inteira (`new` de `tag_bytes` + `read_exact` de `tag_bytes`) antes do walk, **sem remover** `read_padding` (compila sob `-Werror`) | `test_music_metadata_io` | EXIT=2, `172/190`, 18 FAIL: 478/479/480/484 e 494/495/497 (maior leitura > janela, total > APIC, overlap), 510/511 (TIT2 gigante), 523/524 (teto), 622/623 (`compact_largest == wide_largest` e janela) e 645 — prova carga da tag inteira contra READ-SKIP-APIC | `964fb918…e5042c` |

Após cada restauração o sha256 foi conferido (coluna “Restauração”). Ao final,
os focados foram reexecutados no estado restaurado e passaram:
`test_music_metadata_io` `190/190`, `test_music_model` `361/361`,
`test_music_player` `598/598`, `test_music_app` `438/438`, todos EXIT=0
(`/tmp/opencode/final7-*.log`). Logs das mutações:
`/tmp/opencode/mut-M{1,7,8,9}.log`.

### M1 — situação resolvida

O M1 anterior era inconclusivo apenas porque o mutante deixava `read_padding`
não usado sob `-Werror`. A versão M1' acima é compilável, mantém todos os
testes e é detectada: a leitura de `tag_bytes` viola as quatro asserções de
READ-SKIP-APIC (janela, total, overlap e alocação). Não é necessário apagar
testes nem mexer em `read_padding`.

### Mutações ainda pendentes

M10 (restaurar teto 64 KiB), M11 (alocar por tag/frame) e M12 (truncar a saída
em vez de preencher a capacidade) continuam pendentes por orçamento; alvos e
asserções previstos permanecem os da especificação do ciclo 5.

### Limitações

- M9 produziu SIGFPE no binário de teste depois de 20 falhas em cascata; é
  efeito do estado mutado (player sem worker saudável), não do caminho verde —
  os quatro focados passam após a restauração.
- As mutações provam sensibilidade das asserções atuais, não uma nova revisão
  da produção; `--wrap`/contadores não provam FatFs nem custo de scan no SD.
- Gate físico continua não executado (fixtures no SD, `ui.dump`/`screen.dump`,
  custo de scan, ausência de panic/reboot).

## Ciclo 8 — mutações M10, M11 e M12

### Snapshot e regras

- `HEAD ff47b9a31c8d8a1fdb5746c60013619cf75c7481`, `version.txt 1.15.2`,
  `git diff` sha256 `f6c2d78a…a875b239`, `git status` 25 entradas (início e
  fim idênticos). Produção e testes inalterados no fim; nenhuma edição de
  docs de projeto além desta nota.
- Cada mutação foi aplicada sozinha, rodou alvos mínimos, foi revertida com
  `cp` do `.orig` e o sha256 do fonte foi conferido. Nenhuma falha de
  compilação foi contada como sensibilidade.

### M10 — restaurar teto de 64 KiB

Mutante: `metadata.h` `k_id3_max_tag_bytes = 64 * 1024`.

| Alvo | Comando | Resultado |
| --- | --- | --- |
| parser | `make -C tests/host/keymap test_music_model` | **361/361 EXIT=0 — resultado STALE, não conta** (ver defeito abaixo) |
| parser (forçado) | `make -C tests/host/keymap -B test_music_model` | `333/361`, EXIT=2; FAIL 313–316 (tag regular de 64 KiB+1 rejeitada) e 354–357 em diante (walk com APIC 252112 rejeitado) → detectado |
| player | `make -C tests/host/keymap test_music_player` | `590/598`, EXIT=2; FAIL 1034–1037 nos dois blocos PLAYER-LARGE (identidade virou basename) → detectado |
| app | `make -C tests/host/keymap test_music_app` | `435/438`, EXIT=2; FAIL 1366–1368 (scan da tag real-shaped perde a tripla) → detectado |

O caso over-cap continua caindo no fallback esperado, então não é ele que
detecta M10; a detecção vem dos fixtures de 254 KiB e da tag regular de
64 KiB+1. Restauração: `10c82bad…699867`.

**Defeito de wiring de teste encontrado:** `tests/host/keymap/Makefile:2951`
declara `test_music_model_bin` com pré-requisitos `$(MUSIC_MODEL_TEST)
$(MUSIC_MODEL_SRC) $(MUSIC_MODEL_HDR) music_traceability.md Makefile`, mas
`test_music_model.cpp` inclui `apps/music/cyberdeck_music_metadata.h` e não há
`$(MUSIC_METADATA_HDR)` na lista. Por isso, mudar só `metadata.h` deixa o
binário velho e o alvo reporta verde falso (aconteceu aqui: 361/361 sem
recompilar). O rebuild forçado `-B` provou a sensibilidade real. Correção é
test-only (adicionar o header aos pré-requisitos), pendente para um próximo
ciclo do tester; não alterei o Makefile neste dispatch.

### M11 — alocar proporcionalmente à tag/frame

Mutante em `metadata.cpp`: janela `new` de `tag_bytes` em vez de
`k_id3_read_window_bytes`; para frame-alvo, `want = frame.payload_bytes` em vez
de `min(payload, janela)`.

| Alvo | Comando | Resultado |
| --- | --- | --- |
| I/O real | `make -C tests/host/keymap test_music_metadata_io` | `185/190`, EXIT=2; FAIL 510/511 (READ-SKIP: leitura de TIT2 gigante > janela e total > texto), 622/623 (READ-OOM: `compact_largest == wide_largest` e `wide_largest <= janela`), 744 (janela UTF-16) → detectado |

Leituras pequenas dos fixtures normais continuam corretas, então só as
asserções de bound/alocação detectam — que é o objetivo do M11. Restauração:
`964fb918…e5042c`.

### M12 — truncar a saída antes da capacidade

Mutante em `model.cpp` (`decode_id3v2_text`): `--max` após os guards, parando
uma unidade antes da capacidade.

| Alvo | Comando | Resultado |
| --- | --- | --- |
| parser | `make -C tests/host/keymap test_music_model` | `352/361`, EXIT=2; FAIL 562/563 (elipse no limite), 579/580 (2 bytes exatos), 592/593 (`k_max_title_bytes`/`k_max_artist_bytes`) e demais limites → detectado |
| I/O real | `make -C tests/host/keymap test_music_metadata_io` | `187/190`, EXIT=2; FAIL 509 (TEXT-HUGE truncado), 742/743 (janela UTF-16 não preenche 96) → detectado |

Restauração: `5f5b4a3b…bb1085`.

### Estado final e revalidação

- Após as restaurações, os quatro focados foram reexecutados no snapshot
  `f6c2d78a…`: io `190/190`, model `361/361`, player `598/598`, app
  `438/438`, todos EXIT=0 (`/tmp/opencode/final8-*.log`).
- Produção conferida: `metadata.cpp 964fb918…`, `model.cpp 5f5b4a3b…`,
  `metadata.h 10c82bad…`, `player.cpp f5b7fe59…`; `git diff` sha256
  `f6c2d78a…`; `grep -rn MUTANT` → nenhum marcador; 25 entradas de status.
- Logs das mutações: `/tmp/opencode/mut-M10-*.log`,
  `/tmp/opencode/mut-M11.log`, `/tmp/opencode/mut-M12-*.log`.

### Situação das mutações M1–M12

M1 (agora M1'), M2, M3, M4, M5, M6, M7, M8, M9, M10, M11 e M12 executadas e
detectadas; M10 no parser exigiu `-B` por causa do defeito de dependência
acima. Nenhuma mutação fica pendente, mas a lacuna de wiring do Makefile e o
SIGFPE do harness sob M9 continuam registrados.

## Ciclo 9 — correção do wiring de dependência do header

### Correção (test-only)

`tests/host/keymap/Makefile`, regra do alvo de parser:

```make
$(MUSIC_MODEL_BIN): $(MUSIC_MODEL_TEST) $(MUSIC_MODEL_SRC) $(MUSIC_MODEL_HDR) $(MUSIC_METADATA_HDR) music_traceability.md Makefile
```

`test_music_model.cpp` inclui `apps/music/cyberdeck_music_metadata.h`, mas o
pré-requisito `$(MUSIC_METADATA_HDR)` não estava listado; por isso, mudança só
no header não recompilava o binário e o alvo reportava verde falso (visto no
M10 do ciclo 8). Nenhuma produção foi tocada.

### Validação de dependência

| Comando | Resultado |
| --- | --- |
| `make -C tests/host/keymap -n test_music_model` (após editar o Makefile) | mostra `g++ … test_music_model.cpp … -o test_music_model_bin` e `./test_music_model_bin` |
| `make -C tests/host/keymap -W ../../../components/cyberdeck/include/apps/music/cyberdeck_music_metadata.h -n test_music_model` | mostra a linha de compilação do TU + execução: o header agora invalida o binário |
| `make -C tests/host/keymap test_music_model` (estado restaurado) | recompilou e `361/361 checks passed`, EXIT=0 (`/tmp/opencode/fix-model-normal.log`) |

### M10 sem `-B` (prova de que a dependência vale)

Mutante: `metadata.h` `k_id3_max_tag_bytes = 64 * 1024`, aplicado e rodado
**sem `-B`**:

- `make -C tests/host/keymap test_music_model` → a linha
  `g++ … test_music_model.cpp …` aparece no log (recompilou sozinho), EXIT=2,
  `333/361 checks passed`; FAIL 313–316 (tag regular de 64 KiB+1 rejeitada) e
  354–357 em diante (walk com APIC 252112) — **detectado**
  (`/tmp/opencode/fix-model-M10-noB.log`).
- Restaurado `metadata.h` do `.orig` e conferido:
  `10c82bad0462e318f07f2a12e2bf1e25140a00109bf92e2f25ef8855bbe98673`.
- Nova execução normal: recompilou (linha `g++` no log), `361/361`,
  EXIT=0 (`/tmp/opencode/fix-model-restored.log`).

### Estado final

- Produção inalterada: `metadata.cpp` `964fb918…e5042c`,
  `model.cpp` `5f5b4a3b…bb1085`, `metadata.h` `10c82bad…699867`;
  `player.cpp` `f5b7fe59…af36da` (não tocado).
- `tests/host/keymap/Makefile` = `9b11c5dca802dc287637046d89c165c3c5d51bf5c700c0b04008c82620beab34`
  (única mudança: o pré-requisito do header); `test_music_model.cpp` continua
  `31d05a21…a936010`.
- `git diff | sha256sum` =
  `82d53970f97025b7dad79dc1e69b25326d6ad18baefbf7c2b88bdd8b9f8e9fc0`
  (test-only em relação ao `f6c2d78a…` do ciclo 8); `git status` 25 entradas;
  `grep -rn MUTANT` sem marcador.
- Sem suíte/build/verify/flash/playback. Só esta nota e o Makefile de teste
  foram editados.

## Ciclo 10 — contrato estrutural TEST-MUSIC-META-ID3-CONTRACT

### Implementação

Novo `tests/host/keymap/test_music_id3_contract.py`
(`be014e19b7bc17b5f641e92571d908cc801809fce6236be45e5e02a473be7ccd`),
registrado no Makefile como alvo `.PHONY test_music_id3_contract` com
pré-requisitos nos dois headers, nos dois fontes de música, no plano, no manual
e no próprio Makefile, e **uma** entrada no agregado `test`. O contrato
complementa (não substitui) os testes comportamentais; toda checagem deriva
propriedades dos arquivos, sem repetir contagens/valores literais de instância.

Cobertura do contrato:

- constantes únicas e exportadas no header de metadata
  (`k_id3v2_header_bytes == 10`, `k_id3_max_tag_bytes == 1 MiB`,
  `k_id3_read_window_bytes >= 2*k_max_title_bytes + 3`), sem redefinição nos
  `.cpp`; teto contado como `header + payload` (`payload + header > cap` e
  `tag_bytes = header + payload`);
- reader usa `pread` com offset explícito e não contém `open/openat/dup/dup2/
  fopen/close/lseek/mmap` nem `read()` sequencial, `std::string` ou
  `std::vector`; `read_exact` recebe o offset;
- uma única alocação `new (std::nothrow) unsigned char[...]`, exatamente
  `k_id3_read_window_bytes`; nenhum `malloc/calloc/realloc`; frame-alvo lido
  com `min(payload, k_id3_read_window_bytes)`;
- avanço `offset += frame.header_bytes + frame.payload_bytes` exatamente uma vez
  em cada caminho, sem `offset += 1`; rejeição de payload que cruza o fim nos
  dois caminhos; `size == 0` rejeitado; syncsafe v2.4 com bit 0x80 rejeitado;
- política de flags aprovada: máscara reservada por versão (`0x3f/0x1f/0x0f`),
  rejeição de `0x80 | 0x40 | reservados`, v2.2 `flags != 0`, detecção de flags
  de frame v2.3/v2.4 e rejeição de frame-alvo com flags nos dois caminhos;
- wiring: `test_music_metadata_io_bin` em `BINS` uma vez, `.PHONY` e wrapper
  únicos, regra do binário dependendo de teste/modelo/metadata (fontes e
  headers) e linkando `--wrap=pread/open/openat/dup/close`, agregado invocando
  o alvo uma vez; mesmo padrão para o próprio contrato;
- documentação: plano e manual citam “1 MiB”, “header + payload” e a constante
  `k_id3_max_tag_bytes`; toda menção remanescente a 64 KiB/64KiB/65536/`64 *
  1024` precisa de marcador histórico na vizinhança (±2 linhas), com nomes de
  fixture entre backticks isentos (ex.: `compact-64KiB`).

### Comandos e resultados

| Comando | Resultado |
| --- | --- |
| `make -C tests/host/keymap test_music_id3_contract` | `PASS: 1 MiB ceiling … docs document 1 MiB …`, EXIT=0 (`/tmp/opencode/final-contract.log`) |

Primeira execução acusou três falsos negativos do próprio contrato (comparação
de avanço com string regex-escapada, parsing de continuação de pré-requisitos e
marcador histórico quebrando linha); corrigidos no script e reexecutado PASS.

### Sensibilidade (mutações estruturais)

- **C1 — wiring**: removido `$(MUSIC_METADATA_HDR)` da regra
  `$(MUSIC_METADATA_IO_BIN)` → contrato falha com
  `FAIL: $(MUSIC_METADATA_IO_BIN) must depend on $(MUSIC_METADATA_HDR)`,
  EXIT=1 (`/tmp/opencode/mut-C1-wiring.log`); Makefile restaurado de
  `/tmp/opencode/tester-mut-c9/Makefile.orig` e conferido em `10a29a00…`.
- **C2 — alocação proporcional**: `new (std::nothrow) unsigned char[tag_bytes]`
  em `metadata.cpp` → contrato falha com
  `FAIL: the only allocation must be k_id3_read_window_bytes, not a
  tag/frame-sized expression (tag_bytes)`, EXIT=1
  (`/tmp/opencode/mut-C2-alloc.log`); fonte restaurado e conferido em
  `964fb918…`.

### Estado final

- Produção inalterada: `metadata.cpp 964fb918…`, `model.cpp 5f5b4a3b…`,
  `metadata.h 10c82bad…`, `player.cpp f5b7fe59…`.
- Testes: `Makefile 10a29a00…` (novo alvo + entrada no agregado e a correção
  do ciclo 9), `test_music_id3_contract.py be014e19…` (novo); demais testes
  `test_music_metadata_io.cpp 5f9f826e…`, `test_music_player.cpp 6f03dca5…`,
  `test_music_app.cpp 0ed05be4…` inalterados.
- `git diff | sha256sum` =
  `8743eb388069918653d86e15caf046743db546c56ea807fc4792d95d486154c5`;
  `git status` 26 entradas (novo untracked do contrato); sem marcador
  `MUTANT`; nenhuma execução de suíte/build/verify/flash/playback.

## Ciclo 11 — checkpoint docs-only e validade do reuso

### Snapshot e delta

O coder atualizou somente documentação desde `8743eb38…`; o snapshot sujo
informado/confirmado é `git diff` sha256
`aff8eda37593ad94eb862cbba70fdc68ab6fa622f6f92b6242080f33d1699b71`
(26 entradas de status, untracked excluídos). Arquivos alterados **depois** da
última execução minha (`final-contract.log`, 23:02:02), todos docs:

| Arquivo | mtime |
| --- | --- |
| `code-map.md` | 23:04:54 |
| `tests/host/keymap/music_traceability.md` | 23:05:20 |
| `tests/host/keymap/player_traceability.md` | 23:05:30 |
| `tests/manual/music-player-validation.pt-BR.md` | 23:05:43 |
| `docs/03-audio-player-plan.pt-BR.md` | 23:05:54 |

Nenhum fonte de produção, header, teste ou Makefile mudou depois de 23:01:53
(restauração byte-idêntica de `metadata.cpp`), portanto **não houve delta de
código/teste**, só de documentação. `version.txt` = `1.15.2` (22:40:28) e o app
declara `1.0.4` em `cyberdeck_music_app.cpp:27` (22:40:32). A
`music_traceability.md` atual registra **M1'–M12 (12/12)** executadas,
detectadas e restauradas (`:515-518`).

### Comparação hashes — medido vs. atual

Os hashes abaixo foram medidos/atribuídos no snapshot `f6c2d78a…` (final7/final8
e fix-model) e reconferidos agora; todos idênticos:

| Arquivo | Hash no snapshot medido | Hash agora | Veredito |
| --- | --- | --- | --- |
| `cyberdeck_music_metadata.cpp` | `964fb918…e5042c` | `964fb918…e5042c` | idêntico |
| `cyberdeck_music_model.cpp` | `5f5b4a3b…bb1085` | `5f5b4a3b…bb1085` | idêntico |
| `cyberdeck_music_metadata.h` | `10c82bad…699867` | `10c82bad…699867` | idêntico |
| `cyberdeck_music_player.cpp` | `f5b7fe59…af36da` | `f5b7fe59…af36da` | idêntico |
| `test_music_metadata_io.cpp` | `5f9f826e…329cd4b` | `5f9f826e…329cd4b` | idêntico |
| `test_music_model.cpp` | `31d05a21…a936010` | `31d05a21…a936010` | idêntico |
| `test_music_player.cpp` | `6f03dca5…6760eb75a` | `6f03dca5…6760eb75a` | idêntico |
| `test_music_app.cpp` | `0ed05be4…62e77f68` | `0ed05be4…62e77f68` | idêntico |
| `Makefile` | `10a29a00…0be933` | `10a29a00…0be933` | idêntico |
| `test_music_id3_contract.py` | `be014e19…be7ccd` | `be014e19…be7ccd` | idêntico |
| `test_music_metadata_contract.py` | `10a8585e…6dae680` | `10a8585e…6dae680` | idêntico |

Observação de mtime: `metadata.h` (22:57), `model.cpp` (22:54), `player.cpp`
(22:49) e `metadata.cpp` (23:01:53) têm mtime posterior ao snapshot medido
porque foram **restaurados byte-a-byte** após mutações; os hashes provam que o
conteúdo é o mesmo. Testes focados têm mtime anterior às execuções.

### Evidência reutilizável e alvos stale

- `190/190` (io), `598/598` (player) e `438/438` (app) foram medidos em
  `final8-*` (22:54:41–22:55:04) sobre `f6c2d78a…`, com os hashes de teste e de
  produção agora confirmados. **Reuso válido como evidência de código/teste**;
  não foram reexecutados desde a adição do alvo do contrato ao Makefile
  (23:01:44) nem no snapshot `aff8eda3…`.
- `361/361` (model) foi medido em `fix-model-restored.log` (22:57:08), já com o
  fix de dependência do header no Makefile; fonte/teste/header idênticos agora.
  Reuso válido como evidência de código/teste.
- **Alvo stale:** `test_music_id3_contract` (PASS em 23:02:02, snapshot
  `8743eb38…`). O plano e o manual são **pré-requisitos** do alvo e foram
  reescritos às 23:05, então o PASS não cobre o snapshot atual; precisa ser
  reexecutado antes de qualquer gate final (as checagens de docs podem ter
  mudado).
- `test_music_model` (e também io/player/app) listam `music_traceability.md`
  como pré-requisito da regra do binário; esse doc foi editado às 23:05 depois
  das execuções (22:54–22:57). Pelo grafo do Makefile os quatro alvos estão
  **out-of-date**, embora os hashes de código/teste sejam idênticos: os números
  são evidência válida do mesmo código, mas **não** são execuções do snapshot
  atual. Reexecutar os quatro (e o contrato) no gate final.
- Mutação e wiring: **M1'–M12 12/12 detectadas e restauradas** (ciclos 7–9),
  incluindo M10 sem `-B` após o fix de dependência (ciclo 9); contrato
  estrutural PASS no ciclo 10, agora stale apenas por docs.

### Escopo deste ciclo

Nenhum teste/build/verify/flash/playback executado; apenas esta nota foi
editada. Produção e testes não foram tocados (hashes acima).

## Ciclo 12 — reexecução focada no snapshot `aff8eda3…`

### Motivo e estado

Os alvos estavam out-of-date pelo grafo do Makefile: `Makefile` (23:01:44) e
`tests/host/keymap/music_traceability.md` (23:05:20) são pré-requisitos
declarados das regras de objeto/binário, mas os binários/objetos eram de
21:27–22:57. Reexecutei os alvos no snapshot atual, sem mutações e sem editar
código/testes.

Snapshot antes e depois (idêntico):

- `HEAD ff47b9a31c8d8a1fdb5746c60013619cf75c7481`, `version.txt 1.15.2`;
- `git diff | sha256sum` = `aff8eda37593ad94eb862cbba70fdc68ab6fa622f6f92b6242080f33d1699b71`;
- `git status --porcelain` = 26 entradas; produção e testes com os hashes do
  ciclo 11 (metadata `964fb918…`, model `5f5b4a3b…`, metadata.h `10c82bad…`,
  player `f5b7fe59…`; testes io `5f9f826e…`, model `31d05a21…`, player
  `6f03dca5…`, app `0ed05be4…`, `Makefile 10a29a00…`, contrato `be014e19…`).

### Comandos e resultados

Todos em `/home/moises/Projetos/cyberdeck5`; logs em
`/tmp/opencode/recheck-<alvo>.log`.

| Alvo | Comando | Resultado | Recompilou? |
| --- | --- | --- | --- |
| `test_music_metadata_io` | `make -C tests/host/keymap test_music_metadata_io` | `190/190 checks passed`, EXIT=0 (23:10:48) | 1 linha `g++` |
| `test_music_model` | `make -C tests/host/keymap test_music_model` | `361/361 checks passed`, EXIT=0 (23:10:51) | 1 linha `g++` |
| `test_music_player` | `make -C tests/host/keymap test_music_player` | `598/598 checks passed`, EXIT=0 (23:11:00) | 1 linha `g++` |
| `test_music_app` | `make -C tests/host/keymap test_music_app` | `438/438 checks passed`, EXIT=0 (23:11:12) | 2 linhas |
| `test_music_id3_contract` | `make -C tests/host/keymap test_music_id3_contract` | `PASS: 1 MiB ceiling … docs document 1 MiB …`, EXIT=0 (23:11:12) | não (Python) |
| `test_music_metadata_contract` | `make -C tests/host/keymap test_music_metadata_contract` | `PASS: the NOW PLAYING identity region … 64 + 15 ≤ 120`, EXIT=0 (23:11:12) | não (Python) |
| `test_shell_help_contract` | `make -C tests/host/keymap test_shell_help_contract` | `PASS: TEST-1 + TEST-HELP-… + wiring (8 mutations rejected)`, EXIT=0 (23:11:12) | não (Python) |
| `test_shell_help` | `make -C tests/host/keymap test_shell_help` | `243 checks, 0 failures`, EXIT=0 (23:11:24) | 25 linhas |
| `test_display_views` | `make -C tests/host/keymap test_display_views` | `display view tests passed`, EXIT=0 (23:11:28) | 9 linhas |

`test_display_views` **foi incluído** porque, embora não seja alvo de música,
suas regras de objeto listam `Makefile` como pré-requisito e os objetos eram de
21:09–21:27, anteriores à edição de 23:01:44; o rebuild (9 compilações)
confirma a invalidação e o resultado substitui a evidência antiga.

Nenhum `FAIL` nos logs; `grep -c FAIL` = 0 em todos. Nenhuma mutação foi
aplicada (M1'–M12 já têm evidência de detecção/restauração nos ciclos 7–9) e
nenhum teste foi alterado.

### Conclusão

- Evidência agora é do snapshot `aff8eda3…`, não apenas atribuível por hash:
  os nove alvos recompilaram quando necessário e passaram.
- O contrato estrutural voltou a ser válido para o texto atual dos docs.
- Pendências inalteradas: gate físico e gates finais do reviewer (suíte/verify/
  cobertura/build/CI não executados aqui).

## Ciclo 13 — blocker AC-MUSIC-META-27 (padding all-zero)

### Contexto

A revisão `development` (reviewer, 23:14) confirmou um **blocker**: um byte
`0x00` no início da área restante encerrava a caminhada nos dois caminhos e o
resto da área declarada não era validado; tag com texto válido + `0x00` + lixo
não-zero publicava metadados, violando AC-MUSIC-META-27 e o all-or-nothing.
O coder (ciclo 9) reproduziu e corrigiu produção + testes. Li as notas do
coder/reviewer e inspecionei o delta contra as cópias pré-fix.

### Mudança inspecionada

- `model.cpp::parse_tag`: removida a flag `padding` que pulava a validação; o
  `break` no byte zero agora apenas encerra a caminhada e a varredura
  `for (i = offset; i < end; ++i) if (p[i] != 0) return false;` roda **sempre**
  (buffer já em memória, sem I/O nem alocação).
- `metadata.cpp`: `read_padding` (leitura única do resto) substituída por
  `read_zero_padding`, que valida `[offset, tag_bytes)` em blocos de no máximo
  `k_id3_read_window_bytes` (196 B) na janela fixa já alocada; short read/erro
  rejeitam; a posição do fd não muda e não há alocação nova.
- `metadata.h`: contrato documenta rejeição por byte não-zero depois do último
  frame (padding precisa ser todo zero).
- Testes do coder: parser com `TIT2` válido + 1 zero + `header+6` bytes `'x'`
  (rejeita, sentinelas preservadas), lixo profundo a `2*window+3` (rejeita) e
  padding zero do mesmo tamanho (aceita); streaming com os três casos em
  arquivo real e, no positivo, `probe.largest <= k_id3_read_window_bytes`,
  `probe.total <= window*16` e `probe.highest_end == tag_bytes` (prova que a
  validação lê até o fim declarado em mais de um bloco).

### Comandos e resultados (snapshot `fc54a8dd…`)

| Alvo | Comando | Resultado |
| --- | --- | --- |
| parser | `make -C tests/host/keymap test_music_model` | `367/367 checks passed`, EXIT=0 (`/tmp/opencode/pad-test_music_model.log`) |
| I/O real | `make -C tests/host/keymap test_music_metadata_io` | `205/205 checks passed`, EXIT=0 (`/tmp/opencode/pad-test_music_metadata_io.log`) |

Contagens anteriores (361/190) foram superadas pelas novas asserções; nenhum
`FAIL`. Nenhum teste precisou ser ajustado pelo tester (cobertura do coder
suficiente: negativo raso, negativo profundo e positivo com prova de leitura).

### Sensibilidade de mutação (pular validação do padding)

Cada mutante foi aplicado sozinho, rodou só o alvo afetado e foi restaurado com
`cp` do `.orig` e sha256 conferido.

| Mut. | Mutante | Alvo | Detecção | Restauração |
| --- | --- | --- | --- | --- |
| PAD-A | `metadata.cpp`: validar só resto curto (`tag_bytes - offset < header && read_zero_padding(...)`) — aceita zero+lixo | `test_music_metadata_io` | EXIT=2, `202/205`; FAIL 718 (`preserved` do zero-then-garbage), 729 (`preserved` do deep garbage) e 744 (`probe.highest_end == full.size()` — o positivo deixa de ler o padding) | `3141220b…649733` |
| PAD-B | `model.cpp`: varrer só resto curto (`if (end - offset < frame_header_bytes) { … }`) — aceita zero+lixo | `test_music_model` | EXIT=2, `363/367`; FAIL 471/472 (zero-then-garbage: parse e `preserved`) e 481/482 (deep garbage) | `4f9683ec…5fbfd1b` |

Após as restaurações, os dois alvos voltaram a `367/367` e `205/205`, EXIT=0
(`pad-final-*`), com hashes de produção exatamente os do snapshot da correção;
sem marcador `MUTANT`.

### Custo do padding e classificação contra os ACs

- Janela fixa = 196 B (`2*k_max_title_bytes + 4`). Padding de 1 MiB
  (1.048.576 B) ⇒ `ceil(1048576/196) = 5350` `pread` de padding (mais os
  headers/frames; ~5,35 mil, compatível com o “~5,5k” do coder).
- Casos reais: padding do arquivo do usuário (1757 B) ⇒ 9 leituras extras;
  fixture legado de 70 KiB ⇒ 366. Só ocorre quando há byte zero abrindo a área.
- AC-24 (mesmo fd/offsets): preservado — `pread` explícito, sem `open/dup/close`
  e sem mover a posição.
- AC-25 (I/O bounded, sem payload de APIC, sem alocação proporcional):
  preservado — cada leitura ≤ janela, uma única alocação fixa, total lido ≤
  tamanho da tag ≤ teto; o custo agora é **O(padding)** em número de syscalls,
  inerente a validar AC-27.
- AC-26 (teto 1 MiB) e AC-27 (all-or-nothing): preservados/fortalecidos; o
  positivo prova que a área é lida até o fim declarado.
- Impacto físico: no pior caso (tag de 1 MiB quase toda padding), ~5350
  `pread` por arquivo podem pesar no scan de vários arquivos no SD; a latência
  real não foi medida em host. Classificação: **risco aceito/pendente de
  medição** (`TEST-MUSIC-META-PHYS-SCAN-COST`), não violação de AC nem defeito
  novo. Alternativas (chunk maior só para padding/cache) mudariam a redação da
  janela fixa e exigem decisão de contrato — não implementadas.

### Escopo

Sem suíte/build/verify/flash/playback e sem edição de produção/testes neste
ciclo; só esta nota foi escrita. A mudança de docs feita pelo coder
(`code-map`, `music_traceability`, plano) não foi revalidada por mim além da
leitura.

## Work done

### Interfaces confirmadas no código (base de cada cenário)

- `cyberdeck_music::metadata` / `track` são bounded e hoje têm **só**
  `title` e `artist` (`include/apps/music/cyberdeck_music_model.h:29-55`).
  `replace_queue` valida terminação NUL de `path`/`title`/`artist`
  (`cyberdeck_music_model.cpp:263-279`).
- Parser `parse_id3v2` aceita v2.2/2.3/2.4, mapeia `TT2/TIT2` e `TP1/TPE1`,
  rejeita tag > 64 KiB / header inválido / tamanho 0 / frame que estoura o fim,
  e decodifica encodings 0/1/2/3 com transliteração para o repertório da
  `cyberdeck_font` (`cyberdeck_music_model.cpp:195-255`, `44-186`).
- `read_metadata(fd,size,out)` lê só o prólogo ID3v2 do mesmo descritor já
  aberto, buffer no heap limitado por `k_id3_metadata_limit`, e só sobrescreve
  campo não vazio (`cyberdeck_music_metadata.cpp:16-38`).
- Worker publica a identidade da faixa em `state.current` sob
  `state_mutex` **e** guarda de geração, com `state.current = current`
  (atribuição integral) antes de iniciar decode; título começa como basename
  sanitizado e é sobrescrito por `TIT2` quando presente
  (`cyberdeck_music_player.cpp:394-422`). `publish_error` publica
  `state=error` e **preserva** `state.current` anterior
  (`cyberdeck_music_player.cpp:277-306`); `status()` copia o snapshot sob o
  mesmo mutex (`~:880-895`).
- App monta o texto de status a partir de `info.current`
  (`cyberdeck_music_app.cpp:558-587`); `music list` só usa `title`
  (`:614-626`).
- View: card NOW PLAYING tem caption `now_title_` ("NOW PLAYING"), `status_`
  (3 linhas, filtradas do texto de status) e `render_browser()` **sobrescreve**
  `status_` com "Folders and MP3 files"/"No folders or MP3 files"
  (`cyberdeck_music_view.cpp:240-329` para `render`, `:331-372` para o
  overwrite, `:491-501` para geometria). Ou seja, hoje o texto de
  título/artista é destruído a cada tick quando o browser está ativo — a
  motivação técnica da região dedicada.
- UI já tem o snapshot em mão: `process_music_status` chama
  `music.player_snapshot(playback)` para o header e depois
  `s_music_view.render(text)` + `render_browser(...)`
  (`cyberdeck_ui.cpp:1007-1064`).
- Evidência de device disponível: `ui.dump` serializa labels com clamp de
  `ui_dump::k_max_text_bytes` e no máximo `k_max_nodes`
  (`include/apps/serial/cyberdeck_ui_dump_serializer.h`); `test_display_views`
  executa a TU LVGL real sob shim e ancora filhos por índice
  (`test_display_views.cpp:533-700`), o que exige atenção ao inserir labels.

### Especificação — REQ / AC

Convenção nova e isolada (`REQ-MUSIC-META-*`), sem reutilizar ids existentes
(`REQ-MUSIC-0x`, `REQ-MUSIC-UI-*`, `REQ-MUSIC-DUR-*`).

**REQ-MUSIC-META-01 — ler álbum do ID3v2 existente, bounded**

- `AC-MUSIC-META-01`: `parse_id3v2` preenche álbum a partir de `TALB`
  (v2.3/v2.4) e `TAL` (v2.2), pelo mesmo caminho de decodificação de
  title/artist (encodings 0/1/2/3, transliteração tipográfica, code point fora
  do repertório da fonte vira `?`). Frame duplicado: o último não-vazio vence,
  igual a title/artist.
- `AC-MUSIC-META-02`: frame presente e vazio limpa o campo; frame ausente deixa
  o campo intocado no overload `metadata` e vazio no overload `track`; encoding
  não suportado limpa (não herda lixo).
- `AC-MUSIC-META-03`: capacidade de álbum é uma constante nomeada única;
  truncagem entre code points, saída sempre UTF-8 válida e NUL-terminada,
  sem escrita fora do array (ASan/UBSan no alvo).
- `AC-MUSIC-META-04`: falha fechada — header inválido, versão fora de 2..4,
  syncsafe com bit 0x80, tag > limite existente, frame de tamanho 0 ou que
  ultrapassa o fim ⇒ `parse_id3v2` retorna false e **nenhum** campo do álbum é
  publicado parcialmente (título/artista também não são publicados nesse
  retorno).
- `AC-MUSIC-META-05`: `read_metadata` preenche álbum só quando o valor
  decodificado não é vazio (mesma regra de title/artist), respeita `size`
  menor que o tag, mantém o buffer de heap no limite atual e não adiciona
  leituras além da janela bounded.

**REQ-MUSIC-META-02 — transportar a identidade da faixa em execução pelo snapshot `current`**

- `AC-MUSIC-META-06`: `snapshot.current.{title,artist,album}` correspondem ao
  **path do request que está tocando** (o mesmo path usado para abrir o
  stream), não ao índice selecionado na fila nem à seleção do browser.
- `AC-MUSIC-META-07`: substituição integral — de A (título+artista+álbum) para
  B (só título), nenhum campo de A pode permanecer em `current` (artista e
  álbum ficam vazios, não herdados).
- `AC-MUSIC-META-08`: guarda de geração preservada — publicação de request
  obsoleto não altera `current`; após rajada A→B o snapshot final é B, sem
  reaparecimento de tokens de A.
- `AC-MUSIC-META-09`: `state=error` não apresenta metadado de faixa alheia: se
  o play de B falha enquanto A era `current`, o snapshot não pode expor o path
  de A como faixa corrente (aceito: identidade de B a partir do basename, ou
  `current` vazio/indisponível). `failure_stage` continua correto.
- `AC-MUSIC-META-10`: pause/resume/stop preservam os três campos; `select`,
  `set_queue` e next/prev **sem** play não alteram `current`.
- `AC-MUSIC-META-11`: leitura atômica — `status()` nunca devolve tripla
  misturada (título de uma faixa com álbum de outra). Regressão observável, não
  prova formal.

**REQ-MUSIC-META-03 — renderizar os três campos em região própria, independente do browser**

- `AC-MUSIC-META-12`: a view expõe seam read-only com os três textos
  renderizados e o flag de fallback do título (ex.: accessor que devolve
  `title/artist/album` + flag). O texto renderizado é **exatamente** o valor
  recebido da produção (sem re-sanitização, sem parse do texto de status).
- `AC-MUSIC-META-13`: `render_browser()` não altera nem limpa a região;
  mudanças no texto passado a `render()` não alteram a região; conteúdo
  independe de lista da fila, entradas do browser e seleção do browser.
- `AC-MUSIC-META-14`: campo ausente/vazio mostra placeholder único e estável
  (constante de produção), não vazio, distinto do texto de fallback e de
  qualquer valor de tag; sem nenhum play ainda (`current` vazio) os três campos
  aparecem indisponíveis.
- `AC-MUSIC-META-15`: fallback de título identificável por máquina: flag
  (`title_from_filename`, nome final fixado pelo coder) verdadeiro sse não
  houve `TIT2`; texto = basename sanitizado **com** extensão; `TIT2` presente
  (mesmo idêntico ao nome do arquivo) ⇒ flag falso.
- `AC-MUSIC-META-16`: três linhas de uma linha cada, `LV_LABEL_LONG_MODE_DOTS`,
  largura ≤ largura interna do card, sem `\n`; sem sobreposição com caption
  NOW PLAYING, status, progresso, tempo, volume, Command e controles em
  portrait e landscape, com Command fechado e aberto; resize nos dois sentidos
  reaplica geometria; a lista continua sendo o único scroll vertical.
- `AC-MUSIC-META-17`: lifecycle — create→render→destroy→create volta a
  renderizar a região; após `destroy` não há texto residual nem callback extra.

**REQ-MUSIC-META-04 — custos bounded e legibilidade no device**

- `AC-MUSIC-META-18`: literais novos (prefixos/placeholder/marcador) ficam em
  `0x20–0x7E` e cobertos pela `cyberdeck_font`; o contrato de glifos existente
  passa sem exceção nova.
- `AC-MUSIC-META-19`: `k_max_album_bytes` + prefixo literal mais longo ≤
  `ui_dump::k_max_text_bytes` (derivado dos dois headers), para que `ui.dump`
  mostre o álbum inteiro no device; nenhuma nova task nem buffer de stack
  grande nos caminhos de scan/player (contratos estruturais existentes verdes).

**REQ-MUSIC-META-05 — sem ampliação de escopo nem regressão**

- `AC-MUSIC-META-20`: `music status`, `music list` e `music help` mantêm as
  linhas/ordem atuais e **não** ganham linha de álbum; comandos, formatos
  aceitos (`.mp3` com ID3v2) e dependências inalterados; ID3v1/outros formatos
  continuam fora.
- `AC-MUSIC-META-21`: playback inalterado — mesmos contadores
  `generations_queued/started/retired/failures`, mesma sequência
  open→id3→probe→decode→codec→write, e nenhuma abertura extra do SD por
  metadado (álbum vem do mesmo fd já aberto).
- `AC-MUSIC-META-22`: asserções existentes de player/view/app preservadas; se
  a inserção de labels deslocar índices de filhos em `test_display_views`, as
  mesmas asserções são reescritas contra os novos índices ou contra o accessor
  — nunca removidas nem enfraquecidas. O header continua mostrando apenas o
  indicador de playlist.

### Especificação — TEST (cenários independentes)

Alvos: `make -C tests/host/keymap <alvo>`. Cada TEST indica alvo, o que prova e
a obrigação de mutação (sensibilidade da asserção).

**Grupo parser** (`test_music_model`; item 6 em `test_music_player`, que linka
`cyberdeck_music_metadata.cpp`)

| TEST | Alvo | Prova |
| --- | --- | --- |
| TEST-MUSIC-META-PARSE-ALBUM | `test_music_model` | `TALB` em v2.3 e v2.4 e `TAL` em v2.2 preenchem álbum; `TT2/TIT2`, `TP1/TPE1` continuam corretos nas três versões; frame duplicado: último não-vazio vence. Mutação: trocar os ids `TAL`↔`TALB` entre versões deve falhar. |
| TEST-MUSIC-META-PARSE-ENCODINGS | `test_music_model` | Álbum em latin-1, UTF-16 LE/BE (com e sem BOM), UTF-8 e tipográficos decodifica igual a título; encoding não suportado (4+) limpa; frame vazio limpa; frame ausente não altera no overload `metadata`; bytes não-UTF8 viram `?` sem consumir o resto. Mutação: remover o `seen.album = true` do frame vazio deve falhar. |
| TEST-MUSIC-META-PARSE-LIMIT | `test_music_model` (ASan/UBSan) | Álbum de exatamente `k_max_album_bytes` passa inteiro; `+1` byte é truncado entre code points (não corta sequência UTF-8), com NUL e sem overflow; code point de 4 bytes no limite vira `?` inteiro. Mutação obrigatória: substituir o par `(cap, cap+1)` por `(cap-2, cap-1)` e inverter a comparação de capacidade — o teste deve falhar nos dois casos. |
| TEST-MUSIC-META-PARSE-FAIL-CLOSED | `test_music_model` | Matriz de falha (header, versão 1/5, 0xff reservado, syncsafe 0x80, tag > limite, frame tamanho 0, frame que ultrapassa o fim) ⇒ retorno false e álbum não publicado; `metadata`/`track` de entrada não recebem valor parcial. |
| TEST-MUSIC-META-PARSE-REGRESSION | `test_music_model` | Toda a matriz atual de título/artista/encoding/limite/`sanitize_display_text` permanece verde sem reescrita de expectativa (só adição). |
| TEST-MUSIC-META-READ-FD | `test_music_player` | `read_metadata` sobre arquivo real: preenche álbum; não sobrescreve com frame vazio; respeita `size` menor que o tag (nada de ler além); arquivo sem `ID3` deixa `out` intacto; número de `pread`/janela continua bounded. |

**Grupo transporte/identidade** (`test_music_player`, player+decoder reais,
saída de áudio gated; reusar `write_id3_tagged_mp3`/`begin_gated_playback`)

| TEST | Prova |
| --- | --- |
| TEST-MUSIC-META-CURRENT-TAGGED | Play de arquivo com TIT2+TPE1+TALB ⇒ `snapshot.current` com os três valores exatos, `path` = path do request, `state=playing`, `failure_stage=none`. Mutação: ler álbum do índice da fila em vez do request deve falhar. |
| TEST-MUSIC-META-CURRENT-SWITCH-NO-STALE | A(título+artista+álbum) → B(só título): após B, artista e álbum vazios (sem herança). Variante inversa: B com álbum e sem artista. Mutação: `state.current.title = ...` campo a campo sem limpar os demais deve falhar. |
| TEST-MUSIC-META-CURRENT-GENERATION | Rajada A→B antes de A publicar metadado: snapshot final é B; tokens exclusivos de A (título/artista/álbum) não reaparecem em consultas subsequentes; `generations_retired` conta os aposentados. |
| TEST-MUSIC-META-CURRENT-ERROR | Play de B malformado após A tocando: `state=error` com `failure_stage` correto e `current` sem o path de A (identidade de B via basename ou vazio). Mutação: manter `current` de A deve falhar. |
| TEST-MUSIC-META-CURRENT-PAUSE-STOP-SELECT | `pause`/`resume`/`stop` preservam a tripla; `select(n)`/`set_queue` sem play não mudam `current`; `stop` mantém a última faixa identificável. |
| TEST-MUSIC-META-CURRENT-CONSISTENCY | Loop de `status()` durante A→B nunca observa tripla misturada (tokens únicos por faixa). Regressão observável (não prova formal); TSan fica no gate do reviewer. |
| TEST-MUSIC-META-CURRENT-NO-EXTRA-OPEN | Cada play faz exatamente uma abertura segura (contagem do fake `secure_open_paths`) e o álbum não introduz acesso novo a SD/`pread` extra fora da janela já usada. |

**Grupo app** (`test_music_app`)

| TEST | Prova |
| --- | --- |
| TEST-MUSIC-META-APP-SNAPSHOT | `player_snapshot` do app expõe álbum da faixa corrente (fake player) e `music status` **não** contém o valor do álbum (negativo exato), mantendo as linhas atuais. |
| TEST-MUSIC-META-APP-QUEUE-UNAFFECTED | `music list` mantém formato `n. <title>` (sem álbum); `replace_queue` aceita álbum vazio e rejeita álbum sem NUL (mesma validação defensiva de title/artist). |

**Grupo view** (`test_display_views`, TU LVGL real sob shim; novo alvo por TU
seguindo o padrão `test_display_views/music_view.o`)

| TEST | Prova |
| --- | --- |
| TEST-MUSIC-META-VIEW-FIELDS | Accessor devolve a tripla exata recebida; cada label tem esse texto, uma linha, `LV_LABEL_LONG_MODE_DOTS`, largura ≤ largura interna. Mutação: renderizar valor diferente do accessor deve falhar. |
| TEST-MUSIC-META-VIEW-BROWSER-INDEPENDENT | Após `render(text)` e `render_browser(snapshot, scanning)` com diretório/entradas/seleção arbitrários, a região permanece byte-idêntica; dois renders de browser consecutivos não a alteram. Mutação: voltar a escrever `status_` a partir do browser deve falhar. |
| TEST-MUSIC-META-VIEW-UNAVAILABLE | Artista e álbum vazios ⇒ placeholder constante para os dois, não vazio, ASCII, diferente do texto de fallback e de tokens de tag; `current` vazio ⇒ três indisponíveis. |
| TEST-MUSIC-META-VIEW-FALLBACK | Flag verdadeiro ⇒ texto = basename sanitizado com extensão (inclusive casos `bad?.mp3`, acento e basename no limite); flag falso com `TIT2`; `TIT2` igual ao basename ⇒ flag falso. |
| TEST-MUSIC-META-VIEW-LAYOUT-PORTRAIT | Em 720x1280: região dentro do card, sem sobrepor caption/status/progresso/tempo/volume/controles; cards acima da faixa do footer; só a lista rola vertical. |
| TEST-MUSIC-META-VIEW-LAYOUT-LANDSCAPE | Idem em 1280x720 e no resize portrait→landscape→portrait→landscape (geometria reaplicada, sem label órfão). |
| TEST-MUSIC-META-VIEW-COMMAND-OPEN-COEXIST | Com `set_command_open(true)` a região continua visível e sem sobreposição com o textarea (que cresce) nem com os controles, em ambas as orientações; `command_open` fechado restaura. |
| TEST-MUSIC-META-VIEW-TRUNCATION | Texto no limite da capacidade é passado verbatim ao label; a truncagem visual é responsabilidade do DOTS. Limite do shim registrado: o host **não** prova pixels de DOTS, só `long_mode`, largura e ausência de `\n`. |
| TEST-MUSIC-META-VIEW-LIFECYCLE | create→destroy→create renderiza a região de novo, sem texto residual e sem callbacks extras; destruição não deixa ponteiro usado. |

**Grupo contratos estruturais** (Python, no agregado)

| TEST | Prova |
| --- | --- |
| TEST-MUSIC-META-UI-WIRING | Contrato sobre `cyberdeck_ui.cpp`: a região é alimentada por `playback.current` de `player_snapshot`, nunca por `render(text)` de status, `music list` ou `render_browser`; `render_browser` não escreve os labels da região. Mutação: trocar a fonte para o snapshot do browser deve falhar. |
| TEST-MUSIC-META-GLYPH-ASCII | Os literais novos (rótulos, placeholder, marcador de fallback) estão dentro da cobertura real da `cyberdeck_font` (reuso de `test_music_player_ui_contract.py`); mutação: inserir code point fora do cmap deve falhar. |
| TEST-MUSIC-META-BOUNDS-DUMP | Invariante derivado: `k_max_album_bytes` + prefixo literal mais longo ≤ `ui_dump::k_max_text_bytes`; garante que `ui.dump` no device mostre o álbum completo. |

### Pré-condições da validação manual no Tab5

Gate comum (bloqueia o resto): `sys.info` com `safe_mode=false`,
`interrupted_boots=0`, uptime monotônico, checkpoint/`CYBERDECK5 iniciado` no
log, Wi-Fi/RPC Hosted sem degradação; registrar `fw_version` + SHA-256 do
`build/cyberdeck5.bin` gravado. Sessão serial aberta uma vez (reabrir provoca
`CHIP_USB_UART_RESET`); sem abrir `idf.py monitor` durante o playback.

Oráculo independente do firmware — preparar no PC e registrar **antes** de
copiar para `/musica`:

- (A) MP3 com `TIT2`+`TPE1`+`TALB` (v2.3), valores ASCII e acentuados;
- (B) MP3 só com `TIT2` (sem `TPE1`/`TALB`);
- (C) MP3 sem ID3v2 (fallback de basename; incluir um nome longo próximo da
  capacidade para checar truncagem);
- (D) opcional: MP3 com tag UTF-16/BOM e tipográficos.
- Evidência esperada dos três primeiros: `ffprobe -show_entries
  format_tags=title,artist,album` (ou exiftool/mid3v2) — os textos exibidos
  devem bater com esse dump, não com uma leitura subjetiva.

Cenários manuais (cada um com evidência própria):

- `TEST-MUSIC-META-PHYS-PORTRAIT`: a partir da shell, `music queue /musica` →
  `music list` (confirmar faixas) → `music play <A>` (uma tentativa) →
  `music` para abrir a surface → `ui.dump` (textos dos três labels, ≤120 B) e
  `screen.dump` (BMP para inspeção de truncagem/sobreposição). Esperado:
  título/artista/álbum iguais ao oráculo A; `sys.info` com uptime monotônico.
- `TEST-MUSIC-META-PHYS-LANDSCAPE`: girar fisicamente o Tab5 e repetir
  `ui.dump`+`screen.dump`: três linhas legíveis, sem sobreposição nem clipping.
  Se a rotação não puder ser exercida com segurança, registrar landscape como
  pendente (não inferir por host).
- `TEST-MUSIC-META-PHYS-SWITCH`: com o player aberto, `ui.tap "Next"` (ou
  `Esc` + `music next`) para a faixa B: artista/álbum mostram o placeholder de
  indisponível, título mostra o `TIT2` de B; voltar para A restaura os três.
  Depois `music stop` (via shell, após `Esc`): a tripla de A/B persiste e não
  aparece metadado de outra faixa. Nenhum reboot; uptime monotônico.
- `TEST-MUSIC-META-PHYS-UNAVAILABLE`: com o player aberto e `state=stopped`
  antes de qualquer play (reabrir o app sem tocar), os três campos aparecem
  indisponíveis e **não** o metadado da faixa selecionada na fila.

Gotchas operacionais já observados no projeto: com a surface do player em
foreground, `ui.type` não chega à shell (usar comandos antes de abrir, ou
`ui.tap "Command"`); `ui.dump` trunca a lista de nós (64 nós / 3500 B) — se os
labels da região não aparecerem, usar `screen.dump` e registrar a limitação;
tap antes de `create()/activate()` é consumido sem efeito — aguardar a surface.

Fora de escopo manual desta entrega: acústica speaker/jack, fone USB/P2,
A2DP; exigem autorização explícita separada.

## Execução pós-implementação (ciclos 2-4)

### Reset de orçamento (início deste ciclo)

O ciclo anterior do tester terminou por limite de orçamento com uma mutação
ainda aplicada em
`components/cyberdeck/src/platform/display/cyberdeck_music_view.cpp`, e a nota
tinha ficado na versão do ciclo 1. Esta seção registra a recuperação e o estado
real executado.

Mutação que ficou aplicada (diff contra a cópia pré-mutação):

```diff
 void view::resized(lv_event_t *event) {
     auto *self = static_cast<view *>(lv_event_get_user_data(event));
-    if (self) self->layout();
+    if (self && lv_obj_get_width(self->surface_) > lv_obj_get_height(self->surface_)) self->layout();
 }
```

Ela inverte o gate de orientação: em surface portrait (720x1280) `layout()`
deixa de rodar e a região conserva a geometria landscape.

Restauração (somente esse arquivo, como autorizado):

- cópia `/tmp/opencode/tester-mut-view.cpp` com sha256
  `50011e11096ac14b3420e47b3cfa2518b50814ada1e473ced29ec7a6eedbbfa1`
  (o hash exigido foi conferido **antes** de copiar);
- destino mutado antes da restauração:
  `087b407decc5c6223a94da161a7f96fd7cc6c29e57f46da80e17a9813711339e`;
- após `cp`: destino com sha256
  `50011e11096ac14b3420e47b3cfa2518b50814ada1e473ced29ec7a6eedbbfa1`,
  idêntico à cópia; `cmp` sem diferença.

### Ausência de resíduo de mutação (produção)

Todos os fontes de produção que receberam mutação em ciclos anteriores batem
byte-a-byte com as cópias pré-mutação:

| Fonte | sha256 atual | Cópia pré-mutação (idêntica) |
| --- | --- | --- |
| `cyberdeck_music_view.cpp` | `50011e11…dbbfa1` | `tester-mut-view.cpp`, `view-mut.bak`, `view-mut2.bak` |
| `cyberdeck_music_model.cpp` | `abe9f48e…a9a6df` | `model-mut.bak`, `model-mut2.bak`, `model-mut3.bak` |
| `cyberdeck_ui.cpp` | `848cf9fd…6fe020` | `ui-mut.bak` |
| `cyberdeck_music_player.cpp` | `f5b7fe59…af36da` | `tester-mut-player.cpp` |

`grep -rn "MUTATION|MUTANT|mutant|tester-mut"` em `components/`, `tests/` e
`main/` retorna apenas as constantes `MUTATIONS`/`TIER_A_MUTATIONS`
pré-existentes de `tests/host/keymap/test_incremental_batch_contract.py`
(mecanismo do próprio harness, sem relação com esta entrega).

### Alvos executados no ciclo 3 (resultados reais)

Na raiz do repositório, sem suíte completa, sem `verify`, sem cobertura, sem
build ESP-IDF e sem flash:

- `make -C tests/host/keymap test_display_views` → `EXIT=0`,
  `display view tests passed`. O build recompilou
  `test_display_views/music_view.o` a partir do fonte restaurado e religou o
  binário antes de executar, então o PASS é do código restaurado.
- `make -C tests/host/keymap test_music_player` → `EXIT=0`,
  `560/560 checks passed`.

### Ciclo 4 — reexecução final delimitada (versão `1.15.1`)

Objetivo do dispatch: evidência independente sobre o snapshot final, já que o
`git status` registrado no ciclo 3 estava stale (relatava só
`?? docs/agent-state/music-track-metadata/`) e `version.txt` mudou para
`1.15.1` depois da maior parte das execuções. Execução restrita: apenas os 10
alvos focados; sem mutação nova, sem suíte completa, sem `verify`, sem
cobertura, sem build ESP-IDF e sem flash. Nenhum arquivo de produção ou
documento de projeto foi editado.

Snapshot antes e depois da rodada (idêntico):

- `HEAD` = `ff47b9a31c8d8a1fdb5746c60013619cf75c7481`; `version.txt` =
  `1.15.1`;
- `git diff | sha256sum` =
  `86f94113faeab257990aeac3c62513a04811cbb536737686107bfbffc312cf54`
  (mesmo hash informado no dispatch);
- `git status --porcelain` = 23 entradas: 21 arquivos modificados + os não
  rastreados `docs/agent-state/music-track-metadata/` e
  `tests/host/keymap/test_music_metadata_contract.py`
  (sha256 `10a8585e…6dae680`, inalterado).

Ausência de resíduo de mutação (verificada antes e depois da rodada):

- `cmp` byte-a-byte idêntico contra as cópias pré-mutação:
  `cyberdeck_music_model.cpp` ≡ `/tmp/opencode/model-mut.bak`
  (`abe9f48e…a9a6df`), `cyberdeck_music_view.cpp` ≡
  `/tmp/opencode/tester-mut-view.cpp` (`50011e11…dbbfa1`),
  `cyberdeck_ui.cpp` ≡ `/tmp/opencode/ui-mut.bak` (`848cf9fd…6fe020`),
  `cyberdeck_music_player.cpp` ≡ `/tmp/opencode/tester-mut-player.cpp`
  (`f5b7fe59…af36da`).
- `grep -rniE "tester-mut|MUTANT|MUTATION"` nos fontes de música/view e nos
  quatro testes C++ só encontra o comentário legado
  `cyberdeck_ui.cpp:2739` ("the exact UI-side mutation the legacy switch
  used"), sem código mutante.

Execução (sequencial, na raiz do repositório; logs em
`/tmp/opencode/tester-final/<alvo>.log`):

| Alvo | Resultado |
| --- | --- |
| `test_music_model` | `EXIT=0`, `290/290 checks passed` |
| `test_music_player` | `EXIT=0`, `560/560 checks passed` |
| `test_music_app` | `EXIT=0`, `428/428 checks passed` |
| `test_display_views` | `EXIT=0`, `display view tests passed` |
| `test_music_metadata_contract` | `EXIT=0`, 1 `PASS`, 0 `FAIL` |
| `test_music_player_ui_contract` | `EXIT=0`, 1 `PASS`, 0 `FAIL` |
| `test_music_scan_stack_contract` | `EXIT=0`, 24 `PASS`, 0 `FAIL` |
| `test_music_player_teardown_contract` | `EXIT=0`, 14 `PASS`, 0 `FAIL` |
| `test_shell_help_contract` | `EXIT=0`, 2 `PASS`, 0 `FAIL` |
| `test_shell_help` | `EXIT=0`, `243 checks, 0 failures` |

Nenhum alvo falhou; zero alvos faltantes (10/10 executados).

Por que a ausência de recompilação não invalida a evidência: o make não
recompilou nenhum binário (logs sem linhas de compilação) porque todos os
artefatos já eram mais novos que seus pré-requisitos declarados. Conferido:

- `test_music_model_bin` 21:11:43 > `cyberdeck_music_model.cpp` 21:08:56;
- `test_music_app_bin` 21:22:51 > `cyberdeck_music_app.cpp` 21:18:06;
- `test_music_player_bin` 21:25:10 > `cyberdeck_music_player.cpp` 21:24:58;
- `test_display_views_bin` 21:27:46 > `cyberdeck_music_view.cpp` 21:27:09;
- headers da entrega mais antigos que os bins
  (`cyberdeck_music_metadata.h` 20:51, `cyberdeck_music_model.h` 20:51,
  `cyberdeck_music_view.h` 20:57).

O diff sha256 foi o mesmo antes e depois, logo os bytes dos fontes no build
são os do snapshot final. Adicionalmente, nenhum desses 10 alvos lê
`version.txt` (`grep -l "version.txt\|1\.15\."` nos 10 arquivos → sem
correspondência), então o bump `1.15.1` não altera o resultado esperado
desses testes.

### Sensibilidade de mutação (evidência recuperada)

A mutação de `resized()` foi detectada pela asserção de orientação de
`test_display_views`:

- `sh_1b88c34ae002…`:
  `test_display_views_bin: test_display_views.cpp:1339: main()::<lambda(bool)>:
  Assertion 'now_playing->width == browser->width' failed` — é
  `assert_orientation(false)` na volta portrait→landscape→portrait;
- `sh_1b88c1c83…` e `sh_1b88c7ecd…`: `make … test_display_views` →
  `Abortado (arquivo core criado)` com a mutação aplicada;
- `sh_1b88c6721…`: `display view tests passed` no estado restaurado.

Isso cumpre a exigência de borda: trocar o gate por um par de valores do mesmo
intervalo (`width > height`) e deixar de chamar `layout()` em portrait faz o
teste falhar; sem a mutação ele passa. A lacuna é que
`music_traceability.md` ainda não registra esta mutação específica — fora do
escopo de edição deste ciclo, que só pode tocar esta nota.

### Inclusões test-only do tester

- `tests/host/keymap/test_music_player.cpp`: `549` checks medidos pelo coder
  (21:12) → `560` agora. Os **11 checks** adicionados pelo tester estão
  preservados e verdes; neste ciclo o arquivo não foi editado e a contagem foi
  confirmada pela execução.
- `tests/host/keymap/test_display_views.cpp`: bloco de orientação/resize
  (`assert_orientation` + resize real da surface via `LV_EVENT_SIZE_CHANGED`)
  que distingue portrait de landscape e sustenta a detecção da mutação de
  `resized()`.
- Nenhum arquivo de produção foi editado por mim além da restauração acima.
- Limitação de atribuição: após o reset não é possível reconstruir check a
  check o que é do coder e o que é do tester dentro do bloco reescrito. A
  propriedade verificável é o total `560/560` e a ausência de enfraquecimento:
  as 10 linhas de `CHECK` removidas no diff conjunto foram reescritas em
  blocos `assert_identity()` (4 `CHECK` cada) ou em asserções equivalentes.

## Evidence

- Ciclo 1 (pré-implementação): nenhum alvo `make` foi executado e nenhum
  artefato de build foi gerado, porque não havia implementação para exercitar.
- Ciclo 3 (este, após o reset): `make -C tests/host/keymap test_display_views` →
  `EXIT=0`, `display view tests passed` (recompilou `music_view.o` do fonte
  restaurado); `make -C tests/host/keymap test_music_player` → `EXIT=0`,
  `560/560 checks passed`. Saídas persistidas em
  `~/.local/share/opencode/shell/888faee7…/`.
- Restauração da mutação: sha256 de origem e destino
  `50011e11096ac14b3420e47b3cfa2518b50814ada1e473ced29ec7a6eedbbfa1`, `cmp`
  idêntico; o destino mutado anterior era
  `087b407decc5c6223a94da161a7f96fd7cc6c29e57f46da80e17a9813711339e`.
- Detecção recuperada da mutação de `resized()`: `sh_1b88c34ae002…` com
  `Assertion 'now_playing->width == browser->width' failed` em
  `test_display_views.cpp:1339`, além de duas execuções `Abortado (arquivo core
  criado)` com a mutação aplicada.
- Ciclo 4 (reexecução final delimitada sobre `1.15.1`): os 10 alvos focados
  passaram (`EXIT=0`) — `test_music_model` 290/290, `test_music_player` 560/560,
  `test_music_app` 428/428, `test_display_views` "display view tests passed",
  `test_music_metadata_contract` 1 PASS, `test_music_player_ui_contract` 1 PASS,
  `test_music_scan_stack_contract` 24 PASS, `test_music_player_teardown_contract`
  14 PASS, `test_shell_help_contract` 2 PASS, `test_shell_help` 243 checks/0
  failures. Logs em `/tmp/opencode/tester-final/<alvo>.log`. Nenhuma
  recompilação ocorreu porque os bins já eram mais novos que os fontes do mesmo
  snapshot (`git diff` sha256 inalterado antes/depois).
- Snapshot do worktree: `HEAD=ff47b9a31c8d8a1fdb5746c60013619cf75c7481`,
  `git diff` sha256
  `86f94113faeab257990aeac3c62513a04811cbb536737686107bfbffc312cf54`,
  `version.txt=1.15.1`, e `test_music_metadata_contract.py` (não rastreado)
  sha256 `10a8585eef6df7be60568d76ccee81dca9b7590eecad93be9d41dd6fc6dae680`.
- Estado do código confirmado: `git rev-parse HEAD` →
  `ff47b9a31c8d8a1fdb5746c60013619cf75c7481`; `git log --oneline -3` →
  `ff47b9a docs: record Tab5 USB-C audio host feasibility`, `ab89655
  feat(control): add an authenticated LAN command channel`, `de62279
  perf(music): page the queue scan instead of re-reading per child`;
  `git status --porcelain` (corrigido no ciclo 4; a linha anterior deste
  registro, "somente `?? docs/...`", estava stale) → 21 arquivos modificados
  + não rastreados `docs/agent-state/music-track-metadata/` e
  `tests/host/keymap/test_music_metadata_contract.py`.
- Discovery somente leitura (linhas citadas em "Work done"): `code-map.md`,
  `docs/03-audio-player-plan.pt-BR.md`,
  `components/cyberdeck/{include,src}/apps/music/cyberdeck_music_{model,
  metadata,player,app}.*`,
  `components/cyberdeck/{include,src}/platform/display/cyberdeck_music_view.*`,
  `components/cyberdeck/src/platform/display/cyberdeck_ui.cpp`,
  `components/cyberdeck/include/apps/serial/cyberdeck_ui_dump_serializer.h`,
  `tests/host/keymap/{Makefile,test_music_model.cpp,test_music_player.cpp,
  test_music_app.cpp,test_display_views.cpp,test_music_player_ui_contract.py,
  music_traceability.md,player_traceability.md}`,
  `tests/manual/music-player-validation.pt-BR.md`.
- Ciclo 5 (este, especificação da reaprovação): **nenhum comando de teste,
  build, flash ou mutação executado**. A única atividade não-leitura foi a
  inspeção read-only do MP3 real, cujos resultados estão na tabela do ciclo 5:
  `ls -l` → 2.350.363 bytes; `ffprobe -show_entries
  format=size,duration,format_name -format_tags=title,artist,album` → duração
  121,533 s, título ` The Kid (Kid Cosmic Theme)`, artista
  `Dr. Fang and The Gang`, álbum `Kid Cosmic and the Sonic Courage`; dump
  estrutural Python → header `ID3 03 00 00 | 00 0F 42 01`, payload 254209,
  frames TALB@10/69, TPE1@89/47, TPE2@146/47, TSSE@203/29, TIT2@242/59,
  TRCK@311/3, TYER@324/6, APIC@340/252112, padding 1757, fim 254219.
- Ciclo 5: snapshot informado no cabeçalho — HEAD
  `ff47b9a31c8d8a1fdb5746c60013619cf75c7481`, `version.txt=1.15.1`,
  `git diff` sha256 `30530d23…828025`; produção ainda com
  `tag_bytes > 64 * 1024` e `k_id3_metadata_limit = 64 * 1024` (linhas citadas
  no ciclo 5). Não há resultado de teste novo a atribuir a este ciclo; o
  entregável é a especificação.
- Ciclo 6 (validação independente): baseline `test_music_metadata_io`
  `190/190`, `test_music_model` `361/361`, `test_music_player` `598/598`,
  `test_music_app` `438/438`, todos EXIT=0 (`/tmp/opencode/base-*.log`).
  Mutações detectadas e restauradas com sha256 conferido: M2
  `metadata.cpp` → io `186/190` (FAIL 484/497/646); M3 `model.cpp` →
  model FAIL 354–357/371–374; M4 `metadata.h` → io `189/190` (FAIL 728);
  M5 `model.cpp` → io `188/190` (FAIL 531/532); M6 `model.cpp` → model
  `360/361` (FAIL 435). Logs `/tmp/opencode/mut-M{2,3,4,5,6}.log`. Após as
  restaurações: io `190/190` e model `361/361` de novo
  (`final-io.log`/`final-model.log`); hashes de produção iguais aos do coder e
  `git diff` sha256 `8caf74a2…` igual ao início do ciclo. M1 continua
  inconclusivo (mutante não compilou) e M7–M12 pendentes.
- Ciclo 7: baseline e pós-restauração `test_music_metadata_io` `190/190`,
  `test_music_model` `361/361`, `test_music_player` `598/598`,
  `test_music_app` `438/438`, todos EXIT=0 (`/tmp/opencode/final7-*.log`).
  Mutações detectadas e restauradas (sha256 conferido): M7 em `model.cpp` →
  model `347/361` (FAIL 470/471/489/490/498/499); M8 em `metadata.cpp` → io
  `182/190` (FAIL 447/448/464/557×5); M9 em `player.cpp` → player 20 FAIL
  (1294/1297/1298 PLAYER-OVERCAP e 1405+), SIGFPE após a cascata; M1' em
  `metadata.cpp` → io `172/190` (FAIL 478–484/494–497/510/511/523/524/622/
  623/645). Logs `/tmp/opencode/mut-M{1,7,8,9}.log`. Estado final:
  produção com os hashes do coder, `git diff` sha256 `f6c2d78a…`, sem marcador
  `MUTANT`, 25 entradas de status.
- Ciclo 8: M10/M11/M12 executadas e detectadas, com restauração conferida
  por sha256. M10: `model` normal `361/361` (STALE — ver defeito de wiring),
  `model -B` `333/361` (FAIL 313–316/354+), player `590/598` (FAIL
  1034–1037), app `435/438` (FAIL 1366–1368). M11 (janela/payload
  proporcionais): io `185/190` (FAIL 510/511/622/623/744). M12 (`max-1`):
  model `352/361` (FAIL 562/563/579/580/592/593), io `187/190` (FAIL
  509/742/743). Logs `/tmp/opencode/mut-M10-*.log`, `mut-M11.log`,
  `mut-M12-*.log`. Revalidação final: io `190/190`, model `361/361`, player
  `598/598`, app `438/438`, EXIT=0 (`final8-*.log`); produção e `git diff`
  `f6c2d78a…` conforme acima, sem marcador `MUTANT`.
- **Achado test-only**: `tests/host/keymap/Makefile:2951` (`test_music_model_bin`)
  não lista `$(MUSIC_METADATA_HDR)` como pré-requisito, embora o teste inclua o
  header; mudanças só em `metadata.h` não recompilam o alvo e produzem verde
  falso (observado no M10). Correção pendente (test-only).
- Ciclo 9: correção do wiring no `Makefile` (`$(MUSIC_METADATA_HDR)` como
  pré-requisito de `test_music_model_bin`). Evidência: `make -n` após editar o
  Makefile e `make -W <header> -n` mostram a linha `g++`; M10 (cap 64 KiB)
  executado **sem `-B`** recompilou sozinho e falhou `333/361` (FAIL
  313–316/354+); restaurado `metadata.h` (`10c82bad…`), nova execução
  recompilou e passou `361/361`. Logs `/tmp/opencode/fix-model-*.log`.
  `Makefile` = `9b11c5dc…beab34`; `git diff` sha256
  `82d53970…f8e9fc0`; produção inalterada.
- Ciclo 10: `TEST-MUSIC-META-ID3-CONTRACT` implementado em
  `tests/host/keymap/test_music_id3_contract.py` e ligado ao agregado.
  `make -C tests/host/keymap test_music_id3_contract` → PASS
  (`/tmp/opencode/final-contract.log`). Sensibilidade estrutural: C1 (remover
  `$(MUSIC_METADATA_HDR)` da regra do binário de I/O) → FAIL
  “must depend on $(MUSIC_METADATA_HDR)”; C2 (`unsigned char[tag_bytes]`) →
  FAIL “the only allocation must be k_id3_read_window_bytes”. Restaurados
  `Makefile 10a29a00…` e `metadata.cpp 964fb918…`; `git diff` sha256
  `8743eb38…`; 26 entradas de status.
- Ciclo 11: checkpoint docs-only. Snapshot `git diff` sha256
  `aff8eda3…d1699b71` (26 entradas). Hashes reconferidos e idênticos ao
  snapshot medido: produção `964fb918…/5f5b4a3b…/10c82bad…/f5b7fe59…`, testes
  `5f9f826e…/31d05a21…/6f03dca5…/0ed05be4…`, `Makefile 10a29a00…`, contrato
  `be014e19…`, `test_music_metadata_contract.py 10a8585e…`. Únicos arquivos
  pós-`8743eb38…`: `code-map.md` (23:04:54),
  `tests/host/keymap/music_traceability.md` (23:05:20),
  `tests/host/keymap/player_traceability.md` (23:05:30),
  `tests/manual/music-player-validation.pt-BR.md` (23:05:43) e
  `docs/03-audio-player-plan.pt-BR.md` (23:05:54). Nenhum teste executado
  neste ciclo. `version.txt=1.15.2`, app `1.0.4`, traceability registra
  12/12 mutações. Pelo grafo do Makefile, io/model/player/app e o contrato
  ficam out-of-date porque `music_traceability.md`/plano/manual (pré-requisitos
  declarados) mudaram depois das execuções; reexecução necessária no gate
  final.
- Ciclo 12: reexecução focada no snapshot `aff8eda3…` (antes/depois
  idênticos: `git diff` sha256 `aff8eda3…`, 26 entradas de status; hashes de
  produção/testes inalterados). Resultados: `test_music_metadata_io 190/190`,
  `test_music_model 361/361`, `test_music_player 598/598`,
  `test_music_app 438/438`, `test_music_id3_contract PASS`,
  `test_music_metadata_contract PASS`, `test_shell_help_contract PASS`,
  `test_shell_help 243/0`, `test_display_views PASS`; todos EXIT=0. Logs
  `/tmp/opencode/recheck-<alvo>.log`; recompilações observadas em io/model/
  player/app (1/1/1/2 linhas `g++`), shell_help (25) e display_views (9, alvo
  incluído por invalidação via `Makefile`). Nenhuma mutação nova e nenhum teste
  editado.
- Ciclo 13: correção do blocker AC-MUSIC-META-27 validada de forma
  independente no snapshot `fc54a8dd…` (26 entradas; `git diff` idêntico
  antes/depois). `test_music_model` `367/367` e `test_music_metadata_io`
  `205/205`, EXIT=0. Mutações: PAD-A (`metadata.cpp`, valida só resto curto) →
  io `202/205` (FAIL 718/729/744); PAD-B (`model.cpp`, idem) → model `363/367`
  (FAIL 471/472/481/482); restaurados `3141220b…` e `4f9683ec…`, reexecução
  verde. Custo do padding: 5350 `pread` no pior caso de 1 MiB (196 B/janela) +
  headers; classificado como risco aceito pendente de medição física, sem
  violar AC-24/25/26/27. Sem suíte/build/flash/playback; só a nota editada.
- Workflow/template relidos nesta sessão: `agent-toolkit/workflows/
  developer-workflow.md`, `docs/agent-instruction-delivery.md`,
  `templates/task-note.md` e `AGENTS.md`/`code-map.md` do projeto.

## Decisions and assumptions

- Prefixos novos `REQ-MUSIC-META-*` / `AC-MUSIC-META-*` / `TEST-MUSIC-META-*`
  para não colidir com `REQ-MUSIC-0x`, `REQ-MUSIC-UI-*`, `REQ-MUSIC-DUR-*` e
  `REQ-PLAYER-*` já mapeados em `music_traceability.md`/`player_traceability.md`.
- O álbum entra na estrutura bounded existente (`metadata`/`track`), não em um
  canal paralelo: é o caminho que o worker já usa para title/artist e mantém
  `music list`/browser intocados.
- `music status`/`music list` **não** ganham álbum (AC-MUSIC-META-20). Default
  conservador, alinhado a "não alterar comandos"; se o developer decidir
  mostrar álbum no status para facilitar validação, isso muda um AC e precisa
  voltar como decisão explícita — o teste negativo correspondente não deve ser
  afrouxado por conveniência de validação.
- A região NOW PLAYING é alimentada pelo snapshot (`playback.current`), não
  pelo texto de status: isso é o que torna "independente do browser"
  verificável e evita acoplar a UI a formato textual de comando.
- "Claramente identificado" para o fallback de título vira propriedade
  observável: flag booleana no modelo (nome a fixar pelo coder) + texto do
  basename com extensão. Sem isso o requisito não é testável de forma exata.
- Nome do accessor da view e dos rótulos fica a cargo do coder; o tester
  vincula ao nome real e preserva as asserções (não enfraquece por renomeação).
- Sensibilidade de fronteira: as mutações obrigatórias foram executadas nos
  ciclos 2-3. As registradas em `music_traceability.md` são troca `TAL`/`TALB`,
  remoção de `seen.album = true`, fronteira `k_max_album_bytes - 2`,
  `render_now_playing()` ignorando `item.title`, en dash no placeholder e fonte
  trocada para `s_music_browser_snapshot.current`. A mutação de
  `view::resized()` foi detectada e restaurada neste ciclo, mas ainda não está
  listada na matriz de rastreabilidade.
- `jev`: não usado — nenhuma lacuna de cobertura dependeu de desempate; as
  escolhas acima são determinadas pelo código atual + plano aprovado.
- Ciclo 5: teto interpretado como **bytes totais da tag** (header+payload),
  porque o usuário escreveu "1 MiB inclusive maximum total tag bytes"; a
  diferença de 10 bytes do limite antigo (payload) está explícita no contrato
  e no teste de fronteira. Default reversível: se o developer/coder optar por
  semântica de payload, isso é mudança de AC e volta ao developer.
- Ciclo 5: rejeitar estruturas que deslocam/mutam o payload (unsync, extended
  header, flags em frame-alvo) e tolerar as que não deslocam (experimental,
  footer) é o default conservador que mantém o parser simples e fail-closed;
  perda de metadado nesses arquivos é aceita (playback inalterado).
- Ciclo 5: sonda comportamental principal = `--wrap=pread` num binário focado
  (mesmo padrão de `--wrap=opendir` já usado no harness) + `operator new`
  nothrow local; propriedades que não são observáveis só por comportamento
  (sem alocação proporcional, sem `open`, wiring do alvo) ficam no contrato
  estrutural, declaradas como tal.
- Ciclo 5: a fixture real-shaped é sintética (APIC zerado, sem áudio real);
  `254209`/`252112` e os textos curtos são proveniência do oráculo e não
  constantes de firmware.
- Ciclo 5: `jev` não usado — as decisões acima são de contrato já reaprovado
  (teto e fail-closed) ou defaults reversíveis de teste (wrapper/fixture).

## Pending and risks

- Ciclo 13 (este): blocker AC-MUSIC-META-27 corrigido e validado
  independentemente (367/367, 205/205; PAD-A/PAD-B detectados e restaurados).
  Nenhuma lacuna material de teste encontrada; nada foi alterado pelo tester.
- Custo do padding no pior caso (1 MiB ⇒ ~5350 `pread`/arquivo) segue como
  risco aceito e **pendente de medição física** de scan no SD; não é violação
  dos ACs, mas pode pesar em diretórios com muitas tags de padding grande.
- Gate físico continua não executado (fixtures no SD, `ui.dump`/`screen.dump`,
  custo de scan, ausência de panic/reboot).
- Gates finais do reviewer (suíte agregada, cobertura/scope guard, verify,
  `idf.py build`, CI) devem usar o snapshot `fc54a8dd…`.
- Observação de harness: M9 encerra o binário com SIGFPE após cascata de falhas
  no estado mutado; sem impacto no caminho verde.

- Ciclo 5 (reaprovação): os cenários REQ-MUSIC-META-06/AC-23..30 e
  TEST-MUSIC-META-ID3-* / -READ-* / -PLAYER-* / -COMPACT-REGRESSION /
  -ID3-CONTRACT estão **especificados, não implementados nem executados**.
  Ordem: coder implementa e roda os focados; tester então adiciona/verifica os
  TESTs e executa as mutações obrigatórias; reviewer roda os gates finais.
  Nenhuma prova de comportamento existe neste ciclo.
- Risco alto em aberto: custo de I/O do scan no SD para tags de até 1 MiB
  (mesmo com leituras bounded por frame, o número de `pread` cresce com o
  número de frames); medir `music queue` no device (PHYS-SCAN-COST) e revisar
  read-ahead se necessário, sem remover o teto.
- Política do teto: bytes totais (header+payload) ≤ 1 MiB foi fixada nesta
  especificação; se o coder implementar semântica só de payload (±10 bytes),
  é divergência de contrato, não detalhe — retornar ao developer antes de
  ajustar os testes.
- Flags: arquivos com unsynchronisation/extended header (e frame-alvo com
  flags) passam a ser fail-closed e perdem metadado, com playback preservado.
  É perda de compatibilidade intencional e deve ser registrada na documentação.
- O teste de OOM depende de override de `operator new` no binário focado; se a
  implementação usar `malloc`/`std::string` que cresça com a tag, o override não
  a cobre — o contrato estrutural cobre esse caso e o teste deve ser ajustado
  ao alocador real, sem afrouxar a propriedade "alocação não cresce com a tag".
- O wrapper `--wrap=pread` prova bytes/offsets/fd observados; não prova
  intenção do implementador. Lacuna residual: leitura com `pread` grande mas
  dentro da janela derivada só é detectada se exceder o bound — por isso o
  bound é derivado das constantes exportadas.
- Validação física dos fixtures acima permanece pendente; nenhum flash,
  `screen.dump` ou ensaio acústico neste ciclo. O MP3 real do usuário não pode
  ser commitado; só a geometria e o oráculo textual curto entram no repositório.
- Alvos focados do ciclo 4 continuam históricos; nesta reaprovação nenhum
  deles foi reexecutado e o resultado anterior **não** prova a correção nova.

- Alvos focados: **todos os 10 reexecutados no ciclo 4** sobre o snapshot
  `1.15.1` e verdes (ver "Ciclo 4"). O bullet anterior, que listava
  `test_music_model`, `test_music_app`, `test_shell_help` e os quatro contratos
  como não reexecutados, foi superado e mantido apenas como histórico do
  ciclo 3 no corpo da nota.
- Regressão de mutação ainda não registrada em `music_traceability.md`: a
  mutação de `view::resized()` foi detectada e restaurada, mas a matriz
  `REQ/AC -> TEST` não a lista. Registrar no próximo ciclo que puder editar
  testes/documentos de rastreabilidade.
- Não executados nesta entrega (nem no ciclo 4, por restrição explícita do
  dispatch): suíte agregada, `make … verify`, gates de
  cobertura/scope guard, ASan/UBSan/TSan, `idf.py build`, `git diff --check` e
  flash. O `build/cyberdeck5.bin` existente antecede as fontes e não contém
  `1.15.1`.
- Validação física pendente (`TEST-MUSIC-META-PHYS-*`): exige SD com tags
  conhecidas e oráculo host-side, `ui.dump`/`screen.dump` e aceite visual no
  Tab5; nenhum arquivo foi preparado nesta sessão e nenhum ensaio físico foi
  executado.
- `LV_LABEL_LONG_MODE_DOTS` real não é provado no host (limite do shim);
  truncagem/legibilidade dependem de `screen.dump` no device.
- `ui.dump` trunca por nó/bytes: se a região não couber no dump, a evidência
  manual cai para `screen.dump` e fica mais fraca (registrar).
- Resolvidos pela entrega (mantidos como histórico de verificação): colisão de
  índice em `test_display_views` (labels criados após `controls_` preservam
  `children[5]` = Command e os checks passam), herança de campo
  (`state.current` é atribuição integral sob geração, coberta por
  `assert_identity` em A→B e no caminho de erro) e identidade em `state=error`
  (o caminho `error_stage=id3`, pré-publicação, substitui `current` pelo
  basename do request que falhou, coberto por teste).

## References

- Plano: `docs/03-audio-player-plan.pt-BR.md`.
- Nota do planner (não editada): `docs/agent-state/music-track-metadata/planner.md`.
- Produção: `components/cyberdeck/include/apps/music/cyberdeck_music_model.h`,
  `components/cyberdeck/src/apps/music/cyberdeck_music_model.cpp`,
  `components/cyberdeck/{include,src}/apps/music/cyberdeck_music_metadata.*`,
  `components/cyberdeck/src/apps/music/cyberdeck_music_player.cpp`,
  `components/cyberdeck/src/apps/music/cyberdeck_music_app.cpp`,
  `components/cyberdeck/{include,src}/platform/display/cyberdeck_music_view.*`,
  `components/cyberdeck/src/platform/display/cyberdeck_ui.cpp`,
  `components/cyberdeck/include/apps/serial/cyberdeck_ui_dump_serializer.h`.
- Testes: `tests/host/keymap/{test_music_model,test_music_player,test_music_app,
  test_display_views}.cpp`, `tests/host/keymap/test_music_player_ui_contract.py`,
  `tests/host/keymap/{music_traceability,player_traceability}.md`,
  `tests/host/keymap/Makefile`. Ciclo 5 (a implementar): novo alvo
  `test_music_metadata_io` (`--wrap=pread/open/openat/dup/close`, `operator
  new` nothrow) e `test_music_id3_contract.py`; casos novos em
  `test_music_model`, `test_music_player` e `test_music_app`.
- Arquivo real (read-only, fora do repositório): `/run/media/moises/
  CC64B5CF64B5BC90/moises/musicas/Kid Cosmic/01 - The Kid (Kid Cosmic
  Theme).mp3`; oráculo ffprobe e geometria na tabela do ciclo 5.
- Manual: `tests/manual/music-player-validation.pt-BR.md`.
- Índice: `code-map.md`, `AGENTS.md`.
