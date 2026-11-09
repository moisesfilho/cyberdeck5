# tester - image-formats

- Role: tester
- Updated: 2026-11-09 (hora local; não aferida, posterior a 15:2x) -03:00
- Code state: HEAD `0f4ab3a3dd43`, worktree da fase 2 pós-correção do miniz/ROM hijack.
  `git diff | sha256sum` = `2aa7816a1e37a91e198c508adb2dcac22e35f46fb5c0fda76df6ca82a196c2b0`;
  `git status --porcelain | sha256sum` = `a5e6d548fe448c50cf987c1cf668bd66427fcdb16af0b8f0393165d5dc266301`.
  `version.txt` = 1.17.0; manifesto `cyberdeck.image_viewer` = 1.0.2.
  Binário: `build/cyberdeck5.bin` sha256
  `9cbc468500863beb8feba5d485bfcd0686bd51284b195f08438b4c88ca13ea13` (2.787.776 B),
  `esp_app_desc` = `cyberdeck5` / `1.17.0`; nenhuma fonte de produção mais nova que o binário.
  **Estado físico (2026-11-09 15:2x): binário `9cbc4685…` gravado e validado no Tab5; paleta
  (4 cores e 128x128 bands), orientação/canais e rejeições medidas OK. Pendente apenas aceite
  visual/percepção humana e navegação por setas física (sem injeção pela ponte).**

## Dispatch objective

Atuar como tester em `risk_profile: high` na expansão do app `image` para abrir BMP, JPEG/JPG e
PNG preservando BMP e os limites atuais (leitura/decode no worker, frame RGB565, extensão
reconhecida + validação por conteúdo, rejeição sem frame parcial).

Critério de saída: **matriz high-risk completa** (campo/limite -> `NEG-*` -> esperado, mais
positivos, regressão BMP, fixtures/oráculos, contratos estruturais, validação manual) suficiente
para autorizar o início da implementação, listando os blockers técnicos que dependem de
coder/research.

Restrições cumpridas: nenhuma produção ou teste alterado, nenhum comando de teste/build/flash
executado, nenhuma suíte ampla rodada.

## Work done

### 1. Escopo derivado do objetivo aprovado

Preservado do baseline `REQ/AC-IMG-01..05` (BMP 24-bit `BI_RGB`, caminho confinado, 5 MiB,
1..1280 px/eixo, 64 imagens, fit sem ampliação, indicador `i/N`, `Esc`). Novo recorte:

| REQ | Conteúdo |
| --- | --- |
| REQ-IMGF-01 | identificação por extensão reconhecida (`.bmp`, `.jpg`, `.jpeg`, `.png`, case-insensitive, nome 5..128) + validação de conteúdo contra o formato declarado, com rejeição tipada; BMP inalterado |
| REQ-IMGF-02 | JPEG baseline suportado (conjunto preciso abaixo), decode no worker, streaming pelo descritor confinado, saída RGB565 top-down |
| REQ-IMGF-03 | PNG estático suportado por decode incremental com janela de inflate limitada (conjunto preciso abaixo) |
| REQ-IMGF-04 | orçamento de memória/stack: pico declarado e derivado, scratch liberado antes da publicação, frame publicado como bloco único `w*h*2`, nada grande na stack de 6144 B |
| REQ-IMGF-05 | lifecycle: cancelamento cooperativo com granularidade limitada, geração monotônica, nenhum frame parcial publicado, `stop()` bounded sem UAF |

| AC | Critério |
| --- | --- |
| AC-IMGF-01 | `image` abre cada variante suportada e publica `width`, `height`, `pixel_bytes == w*h*2`, linha 0 = topo, RGB565 |
| AC-IMGF-02 | qualquer variante não suportada, conteúdo divergente da extensão ou limite excedido falha fechado com motivo tipado, sem frame e sem indicador obsoleto, retornando ao shell |
| AC-IMGF-03 | fit/proporção/sem ampliação e cromo inalterados para todos os formatos |
| AC-IMGF-04 | pico de memória e frames de stack dentro do orçamento declarado, com prova por contagem de bytes e por medição real de frames |
| AC-IMGF-05 | cancelar/stale/erro nunca publica frame parcial e nunca vaza ou libera duas vezes |
| AC-IMGF-06 | listagem/janela 64/pin/navegação/help cobrem o novo conjunto de extensões |

Fora de escopo confirmado (não criar teste que presuma o contrário): GIF/animação, WebP, EXIF
auto-rotation, composição de transparência (pendente de decisão D3), upload HTTP de JPEG/PNG
(`POST /control/upload` permanece `.bmp`-only).

### 2. Conjunto de variantes proposto (preciso, nada "assumido")

**JPEG — aceitar:** SOF0 baseline; precisão 8 bits; Huffman (DHT); 1 componente (grayscale,
replicado para RGB) ou 3 componentes YCbCr; sampling 4:4:4, 4:2:2, 4:2:0, 4:1:1; restart markers
(DRI/RSTn); `APPn`/`COM` ignorados (EXIF/ICC **não** aplicados, sem rotação e sem color management);
um único scan sequencial.
**JPEG — rejeitar:** SOF1 (extended sequential), SOF2 (progressive), SOF3 (lossless), SOF5..7,
SOF9..15 (aritmético/diferencial/DNL), precisão != 8, `Nf` 2 ou 4 (CMYK/Adobe), DHT/DQT inválidos,
`SOI`/`EOI` ausentes, segmento com `length` estourando `file_bytes`.

**PNG — aceitar:** assinatura de 8 bytes; IHDR primeiro; profundidade 8; sem interlace; compressão
0; filtro 0; color type 2 (truecolor) e 0 (grayscale → replicado em R=G=B); color type 3 (palette,
`PLTE` obrigatório, ≤256 entradas, índice válido); múltiplos `IDAT` concatenados e `IDAT` de
comprimento zero; `IEND` obrigatório; chunks auxiliares desconhecidos ignorados; filtros de linha
0..4.
**PNG — rejeitar:** profundidade 1/2/4/16; interlace 1 (Adam7); color type 1/5/7; compressão != 0;
método de filtro != 0; `PLTE` ausente/inválido para type 3; índice de paleta fora do range; chunk
crítico desconhecido; CRC inválido em IHDR/PLTE/IDAT/IEND; zlib inválido/truncado; dados
descomprimidos insuficientes ou excedentes; `IDAT` após `IEND`; bit de filtro > 4.
**PNG — decisão pendente D3 (alpha):** color type 4 (`gray+alpha`), 6 (`RGBA`) e `tRNS`.

### 3. Contratos de testabilidade exigidos (pré-requisito da matriz; coder pode renomear, não omitir)

| ID | Contrato |
| --- | --- |
| C1 | enum de formato + `format_from_name(std::string_view)` puro como **única** fonte da tabela de extensões; `is_bmp_name` mantido para o caminho de upload |
| C2 | `inspect_<fmt>(prefix, prefix_bytes, file_bytes, info&)` puro por formato, com constante de prefixo declarada no header e limite declarado de segmentos/chunks; nunca ler além de `prefix_bytes` |
| C3 | `decode_descriptor(ops, descriptor, file_bytes, format, decoded_image&)` preservando os quatro seams (`read_at`/`allocate`/`deallocate`/`cancelled`) e o contrato "falha ⇒ `out` vazio, nada alocado" |
| C4 | `decoded_image` com bloco único `pixel_bytes == width*height*2`; scratch nunca sobrevive à publicação |
| C5 | orçamento declarado (`k_max_decode_peak_bytes` + `peak_bytes(format, info)` derivado de componentes), não literal duplicado no teste |
| C6 | poll de cancelamento ao menos a cada linha (BMP/PNG) ou MCU (JPEG), observável pelo seam (`cancelled` conta chamadas) |
| C7 | todas as TUs de decode/inspeção linkáveis no host com a receita atual do Makefile (sem libz/libpng externa, sem ESP-IDF, sem LVGL); alocação de plataforma só atrás de `decode_ops` |

C7 é gate: uma implementação de PNG que só exista linkada a uma biblioteca externa degrada a
prova host para "estrutural" e é inaceitável para este risco.

### 4. Matriz positiva

| TEST | Cenário | Esperado |
| --- | --- | --- |
| TEST-IMGF-FMT-01 | `a.bmp`/`A.BMP`/`a.BmP`, `a.jpg`/`A.JPG`, `a.jpeg`/`a.JPEG`, `a.png`/`A.PNG` | formato correto pela tabela única |
| TEST-IMGF-FMT-02 | nomes `.bmp`, `.png` (4 bytes, só extensão), 129 bytes, `a.bmp.txt`, `a.jpg.png`, `a.pn`, `a.jpe` | rejeitados antes do I/O |
| TEST-IMGF-REG-01 | BMP 2x2, 101x3 e 1280x1280 | bytes/pixels idênticos ao baseline atual (mesmas asserções de `test_image_bmp`/`test_image_decode`) |
| TEST-IMGF-REG-02 | BMP bottom-up e `biHeight < 0` | ordem de linha inalterada |
| TEST-IMGF-REG-03 | `biSizeImage == 0` (legal em `BI_RGB`) | aceito |
| TEST-IMGF-REG-04 | `validate_upload_name` com `.png`/`.jpg`/`.jpeg` | recusado; `.bmp` aceito (escopo do upload preservado) |
| TEST-IMGF-REG-05 | navegação/indicador com BMP | `i/N`, `i/64+`, clamp e pin inalterados |
| TEST-IMGF-JPG-01 | 8x8, 4 quadrantes cor pura, 4:4:4 | 4 pixels-âncora de centro de quadrante na ordem topo-base e canal correto |
| TEST-IMGF-JPG-02 | 16x16 quadrantes, 4:2:0 | idem, tolerância de croma declarada; ordem de linha distinguível |
| TEST-IMGF-JPG-03 | 32x16, 4:2:2 | idem, sem "shear" horizontal/vertical |
| TEST-IMGF-JPG-04 | 8x8 grayscale | `R == G == B` nos quatro quadrantes |
| TEST-IMGF-JPG-05 | 3x5 (múltiplo de MCU falso) | `width == 3`, `height == 5`, `pixel_bytes == 30`; crop sem linhas/colunas de padding |
| TEST-IMGF-JPG-06 | 1x1 | aceito, 1 pixel |
| TEST-IMGF-JPG-07 | 1280x1280 baseline | `pixel_bytes == 1280*1280*2` derivado; sem padding |
| TEST-IMGF-JPG-08 | restart markers (DRI) | decodifica igual à variante sem DRI |
| TEST-IMGF-JPG-09 | EXIF `Orientation = 6` | aceito **sem** rotação: pixels na ordem crua do arquivo (o teste falha se houver auto-rotation) |
| TEST-IMGF-JPG-10 | `APP1`/`ICC_PROFILE`/`COM` grandes + imagem válida | aceito; perfil ignorado (sem alteração de cor) |
| TEST-IMGF-JPG-11 | trailing bytes após `EOI` | aceito; `file_bytes` maior que o stream não muda o frame |
| TEST-IMGF-PNG-01 | 4x4 truecolor type 2 | valores RGB565 exatos por pixel (cores em múltiplos de 8/4) |
| TEST-IMGF-PNG-02 | 4x4 grayscale type 0 | `R == G == B` exatos |
| TEST-IMGF-PNG-03 | 4x4 palette type 3, 4 entradas | mapeamento exato índice→cor |
| TEST-IMGF-PNG-04 | múltiplos `IDAT` + um `IDAT` de comprimento zero | mesmo frame do `IDAT` único |
| TEST-IMGF-PNG-05 | chunks auxiliares (`gAMA`, `tEXt`, desconhecido não crítico) antes de `IDAT` | aceito, ignorados |
| TEST-IMGF-PNG-06 | `PLTE`/`tRNS` em type 2 (auxiliares sem efeito) | aceito (ou decisão D3), cores intactas |
| TEST-IMGF-PNG-07 | 1x1, 1x1280, 1280x1, 1280x1280 | geometria/frame exatos derivados |
| TEST-IMGF-PNG-08 | trailing bytes após `IEND` | aceito, frame inalterado |
| TEST-IMGF-PNG-09 | filtros 0..4, um por linha | reconstrução exata das linhas |
| TEST-IMGF-PNG-10 | type 4 / type 6 (RGBA) | conforme decisão D3: rejeição tipada ou composição determinística sobre preto com valores exatos |
| TEST-IMGF-UI-01 | qualquer variante suportada | título + `i/N` + frame instalado no `lv_image` (stride `w*2`, `LV_COLOR_FORMAT_RGB565`) |
| TEST-IMGF-UI-02 | JPEG retrato 48x64 e PNG 32x32 | fit inalterado, sem ampliação |
| TEST-IMGF-UI-03 | troca de formato na navegação (bmp→jpg→png) | frame anterior liberado uma única vez; conteúdo do novo correto |

### 5. Matriz negativa — campo/limite -> `NEG-*` -> esperado

Cada linha é um caso executável. "Nada alocado" = contador de `allocate` == 0; "nada publicado" =
`published`/`take_published` vazio; "balanceado" = `allocations == frees` e nenhum bloco vivo.

#### 5.1 Identificação, extensão e assinatura (dispatch)

| NEG | campo/limite | entrada | esperado | mutação que deve falhar |
| --- | --- | --- | --- | --- |
| NEG-IMGF-EXT-01 | extensão | `.gif`, `.webp`, `.qoi`, `.tif`, `.heic`, `.txt`, sem extensão | rejeição de caminho/uso (`image: invalid path`), nenhum I/O | aceitar "qualquer extensão" |
| NEG-IMGF-EXT-02 | mismatch | `.bmp` contendo bytes PNG | `bmp_status::invalid_signature`, nada alocado | sniffing que "adivinha" PNG |
| NEG-IMGF-EXT-03 | mismatch | `.bmp` contendo `FF D8 FF` (JPEG) | `invalid_signature`, nada alocado | idem |
| NEG-IMGF-EXT-04 | mismatch | `.png` contendo BMP `BM` | `png invalid_signature` | idem |
| NEG-IMGF-EXT-05 | mismatch | `.jpg`/`.jpeg` contendo PNG | `jpeg invalid_soi` | idem |
| NEG-IMGF-EXT-06 | mismatch | `.png` contendo JPEG | `png invalid_signature` | idem |
| NEG-IMGF-EXT-07 | assinatura parcial | PNG com 1..7 dos 8 bytes | `png invalid_signature`/`header_truncated` | aceitar prefixo |
| NEG-IMGF-EXT-08 | arquivo vazio | 0 byte em cada extensão | `header_truncated`/`invalid_signature`, nada alocado | aceitar tamanho 0 |
| NEG-IMGF-EXT-09 | prefixo curto | 1..53 bytes (BMP) e 1..(n-1) do header mínimo (JPEG/PNG) | rejeição tipada | ler além de `prefix_bytes` |

#### 5.2 JPEG (variantes e integridade)

| NEG | campo/limite | entrada | esperado | mutação |
| --- | --- | --- | --- | --- |
| NEG-IMGF-JPG-01 | SOF2 | progressivo | `unsupported_variant` (SOF2), nada alocado | aceitar progressivo |
| NEG-IMGF-JPG-02 | SOF1 | extended sequential | `unsupported_variant` (SOF1) | aceitar SOF1 sem evidência |
| NEG-IMGF-JPG-03 | SOF3 | lossless | `unsupported_variant` | mapear SOF3→SOF0 |
| NEG-IMGF-JPG-04 | SOF9/SOF10 | aritmético | `unsupported_variant` | idem |
| NEG-IMGF-JPG-05 | precisão | `Pq = 12` no SOF | `unsupported_precision` antes de ler entropia | aceitar 12-bit |
| NEG-IMGF-JPG-06 | componentes | `Nf = 4` (CMYK/Adobe), `Nf = 2` | `unsupported_components` | aceitar CMYK |
| NEG-IMGF-JPG-07 | `SOI` ausente | primeiro byte != `FF D8` | `invalid_soi` | aceitar offset 0 livre |
| NEG-IMGF-JPG-08 | `EOI` ausente | entropia truncada | `decode_failed`, nada publicado, balanceado | tratar EOF como sucesso |
| NEG-IMGF-JPG-09 | DHT inválido | tabela com classe/id inválidos | `decode_failed` tipado | ignorar DHT |
| NEG-IMGF-JPG-10 | `length` de segmento | `length` > bytes restantes | `truncated_segment`, leitura nunca além de `file_bytes` | clamp silencioso |
| NEG-IMGF-JPG-11 | restart desalinhado | RSTn fora de ordem | `decode_failed` | ignorar RST |
| NEG-IMGF-JPG-12 | dimensões | SOF com 8192/65535 | `invalid_dimension` antes de alocar | aceitar >1280 |
| NEG-IMGF-JPG-13 | scan múltiplo | segundo `SOS` (progressivo disfarçado) | `unsupported_variant` | aceitar 2º scan |
| NEG-IMGF-JPG-14 | pool insuficiente | seam `allocate` devolve bloco < necessário / recusa | `allocation_failed`, nada publicado, balanceado | usar pool na stack |
| NEG-IMGF-JPG-15 | fuzz determinístico | N mutações de 1 byte no header (PRNG fixo) | sem crash/leak; se `ok`, invariantes do frame valem | remover sanitização |

#### 5.3 PNG (tipos, profundidade, interlace, integridade, alpha)

| NEG | campo/limite | entrada | esperado | mutação |
| --- | --- | --- | --- | --- |
| NEG-IMGF-PNG-01 | profundidade | 16 bits (tipos 0/2/4/6) | `unsupported_bit_depth` | aceitar 16 e truncar |
| NEG-IMGF-PNG-02 | profundidade | 1/2/4 bits | `unsupported_bit_depth` | expandir sem regra declarada |
| NEG-IMGF-PNG-03 | interlace | `interlace = 1` (Adam7) | `unsupported_interlace` | decodificar como sequencial |
| NEG-IMGF-PNG-04 | color type | 1, 5, 7 | `unsupported_color_type` | mapear para 2 |
| NEG-IMGF-PNG-05 | compressão | método != 0 | `unsupported_compression` | aceitar |
| NEG-IMGF-PNG-06 | filtro | método != 0 | `unsupported_filter_method` | aceitar |
| NEG-IMGF-PNG-07 | `PLTE` | type 3 sem `PLTE`; `PLTE` com length % 3 != 0; > 256 entradas | `invalid_palette`, nada alocado | default preto |
| NEG-IMGF-PNG-08 | índice de paleta | pixel index >= entradas | `invalid_palette_entry` | clamp para 0 |
| NEG-IMGF-PNG-09 | chunk crítico | `ABCD` crítico desconhecido antes de `IDAT` | `unsupported_critical_chunk` | ignorar como auxiliar |
| NEG-IMGF-PNG-10 | CRC IHDR | 1 byte corrompido em IHDR | `checksum_mismatch` | não verificar CRC |
| NEG-IMGF-PNG-11 | CRC IDAT | idem em `IDAT` | `checksum_mismatch`; nada publicado | idem |
| NEG-IMGF-PNG-12 | CRC IEND | idem | `checksum_mismatch` ou rejeição explícita | idem |
| NEG-IMGF-PNG-13 | IHDR | `length != 13`, IHDR não primeiro, IHDR repetido | `invalid_header` | aceitar length variável |
| NEG-IMGF-PNG-14 | zlib | header zlib inválido; stream truncado no meio | `decode_failed` | aceitar stream curto |
| NEG-IMGF-PNG-15 | dados insuficientes | stream produz < `height*(1+stride)` bytes | `truncated_image`, nada publicado | zero-fill das linhas faltantes |
| NEG-IMGF-PNG-16 | excesso descomprimido (bomb) | `IDAT` que expande além de `height*(1+stride)` | rejeição **bounded**: para de inflar no primeiro byte excedente; nenhum buffer proporcional ao expandido | inflar tudo e truncar |
| NEG-IMGF-PNG-17 | bit de filtro | linha com byte de filtro 5..255 | `invalid_filter` | tratar como 0 |
| NEG-IMGF-PNG-18 | `IDAT` após `IEND`; `IEND` ausente | — | rejeição tipada | ignorar `IEND` |
| NEG-IMGF-PNG-19 | chunk length | `length` > 2^31-1 ou > bytes restantes | `truncated_segment`; leitura nunca além de `file_bytes` | wrap de 32 bits |
| NEG-IMGF-PNG-20 | contagem de chunks | milhares de chunks auxiliares | rejeição ao exceder o limite declarado (C2), sem consumo ilimitado | loop sem limite |
| NEG-IMGF-PNG-21 | fuzz determinístico | N mutações de 1 byte no header/IDAT | sem crash/leak; invariantes se `ok` | remover validação |
| NEG-IMGF-PNG-22 | alpha (D3) | type 4, type 6, type 3 com `tRNS`, type 2 com `tRNS` | conforme decisão: `unsupported_alpha` (default proposto) **ou** composição determinística sobre preto com valores exatos | aceitar sem tratar alpha (canal ignorado muda cor) |

#### 5.4 Limites, dimensões e overflow

| NEG | campo/limite | entrada | esperado | mutação |
| --- | --- | --- | --- | --- |
| NEG-IMGF-LIMIT-01 | teto de arquivo | `file_bytes == k_max_file_bytes + 1` em cada formato | `too_large` antes de qualquer alocação | `>`→`>=` |
| NEG-IMGF-LIMIT-02 | teto inclusivo | `file_bytes == k_max_file_bytes` | **não** `too_large` (pode falhar por formato/leitura, jamais por tamanho) | `>=` |
| NEG-IMGF-LIMIT-03 | seam de abertura | arquivo real > 5 MiB | `EFBIG` → `image: file larger than 5 MiB`, sem abrir frame | remover gate do seam |
| NEG-IMGF-LIMIT-04 | dimensão 0 | `width = 0`/`height = 0` (SOF/IHDR/BMP) | `invalid_dimension`, nada alocado | aceitar 0 |
| NEG-IMGF-LIMIT-05 | dimensão 1281 | 1281 em X e em Y, por formato | `invalid_dimension`, nada alocado | `>= 1280` |
| NEG-IMGF-LIMIT-06 | dimensão 1280 | 1280 em X e em Y, por formato | aceito; `pixel_bytes` derivado de `k_max_dimension` | `> 1280` |
| NEG-IMGF-LIMIT-07 | overflow de produto | IHDR `0xFFFFFFFF x 0xFFFFFFFF`; BMP/SOF análogos | rejeitado **antes** da multiplicação (contador de `allocate` == 0) | calcular `w*h*2` primeiro |
| NEG-IMGF-LIMIT-08 | offsets | `pixel_offset`/`fOffBits`/primeiro `IDAT` além de `file_bytes` | rejeição tipada | clamp |
| NEG-IMGF-LIMIT-09 | leitura além do arquivo | witness registra `max(offset+bytes)` | `<= file_bytes` em **todas** as variantes (válidas e inválidas) | ler "um pouco mais" |
| NEG-IMGF-LIMIT-10 | janela de listagem | 65 arquivos mistos | 64 na janela + `truncated`; nenhum excedente | cap 65 |
| NEG-IMGF-LIMIT-11 | nome | 128 bytes aceito; 129 rejeitado (path e pin) | rejeição tipada | off-by-one |
| NEG-IMGF-LIMIT-12 | diretório/symlink/regular | diretório `x.png`, symlink `y.jpg`, FIFO | rejeitado no seam real / pin inelegível | seguir symlink |

#### 5.5 Memória e posse do frame

| NEG | campo/limite | entrada | esperado | mutação |
| --- | --- | --- | --- | --- |
| NEG-IMGF-MEM-01 | recusa na 1ª alocação | `allocate` recusa | `allocation_failed`, nada publicado, balanceado | publicar nada/`nullptr` |
| NEG-IMGF-MEM-02 | recusa na 2ª alocação | JPEG: pool OK, frame recusado (e vice-versa); PNG: janela OK, frame recusado | libera o já alocado, `allocation_failed`, balanceado | vazar o 1º bloco |
| NEG-IMGF-MEM-03 | pico declarado | `1280x1280` de cada formato | `pico observado <= peak_bytes(format, info) <= k_max_decode_peak_bytes`, ambos derivados | literal mágico |
| NEG-IMGF-MEM-04 | scratch independente da área | `1280x1280` vs `1280x1` (PNG) | scratch não cresce com a altura; teto declarado (ex.: `<= 64 KiB`, justificado por janela 32 KiB + 2 linhas `4*w`) | decoder full-frame |
| NEG-IMGF-MEM-05 | tamanho comprimido != memória | arquivo de 4 MiB com geometria 16x16 (JPEG com trailing; PNG com chunk auxiliar grande) | maior alocação `<=` orçamento de 16x16 | alocar proporcional ao arquivo |
| NEG-IMGF-MEM-06 | bloco único publicado | sucesso em 3 formatos | exatamente 1 bloco vivo, `size == w*h*2`, `pixels` não nulo | publicar com scratch anexado |
| NEG-IMGF-MEM-07 | liberação única | view instala, troca e destrói | cada frame liberado exatamente 1 vez (contadores) | double free/leak |
| NEG-IMGF-MEM-08 | falha libera tudo | cada negativa acima | `allocations == frees` e 0 vivo | — |
| NEG-IMGF-MEM-09 | listagem heap-backed | `run_job` com página 64 | nenhuma página de ~8 KiB na stack (contrato de frame já existente, extendido) | `image_directory{}` local |
| NEG-IMGF-MEM-10 | pool/scratch na stack | inspeção estrutural + `-fstack-usage` | nenhum frame > 1024 B na cadeia de decode; nenhum `std::array<...,32768>` local | mover janela para a stack |

#### 5.6 I/O, cancelamento, stale e publicação

| NEG | campo/limite | entrada | esperado | mutação |
| --- | --- | --- | --- | --- |
| NEG-IMGF-IO-01 | leitura curta no header | `read_at` falha no offset 0 / no meio do header | `read_failed`, nada alocado | tratar como sucesso parcial |
| NEG-IMGF-IO-02 | leitura curta na 1ª linha/MCU | falha logo após o header | `read_failed`, frame liberado, nada publicado | publicar frame incompleto |
| NEG-IMGF-IO-03 | leitura curta na última linha/MCU | falha no fim | idem | idem |
| NEG-IMGF-IO-04 | `pread` parcial | `read_at` real devolve em pedaços | sucesso, frame correto | avanço de offset errado |
| NEG-IMGF-IO-05 | arquivo removido entre scan e open | pin válido, arquivo apagado | `image: cannot open file`, sem frame/indicador | publicar frame antigo |
| NEG-IMGF-IO-06 | descritor inválido | `descriptor < 0` | `invalid_argument`, nada alocado | dereferenciar |
| NEG-IMGF-CANCEL-01 | antes do início | `cancelled == true` desde o começo | `cancelled`, nada alocado | decodificar mesmo assim |
| NEG-IMGF-CANCEL-02 | após o header | cancelar no 1º poll | `cancelled`, nada publicado, balanceado | ignorar poll |
| NEG-IMGF-CANCEL-03 | meio do frame | cancelar no meio | `cancelled`, balanceado; granularidade: aborta até 1 linha (BMP/PNG) / 1 MCU (JPEG) após a sinalização | granularidade ilimitada |
| NEG-IMGF-CANCEL-04 | polls existem | imagem com `height > 1` | nº de polls `> 0` e `<= height + constante` | zero polls |
| NEG-IMGF-CANCEL-05 | cancelamento não vira diagnóstico | cancelar durante `run_job` | nenhuma mensagem de erro publicada | notificar "read failed" no Esc |
| NEG-IMGF-STALE-01 | geração superada antes do decode | 2 requisições rápidas | só a mais nova publica; a antiga não aloca/publica | publicar a antiga |
| NEG-IMGF-STALE-02 | geração superada após decode | decode conclui, geração mudou | frame liberado 1 vez, nada publicado | leak/UAF |
| NEG-IMGF-STALE-03 | `fail` de geração antiga | falha tardia | não apaga o frame atual | limpar estado atual |
| NEG-IMGF-STALE-04 | `stop()` no meio | cancelar + join | worker sai, nada liberado enquanto vivo, sem UAF | liberar com worker vivo |
| NEG-IMGF-PUB-01 | nada parcial | qualquer falha | `take_published == false`, `notice` tipado, `state == error` | publicar bitmap "meio pintado" |
| NEG-IMGF-PUB-02 | sem indicador obsoleto | falha após sucesso anterior | `has_published == false`, lista limpa, sem `i/N` | manter `i/N` antigo |
| NEG-IMGF-PUB-03 | retorno ao shell | falha | teclado volta ao shell (`classify_image_key` com `image_ready == false` → `pass_through`, exceto `ESC`) | foreground zumbi |
| NEG-IMGF-PUB-04 | mensagem única e derivada | cada `NEG-*` de formato | `notice == "image: " + <reason do status> + "\n"`; motivo vem de uma única função por formato (sem literal duplicado no teste) | texto divergente da causa |

#### 5.7 Stack e concorrência

| NEG | campo/limite | entrada | esperado | mutação |
| --- | --- | --- | --- | --- |
| NEG-IMGF-STACK-01 | frames host | `-fstack-usage` em `cyberdeck_image_bmp/decode/jpeg/png/list` | cada função da cadeia `<= 1024 B` (limite atual do contrato) | array grande local |
| NEG-IMGF-STACK-02 | cadeia vs orçamento | soma da cadeia + reserva 2048 B | `<= k_worker_stack_bytes` (derivado de `cyberdeck_image_types.h`) | reserva removida |
| NEG-IMGF-STACK-03 | frames do ELF | símbolos de `run_job`/decode no `build/cyberdeck5.elf` | cada frame + reserva `<=` orçamento; ELF mais novo que as fontes (senão falha, não SKIP) | medir ELF velho |
| NEG-IMGF-STACK-04 | worker único | contrato estrutural | exatamente 1 `xTaskCreate`, 0 em `request()` | criar task por job |
| NEG-IMGF-STACK-05 | janela de inflate | estrutura | buffer de janela/pool alocado por `decode_ops`, não em `.bss`/stack local | `static` global grande |

### 6. Listagem, janela 64, pin e navegação (multi-formato)

| TEST | Cenário | Esperado |
| --- | --- | --- |
| TEST-IMGF-LIST-01 | diretório com `.bmp/.jpg/.jpeg/.png/.gif/.webp/.txt` + subdiretório | só as 4 extensões reconhecidas, ordenadas por bytes; subdiretório e as demais fora (**substitui a asserção ".bmp only" de `TEST-IMG-NAV-01..08`, preservando a propriedade "somente conjunto reconhecido" e adicionando os negativos**) |
| TEST-IMGF-LIST-02 | 65 arquivos mistos | 64 na janela, `truncated`, `64/64+` |
| TEST-IMGF-LIST-03 | pin `.png`/`.jpeg` que ordena após a janela | promovido no índice `cap-1`, sem duplicata, `truncated` |
| TEST-IMGF-LIST-04 | pin já dentro da janela (rank 10 e rank `cap`), por formato | página inalterada |
| TEST-IMGF-LIST-05 | pin com extensão não reconhecida / >5 MiB / symlink / diretório `x.png` / vazio / nome 129 | não promove, não contorna filtros |
| TEST-IMGF-LIST-06 | 2×cap com pin no rank 90 | vítima = último ordenado (não o primeiro); retenção preservada |
| TEST-IMGF-LIST-07 | cancelamento/erro com página promovida semeada | página limpa, sem publicação parcial |
| TEST-IMGF-LIST-08 | navegação por índice reusa a página publicada | zero `list_images` no ramo `by_index` (contrato estrutural mantido) |
| TEST-IMGF-LIST-09 | navegação bmp→jpg→png→bmp | cada passo decodifica o formato correto; clamp nos extremos |
| TEST-IMGF-LIST-10 | `format_position`/`step_image_index` | semântica inalterada (`i/N`, `i/N+`, `0/0`) |

**Decisão D4 (default proposto):** navegar para arquivo cuja extensão é reconhecida mas cujo
conteúdo é inválido **falha** com motivo tipado e retorna ao shell (comportamento atual do BMP;
não inventar "skip" silencioso). O teste de navegação usa arquivos válidos e um caso negativo
explícito.

### 7. Fixtures e oráculos

| ID | Regra |
| --- | --- |
| TEST-IMGF-FIX-01 | fixtures versionadas em `tests/host/keymap/fixtures/image/` com `manifest.json` (nome, formato, dimensões, variante, sha256, comando gerador + versão da ferramenta) e `README`; o teste recalcula o sha256 e falha se divergir |
| TEST-IMGF-FIX-02 | proibido re-encodar dentro do teste; nenhum teste importa o conversor de produção como oráculo |
| TEST-IMGF-FIX-03 | BMP continua gerado em memória (como hoje), sem fixture |
| TEST-IMGF-FIX-04 | conjunto mínimo JPEG: 4:4:4 8x8, 4:2:0 16x16, 4:2:2 32x16, grayscale 8x8, 3x5, 1x1, 1280x1280, EXIF orientation 6, progressive, SOF1, CMYK 4 comp, 12-bit (header), sem EOI, DHT inválido |
| TEST-IMGF-FIX-05 | conjunto mínimo PNG: truecolor 4x4, gray 4x4, palette 4x4, RGBA 4x4, multi-IDAT, ancillary, 1x1, 1280x1280, 16-bit, interlace, CRC ruim (IHDR/IDAT), truncado, zlib bomb, PLTE ausente, chunk crítico desconhecido |
| TEST-IMGF-FIX-06 | oráculo PNG/BMP (lossless): literais RGB565 exatos por pixel, derivados da especificação da fixture (cores em múltiplos de 8/4 para evitar ambiguidade de arredondamento) |
| TEST-IMGF-FIX-07 | oráculo JPEG (lossy): pixels de **centro de quadrante plano** com tolerância declarada (ex.: ±6 por canal em 4:2:0, ±4 em 4:4:4, ±2 em grayscale) + digest do frame completo pinado a partir da fixture; bordas de transição de croma excluídas e o motivo documentado |
| TEST-IMGF-FIX-08 | sensibilidade obrigatória do oráculo JPEG: mutação de troca de canal (R↔B) e de ordem de linha deve falhar as asserções |
| TEST-IMGF-FIX-09 | sweep determinístico: cada prefixo de uma fixture válida (1..n-1) deve ser rejeitado sem crash/leak; N mutações de 1 byte com PRNG de semente fixa idem (sob ASan+UBSan) |

### 8. Alvos de teste e contratos estruturais a criar/estender

| ID | Alvo/ação |
| --- | --- |
| TEST-IMGF-GATE-01 | novos alvos `test_image_jpeg`, `test_image_png`, `test_image_formats`, `test_image_decode_contract` em `.PHONY`, `BINS`, artefatos `_bin`, agregado `test` e `clean` |
| TEST-IMGF-GATE-02 | variantes ASan+UBSan para os alvos de decode (padrão já usado em `test_image_view`), pois o parse é de mídia não confiável |
| TEST-IMGF-GATE-03 | `test_image_viewer_contract.py`: manifesto/uso mencionam os formatos, wiring CMake/Makefile, `MIN_VERSION` como piso (nunca versão exata), `code-map.md` e roteiro manual atualizados |
| TEST-IMGF-GATE-04 | `test_image_stack_frame_contract.py` estendido às novas TUs e símbolos (deriva de `k_worker_stack_bytes`, fail-closed se o ELF estiver velho) |
| TEST-IMGF-GATE-05 | contrato de layering: TUs puras sem `lvgl.h`/`esp_*`, sem `lv_fs`, sem I/O no caminho LVGL; `set_image` continua a única dona do frame |
| TEST-IMGF-GATE-06 | coverage scope guard: novas TUs entram como cobertas por host (não na allowlist de hardware); gates 80/90/50 inalterados (execução é do reviewer) |
| TEST-IMGF-GATE-07 | fixture manifest verificado (sha256) e ausência de re-encode |
| TEST-IMGF-GATE-08 | regressão de escopo do upload: `validate_upload_name` continua `.bmp`-only |

### 9. Validação manual no Tab5 (a acrescentar ao roteiro como seção F)

| ID | Passo | Evidência esperada |
| --- | --- | --- |
| TEST-IMGF-MAN-01 | abrir fixture de cada formato em `/fotos` | `screen.dump`; cor conferida por amostragem de pixels (vermelho ≈ `0xF800`) |
| TEST-IMGF-MAN-02 | 1280x1280 JPEG e PNG | tempo de decode registrado (ms) e imagem inteira, sem corte |
| TEST-IMGF-MAN-03 | foto com EXIF orientation 6 | **não** rotacionada |
| TEST-IMGF-MAN-04 | type 6 (alpha) conforme decisão D3 | fundo preto uniforme ou rejeição tipada, conforme a decisão |
| TEST-IMGF-MAN-05 | 10 ciclos de abrir/fechar por formato | `sys.info` antes/depois: PSRAM/heap interno sem queda monotônica; uptime contínuo |
| TEST-IMGF-MAN-06 | `Esc` durante decode de 1280x1280 | retorno ao shell, prompt respondendo, sem panic/WDT/reboot |
| TEST-IMGF-MAN-07 | navegação mista bmp→jpg→png e `i/N` | ordem e indicador corretos; sem `+` indevido |
| TEST-IMGF-MAN-08 | arquivos fora dos limites (>5 MiB, 1281 px, extensão não suportada, conteúdo inválido) | mensagem tipada, volta ao shell, teclado funcional |
| TEST-IMGF-MAN-09 | monitor serial durante todo o roteiro | zero panic/WDT; `sdkconfig` sem `LV_USE_*` de decoder ligado por acidente |
| TEST-IMGF-MAN-10 | `image help`/`help`/`app info` | uso documenta os formatos; `image [app]` inalterado |

## Evidence

Fase de especificação: **nenhum comando de teste, build ou flash foi executado**. Não há resultado
de execução a citar; a nota é especificação e não prova de comportamento.

Leituras que sustentam a matriz (linhas verificadas nesta sessão):

| Fonte | Fato usado |
| --- | --- |
| `code-map.md` §"Visualizador de imagens" (2562-2608) | módulos, seams, matriz `REQ/AC-IMG -> TEST` existente, BUG-IMG-01/02, `k_worker_stack_bytes`, limites declarados |
| `cyberdeck_image_bmp.{h,cpp}` | `k_max_file_bytes` 5 MiB, `k_max_dimension` 1280, `k_header_prefix_bytes` 54, `bmp_status` (11 valores) e `bmp_status_reason`, stride `((w*3)+3)&~3`, gates em 64 bits, `rgb565_buffer_bytes` |
| `cyberdeck_image_decode.{h,cpp}` | `decode_ops` (4 seams), gate `file_bytes == 0 || > cap` (inclusivo em `== cap`), alloc única `pixel_bytes + row_stride`, poll por linha, release em toda falha |
| `cyberdeck_image_list.{h,cpp}` | `is_bmp_name` (5..128), janela 64 ordenada/única, pin substitui a última entrada, `directory_workspace` heap-backed (BUG-IMG-01/02), `valid_directory_path`, recusa `/sdcard` |
| `cyberdeck_image_app.cpp` | manifesto, `execute()` com `is_bmp_name`, mensagens `usage:`/`invalid path`/`cannot open file`/`file larger than 5 MiB`/`read failed`/`out of memory`, geração monotônica, `publish`/`fail` sob mutex, join 200 ms |
| `cyberdeck_image_view.cpp` | posse única do frame, `release_decoded(decoded_image)` com **um** ponteiro, RGB565 `stride = w*2` |
| `tests/host/keymap/test_image_{bmp,decode,list,view}.cpp`, `test_image_{viewer,stack_frame}_contract.py` | IDs e propriedades existentes; `-fstack-usage` (limite 1024 B/frame, reserva 2048 B) e medição por ELF com fail-closed por mtime |
| `sdkconfig` | `LV_USE_LODEPNG/LIBPNG/TJPGD/BMP = n`, `LV_CACHE_DEF_SIZE = 0`, `LV_USE_FS_* = n`, `CONFIG_SPIRAM=y` |
| `managed_components/lvgl__lvgl/src/libs/tjpgd/{tjpgd.c,tjpgdcnf.h}` | SOF0 apenas (SOF1/2/3/9..15 → `JDR_FMT3`), grayscale suportado, pool do caller, `JD_FORMAT 0` (RGB888), `JD_SZBUF 512`, `JD_USE_SCALE 0` |
| `managed_components/lvgl__lvgl/src/libs/{lodepng,libpng,libjpeg_turbo}` | `lodepng.c` presente (full-frame); `libpng`/`libjpeg_turbo` só wrapper (biblioteca externa ausente) |
| `~/esp/esp-idf/components/esp_driver_jpeg/include/driver/jpeg_decode.h` | `jpeg_decoder_process` exige bitstream inteiro, saída arredondada para 16 px → não serve como decode streaming |
| `components/cyberdeck/src/apps/control/cyberdeck_control_upload.cpp` | `validate_upload_name` valida `.bmp` por conta própria (escopo do upload não muda) |
| `tests/manual/image-viewer-validation.pt-BR.md` | roteiro A..E a estender com a seção F |
| `agent-toolkit/workflows/developer-workflow.md` | `high`: matriz de aceitação/limite/falha/regressão antes da implementação |

## Decisions and assumptions

Premissas reversíveis assumidas nesta especificação (o developer pode revertê-las sem perda):

1. Extensão é **autoritativa**: conteúdo divergente gera rejeição tipada, não reaproveitamento por
   sniffing (evita aceitar formatos não aprovados por acidente).
2. Teto de arquivo 5 MiB e 1280 px/eixo valem para os três formatos ("preservando os limites
   atuais"). Consequência aceita a confirmar: fotos de celular 4000×3000 serão rejeitadas.
3. Navegar para conteúdo inválido falha e retorna ao shell (sem skip).
4. `POST /control/upload` permanece `.bmp`-only; nenhum teste novo deve exigir JPEG/PNG por upload.
5. Mensagens de erro vêm de uma função única por formato; testes comparam status + composição
   `"image: " + reason + "\n"`, sem duplicar literais.
6. Versão: `version.txt` sobe MINOR (mudança de base) e o manifesto do app sobe PATCH; contratos
   usam apenas formato + piso, nunca a versão exata.

### Blockers (impedem aprovar início de implementação da parte correspondente)

- **B1 — PNG incremental (bloqueante para REQ-IMGF-03).** Não há evidência, nas fontes consultadas,
  de um decoder PNG incremental/por linha com pico limitado, licença aceitável, host-linkável pela
  receita atual e sem dependência externa não vendorizada. O que existe: `lodepng` (full-frame, pico
  `w*h*4 + 3.2 MB` no pior caso, ~10 MB, acima do recorte "pico adequado") e wrapper `libpng` sem a
  biblioteca. Requer decisão de coder/research (biblioteca, versão pinada, licença/atribuição,
  estratégia de host-compile) e **checkpoint humano** se não houver caminho dentro do orçamento de
  memória; o escopo não pode virar "somente JPEG" silenciosamente.
- **B2 — origem do TJpgDec (bloqueante para REQ-IMGF-02).** Decidir entre copiar `tjpgd.c` para o
  repositório com atribuição e configuração própria (`JD_FORMAT` RGB565, `JD_USE_SCALE`,
  `JD_FASTDECODE`) ou habilitar `LV_USE_TJPGD` no LVGL gerenciado. A segunda opção usa configuração
  de componente gerado e puxa o wrapper LVGL com `lv_fs`/cache desligados; a primeira exige política
  de atualização/licença. Sem essa decisão não há como fixar o orçamento de pool (C5) nem a receita
  de build host.
- **B3 — host-linkabilidade (gate C7).** Se a implementação de qualquer formato exigir biblioteca
  externa no host, a matriz negativa deixa de executar e a prova regride para estrutural; nesse
  caso, parar e replanejar em vez de afrouxar os testes.

### Decisões pendentes de aprovação (não bloqueiam o início de JPEG/BMP, bloqueiam PNG/aceite)

- **D3 — política de alpha PNG** (type 4/6/`tRNS`): default proposto = rejeitar
  (`unsupported_alpha`) por consistência com "sem transparência" do recorte atual; alternativa =
  compositar sobre preto (determinístico, mas visual e portanto decisão humana).
- **D5 — SOF1**: rejeitar (alinhado ao TJpgDec), sem tentar "quase baseline".
- **D6 — EXIF/ICC**: ignorados; sem rotação e sem color management (explícito e testado).
- **D7 — teto 1280 em JPEG/PNG**: manter, aceitando a rejeição de fotos grandes; subir o limite
  exigiria novo orçamento de memória e nova aprovação.
- **D8 — tempo de decode**: 1280x1280 JPEG baseline em software no P4 não medido; medir no roteiro
  físico antes de considerar a UX aceitável (sem pinar valor nesta nota).

Jev: não utilizado. Não há empate técnico fechado sustentado por evidência; B1/B2 são decisões
técnicas que exigem pesquisa/atribuição, não priorização de cobertura.

## Pending and risks

- Nota é especificação; **nada foi executado**. Toda a matriz precisa virar teste e ser executada
  (fases de coder e tester de validação).
- Risco alto residual: parse de mídia não confiável em C++ com histórico de overflow de stack
  (BUG-IMG-01/02) e um único bloco de frame com posse movida; a prova de memória depende de
  contadores e de frames medidos, não de inspeção textual.
- O worker/FreeRTOS não é linkável no host: cancelamento/stale/join são provados pelo modelo puro +
  contrato estrutural + poll granularity, e concluídos apenas na validação física (F6/F5).
- B1/B2 impedem fixar constantes de orçamento; qualquer implementação iniciada antes disso corre
  risco de retrabalho.
- O conjunto de fixtures depende de ferramentas externas (versões registradas); sem `manifest.json`
  verificado, o oráculo perde rastreabilidade.
- A asserção ".bmp only" de `TEST-IMG-NAV-01..08` será **reescrita** (não removida) para o conjunto
  reconhecido; qualquer remoção de cenário ou afrouxamento de asserção deve ser rejeitada na revisão.

## References

- `code-map.md` §"Visualizador de imagens (REQ/AC-IMG-01..05)" (2562-2608)
- `components/cyberdeck/include/apps/image/`, `components/cyberdeck/src/apps/image/`
- `components/cyberdeck/src/platform/display/cyberdeck_image_view.cpp`
- `tests/host/keymap/test_image_{bmp,decode,fit,list,view}.cpp`,
  `test_image_worker_state.cpp`, `test_image_viewer_contract.py`,
  `test_image_stack_frame_contract.py`, `tests/host/keymap/Makefile`
- `tests/manual/image-viewer-validation.pt-BR.md`
- `docs/agent-state/image-formats/planner.md`, `docs/agent-state/image-viewer.md`
- `sdkconfig`, `main/idf_component.yml`, `managed_components/lvgl__lvgl/src/libs/{tjpgd,lodepng,libpng,libjpeg_turbo}`,
  `~/esp/esp-idf/components/esp_driver_jpeg/include/driver/jpeg_decode.h`
- `agent-toolkit/workflows/developer-workflow.md`, `agent-toolkit/templates/task-note.md`

## Updates

### 2026-11-08 (hora não aferida) -03:00 - fechamento da matriz high-risk

- Fact: matriz de aceitação/limite/falha/regressão escrita para BMP/JPEG/PNG, com contratos de
  testabilidade (C1..C7), fixtures/oráculos, alvos de teste, contratos estruturais e roteiro físico;
  blockers B1 (PNG incremental) e B2 (origem do TJpgDec) explícitos; nenhuma produção ou teste
  alterado e nenhum comando executado.
- Supersedes or discards: o aviso anterior de "nota ausente por limite de passos" (lacuna de
  evidência apontada pelo developer) — a nota agora existe; o conteúdo derivado foi preservado sem
  novo discovery.

### 2026-11-09 (hora local; não aferida, posterior a 14:3x) -03:00 - validação independente da fase 1 (host)

- Fact: validação independente da fase 1 (formatos/JPEG/PNG/dispatch, segurança de parse, memória,
  cancelamento, licenças, stack/wiring) executada sobre o snapshot acima. Nenhuma produção foi
  alterada; apenas testes foram acrescentados/ajustados. Não houve flash nem execução de suíte
  completa/gates globais.
- Commands | exit | head (snapshot atual, binários recompilados pelo Makefile após as edições):
  - `make -C tests/host/keymap test_image_formats` | 0 | `51 checks, 0 failures` (48 antes das adições)
  - `make -C tests/host/keymap test_image_jpeg` | 0 | `406 checks, 0 failures` (ASan+UBSan; 401 antes)
  - `make -C tests/host/keymap test_image_png` | 0 | `467 checks, 0 failures` (ASan+UBSan; 431 antes)
  - `make -C tests/host/keymap test_image_decoders_contract` | 0 | `197 checks, 0 failures`
  - `make -C tests/host/keymap test_image_decode` | 0 | `50 checks, 0 failures`
  - `make -C tests/host/keymap test_image_bmp` | 0 | `82 checks, 0 failures`
  - `make -C tests/host/keymap test_image_list` | 0 | `1028 checks, 0 failures`
  - `make -C tests/host/keymap test_image_view` | 0 | `70 checks, 0 failures`
  - `make -C tests/host/keymap test_image_worker_state` | 0 | `788 checks, 0 failures`
  - `make -C tests/host/keymap test_image_viewer_contract` | 0 | `0 failures`
  - `make -C tests/host/keymap test_image_stack_frame_contract` | 0 | `0 failures` (contrato estendido
    pelo tester; host largest: decode 240 B, formats 80 B, bmp 24 B, jpeg 544 B, png 912 B; ELF:
    decode_descriptor 16 B, decode_jpeg 288 B, decode_png 784 B; cadeia da listagem 160 B; budget
    derivado de `k_worker_stack_bytes` = 6144 B; checagem de ELF velho passou, ELF 13:50 mais novo que
    as fontes).
  - `sha256sum` dos vendorizados (independente): `miniz.c 0fcdc98…`, `miniz.h 295d1a0…`,
    `miniz/LICENSE 0115478…`, `tjpgd.c 3a0091b…`, `tjpgd.h 12d7b17…`, `tjpgdcnf.h 3521612…`,
    `LICENSE.txt 5a115ce…`; todos batem com `components/cyberdeck/THIRD_PARTY.md`. Licenças
    conferidas no texto: miniz MIT; TJpgDec ChaN "no restriction… personal, non-profit or commercial…
    retain the above copyright notice". Config vendorizada: `JD_FORMAT=1`, `JD_USE_SCALE=0`,
    `JD_SZBUF=1024`, `JD_FASTDECODE=1`.
  - `CMakeLists.txt`/`Makefile` inspecionados: as TUs novas e os dois `.c` vendorizados entram no
    build; miniz compilado com `MINIZ_NO_STDIO/ARCHIVE_APIS/DEFLATE_APIS/ZLIB_APIS`; no host os
    vendorizados usam `$(CC)` sem `--coverage`; `clean` remove `test_image_vendor/`.
- Delta de spec corrigido (escopo do tester) — D1 `a.jpg.png`:
  - `TEST-IMGF-FMT-02` listava `a.jpg.png` entre os rejeitados. Prevalece a **última extensão**
    (`a.jpg.png` → PNG), exatamente como `a.jpg.bmp` → BMP já era aceito antes desta entrega;
    endurecer para "ponto único" mudaria o conjunto de nomes BMP aceitos (regressão) ou duplicaria a
    regra por formato. A validação de conteúdo continua autoritativa e rejeita disfarce
    (`NEG-IMGF-EXT-02/05/06`). Teste atualizado e ampliado com `a.png.jpg`, `a.jpg.bmp`,
    `a.png.jpeg`.
- Delta de spec corrigido (escopo do tester) — D2 bytes/chunk após IEND:
  - `TEST-IMGF-PNG-08` prevalece: bytes após `IEND` são **ignorados e nunca interpretados** (a
    especificação PNG manda o decoder não interpretar dados após `IEND`; lixo e um `IDAT` completo
    após `IEND` mantêm o frame). A metade "`IDAT` após `IEND` → rejeição" de `NEG-IMGF-PNG-18` fica
    **superseded**. "`IEND` ausente" continua rejeição e agora tem teste explícito
    (`truncated_segment`).
- Blocker B1 (material, decisão de escopo — não decidida pelo tester) — JPEG 4:1:1:
  - A matriz inicial (seção "JPEG — aceitar", linha com "sampling 4:4:4, 4:2:2, 4:2:0, 4:1:1")
    inclui 4:1:1; a entrega recusa com `unsupported_sampling` (`cyberdeck_image_jpeg.cpp` valida luma
    em {1x1,2x1,2x2} e croma 1x1) e o teste fixa essa recusa (mutação de amostragem `0x41`).
  - Evidência decisiva na cópia vendorizada: `third_party/tjpgd/tjpgd.c:1016-1021` aceita apenas
    `0x11`, `0x21`, `0x22` para luma e exige `0x11` no croma; o upsampling é fixo em MCU de 8/16 px.
    Não é configurável por `tjpgdcnf.h`; suportar 4:1:1 exigiria patch em código de terceiro ou outro
    decoder.
  - Alternativas (developer/humano decide): (a) aprovar o estreitamento para 4:4:4/4:2:2/4:2:0 e
    atualizar spec/mensagem/roteiro — menor risco, preserva host-linkabilidade e licenças; (b) exigir
    4:1:1 com patch/novo decoder, nova vendorização e nova validação de upsampling (muda
    escopo/risco); (c) manter recusa e documentá-la como limitação explícita de UX.
  - Consequência: a fase 1 não pode ser declarada concluída enquanto B1 não for decidido; nenhum
    arquivo de produção foi tocado para "resolver" isso.
- Achados menores registrados (não bloqueiam a fase 1):
  - F1 arquivo vazio (0 byte) retorna `decode_status::too_large` ("file larger than 5 MiB"),
    comportamento pré-existente do BMP; a fase 2 deve tratar 0 byte antes de mapear `reason` para a
    mensagem (não fixei esse texto em teste para não cristalizar mensagem enganosa).
  - F2 `NEG-IMGF-MEM-02` ("recusa na 2ª alocação") é inaplicável ao desenho entregue: frame + pool/
    scratch em **um único bloco** (mais forte que o previsto); os testes comprovam 1 alocação e
    balanço `allocations == frees`.
  - F3 `NEG-IMGF-JPG-14` ("allocate devolve bloco menor") não é expressável no seam `decode_ops`
    (retorno booleano + ponteiro, sem tamanho); só a recusa de alocação é testável.
  - F4 bytes comprimidos após `TINFL_STATUS_DONE` são ignorados, de forma bounded, sem frame parcial;
    leniência baixa, sem teste novo.
- Gaps/limitações restantes (honestidade de cobertura): restart desalinhado (`NEG-IMGF-JPG-11`),
  `PLTE` > 256 entradas (`NEG-IMGF-PNG-07` parcial), overflow de offsets/leitura máxima
  (`NEG-IMGF-LIMIT-08/09`), falha no fim do stream (`NEG-IMGF-IO-03`) e leitura parcial direta
  (`NEG-IMGF-IO-04`); stale/cancelamento do worker/publicação pertencem à fase 2 (modelo puro +
  `test_image_worker_state`). Não houve `idf.py build` nem flash nesta validação; a prova de stack usa
  o ELF já construído (fail-closed por mtime) e as medições host das TUs.
- Testes alterados por mim (somente testes): `tests/host/keymap/test_image_formats.cpp` (D1 + 3
  nomes), `test_image_jpeg.cpp` (`NEG-EXT-05` JPEG declarado sobre bytes PNG → `invalid_soi`),
  `test_image_png.cpp` (`IDAT` zero-length aceito, `PLTE` em type 2 sem efeito, `IEND` ausente →
  `truncated_segment`, `IEND` sem `IDAT` → `missing_image_data`, length `>2^31-1` → `invalid_header`,
  length enorme → `truncated_segment`, `PLTE` length %3 → `invalid_palette`, budget de chunks →
  `too_many_chunks`), `test_image_stack_frame_contract.py` (frames host das 5 TUs puras, 3 símbolos de
  decode no ELF, staleness das fontes/headers novos, guarda de que o estado do `tinfl_decompressor`
  não vai para a stack).
- Supersedes or discards: (1) cabeçalho `Updated`/`Code state` acima, que ainda descrevia árvore
  limpa sem a fase 1; (2) a entrada de `TEST-IMGF-FMT-02` para `a.jpg.png` (D1); (3) a metade
  "`IDAT` após `IEND` → rejeição" de `NEG-IMGF-PNG-18` (D2); (4) `NEG-IMGF-MEM-02` como escrito (F2).
  Nada mais da matriz é revogado. A nota de 2026-11-08 continua válida como especificação; esta
  entrada é a evidência de execução correspondente.
- Próximo passo: `developer` decidir B1 (recomendação: opção (a), aprovar 4:4:4/4:2:2/4:2:0 e
  atualizar spec/mensagem); depois `reviewer` para a revisão final ampla (suíte completa, cobertura,
  build). Fase 2 (app/listagem/UI/help/upload, bump de `version.txt`) segue fora desta entrega.

### 2026-11-09 (hora local; não aferida) -03:00 - B1 resolvido por decisão humana

- Fact: o usuário decidiu explicitamente **rejeitar JPEG 4:1:1**. O suporte aprovado do viewer
  passa a ser baseline **SOF0, 8-bit, 1 componente (grayscale) ou 3 componentes YCbCr, sampling
  4:4:4 / 4:2:2 / 4:2:0**, com **erro tipado** para 4:1:1. O comportamento entregue já corresponde a
  essa decisão: a inspeção recusa 4:1:1 com `jpeg_status::unsupported_sampling`
  (`jpeg_status_reason` = "unsupported JPEG chroma sampling"), antes de qualquer alocação/decode.
- Cobertura atual: `tests/host/keymap/test_image_jpeg.cpp` já fixa 4:1:1 → `unsupported_sampling`
  pela mutação de amostragem de luma para `0x41` (e croma != `0x11` pela mutação em `0x21`), assertando
  estado `format`, reason derivado de `jpeg_status_reason` e `pixels == nullptr`; nenhum teste novo é
  necessário para essa decisão.
- Delta de matriz: o item "JPEG — aceitar: … sampling 4:4:4, 4:2:2, 4:2:0, 4:1:1" da matriz inicial
  fica **superseded** por esta entrada; onde se lê aceitar 4:1:1, vale rejeitar com
  `unsupported_sampling`. Nenhum outro item da matriz muda.
- Commands: nenhum comando executado nesta entrada; nenhum teste ou arquivo de produção alterado.
  A evidência de execução da fase 1 continua sendo a entrada imediatamente anterior (mesmo snapshot
  `0f4ab3a` / diff `823afb41…` / status `7483ecdb…`).
- Supersedes or discards: encerra o blocker B1 da entrada anterior (opção (a) escolhida pelo humano);
  substitui apenas o item de sampling de 4:1:1 na lista de aceitação da matriz inicial.
- Consequência: não há blocker de escopo aberto na fase 1. Próximo passo é `reviewer` para revisão
  final ampla (suíte completa, cobertura, build); fase 2 permanece fora desta entrega.

### 2026-11-09 (hora local; não aferida) -03:00 - rastreabilidade da rejeição de 4:1:1

- Fact: a decisão humana de rejeitar JPEG 4:1:1 ganha ID rastreável explícito na matriz negativa.
  Entrada nova: **`NEG-IMGF-JPG-16`** — campo `sampling factors` do SOF0 (fator de amostragem de luma
  `0x41`, i.e. 4:1:1); esperado `jpeg_status::unsupported_sampling` (estado `decode_status::format`,
  reason de `jpeg_status_reason`), **nenhum pixel** publicado/alocado e nenhuma publicação no worker.
- Teste correspondente já existente (nenhum código novo): `tests/host/keymap/test_image_jpeg.cpp`,
  em `test_rejected_variants()`, caso `{"4:1:1", sof + 11, 0x41, image::jpeg_status::unsupported_sampling}`,
  que asserta `state == format`, reason derivada da tabela do formato, `pixels == nullptr` e
  `g_allocations == 0`.
- Delta de matriz: o item positivo da matriz inicial "JPEG — aceitar: … sampling 4:4:4, 4:2:2, 4:2:0,
  4:1:1" fica **superseded** por esta entrada; o recorte aprovado passa a listar 4:1:1 em
  `NEG-IMGF-JPG-16` (rejeição tipada).
- Commands: nenhum comando executado; nenhum teste ou arquivo de produção alterado.
- Supersedes or discards: substitui a referência genérica a "4:1:1 recusado" da entrada anterior por
  `NEG-IMGF-JPG-16`, sem revogar os demais itens da matriz.
- Consequência: rastreabilidade `campo/limite -> NEG -> teste` fechada para 4:1:1. Próximo passo
  continua `reviewer` (revisão final ampla).

### 2026-11-09 16:1x -03:00 - validação independente da fase 2 (app/listagem/help/docs/versão)

- Snapshot confirmado antes de qualquer evidência: HEAD `0f4ab3a3dd43` (sem commit);
  `git diff | sha256sum` = `727d0353c6e6c9363b08a80c47d3419a3eea37cded1f62d7fa36ede4dec36903`;
  `git status --porcelain | sha256sum` = `09755ae04def41adb4ee1d4f2eda1fab798102e61f21d655f2556be2eea71390`;
  `version.txt` = `1.17.0`; manifesto image = `1.0.2`. Bate com a entrada final de `coder.md`.
  A evidência anterior desta nota (diff `823afb41…`, `1.16.0`) está **stale** e foi substituída
  pela reexecução focada abaixo; nada foi assumido do resultado de fase 1.
- Commands | exit | result (todos no snapshot acima):
  - `make -C tests/host/keymap test_image_formats` | 0 | 51 checks
  - `... test_image_jpeg` | 0 | 406 checks (ASan+UBSan)
  - `... test_image_png` | 0 | 467 checks (ASan+UBSan)
  - `... test_image_decode` | 0 | 50 checks
  - `... test_image_bmp` | 0 | 82 checks
  - `... test_image_list` | 0 | 1218 checks (inclui `test_mixed_directory_resolves_and_decodes_each_format`)
  - `... test_image_view` | 0 | 70 checks
  - `... test_image_worker_state` | 0 | 788 checks
  - `... test_image_decoders_contract` | 0 | 197 checks
  - `... test_image_viewer_contract` | 0 | 0 failures (com as duas asserções novas do tester)
  - `... test_image_stack_frame_contract` | 0 | 0 failures (host: jpeg 544 B, png 912 B; ELF:
    decode_descriptor 16 B, decode_jpeg 288 B, decode_png 784 B; budget 6144 B)
  - `... test_shell_help` | 0 | 249 checks
  - `... test_shell_help_contract` | 0 | PASS (fixture reconciliada, 8 mutações rejeitadas)
  - `... test_system_apps_contract` | 0 | PASS
  - `... test_control_upload_contract` | 0 | PASS
  - `... test_control_upload_protocol` | 0 | 293 checks (inclui `image.png` e `.bmp.exe` recusados)
  - `... test_app_runtime` | 0 | 226 checks
- O que foi verificado na fase 2:
  - Listagem mista → dispatch → decode real: `test_image_list` fixa um diretório com `a.bmp`,
    `b.jpg`, `c.jpeg`, `d.png`, resolve `format_from_name` a partir do nome listado e decodifica cada
    arquivo pelo formato declarado, conferindo geometria e `pixel_bytes`; um `wrong.bmp` com bytes PNG
    falha tipado sem frame. Pin/promoção: `.png` é promovido como qualquer BMP
    (`TEST-IMGF-LIST-03`), e `.gif/.webp`, diretório `x.bmp`/`x.png`, `empty.bmp`, `oversize.bmp`,
    symlink e nome de 129 bytes não são elegíveis.
  - App: `execute()` aceita `.bmp/.jpg/.jpeg/.png` via `format_from_name` e recusa o resto com
    `image: invalid path` antes de I/O; `run_job()` resolve o formato do nome (inclusive pin/navegação)
    e chama `decode_descriptor(..., format, ...)`; toda falha é composta de `result.reason` (fonte
    única por formato, sem `switch` de literais). Manifesto: id/nome, versão `1.0.2`, descrição
    "Foreground image viewer (BMP, JPEG, PNG)", `api_version` `1`, `owns_console=false`,
    `commands_require_active_foreground=false`, help citando `.bmp/.jpg/.jpeg/.png`.
  - Help/manifesto: golden de `image help` e contrato de wiring reconciliados com o manifesto real;
    `test_shell_help`/`test_shell_help_contract`/`test_system_apps_contract` passam.
  - Upload BMP-only: comportamento provado em `test_control_upload_protocol` (`image.png`,
    `image.bmp.exe`, nomes com `.` duplo/separadores/NUL recusados) e `validate_upload_name`
    continua com o predicado `.bmp` próprio; nenhum teste novo exige JPEG/PNG por upload.
  - Docs: README/README.pt-BR e ARCHITECTURE{,.pt-BR} descrevem o subconjunto (BMP 24-bit; JPEG SOF0
    8-bit 4:4:4/4:2:2/4:2:0; PNG 8-bit não entrelaçado tipos 0/2/3), recusas (progressivo, aritmético,
    lossless, 12-bit, CMYK, 4:1:1, alfa/`tRNS`, 16-bit, entrelaçado), EXIF/ICC ignorados, trailing após
    `IEND` ignorado, 5 MiB e 1..1280 px; upload segue BMP-only. `tests/manual/image-viewer-validation.pt-BR.md`
    tem seção F (F1..F20), incluindo 4:1:1, `a.jpg.png`→PNG e upload `.png` recusado. `code-map.md`
    atualizado (app, listagem, dispatch, matriz REQ/AC-IMGF). `version.txt` 1.17.0 e app 1.0.2 com
    pisos em teste (`MIN_VERSION (1,17,0)`, `APP_MIN_VERSION (1,0,2)`), sem pinar versão exata.
  - Fixtures/oráculos: `test_image_decoders_contract` reconfere `manifest.json` (33 fixtures, tamanho
    e sha256), ausência de órfãos e que os testes não reencodam; `test_image_list` e `test_image_*`
    consomem as fixtures commitadas. Licenças/proveniência inalteradas desde a fase 1 e reconferidas
    pelo contrato (hashes do vendor batem com `THIRD_PARTY.md`; miniz MIT e TJpgDec ChaN).
- Defeito D3 (implementação incorreta, severidade baixa; diagnóstico de usuário + claim divergente):
  - Claim do coder/`code-map.md`: arquivo de 0 byte vira `image: empty file` "antes do gate de
    tamanho". O branch existe (`cyberdeck_image_app.cpp:369-375`), mas é **inalcançável**:
    `storage_facade::open_readonly_stream` delega a `cyberdeck_local_shell_open_readonly`, que rejeita
    `st_size <= 0` com `errno = EFBIG` (`components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp:508-519`);
    além disso, a listagem/pin usa `shell_is_regular_file` sobre o mesmo seam, então um `.png`/`.bmp`
    vazio nem entra na página (`test_image_list.cpp:988,998-1007` asserta `empty.bmp` fora da lista).
  - Efeito real: `image /dir/empty.png` com outros arquivos → `image: not found in directory`; diretório
    só com o vazio → `image: no supported image in directory`. Nunca `image: empty file`.
  - Evidência de teste fraca: `test_image_viewer_contract.py` apenas exige a substring "empty file" no
    fonte (passa com o branch morto). Nenhum teste de app é linkável no host (a TU depende de
    FreeRTOS/storage) e o roteiro manual não tem caso de 0 byte.
  - Correção sugerida (coder): detectar 0 byte por `stat`/tamanho antes do pin/listagem (ou permitir o
    open reportar tamanho 0 e só então decidir), restaurando a mensagem pretendida; depois criar a
    regressão (executável no seam de listagem/app ou caso manual F) antes do reviewer. Alternativa
    aceitável, se o humano preferir: manter a rejeição como "não é imagem suportada" e corrigir o
    claim/`code-map` e o contrato — hoje a documentação afirma algo que o binário não faz.
- Achados menores (dívida, sem impacto de comportamento):
  - F6 `is_bmp_name` não tem mais chamador de produção (app usa `format_from_name`, upload tem
    `validate_upload_name` próprio); permanece API pública usada só por testes, e os comentários em
    `cyberdeck_image_list.h`/`cyberdeck_image_decode.h` afirmam que é o predicado do upload, o que não
    é verdade.
  - F7 comentário em `run_job()` ainda fala em "eligible BMP" e "path/BMP/regular-size filters"; o
    filtro agora é o conjunto `is_viewer_name`.
- Lacunas de cobertura (honestidade): mensagens/estado do app (`invalid path`, pin, 0 byte, falhas) não
  têm prova executável no host — só inspeção + contrato estrutural + roteiro manual; `NEG-IMGF-EXT-01`
  (`image x.gif`) e `NEG-IMGF-PUB-02/03` idem; UI-03 (troca de formato na navegação) fica no manual
  F12; validação física/flash não executada (fora do despacho). Nada indica regressão nas negativas
  de parse já executadas (jpeg/png 406/467 checks).
- Testes alterados por mim nesta rodada (somente testes): `test_image_viewer_contract.py` ganhou
  (a) exigência de composição por `result.reason` e ausência dos literais antigos de status do
  `switch` (`image: invalid request`, `image: read failed`), e (b) verificação precisa do predicado
  BMP do upload (`ascii_lower(name[extension + 1..3]) == 'b'/'m'/'p'`). Produção intocada.
- Supersedes or discards: (1) evidência de execução da entrada de fase 1 (diff `823afb41…`) — os
  testes foram reexecutados no snapshot `727d0353…`; (2) a afirmação de coder/`code-map` de que
  0 byte resulta em `image: empty file` (D3); (3) a asserção textual fraca de upload no contrato.
  A decisão humana de rejeitar 4:1:1 (`NEG-IMGF-JPG-16`) e os deltas D1/D2 permanecem válidos.
- Próximo passo: `coder` corrigir D3 (e, se barato, F6/F7) com regressão criada após a correção;
  depois `reviewer` para suíte completa/gates. Fase 2 funcional (listagem, app, help, docs, versão) está
  validada em host; o único item que impede declarar a entrega fechada é o diagnóstico de arquivo
  vazio acima.

### 2026-11-09 16:4x -03:00 - revalidação pós-D3/F6/F7 (D3 fechado)

- Snapshot novo confirmado: HEAD `0f4ab3a3dd43` (sem commit);
  `git diff | sha256sum` = `3da6266e9f12cc95b2dba74c3e8915352a1a2eba1857cfffcccf0870cae86483`;
  `git status --porcelain | sha256sum` = `09755ae04def41adb4ee1d4f2eda1fab798102e61f21d655f2556be2eea71390`;
  `version.txt` = `1.17.0`; manifesto image = `1.0.2`. Bate com a última entrada de `coder.md`.
  A evidência da entrada anterior (diff `727d0353…`) está **stale** e foi substituída pela reexecução
  abaixo; nenhum resultado foi herdado.
- Commands | exit | result (snapshot `3da6266e…`):
  - `make -C tests/host/keymap test_image_list` | 0 | 1231 checks (inclui a regressão nova de D3)
  - `... test_image_viewer_contract` | 0 | 0 failures
  - `... test_image_decoders_contract` | 0 | 197 checks
  - `... test_image_formats` | 0 | 51 checks
  - `... test_image_jpeg` | 0 | 406 checks (ASan+UBSan)
  - `... test_image_png` | 0 | 467 checks (ASan+UBSan)
  - `... test_image_decode` | 0 | 50 checks
  - `... test_image_bmp` | 0 | 82 checks
  - `... test_image_view` | 0 | 70 checks
  - `... test_image_worker_state` | 0 | 788 checks
  - `... test_shell_help` | 0 | 249 checks
  - `... test_shell_help_contract` | 0 | PASS (fixture reconciliada, 8 mutações rejeitadas)
  - `... test_control_upload_protocol` | 0 | 293 checks (BMP-only preservado)
  - `... test_control_upload_contract` | 0 | PASS
  - `... test_image_stack_frame_contract` | 0 | 0 failures (ELF: decode_png 784 B, decode_jpeg 288 B)
  - (não rodei suíte completa, `idf.py build` nem gates globais; o build da rodada é o do coder.)
- D3 **fechado** (verificado de forma independente):
  - O branch e a mensagem mortos saíram: `grep` por `image: empty file`/`file_bytes == 0` em
    `cyberdeck_image_app.cpp`, `tests/manual/image-viewer-validation.pt-BR.md` e `code-map.md` não
    retorna nada. `run_job()` abre o arquivo direto após o pin e comenta, em `listing.count == 0`, que
    `st_size <= 0` já foi recusado pelo seam.
  - Vazio é inelegível, provado no nível mais baixo testável: novo
    `test_image_list::test_empty_files_are_not_listed` cria `empty.bmp/.png/.jpg` (0 byte) e asserta
    página vazia para pasta só com vazios, pin de `empty.png` sem promoção, diretório misto com apenas
    a imagem real e `find_image` falso para os vazios; o mesmo teste confirma que
    `cyberdeck_local_shell_open_readonly` recusa `/empty.png` com `EFBIG` (o gate real).
  - Comportamento contratado e documentado: com outras imagens na pasta →
    `image: not found in directory`; pasta só com o vazio → `image: no supported image in directory`;
    não existe mensagem dedicada a vazio. `code-map.md` (linha do app) e o roteiro manual A10 dizem
    exatamente isso, e o manual não repete a mensagem morta.
  - Contrato deixou de ser vácuo: proíbe `image: empty file`/`file_bytes == 0` no app, exige
    `info.st_size <= 0` no seam de shell, exige a mensagem `image: no supported image in directory`,
    exige `empty` no manual e proíbe a mensagem morta no manual. Minhas asserções da rodada anterior
    (`result.reason`, ausência de `image: invalid request`/`image: read failed`, predicado BMP exato do
    upload) foram preservadas.
  - F6/F7 fechados: `cyberdeck_image_list.h`/`cyberdeck_image_decode.h` agora dizem que
    `is_bmp_name` não é o gate do upload (o upload valida com `validate_upload_name` próprio), e o
    comentário do `run_job()` cita imagem elegível + filtros de caminho/extensão/tamanho, sem "BMP".
- Achados anteriores preservados e reexecutados neste snapshot: 4:1:1 continua recusado
  (`NEG-IMGF-JPG-16`, caso `0x41 → unsupported_sampling` em `test_image_jpeg.cpp`; 406 checks),
  `a.jpg.png` → PNG pela última extensão (51 checks de formats), trailing após `IEND` ignorado e `IEND`
  ausente rejeitado (467 checks de PNG), upload BMP-only (293 + contrato), fixtures/manifest e
  proveniência/licenças inalterados (197 checks), stack host/ELF dentro do budget.
- Lacunas que permanecem (não bloqueiam): mensagens/estado do app seguem sem prova executável no host
  (TU não linkável; prova por listagem/seam + contrato + manual A10); `NEG-IMGF-EXT-01` (`image x.gif`)
  e `NEG-IMGF-PUB-02/03` idem; UI-03 no manual F12. Sem flash/validação física nesta rodada.
- Nenhum teste ou produção foi alterado por mim nesta rodada; apenas esta nota. A correção foi do
  coder (app, contratos, teste de listagem, manual, code-map).
- Supersedes or discards: substitui o estado "D3 aberto" e o snapshot/diff da entrada anterior; a
  evidência de execução passa a ser a desta entrada. Nada dos achados anteriores é revogado (4:1:1,
  D1, D2, F2–F4, licenças e limites permanecem válidos).
- Próximo passo: `reviewer` para revisão final ampla (suíte completa, cobertura, build). Não há
  blocker reproduzível aberto no escopo da fase 2.

### 2026-11-09 17:0x -03:00 - revalidação de NEG-IMGF-CANCEL-01 (BMP/JPEG/PNG)

- Snapshot novo confirmado: HEAD `0f4ab3a3dd43` (sem commit);
  `git diff | sha256sum` = `0d534e5605b95bd9f7f6b70e35e562f0ab2c0fbaeb78319722f807bdce179506`;
  `git status --porcelain | sha256sum` = `09755ae04def41adb4ee1d4f2eda1fab798102e61f21d655f2556be2eea71390`;
  `version.txt` = `1.17.0`. Bate com o delta do coder. A evidência anterior (diff `3da6266e…`) está
  **stale** e foi substituída pela reexecução abaixo.
- Código inspecionado: os três decoders fazem o poll inicial de cancelamento **antes** de ler o
  envelope e antes de qualquer alocação:
  `decode_bmp` (`cyberdeck_image_decode.cpp:65-69`, antes do prefixo de 54 B), `decode_jpeg`
  (`cyberdeck_image_jpeg.cpp:399-402`, antes de `walk_markers`) e `decode_png`
  (`cyberdeck_image_png.cpp:670-673`, antes do `chunk_walk`). O retorno é
  `{decode_status::cancelled, bmp_status::ok, decode_status_reason(cancelled)}`; `decode_descriptor`
  zera `out` no início, então não há frame parcial. Os gates de argumentos inválidos continuam
  vencendo (descriptor/ops nulos → `invalid_argument`) e o gate de tamanho vem depois do cancel.
- Lacuna material encontrada e coberta (teste, não produção): o teste de BMP
  (`test_cancellation_publishes_nothing`) não contava leituras nem exigia zero alocação — o
  comportamento antigo (ler o prefixo, alocar e só então cancelar na 1ª linha) passaria nele. Adicionei
  contador `g_reads` ao witness e reforcei o caso: `cancelled`, `reason ==
  decode_status_reason(cancelled)`, `pixels == nullptr`, `pixel_bytes == 0`, `reads == 0`,
  `allocations == 0`, `frees == 0`. Sensibilidade: sem o poll inicial o BMP lê o prefixo e aloca antes do
  poll por linha, então `reads > 0`/`allocations == 1` falham o teste; os análogos de JPEG/PNG já
  assertavam `g_reads == 0` e `g_allocations == 0` no snapshot.
- Commands | exit | result (snapshot `0d534e56…`):
  - `make -C tests/host/keymap test_image_decode` | 0 | 55 checks (BMP CANCEL-01 reforçado)
  - `... test_image_jpeg` | 0 | 408 checks (ASan+UBSan; inclui CANCEL-01 com 0 reads/0 allocs)
  - `... test_image_png` | 0 | 469 checks (ASan+UBSan; idem)
  - `... test_image_formats` | 0 | 51 checks
  - `... test_image_list` | 0 | 1231 checks
  - `... test_image_viewer_contract` | 0 | 0 failures
  - `... test_image_decoders_contract` | 0 | 197 checks
  - `... test_image_stack_frame_contract` | 0 | 0 failures (ELF: decode_png 784 B, decode_jpeg 288 B)
  - `... test_image_bmp` | 0 | 82 checks; `test_image_view` | 0 | 70; `test_image_worker_state` | 0 |
    788 checks
  - Não executei suíte completa, `idf.py build` nem gates globais; sem flash.
- Achados anteriores preservados: 4:1:1 recusado (`NEG-IMGF-JPG-16`), D1 (`a.jpg.png`→PNG), D2
  (trailing após `IEND` ignorado, `IEND` ausente rejeitado), D3 fechado (0 byte inelegível), F6/F7,
  upload BMP-only, fixtures/licenças e budget de stack.
- Testes alterados por mim nesta rodada (somente testes): `tests/host/keymap/test_image_decode.cpp`
  (contador de leituras + asserções de CANCEL-01). Produção intocada.
- Supersedes or discards: substitui o snapshot/evidência da entrada anterior e o estado "CANCEL-01
  apenas corrigido segundo o coder"; agora está confirmado independentemente, com regressão que
  distingue o comportamento antigo. Nada mais é revogado.
- Próximo passo: `reviewer` (revisão final ampla). Nenhum blocker reproduzível aberto no escopo
  revalidado.

### 2026-11-09 14:4x -03:00 - CHECKPOINT físico Tab5 1.17.0: AC-IMG-01 invertido + PNG paleta falha

- Natureza: checkpoint de emergência por limite de ferramentas. **Nenhum comando de correção,
  cleanup, suíte completa, build ou gate foi executado depois**; nenhum código foi alterado nesta
  etapa. `evidence_kind: medido` (dispositivo real).
- Build/firmware:
  - `build/cyberdeck5.bin` sha256 `2f4a78f445a01c6e230b0e7c95e070be00d516174f4b5f9122471657b413f636`
    (2.782.240 B); `build/cyberdeck5_flashed.bin` tem o mesmo sha256; `esp_app_desc` = `cyberdeck5`
    / `1.17.0` (built 14:12:38); `find components main ... -newer build/cyberdeck5.bin` vazio.
  - Snapshot de código: HEAD `0f4ab3a3dd43`, diff sha `fd002fa5…`, `version.txt` 1.17.0,
    manifesto image 1.0.2 (detalhe no cabeçalho).
- Comandos e efeito (porta `/dev/ttyACM0`, sem monitor concorrente):
  - Antes do flash: `ping` ok; `sys.info` → fw `1.16.0`, uptime `01:03:00`, `safe_mode=false`,
    `interrupted_boots=0`. Otadata (0x10000) lido por `esptool read_flash`: ambos os setores `FFFF…`
    → boot por factory (0x20000, app `1.16.0`).
  - Flash: `idf.py -p /dev/ttyACM0 app-flash` → exit 0, "Hash of data verified".
  - Pós-flash: `sys.info` → fw **`1.17.0`**, uptime 14 s, `safe_mode=false`,
    `interrupted_boots=0`, `internal_free=114915`, `psram_free=31279132`; `ui.dump` → header
    `CYBERDECK5 1.17.0`.
  - Fixtures: `mkdir cd5t-1435` no shell local e `fs.write` (≤2048 B) para
    `/sdcard/cd5t-1435/` — 12 arquivos, todos `ok:true` com size/crc32 conferidos. Arquivos:
    `a.bmp` 246 (8x8 verde), `o.bmp` 822 (16x16 topo azul/base vermelha), `p.png` 1585 (640x480 topo
    vermelho/base azul), `e.png` 1580 (640x480 vermelho liso), `f.png` 197 (128x128 paleta 4 cores),
    `b.jpg` 1051 (128x96 quadrantes 4:4:4), `c.jpeg` 862 (4:2:0), `d.jpg` 723 (grayscale),
    `i.jpg` 907 (4:1:1; SOF luma `0x41` conferido), `g.png` 385 (bytes JPEG), `h.png` 121 (tRNS),
    `j.png` 86 (1281x1).
  - Segurança/cleanup: tentativa de sobrescrever `a.bmp` foi **recusada** pelo firmware ("FATFS sem
    replace atomico garantido"); **nenhum arquivo preexistente do usuário foi tocado** (pasta/arquivos
    são novos em `/cd5t-1435`) e **nenhum cleanup foi executado** (permanecem no SD). Sem UI
    destrutiva.
- Evidência objetiva no dispositivo (frames invertidos verticalmente):
  - Host de referência: probe local linkando os decoders de produção (`png/bmp/jpeg/decode/formats`
    + vendor objs) sobre os mesmos arquivos:
    `p.png` TL=`f800` (vermelho), BL=`001f` (azul); `o.bmp` TL=`001f` (azul), BL=`f800` (vermelho);
    `b.jpg` TL=`f800`, BL=`001e`, TR/TC=`07c0`, BR/BC=`ffdf`. `test_image_*` focados já passavam.
  - `screen.dump` e HTTP `/screenshot` (`192.168.68.127`) concordam entre si, 1280x720 24-bit:
    - `p.png`: azul bbox y=146..385, vermelho y=386..625; amostra (640,160)=(0,0,255),
      (640,620)=(255,0,0) → **topo azul/base vermelha**, oposto do host (topo vermelho).
    - `o.bmp`: vermelho bbox y=378..385, azul y=386..393 → **topo vermelho/base azul**, oposto do
      host.
    - `b.jpg`: amostras (608,362)=(0,0,255) azul no topo-esquerdo, (608,410)=(247,0,0) vermelho no
      fundo-esquerdo; TR verde e BR branco → **inversão vertical** (troca topo↔base).
  - `a.bmp` (verde sólido 8x8): abre, pixel (639,385)=(0,255,0), bbox 636..643 x 382..389 (8x8, sem
    ampliação), título `a.bmp`, indicador `1/10`; verde não revela orientação.
  - Indicação de orientação da cena/captura: header/texto do app corretos no topo (texto em y≈27-39)
    e hint no rodapé (y≈690); logo **não** é rotação 180 da cena nem artefato de uma única via de
    captura (duas vias independentes concordam). Viola AC-IMG-01 ("linha 0 = topo") e manual F1.
  - Fronteira host vs dispositivo: os decoders produzem frames top-down corretos no host; a inversão
    aparece no caminho físico de instalação/renderização do frame no Tab5 e afeta **BMP, JPEG e PNG**.
- Segundo achado físico (PNG paleta):
  - `f.png` (128x128, tipo 3, 4 entradas, 197 B, CRC/sha conferidos; host probe `state=0`, dimensões
    128x128, TL=`f800`; ImageMagick abre e amostra corretas) falha no Tab5 com
    **`image: PNG decode failed`** (mensagem no `term.dump`), sem frame. Reproduzir/limitar antes de
    concluir; pode ser fixture acima de algum limite de staging de paleta ou defeito real do decoder
    no alvo.
- Saúde do dispositivo durante a etapa: uptime monotônico, `safe_mode=false`, `interrupted_boots=0`,
  heap interno/PSRAM dentro da faixa, **sem panic/WDT/reboot**. A proteção de tela apagou a tela
  durante a pausa de análise e exigiu `screen.wake` antes de `ui.type` (comportamento esperado).
- Cenários executados: abertura BMP (a.bmp/o.bmp), JPEG 4:4:4 (b.jpg), PNG truecolor (p.png),
  indicador/ título/ hint, duas vias de captura, `sys.info` antes/depois. **Não executados** (ficam
  pendentes): `e.png`, `c.jpeg`, `d.jpg` com captura/amostragem; rejeições tipadas `i.jpg` (4:1:1),
  `h.png` (alfa), `g.png` (assinatura), `j.png` (1281 px); navegação por setas F12; `image help`/
  `app info` F18; upload `.png` recusado F19; percepção humana de cores/fluidez e tempo de decode
  (F1/F10/F11/F14).
- Supersedes or discards: nada da matriz host é revogado; esta entrada acrescenta a evidência física
  e mantém abertos o blocker de orientação (AC-IMG-01) e o defeito do PNG paleta. A entrada anterior
  (CANCEL-01) continua válida para host.
- Próximo passo: **coder** diagnosticar/corrigir (1) inversão vertical do frame no caminho
  device→`lv_image` (o frame decodificado é top-down no host; verificar montagem do descriptor,
  buffer/stride e cópia de linhas no alvo) e (2) `PNG decode failed` do PNG paleta; depois `tester`
  reexecuta a etapa física (sem repetir cenário de crash — nenhum ocorreu) e completa os cenários
  pendentes. Sem flash adicional até diagnóstico.

### 2026-11-09 15:0x -03:00 - retificação: orientação é falso positivo; causa-raiz do paleta confirmada

- Snapshot novo: HEAD `0f4ab3a3dd43`; `git diff | sha256sum` =
  `2aa7816a1e37a91e198c508adb2dcac22e35f46fb5c0fda76df6ca82a196c2b0`;
  `git status --porcelain | sha256sum` = `a5e6d548fe448c50cf987c1cf668bd66427fcdb16af0b8f0393165d5dc266301`;
  `version.txt` 1.17.0; `build/cyberdeck5.bin` sha256
  `9cbc468500863beb8feba5d485bfcd0686bd51284b195f08438b4c88ca13ea13` (14:55, 2.787.776 B),
  `esp_app_desc` = `cyberdeck5`/`1.17.0`; nenhuma fonte de produção mais nova que o binário.
  Substitui o snapshot `fd002fa5…`/bin `2f4a78f4…` como referência atual.
- (1) **Orientação: falso positivo do tester; BLOCKER RETIRADO.**
  - Erro de análise: eu li o BMP de captura (24-bit) como triplas **RGB**, mas BMP é **B,G,R**, e
    deixei de aplicar a inversão de linha de `biHeight > 0` (bottom-up) na primeira triagem de
    canais; com a leitura correta os mesmos arquivos mostram orientação/canais certos.
  - Releitura independente de `/tmp/opencode/cd5-*.bmp`, `http-shot.bmp` e `serial-shot.bmp`
    (arquivos presentes, 2.764.854 B, 1280x720x24) conforme spec (B,G,R + linhas de
    `height-1..0`), com os decoders de produção como referência host:
    - `p.png` (host TL=`f800` vermelho, BL=`001f` azul): captura tem **vermelho** y=146..385 e
      **azul** y=386..625 → topo vermelho, igual ao host.
    - `b.jpg` (host TL vermelho, TR verde, BL azul, BR branco): captura TL=`red`, TR=`green`,
      BL=`blue`, BR=`white` nos centros de quadrante → igual ao host.
    - `o.bmp` (host TL azul, BL vermelho): captura **azul** y=378..385 e **vermelho** y=386..393
      → igual ao host.
    - `a.bmp`: pixel verde em (639,385), bbox 8x8 em x636..643/y382..389, título `a.bmp`,
      indicador `1/10`.
    - `screen.dump` e HTTP `/screenshot` concordam; header/texto permanecem no topo. Nenhuma
      inversão de linha nem troca de canal observada na leitura correta.
  - Classificação: **defeito de teste/análise (tester)**, não defeito de produto; nenhum código de
    produção precisa mudar por esse item. O coder adicionou `test_image_capture_orientation`
    (`50 checks`) com leitor independente que honra B,G,R e `biHeight`, e o manual agora documenta
    como ler capturas; concordo com a cobertura.
- (2) **PNG paleta: causa-raiz confirmada e correção verificada no binding.**
  - Fato do problema: `esp_rom/esp32p4/ld/esp32p4.rom.ld` atribui
    `tinfl_decompress = 0x4fc000f8` (e `*_mem_to_*`, `mz_adler32`, `mz_free`). A atribuição absoluta
    vencia a definição do objeto e o alvo executava o inflater **ROM (miniz 2.x)** com o estado
    `tinfl_decompressor` do header 3.0.2 — ABI incompatível; uns streams passavam (`p.png`) e outros
    falhavam (`f.png`, paleta 128x128), com risco de corrupção no scratch.
  - Correção presente: `third_party/miniz/cyberdeck_miniz_names.h` prefixa os 7 símbolos e
    `third_party/miniz/miniz_cyberdeck.c` é a única unidade compilada (inclui prefixo + `miniz.c`);
    `cyberdeck_image_png.cpp` inclui o prefixo antes de `miniz.h`; CMake e Makefile compilam o shim.
  - Verificação independente no snapshot atual: `riscv32-esp-elf-nm build/cyberdeck5.elf` →
    `T 400768c2 cyberdeck_tinfl_decompress` e `T 40076826 cyberdeck_mz_crc32` (flash text, fora da
    faixa ROM `0x4fc0_xxxx`); `nm -u` do objeto `cyberdeck_image_png.cpp.obj` referencia somente
    `cyberdeck_tinfl_decompress`/`cyberdeck_mz_crc32`; `miniz_cyberdeck.c.obj` define os `cyberdeck_*`;
    nenhum objeto do componente referencia `tinfl_decompress`/`mz_*` sem prefixo. Os símbolos `A`
    da ROM continuam na tabela, mas não são usados pelo nosso código. `miniz.c`/`miniz.h` seguem
    com os sha256 do `THIRD_PARTY.md`.
- Commands | exit | result (focados, sem suíte completa/build/flash):
  - `make -C tests/host/keymap test_image_png` | 0 | **500 checks** (inclui `test_palette_128_bands`)
  - `... test_image_capture_orientation` | 0 | **50 checks**
  - `... test_image_decoders_contract` | 0 | **212 checks** (proíbe `miniz.c` cru, exige shim/prefixo)
  - `... test_image_stack_frame_contract` | 0 | **0 failures** — `elf miniz binding: vendored
    inflater linked, no ROM tinfl_decompress`; frames ELF decode_png 784 B / decode_jpeg 288 B
  - `... test_image_decode` | 0 | 55 checks
  - `git diff`/`git status` recomputados acima após os testes; sem alteração de arquivos.
- Pendente (honestidade): a correção do paleta ainda **não foi validada no Tab5** — nesta etapa não
  houve flash (conforme despacho). Pendências físicas anteriores permanecem: `f.png`/paleta 128x128,
  `e.png`, `c.jpeg`, `d.jpg`, rejeições (4:1:1, alfa, assinatura, 1281 px), navegação F12, help/F18,
  upload F19 e percepção humana/tempo de decode. Nenhum panic/reboot ocorreu; nenhum arquivo
  preexistente foi tocado; os arquivos novos em `/cd5t-1435` seguem no SD (sem cleanup).
- Testes ajustados por mim nesta rodada: **nenhum** (apenas a nota). Produção intocada.
- Supersedes or discards: substitui a afirmação de "frames invertidos verticalmente/AC-IMG-01" do
  checkpoint anterior (falso positivo, retirado) e o diagnóstico em aberto do PNG paleta (agora ROM
  hijack confirmado e corrigido no binding). Mantém válidos todos os resultados host anteriores e a
  matriz 4:1:1/D1/D2/D3/F6/F7.
- Próximo passo: `reviewer` pode usar este feedback independente; antes do aceite final, `tester`
  deve refazer o flash autorizado com o binário `9cbc4685…` e repetir no Tab5 `f.png`/paleta 128x128 e
  os cenários físicos pendentes (sem repetir cenário de crash — nenhum houve).

### 2026-11-09 15:2x -03:00 - revalidação física do binário 9cbc4685 (paleta corrigida; orientação OK)

- Natureza: etapa física aprovada pelo usuário. `evidence_kind: medido`. **Medição não é aceite de
  usuário**; percepção visual/fluidez e navegação por teclado físico continuam pendentes.
- Snapshot/build conferido antes do flash:
  - HEAD `0f4ab3a3dd430932241df79fa3ea9de5a610bf91`; `git diff | sha256sum` =
    `2aa7816a1e37a91e198c508adb2dcac22e35f46fb5c0fda76df6ca82a196c2b0`; status =
    `a5e6d548fe448c50cf987c1cf668bd66427fcdb16af0b8f0393165d5dc266301`; `version.txt` 1.17.0.
  - `build/cyberdeck5.bin` sha256 **`9cbc468500863beb8feba5d485bfcd0686bd51284b195f08438b4c88ca13ea13`**
    (2.787.776 B); `esp_app_desc` = `cyberdeck5`/`1.17.0`; `find … -newer build/cyberdeck5.bin`
    vazio → fonte de produção atual corresponde ao snapshot revisado.
  - Antes do flash: `ping` ok; `sys.info` → fw `1.17.0` (build anterior), uptime `00:29:23`,
    `safe_mode=false`, `interrupted_boots=0`; `screen.status` off (acordado para operar).
- Flash e boot:
  - `idf.py -p /dev/ttyACM0 app-flash` | **exit 0** | "Hash of data verified", hard reset.
  - Pós-flash: `sys.info` → fw **1.17.0**, uptime 7 s, `safe_mode=false`,
    `interrupted_boots=0`, `internal_free=163371`, `psram_free=31310212`.
  - Fixtures reutilizadas em `/sdcard/cd5t-1435/` (12 existentes). Novo arquivo exclusivo
    `f128.png` (fixture `png_palette_128.png`, 166 B) via `fs.write` → size/crc
    `1863174189` iguais ao local; **nenhuma sobrescrita** e nenhum arquivo preexistente tocado.
- Resultados medidos no Tab5 (capturas `screen.dump`, lidas conforme spec: bytes **B,G,R** e
  `biHeight>0` bottom-up; HTTP/serial já provados equivalentes):
  - **PNG paleta 4 cores `f.png`** (falhava antes com `PNG decode failed`): agora abre,
    título `f.png`, indicador **6/13**; captura reproduz o padrão diagonal esperado dos blocos
    16x16 (R,G,B,W ciclando por `(gx+gy)%4`) → correção do ROM hijack confirmada no aparelho.
  - **PNG paleta 128x128 `f128.png`** (4 faixas de 32 linhas): abre, **7/13**; amostras
    (640,338)=vermelho, (640,370)=verde, (640,402)=azul, (640,434)=branco → faixas na ordem
    topo→base, sem espelhamento.
  - **BMP `o.bmp`** (16x16, topo azul/base vermelha no oráculo): **12/13**; captura topo=azul
    (y378..385), base=vermelho (y386..393) → orientação/canais corretos.
  - **JPEG 4:4:4 `b.jpg`** (128x96): **2/13**; quadrantes TL=vermelho, TR=verde, BL=azul,
    BR=branco → ordem de linha e canais corretos.
  - **JPEG 4:2:0 `c.jpeg`** (128x96): **3/13**; mesmos quadrantes corretos.
  - **JPEG grayscale `d.jpg`** (128x96, gradiente): **4/13**; topo≈preto (valor ~16),
    meio cinza 107, base≈branco (206), todos com R=G=B → replicação grayscale e orientação OK.
  - **PNG truecolor `e.png`** (640x480 vermelho): **5/13**; centro (640,360)=vermelho e
    (400,200) dentro da área também vermelho.
  - **PNG truecolor `p.png`** (640x480, topo vermelho/base azul): **13/13**; (640,200)=vermelho,
    (640,600)=azul; bbox 640x480 em x320..959/y146..625 → escala nativa 1:1, sem ampliação.
  - **Rejeições (imagem anterior `p.png` na tela antes de cada uma; sem frame obsoleto)**:
    `i.jpg` (4:1:1) → `image: unsupported JPEG chroma sampling`; `h.png` (tRNS) →
    `image: unsupported PNG transparency`; `g.png` (bytes JPEG com extensão PNG) →
    `image: invalid PNG signature`; `j.png` (1281 px) → `image: invalid PNG dimensions`.
    Todas retornam ao prompt `/$`; `ui.dump` sem título/indicador/hint de imagem; captura após a
    primeira rejeição tem 0 pixels vermelhos/azuis na área central (nenhum frame parcial/obsoleto).
  - Indicadores por abertura explícita mapeados: a.bmp 1, b.jpg 2, c.jpeg 3, d.jpg 4, e.png 5,
    f.png 6, f128.png 7, o.bmp 12, p.png 13 (13 arquivos).
- Saúde: `sys.info` final → uptime **00:05:32** contínuo desde o flash, `safe_mode=false`,
  `interrupted_boots=0`, `internal_free=114827`, `psram_free=26592952`; **sem panic, WDT ou
  reboot** durante todo o roteiro. Uptime monotônico e sem repetição de cenário.
- Não executado/pendente de aceite: navegação por **setas físicas** F12 (a ponte não injeta
  setas; indicador por abertura explícita foi medido), help/manifest F18, upload `.png` F19,
  percepção humana de cores/fluidez/tempo de decode (F10/F11/F14), cleanup (não executado;
  arquivos novos permanecem em `/cd5t-1435`, nenhum preexistente tocado). Sem suíte completa,
  build, gates amplos ou novo flash além deste.
- Testes/ajustes por mim nesta rodada: nenhum (apenas a nota). Produção intocada.
- Classificação: **paleta (f.png/f128) — BLOCKER FECHADO no hardware**; orientação/canais — OK,
  confirmando a retificação do falso positivo; rejeições tipadas — OK, sem frame obsoleto; nenhum
  blocker novo. Restam apenas itens de aceite/percepção e a navegação física por teclado.
- Supersedes or discards: substitui o estado "revalidação física pós-correção pendente" e o
  diagnóstico físico anterior de inversão (já retirado). Mantém toda a matriz host e o feedback
  do ROM binding.
- Próximo passo: `reviewer` para aceite final; pendência explícita de **aceite visual humano** e
  navegação por setas no teclado físico. Nenhum flash adicional necessário até novo defeito.

### 2026-11-09 15:25 -03:00 - RESET DE SESSÃO + matriz high-risk: erro de navegação recuperável (D4-navegação)

- Fact (reset de sessão): a sessão anterior deste papel está encerrada e **não** é herdada como
  critério. Tarefa nova aprovada pelo usuário em 2026-11-09: quando as setas chegam a um arquivo
  listado cujo conteúdo/formato é inválido, o viewer **permanece aberto**, limpa o frame anterior,
  mostra o erro tipado **na própria tela** e permite seguir com setas até uma imagem válida; `Esc`
  fecha. Abertura inicial inválida por `image <path>` continua retornando ao shell. Nenhum
  resultado de execução da sessão anterior é reaproveitado; só o snapshot de código abaixo.
- Code state (medido com comandos nesta entrada): HEAD `0f4ab3a3dd43`;
  `git diff | sha256sum` = `2aa7816a1e37a91e198c508adb2dcac22e35f46fb5c0fda76df6ca82a196c2b0`;
  `git status --porcelain | sha256sum` = `a5e6d548fe448c50cf987c1cf668bd66427fcdb16af0b8f0393165d5dc266301`;
  `version.txt` = `1.17.0`; manifesto `cyberdeck.image_viewer` = `1.0.2`; binário local
  `9cbc468500863beb8feba5d485bfcd0686bd51284b195f08438b4c88ca13ea13` (não reexecutado).
- Carimbo: medido com `date` (`2026-11-09 15:25 -0300`). O cabeçalho `Updated` não foi reescrito
  (append-only) e está defasado; a posição no arquivo é a entrada vigente. Entradas anteriores com
  carimbo 16:xx/17:0x usaram relógio não aferido.
- Fontes lidas nesta entrada (nenhuma produção/teste alterada): `AGENTS.md`,
  `docs/agent-state/image-formats/{planner,coder,reviewer}.md`, `code-map.md` (seção do viewer e
  matriz REQ/AC), `components/cyberdeck/{include,src}/apps/image/cyberdeck_image_app.{h,cpp}`,
  `.../cyberdeck_image_types.h`, `.../cyberdeck_image_list.{h,cpp}` (inclui
  `classify_image_key`/`step_image_index`/`format_position`), `.../cyberdeck_image_decode.{h,cpp}`,
  `platform/display/cyberdeck_image_view.{h,cpp}`, `platform/display/cyberdeck_ui.cpp`
  (`process_image_status`, `image_local_key`, `leave_image_viewer`, timer),
  `apps/runtime/cyberdeck_app_runtime.cpp` (`switch_foreground`/`active_foreground`),
  `tests/host/keymap/test_image_{list,view,worker_state}.cpp`,
  `tests/host/keymap/test_image_viewer_contract.py`,
  `tests/manual/image-viewer-validation.pt-BR.md`, `agent-toolkit/workflows/developer-workflow.md`,
  `templates/task-note.md`, `docs/agent-instruction-delivery.md`.
- Restrição cumprida: nenhuma produção ou teste alterado; nenhuma suíte, build ou flash executado.
  Esta entrada é **especificação**, não prova de comportamento.

#### Deltas de especificação (superseded apenas no ramo de navegação)

- **D4 antiga** (linhas 277-280, "navegar para conteúdo inválido falha e retorna ao shell") fica
  **superseded somente para navegação**. A abertura inicial (`execute`/`by_index=false`) mantém
  exatamente o comportamento antigo (AC-IMGF-08).
- **AC-IMGF-02** ("...retornando ao shell"): a metade de navegação fica superseded; a metade inicial
  permanece. Substituída por AC-IMGF-07/08 abaixo.
- **NEG-IMGF-PUB-02** ("sem indicador obsoleto") permanece válido; no erro recuperável o indicador
  passa a ser o do índice que falhou (não o do item anterior).
- **NEG-IMGF-PUB-03** ("retorno ao shell") fica **superseded apenas no ramo de navegação**;
  preservado no ramo inicial.
- **TEST-IMG-NAV-09 / `classify_image_key(true,false,...) == pass_through`** permanece válido para
  "foreground sem frame **e sem erro**" (viewer sem superfície, típico da falha inicial). No erro
  recuperável a superfície existe e as setas são consumidas — nada do baseline é removido.

#### REQ/AC novos e rastreabilidade

| REQ | Conteúdo |
| --- | --- |
| REQ-IMGF-06 | Navegação recuperável: pedido de navegação para entrada listada cujo conteúdo/formato/limite falha não encerra o viewer; publica erro recuperável tipado, sem frame, mantendo página e índice; setas continuam; `Esc` encerra. |
| REQ-IMGF-07 | Disposição de conclusão pura e host-testável (origem inicial/navegação × ok/cancelado/stale) e modelo puro de chrome (título/indicador/erro/posse de teclado) **usados pela produção**, com atualização atômica. |
| REQ-IMGF-08 | Lifecycle/posse no erro recuperável: frame anterior (no app e/ou na view) liberado exatamente uma vez; stale nunca aplica; cancelar/parar não exibe erro; nada parcial. |

| AC | Critério |
| --- | --- |
| AC-IMGF-07 | Após erro de navegação o app continua foreground, sem devolver o console ao shell; `Esc` volta ao shell; a mensagem do erro navegacional **não** vai para o terminal. |
| AC-IMGF-08 | Abertura inicial inválida por `image <path>` mantém o baseline: mensagem no terminal, viewer não permanece, retorno ao shell. |
| AC-IMGF-09 | No erro recuperável: frame anterior limpo, título = nome que falhou, indicador = posição desse índice (`i/N`, `i/N+`), mensagem = `"image: " + reason` da tabela do formato, sem cromo do item anterior. |
| AC-IMGF-10 | O índice corrente avança para o alvo mesmo quando a navegação falha; a próxima seta parte desse índice; clamp nas bordas; sem wrap; `+` preservado. |

| REQ/AC | TEST/NEG |
| --- | --- |
| REQ-IMGF-06 / AC-IMGF-07 | `TEST-IMGF-RNAV-01..03,07,10,12`; `NEG-IMGF-RNAV-01,05,09,15,29,30` |
| AC-IMGF-08 | `TEST-IMGF-RNAV-04,10`; `NEG-IMGF-RNAV-01,02,29,32`; regressão `TEST-IMG-*`/`NEG-IMGF-EXT/PUB` |
| REQ-IMGF-07 | contratos C8/C9; `TEST-IMGF-RNAV-05,06,07,09,13`; `NEG-*` de ordem/estado |
| REQ-IMGF-08 / AC-IMGF-05 estendido | `TEST-IMGF-RNAV-08,11,12`; `NEG-IMGF-RNAV-05..08,17..21,23,25` |
| REQ-IMGF-04/AC-IMGF-04 (memória) | `NEG-IMGF-RNAV-22,23,45` + regressão de memória/stack |
| AC-IMGF-09/10 | `TEST-IMGF-RNAV-01,03,07,09`; `NEG-IMGF-RNAV-29,30,31,33,35..38` |
| REQ-IMGF-01..03/AC-IMGF-01/03/06 (formatos/limites/listagem/upload) | seção de regressão (nada muda) |

#### Contratos de testabilidade (gate: coder pode renomear, não omitir)

| ID | Contrato |
| --- | --- |
| C8 | TU pura host-linkável, sem LVGL/ESP-IDF, que decide a disposição de uma conclusão: entradas `origem ∈ {initial, navigation}`, `ok`, `cancelled`, `current` (geração == pendente) e saída `publish` / `recoverable_error` / `initial_error` / `discard`. `run_job`/publicação **chamam** essa função (o teste não reimplementa a regra). |
| C9 | Função pura de chrome/aplicação atômica: (i) conclusão aplica frame+nome+posição juntos OU sem-frame+nome+posição+erro juntos; (ii) ao aceitar navegação a partir de erro, o erro é limpo imediatamente e o indicador passa ao alvo aceito, sem frame; (iii) nenhum estado mostra índice novo com frame/erro antigo. Chamada pelo caminho de aceite da UI/app (contrato estrutural). |
| C10 | `cyberdeck_image_view::view` passa a oferecer erro recuperável: `lv_image_set_src(nullptr)` **antes** de liberar, libera o frame exatamente 1×, mantém a superfície, expõe mensagem/nome/indicador e limpa o erro ao receber novo frame; `has_image()==false` no erro. |
| C11 | Canal tipado UI↔app: erro recuperável entregue separado do erro inicial (nunca por parse do texto), com `message`, `name`, `index`, `count`, `truncated`; um `publish` novo limpa o erro; `take_*` é one-shot; stale/cancelado não entra no canal. |
| C12 | `navigate` preserva página/índice: erro recuperável mantém `directory_`/`directory_path_`/`has_directory_` e atualiza `current_index_` para o alvo aceito; `initial_error` limpa como hoje; `navigate` sem página → `false` sem gerar job. |
| C13 | Contrato estrutural: exatamente 1 `xTaskCreate`, nenhum I/O na task LVGL, `fail`/rota recuperável re-checa geração e cancelamento, e `process_image_status` consome o canal recuperável **sem** `switch_foreground("cyberdeck.shell")` nem `append_output`; o ramo inicial continua chamando os dois. |

#### Matriz positiva (aceitação e sequências)

| TEST | Cenário | Esperado |
| --- | --- | --- |
| TEST-IMGF-RNAV-01 | navegar (seta) para: `.bmp` com bytes PNG; `.png` com bytes JPEG; `.jpg` com SOF2; `.jpg` 4:1:1 (`NEG-IMGF-JPG-16`); `.png` 16-bit/entrelaçado/RGBA (`NEG-IMGF-PNG-*`) | disposição `recoverable_error`; `take_published == false`; erro tipado derivado da tabela do formato; página/índice preservados; nada alocado/pendente |
| TEST-IMGF-RNAV-02 | do erro, seta para imagem válida (BMP/JPEG/PNG) | `publish`; frame instalado; erro limpo; título/indicador do novo item; nenhum resíduo do item que falhou |
| TEST-IMGF-RNAV-03 | válida k → inválida k+1 → válida k+2; e k → inválida k+1 → esquerda k | índice/posição seguem o alvo; a seta seguinte parte de k+1 (falho); sem rescan; sem wrap |
| TEST-IMGF-RNAV-04 | o mesmo arquivo inválido aberto por `image <path>` (origem inicial) | `initial_error`; mensagem no terminal; nenhuma superfície/erro de viewer; volta ao shell |
| TEST-IMGF-RNAV-05 | ≥2 setas rápidas com um item inválido no meio da página | só a última geração aceita aplica resultado; intermediárias `discard` (nenhum frame/erro exibido); alocações balanceadas |
| TEST-IMGF-RNAV-06 | setas durante `loading`, inclusive ida-e-volta rápida | consumidas (não viram texto no shell); alvo avança por pedido aceito (D11); resultado final = último pedido; D9: cromo nunca mistura alvo novo com frame/erro antigo |
| TEST-IMGF-RNAV-07 | erro no índice 0, no último índice e em página truncada (`k_max_directory_images + 1` arquivos) | clamp: `navigate` `false` sem nova geração; erro permanece; indicador `1/N`, `N/N`, `N/N+`; sem wrap |
| TEST-IMGF-RNAV-08 | erro com frame instalado na view e com frame ainda pendente no app | cada frame liberado 1× (view e app); `lv_image_set_src(nullptr)` antes; `has_image()==false`; nada pendente |
| TEST-IMGF-RNAV-09 | erro exibido → frame válido → `destroy()` | erro limpo ao instalar o frame; `destroy` libera 1×; `image_src == nullptr`; nenhum descritor aponta para memória liberada |
| TEST-IMGF-RNAV-10 | UI: erro recuperável vs notice inicial | recuperável: sem switch para shell e sem texto no terminal; inicial: abandona 1× com a mensagem; `Esc` destrói a view só depois do switch (contrato preservado) |
| TEST-IMGF-RNAV-11 | `Esc` com erro exibido, `Esc` durante loading e `stop()` | sem erro publicado, sem frame; view destruída; join bounded (`k_worker_join_budget_ms`); estado idle; conclusão tardia descartada |
| TEST-IMGF-RNAV-12 | falha de navegação por open/read/alocação/workspace | todas recuperáveis com reason derivada; página/índice preservados; seta seguinte funciona; `allocations == frees` |
| TEST-IMGF-RNAV-13 | falha de exibição (`create`/`set_image`/erro não aplicável) | abandona ao shell com `image: display unavailable` (baseline), sem foreground zumbi |
| TEST-IMGF-RNAV-14 | erro exibido e digitação de letras/Enter | teclado não vaza para o shell (`image_view_active()` continua verdadeiro); só setas/`Esc` são consumidos; sem eco/execução local |
| TEST-IMGF-RNAV-15 | erro exibido e vários ticks do timer sem novo pedido | o erro persiste até novo resultado ou `Esc` (não é consumido/limpo no tick seguinte); frame continua ausente |

#### Matriz negativa — campo/limite -> `NEG-IMGF-RNAV-*` -> esperado (cada linha executável)

Grupo 1 — origem do pedido e retorno ao shell:

| NEG | campo/limite | entrada | esperado | mutação que deve falhar |
| --- | --- | --- | --- | --- |
| NEG-IMGF-RNAV-01 | `origem` (`by_index`) | `execute` (inicial) com arquivo listado de conteúdo inválido | `initial_error`: mensagem no terminal, sem superfície de viewer, shell recuperado | tratar `by_index=false` como recuperável e manter o app |
| NEG-IMGF-RNAV-02 | caminho/uso inicial | `image` sem argumento, `image x.gif`, traversal, nome > `k_max_name_bytes` | rejeição antes do I/O; `usage:`/`image: invalid path`; volta ao shell | criar viewer e mostrar erro de tela |
| NEG-IMGF-RNAV-03 | `has_directory_` | `navigate()` sem página publicada | `false`, nenhuma geração/job, estado intacto | navegar com página vazia |
| NEG-IMGF-RNAV-04 | alvo repetido/clampado | `navigate` para o mesmo índice nas duas bordas | `false`, sem geração, frame/erro atual inalterado | reenviar job e limpar a tela |

Grupo 2 — frame anterior, stale e posse:

| NEG | campo/limite | entrada | esperado | mutação que deve falhar |
| --- | --- | --- | --- | --- |
| NEG-IMGF-RNAV-05 | frame instalado | erro recuperável com frame na view | `lv_image_set_src(nullptr)` antes de liberar; liberação 1×; `has_image()==false`; nenhum ponteiro pendente | liberar sem repointar (UAF visual) ou vazar |
| NEG-IMGF-RNAV-06 | frame pendente no app | erro chega antes do timer consumir o frame pendente | frame pendente liberado 1×; `take_published == false`; nada exibido | deixar frame órfão |
| NEG-IMGF-RNAV-07 | `generation` (erro antigo) | erro de g chega após g+1 aceita/exibida | `discard`; frame/erro atual intacto; nenhuma mensagem | aplicar erro antigo sobre o novo frame |
| NEG-IMGF-RNAV-08 | `generation` (frame antigo) | frame de g chega após g+1 | `discard` e liberação 1×; nada exibido | publicar frame antigo |
| NEG-IMGF-RNAV-09 | ordem erro→sucesso | erro de g exibido e depois frame de g+1 | erro limpo; frame instalado; cromo novo; nada do erro anterior | manter erro/duplicar cromo |
| NEG-IMGF-RNAV-10 | resultado terminal duplo | frame e erro da mesma geração | exatamente 1 aplicado; o segundo `discard`; nenhuma liberação dupla | aplicar ambos |
| NEG-IMGF-RNAV-11 | foreground | erro recuperável quando o foreground já é o shell | nada exibido, nenhuma mensagem/switch | trocar foreground/imprimir |

Grupo 3 — setas durante loading/rápidas e posse de teclado:

| NEG | campo/limite | entrada | esperado | mutação que deve falhar |
| --- | --- | --- | --- | --- |
| NEG-IMGF-RNAV-12 | slot pendente | 2 pedidos em voo | 1 slot; última geração vence; nenhuma publicação/erro intermediário | fila ilimitada ou resultado fora de ordem |
| NEG-IMGF-RNAV-13 | índice aceito | duas setas rápidas, alvo válido e depois clamp | alvo avança por pedido aceito (D11); resultado final = último; borda sem wrap | calcular pelo último concluído e "pular" um passo |
| NEG-IMGF-RNAV-14 | `classify_image_key` | foreground sem frame e sem erro | setas `pass_through` (baseline TEST-IMG-NAV-09) | consumir seta sem superfície |
| NEG-IMGF-RNAV-15 | `classify_image_key` | erro recuperável exibido | `previous`/`next`/`exit` (`Esc`), não `pass_through` | deixar o teclado cair no shell |
| NEG-IMGF-RNAV-16 | foreground falso | `classify_image_key(false,*,*)` | tudo `pass_through` | ignorar o foreground |

Grupo 4 — `Esc`, cancelamento, `stop()` e stale:

| NEG | campo/limite | entrada | esperado | mutação que deve falhar |
| --- | --- | --- | --- | --- |
| NEG-IMGF-RNAV-17 | `cancel_` durante loading | `Esc` antes da conclusão | `cancelled`; nenhum frame/erro publicado; shell; recursos liberados 1× | publicar erro "cancelled" |
| NEG-IMGF-RNAV-18 | cancelamento ≠ diagnóstico | `cancelled` na navegação | sem canal recuperável (NEG-IMGF-CANCEL-05 preservado) | emitir mensagem/shell |
| NEG-IMGF-RNAV-19 | `stop()` com erro exibido | `stop` | join bounded; view destruída; frame/dir/notice limpos; idle | liberar com worker vivo |
| NEG-IMGF-RNAV-20 | conclusão após `stop` | worker conclui tarde | `discard`; nada pendente; nenhum switch | aplicar em app parado |
| NEG-IMGF-RNAV-21 | `Esc` com erro, sem termo | `Esc` no erro | erro não vai ao terminal (D10); shell limpo | despejar a mensagem no scrollback |

Grupo 5 — alocação, I/O e limites:

| NEG | campo/limite | entrada | esperado | mutação que deve falhar |
| --- | --- | --- | --- | --- |
| NEG-IMGF-RNAV-22 | `allocate` (workspace da página) | recusa na navegação | `recoverable_error` `image: out of memory`; `directory_`/índice preservados; balanceado | tratar como inicial e abandonar |
| NEG-IMGF-RNAV-23 | `allocate` (frame/scratch) | recusa | `recoverable_error` com reason de `allocation_failed`; 0 publicado; `allocations == frees` | publicar frame parcial |
| NEG-IMGF-RNAV-24 | `open` pós-listagem | arquivo removido entre scan e open | `image: cannot open file` recuperável; próxima seta continua | abandonar o viewer |
| NEG-IMGF-RNAV-25 | leitura curta | falha no header/linha/MCU | `image: read failed` recuperável; frame liberado; nada publicado | exibir frame incompleto |
| NEG-IMGF-RNAV-26 | `file_bytes` TOCTOU | arquivo cresce além de `k_max_file_bytes` após a listagem | `EFBIG` → `image: file larger than 5 MiB` recuperável, sem alocar | mensagem errada/abandonar |
| NEG-IMGF-RNAV-27 | dimensão/limite | entrada listada que excede `k_max_dimension` só é alcançável por conteúdo (extensão/size já filtrados) | reason tipada do formato; recuperável; sem frame | aceitar/estimar frame |
| NEG-IMGF-RNAV-28 | display | `create`/`set_image`/erro falha | abandona ao shell com `image: display unavailable` (baseline), sem foreground zumbi | ficar em foreground sem superfície |

Grupo 6 — mensagem, cromo e canais:

| NEG | campo/limite | entrada | esperado | mutação que deve falhar |
| --- | --- | --- | --- | --- |
| NEG-IMGF-RNAV-29 | composição da mensagem | cada status/format | tela = `"image: " + <reason da tabela>` (sem LF no rótulo); nenhum literal paralelo no app/UI | texto próprio divergente |
| NEG-IMGF-RNAV-30 | título/posição do erro | índice i, count N, truncated | título = nome do arquivo que falhou; indicador = `format_position(i,N,truncated)`; rodapé inalterado; nada do item anterior | manter nome/indicador antigos |
| NEG-IMGF-RNAV-31 | limpeza no sucesso | erro exibido → frame válido | erro vazio e cromo atualizado na mesma aplicação; erro não reaparece | rótulo residual |
| NEG-IMGF-RNAV-32 | canal | navegacional vs inicial | navegacional só no chrome do viewer; inicial só no terminal; sem parse de texto | canal único |
| NEG-IMGF-RNAV-33 | aceite a partir de erro | seta aceita com erro exibido | erro limpo imediatamente; indicador do alvo aceito; sem frame | manter o erro antigo como atual |
| NEG-IMGF-RNAV-34 | aceite a partir de frame | seta aceita com frame exibido | até o resultado, frame+índice anteriores permanecem coerentes; nenhum índice novo com frame antigo (D9) | atualizar só o índice |

Grupo 7 — índice, extremos e página:

| NEG | campo/limite | entrada | esperado | mutação que deve falhar |
| --- | --- | --- | --- | --- |
| NEG-IMGF-RNAV-35 | `current_index_` após falha | válida k → inválida k+1 | índice vira k+1; próxima seta = k+2; esquerda = k | manter k (retenta k+1) |
| NEG-IMGF-RNAV-36 | borda 0 | erro no índice 0, esquerda | `false`; erro permanece; sem geração | wrap para N-1 |
| NEG-IMGF-RNAV-37 | borda N-1 | erro no último índice, direita | `false`; erro permanece; sem geração | wrap para 0 |
| NEG-IMGF-RNAV-38 | `truncated` | `k_max_directory_images + 1` arquivos, erro no último da janela | `N/N+`; janela mantém `k_max_directory_images`; nenhuma entrada excedente | perder `+`/adicionar excedente |
| NEG-IMGF-RNAV-39 | pin/página | abrir arquivo além da janela, navegar para vizinho e voltar | página publicada reutilizada (zero `list_images` no `by_index`); pin preservado | rescan e perder o pin |
| NEG-IMGF-RNAV-40 | página vazia/índice inválido | job de navegação com `count == 0`/índice fora da página (defesa) | produção não emite esse job (`navigate` calcula do `count`); se a defesa disparar em navegação, ainda é recuperável e não destrói a página; `format_position(0,0)` mantém `0/0` | transformar a defesa em erro inicial ou abandonar |

Grupo 8 — regressão obrigatória (não remover/afrouxar):

| NEG | campo/limite | entrada | esperado | mutação que deve falhar |
| --- | --- | --- | --- | --- |
| NEG-IMGF-RNAV-41 | decoders (BMP/JPEG/PNG) | todas as negativas já aceitas (SOF1/2/3/9..15, precisão, componentes, EOI/DHT/segmento, 4:1:1 = `NEG-IMGF-JPG-16`; profundidade/interlace/color type/CRC/PLTE/zlib/bomba/filtro; BMP envelope/stride) | mesmos status/reason; nada publicado; `allocations == frees` | regressão silenciosa ao tocar `run_job`/dispatch |
| NEG-IMGF-RNAV-42 | limites | `k_max_file_bytes` (inclusivo/exclusivo), `k_max_dimension` (0/1/limite/limite+1), overflow de produto, offsets | exatamente como hoje (derivados dos headers) | mudar gate para `>=`/`>` |
| NEG-IMGF-RNAV-43 | D1/D2/D3 | última extensão decide (`a.jpg.png`→PNG); bytes após `IEND` ignorados sem interpretar; 0 byte inelegível | inalterados | reintroduzir a mensagem morta/`IDAT` pós-`IEND` |
| NEG-IMGF-RNAV-44 | upload | `upload foto.png` | recusado; `validate_upload_name` continua BMP-only | aceitar PNG/JPEG por upload |
| NEG-IMGF-RNAV-45 | stack/memória | cadeia do worker, frames host/ELF, `k_worker_stack_bytes`, página no heap | dentro do budget; nenhuma página/frame grande na stack; sem novo `xTaskCreate` | mover página para a stack |

#### Cobertura do despacho (todos os tópicos exigidos -> IDs)

| Tópico exigido | Onde está coberto |
| --- | --- |
| erro ao navegar | `TEST-IMGF-RNAV-01`; `NEG-IMGF-RNAV-01,05,29,30` |
| sucesso seguinte | `TEST-IMGF-RNAV-02,03,09`; `NEG-IMGF-RNAV-09,31` |
| frame stale/old | `TEST-IMGF-RNAV-08,09`; `NEG-IMGF-RNAV-05..10,20` |
| posição/index atualiza mesmo ao falhar | `TEST-IMGF-RNAV-03,07`; `NEG-IMGF-RNAV-35..40` |
| setas durante loading/rápidas | `TEST-IMGF-RNAV-05,06,14`; `NEG-IMGF-RNAV-12..16,33,34` |
| Esc/cancel/stop/stale | `TEST-IMGF-RNAV-11`; `NEG-IMGF-RNAV-17..21` |
| falha de alocação/I/O | `TEST-IMGF-RNAV-12`; `NEG-IMGF-RNAV-22..28` |
| mensagem inicial vs navegacional | `TEST-IMGF-RNAV-04,10`; `NEG-IMGF-RNAV-01,02,29,32` |
| clamp/endpoints/truncated (cap+1) | `TEST-IMGF-RNAV-07`; `NEG-IMGF-RNAV-36..40` (cap derivado de `k_max_directory_images`) |
| retorno ao shell só na falha inicial | `TEST-IMGF-RNAV-04,10`; `NEG-IMGF-RNAV-01,02,11,28` |
| persistência do erro até novo resultado/Esc | `TEST-IMGF-RNAV-15`; `NEG-IMGF-RNAV-31,33` |
| regressão decoder | `NEG-IMGF-RNAV-41..45` (reexecução dos alvos focados existentes) |

#### Testes/alvos a criar/estender (sem remover nem afrouxar)

- Novo alvo focado (nome sugerido `test_image_navigation`) para C8/C9/C12: disposições
  origem×ok/cancelado/stale, índice aceito, posição/clamp/truncado e chrome pós-aceite; TU pura
  compartilhada com a produção. Registrar em `.PHONY`, `BINS`, artefato `_bin`, agregado `test` e
  `clean`; ASan/UBSan como nos demais alvos de decode.
- `test_image_view`: **acrescentar** erro recuperável (limpa frame, repõe indicador, limpa no
  sucesso, `destroy` sem duplo free) sem tocar nas asserções existentes.
- `test_image_list`: **preservar** `test_key_ownership` e `test_clamped_steps_and_indicator`;
  acrescentar a leitura de que erro recuperável (superfície existente) consome setas.
- `test_image_viewer_contract.py`: acrescentar (a) canal recuperável consumido sem
  `switch_foreground`/`append_output`, (b) ramo inicial ainda com os dois, (c) `navigate`/`by_index`
  sem `list_images`, (d) mensagem composta de `reason`, (e) alvos novos no agregado; **não**
  enfraquecer as asserções atuais (inclui o padrão exato de `Esc`).
- `test_image_stack_frame_contract.py`: incluir a TU/funções novas na cadeia e derivar o budget de
  `k_worker_stack_bytes`; fail-closed se o ELF estiver velho.
- Docs no mesmo trabalho: `code-map.md` (matriz REQ/AC-IMGF-06..08 e linhas do app/UI),
  READMEs/ARCHITECTURE (comportamento navegacional recuperável), manual F21..F27,
  `tests/manual/serial-bridge-validation.pt-BR.md` se a navegação física precisar de nota.

#### Validação manual no Tab5 (pendente; setas não são injetáveis pela ponte)

| ID | Passo | Evidência esperada |
| --- | --- | --- |
| TEST-IMGF-RNAV-MAN-01 | abrir imagem válida e, com seta física, ir a um arquivo listado inválido (ex.: `.png` com bytes JPEG) | viewer permanece; frame anterior some; erro tipado na tela; título/indicador do índice falho; sem texto no prompt |
| TEST-IMGF-RNAV-MAN-02 | do erro, seta para a próxima imagem válida; depois voltar ao inválido | na válida: frame novo e erro ausente; ao voltar: erro de novo; `i/N` correto nos dois |
| TEST-IMGF-RNAV-MAN-03 | várias setas rápidas cruzando válida/inválida | nenhum frame/erro obsoleto piscando; estado final = último alvo aceito; sem texto no shell |
| TEST-IMGF-RNAV-MAN-04 | `Esc` durante loading de uma imagem `k_max_dimension` × `k_max_dimension` e `Esc` com erro exibido | volta ao shell; prompt responde; sem panic/WDT/reboot; `sys.info` com uptime contínuo |
| TEST-IMGF-RNAV-MAN-05 | `image <path>` do mesmo arquivo inválido (abertura inicial) | mensagem tipada no terminal e volta ao shell, sem viewer — contraste com MAN-01 |
| TEST-IMGF-RNAV-MAN-06 | diretório com `k_max_directory_images + 1` arquivos; erro no último da janela | indicador `N/N+` no erro; direita não faz nada; esquerda recupera |
| TEST-IMGF-RNAV-MAN-07 | monitor serial durante MAN-01..06 | zero panic/WDT; heap/PSRAM sem queda monotônica em 10 ciclos; `screen.dump` da área central após o erro sem pixels do frame anterior |

#### Decisões e premissas (defaults reversíveis; nada bloqueia o início)

- **D9 (loading):** com **frame** exibido, o frame+índice anteriores permanecem coerentes até o
  resultado (atualização atômica); com **erro** exibido, o aceite limpa o erro imediatamente e
  mostra o indicador do alvo aceito, sem frame. Nunca índice novo com frame/erro antigo. Se o
  humano preferir limpar também no aceite a partir de frame, só `NEG-IMGF-RNAV-34` e
  `TEST-IMGF-RNAV-05/06` mudam.
- **D10 (canal):** erro navegacional é **chrome do viewer**, não vai para o terminal/scrollback nem
  para `/control/terminal`; `Esc` descarta. O erro inicial continua indo ao terminal.
- **D11 (índice):** cada pedido de navegação **aceito** atualiza o índice alvo (não só a conclusão),
  o que torna setas rápidas determinísticas e satisfaz "índice atualiza mesmo ao falhar".
- **D12 (layout):** mensagem na área de conteúdo, título = nome falho, indicador = posição do
  índice, rodapé de ajuda inalterado; o widget exato é escolha do coder.
- **D13 (estado):** o erro recuperável é volátil (sem NVS); não há novo estado persistente.
- **D14 (versão):** a mudança é funcional e toca app + UI/base; aplica-se a política do `AGENTS.md`
  (MINOR do firmware e PATCH do app, derivados de `version.txt`/manifesto). Os contratos usam
  formato + piso, nunca a versão exata.

#### Blockers, riscos e limites de prova

- **Nenhum blocker material** para o coder iniciar: objetivo e comportamento estão fechados pelos
  defaults acima. Gates condicionais: se o coder **não** extrair C8 (TU pura compartilhada) ou
  **não** expuser C10 (view com erro), a cobertura comportamental regride para estrutural/manual;
  nesse caso o tester reabre a matriz e o developer decide — não afrouxar asserções.
- **Pontos de código atuais que precisam mudar** (leitura, não defeito novo): (1)
  `application::fail()` limpa `published_`, `has_directory_`, `directory_` e `current_index_` para
  **qualquer** falha e `process_image_status` chama `abandon_image_view()` ao consumir qualquer
  `notice` — sem separar a origem, AC-IMGF-07 é inatingível; o caminho recuperável não pode
  reutilizar `fail()` como está. (2) `image_local_key` passa `s_image_view.exists()` como
  "image_ready"; se o erro recuperável mantém a superfície, as setas continuam consumidas — o teste
  novo fixa essa propriedade e **não** deve ser trocado por `has_image()` (que é falso no erro).
  (3) a view ainda não tem rótulo de erro nem operação de limpeza; C10 é requisito, não sugestão.
- A TU do app continua **não linkável no host** (FreeRTOS/storage). O que é provado no host:
  disposição/chrome puros (C8/C9), posse/limpeza da view (C10), listagem/teclado e clamp
  existentes. O que depende de contrato estrutural + físico: preservar página/índice no erro,
  canais e a ordem no `process_image_status`.
- Setas físicas não são injetáveis pela ponte serial; MAN-01..07 exigem teclado real e um novo
  build/flash autorizado na etapa física (não nesta).
- Nada foi executado nesta entrada; a matriz é especificação. A evidência física histórica do
  binário `9cbc4685…` não prova este delta.

Jev: não utilizado. As escolhas D9..D14 são defaults técnicos reversíveis já delimitados pelo
objetivo; não há empate fechado que mude escopo/risco nem priorização de cobertura a decidir.

`cenarios_prontos: sim` — todos os tópicos exigidos têm `TEST-*`/`NEG-*` com entrada, esperado e
mutação de sensibilidade; nenhum cenário antigo foi removido ou enfraquecido.

- Supersedes or discards: (1) D4 antiga **somente no ramo de navegação** (abertura inicial
  preservada); (2) a metade de navegação de AC-IMGF-02 e de `NEG-IMGF-PUB-02/03` (inicial
  preservado); (3) a leitura de `classify_image_key(true,false,...)` como "qualquer erro" — agora
  vale só para "sem frame e sem erro". Nada mais da matriz de formatos/limites/decoders é revogado.
- Próximo passo: `coder` implementar com C8/C10/C11 e os gates acima; depois `tester` executar os
  alvos focados e o contrato, e por fim a etapa física MAN-01..07 antes do `reviewer`.

### 2026-11-09 16:00 -03:00 - validação independente da navegação recuperável (host): APROVADA, sem defeito bloqueante

- Fact (escopo): validação independente de `REQ/AC-IMGF-06..08` e da matriz `TEST/NEG-IMGF-RNAV-01..45`
  no snapshot do coder. **Nenhuma produção foi alterada**; alterei somente dois arquivos de teste
  para fechar lacunas materiais de asserção (abaixo). Não executei suíte completa, gates globais de
  cobertura/scope guard, build nem flash. `git diff --check` limpo.
- Code state:
  - Snapshot do coder confirmado **antes** de qualquer evidência (as claims da nota do coder eram
    stale até esta confirmação): HEAD `0f4ab3a3dd43`;
    `git diff | sha256sum` = `e9a4070af8d72d3b4457e5ed097fac7e87f18f8dd68a7b885832901085bf3546`;
    `git status --porcelain | sha256sum` = `a90b19b8d9adcf30dd85bab95eb1d722b66ce7449f72a7f86906aba17e5b5af3`;
    `version.txt` = `1.18.0`; manifesto `cyberdeck.image_viewer` = `1.0.3`;
    `build/cyberdeck5.bin` sha256 = `f2d832cb358cfe3b23a2c7990398abd9c315f877f770f289afc01dcdddd755c9`
    (2.790.784 B, 15:52), `strings` contém `1.18.0`; `find components main -newer
    build/cyberdeck5.bin` = 0 arquivos e `build/cyberdeck5.elf` (15:52) mais novo que
    `cyberdeck_image_app.cpp` (15:45) e `cyberdeck_image_navigation.{h,cpp}` (15:36) → o contrato de
    stack não estava em modo stale.
  - Snapshot **depois** das minhas edições de teste: `git diff | sha256sum` =
    `9cf22da07e7f7e6155dac2ec9a40b41ce260b5e3dad6a640c4579559b71eec18`; `status` inalterado
    (`a90b19b8…`); binário inalterado (`f2d832cb…`). A diferença é exclusivamente
    `tests/host/keymap/test_image_view.cpp` e `tests/host/keymap/test_image_viewer_contract.py`
    (acréscimos, nenhuma asserção removida).
- Commands | exit | result (todos no snapshot confirmado; testes recompilados de forma incremental):
  - `make -C tests/host/keymap test_image_navigation` | 0 | `78 checks, 0 failures`
  - `... test_image_view` | 0 | `102 checks, 0 failures` antes; **`108 checks, 0 failures`** depois
    das minhas asserções (ASan+UBSan)
  - `... test_image_list` | 0 | `1247 checks, 0 failures`
  - `... test_image_viewer_contract` | 0 | `0 failures` antes e depois
  - `... test_image_stack_frame_contract` | 0 | `0 failures`; host chain 160 B / budget 6144 B;
    ELF `fail=16B, fail_initial=368B, fail_recoverable=384B, navigate=448B, publish=368B,
    run_job=800B, start=240B, stop=368B, worker_entry=448B, decode_jpeg=288B, decode_png=784B`;
    miniz vendorizado (sem ROM `tinfl_decompress`)
  - `... test_image_decoders_contract` | 0 | `212 checks, 0 failures`
  - `... test_image_decode` | 0 | `55`; `... test_image_bmp` | 0 | `82`;
    `... test_image_formats` | 0 | `51`; `... test_image_capture_orientation` | 0 | `50`;
    `... test_image_worker_state` | 0 | `788` (todos 0 failures)
  - `... test_image_jpeg` | 0 | `408 checks, 0 failures` (ASan+UBSan)
  - `... test_image_png` | 0 | `500 checks, 0 failures` (ASan+UBSan)
  - `... test_shell_help` | 0 | `249 checks, 0 failures`; `... test_shell_help_contract` | 0 | PASS
    (8 mutações rejeitadas)
  - `... test_control_upload_protocol` | 0 | `293 checks`; `... test_control_upload_contract` | 0 | PASS
  - `git diff --check` | 0 | limpo
  - **Não executado** (fora do despacho): suíte completa, gcov/scope guard, build, flash, validação
    física. Nenhuma mudança de produção feita por mim.
- O que foi verificado na implementação (inspeção + execução):
  - **C8/C9/C12 (modelo puro)** `cyberdeck_image_navigation.{h,cpp}`: `completion_disposition_for`
    decide `discard` antes de qualquer outcome quando `!current`/cancelado; `failed` + navegação →
    `recoverable_error`; `failed` + inicial → `initial_error`; `ok` → `publish`. `chrome_after_*`
    produz nome/índice/posição/erro juntos; aceite a partir de erro limpa a mensagem e move o
    indicador sem frame; aceite a partir de frame mantém frame+índice (D9). `test_image_navigation`
    executa a matriz origem×outcome×current, cromo truncado, bordas, setas rápidas e cancelamento.
  - **Origem initial vs navigation / retorno ao shell**: `execute()` cria `job` com `by_index=false`;
    só `navigate()` seta `by_index=true`. `fail()` roteia `fail_recoverable` vs `fail_initial`;
    `fail_initial` limpa página/frame, grava `notice_` **com LF** e estado `error`, e o ramo
    `take_notice` da UI chama `abandon_image_view` (terminal + `switch_foreground`). O ramo recuperável
    não chama nenhum dos dois. Contrato fixa ambos os ramos.
  - **Índice alvo em erros e setas rápidas (D11)**: `navigate()` calcula `step_image_index` sobre a
    página publicada, aceita via `request`, atualiza `current_index_ = next` **no aceite** e devolve
    `navigation_target`; `fail_recoverable` mantém `directory_`/`has_directory_`/`directory_path_` e
    reancora `current_index_` no índice que falhou. `image_navigate` aplica
    `chrome_after_accept` no mesmo evento. Contrato fixa `current_index_ = next`,
    `accepted->index = next`, ausência de `list_images` em `navigate` e preservação da página/caminho
    no erro recuperável.
  - **Cromo recuperável / frame antigo / posse (C9/C10)**: `view::apply_chrome` com mensagem chama
    `drop_frame()` (`lv_image_set_src(nullptr)` antes de `clear_image`) e mantém a superfície; o título
    e o indicador do erro vêm do `viewer_chrome`; `set_image` limpa o erro; `destroy` repointa antes de
    liberar e é idempotente. `test_image_view` cobre erro com frame instalado, reaplicação do erro,
    sucesso seguinte, destroy e (novo) aceite a partir de frame preserva o bitmap.
  - **`Esc`/stop/cancel/stale**: `stop()` seta `cancel_`, faz join com `k_worker_join_budget_ms`, limpa
    frame/página/canal recuperável e só então libera semáforos; `run_job` checa `disposition` antes de
    decodificar e antes de publicar; `decode_status::cancelled` retorna sem diagnóstico;
    `publish`/`fail`/`fail_recoverable` re-checam geração/cancelamento; `request` limpa erro pendente
    no aceite. Contrato fixa geração em `disposition`/`fail_initial` e cancelamento sem diagnóstico;
    `test_image_navigation` executa os descartes.
  - **Canais assíncronos**: `take_published` → `take_recoverable_error` → `take_notice` nessa ordem;
    recuperável exige `image_viewer_foreground()` (senão descarta), não escreve terminal nem troca
    foreground; notice inicial mantém `abandon_image_view`. Ambos os canais são one-shot e mutuamente
    limpos (`request`/`publish`/`fail_initial`/`stop`).
  - **Teclado**: `image_local_key` usa `s_image_view.exists()` (não `has_image()`), logo erro
    recuperável mantém setas/`Esc`; `image_view_active()` mantém o gate de digitação. `test_image_list`
    executa a posse para frame, erro, rejeição inicial e shell; baseline `TEST-IMG-NAV-09` preservado.
  - **Versões/docs/manual**: `version.txt` 1.18.0 (MINOR), app 1.0.3 (PATCH), sem tag; contrato usa
    pisos `(1,18,0)`/`1.0.3`, não versão exata. READMEs/ARCHITECTURE/code-map descrevem o erro
    recuperável; manual tem `F21..F27`; upload segue BMP-only. Não restou menção `1.17.0`/`1.0.2` nos
    documentos verificados.
  - **4:1:1**: continua recusado (`unsupported_sampling`) e coberto em `test_image_jpeg`
    (`NEG-IMGF-JPG-16`), sem reabrir a decisão humana.
  - **Regressão de decoders/limites/upload**: `test_image_jpeg` 408, `test_image_png` 500,
    `test_image_decode` 55, `test_image_bmp` 82, `test_image_formats` 51,
    `test_image_capture_orientation` 50, `test_image_decoders_contract` 212 e upload 293+contrato
    passaram no snapshot; as minhas edições de teste são aditivas e o delta do coder no contrato
    substituiu o gate antigo de stale por uma verificação equivalente mais forte (geração via
    `disposition`/`fail_initial` com `cancel_`), sem remover cobertura.
- Lacunas materiais de teste que eu fechei (somente testes; produção intacta):
  - `tests/host/keymap/test_image_view.cpp` (+6 checks, 102→108): o teste anterior provava
    `error_text()`/`has_error()` mas **não** que o rótulo de erro estivesse visível. Remover
    `lv_obj_set_hidden(error_, false)` sobrevivia. Agora o teste localiza o label pelo texto na
    superfície do shim e exige `hidden == false`; também aplica `chrome_after_accept` a partir de um
    frame instalado e exige que `has_image()`/`image_src`/título/indicador continuem os antigos
    (mutação que limpasse o frame em todo aceite passa a falhar).
  - `tests/host/keymap/test_image_viewer_contract.py` (aditivo): fixa (a)
    `request()` limpa `recoverable_error_`/`has_recoverable_error_` no aceite (mutação que mantém o
    erro pendente sobre o novo alvo deixa de passar); (b) `fail_recoverable` publica
    `has_recoverable_error_ = true`; (c) `fail_recoverable` não limpa `directory_`/`directory_path_`;
    (d) o ramo recuperável da UI tem o guard `image_viewer_foreground()` (NEG-11: erro tardio não
    recria a superfície sobre o shell); (e) aplica `recovered` (não um cromo vazio); (f)
    `image_view_active()` usa `exists()` e não `has_image()` (digitação não vaza durante o erro).
    Nenhuma asserção anterior foi removida/afrouxada; contrato continua `0 failures`.
- Achados não bloqueantes (classificação):
  - **F8 (dívida de documentação, baixa)**: o comentário de `classify_image_key` em
    `components/cyberdeck/include/apps/image/cyberdeck_image_list.h:144-148` ainda diz que o viewer
    "consome tecla só enquanto uma imagem está publicada", o que **não** vale no erro recuperável
    (superfície sem frame, setas consumidas). O código/teste estão corretos; recomendo ao coder
    ajustar o comentário (não bloqueia a revisão).
  - **F9 (risco baixo, caminho degradado)**: `application::request()` limpa o erro recuperável antes
    de `worker_.enqueue()`; se o worker não estiver vivo, `enqueue()` retorna erro e `navigate()`
    retorna `false` com o canal já limpo (a UI mantém o cromo antigo na tela, sem canal para
    atualizá-lo). Só ocorre com worker morto (já degradado) e `Esc` continua funcionando; se o coder
    tocar no método, mover a limpeza para depois do aceite do enqueue. Não reproduzi em host (app TU
    não linkável) e não considero bloqueante.
  - **Limite de prova**: a TU `cyberdeck_image_app.cpp` não é linkável no host; preservação de
    página/índice/caminho, ordem dos três canais no `process_image_status`, gate de digitação e
    limpeza do canal são provados por **modelo puro + view sob shim + contrato estrutural**, não por
    execução do app. Nenhum defeito concreto encontrado nesses pontos na inspeção.
- Limitações/pendências:
  - Validação física `F21..F27`/`TEST-IMGF-RNAV-MAN-01..07` **não executada**: setas não são
    injetáveis pela ponte serial e não houve build/flash nesta etapa; fica para depois da revisão de
    desenvolvimento, antes do aceite final.
  - Suíte completa, gates globais de cobertura/scope guard e fuzz ampliado não executados.
- Supersedes or discards: (1) o estado "evidência do coder stale até confirmação" — snapshot
  `e9a4070a…`/`a90b19b8…` foi confirmado independentemente e é a base desta aprovação; (2) o valor de
  `git diff | sha256sum` passa a `9cf22da0…` por causa das minhas **duas** edições de teste; (3) a
  evidência de `test_image_view` passa de 102 para 108 checks. Nada da matriz aprovada
  (`TEST/NEG-IMGF-RNAV`, D9..D14, 4:1:1, D1..D3, limites, upload) é revogado.
- Classificação final: **APROVADO no escopo host** (sem defeito de implementação bloqueante, sem
  defeito de teste que invalide resultados; lacunas materiais fechadas com testes). `F8`/`F9` são
  dívida/risco baixo; a aprovação **não** cobre hardware nem substitui a revisão final.
- Próximo passo: `reviewer` em modo desenvolvimento (revisão do delta e encaminhamento da suíte/gates
  quando couber); a validação física `F21..F27` segue pendente e deve ocorrer antes do aceite final.

### 2026-11-09 16:05 -03:00 - F8/F9 fechados: contrato verifica enqueue→clear e comentário de posse

- Fact: a evidência anterior deste papel para F8/F9 estava **stale** (snapshot `9cf22da0…`); o
  snapshot atual foi confirmado independentemente antes de qualquer execução, e o coder não
  alterou produção além dos dois pontos de F8/F9.
- Code state (medido): HEAD `0f4ab3a3dd43`;
  `git diff | sha256sum` = `cb017eac4028392852b15af22a3b384164a0a65efcbf9acee58dc3271a79cb01`;
  `git status --porcelain | sha256sum` = `a90b19b8d9adcf30dd85bab95eb1d722b66ce7449f72a7f86906aba17e5b5af3`
  (inalterado); `version.txt` = `1.18.0`; app `cyberdeck.image_viewer` = `1.0.3`;
  `build/cyberdeck5.bin` sha256 = `68556364154dc0eb4d334392007356c653706e58aaf93da12dad5a0f7de50fcf`
  (16:03, 2.790.784 B); `build/cyberdeck5.elf` (16:03) mais novo que
  `cyberdeck_image_app.cpp`/`cyberdeck_image_list.h` (16:02) e
  `cyberdeck_image_navigation.{h,cpp}` (15:36) → `find components main -newer
  build/cyberdeck5.bin` = 0 arquivos, contrato de stack não estava stale.
- F9 (implementação, verificado por inspeção): `application::request()` agora só limpa
  `recoverable_error_`/`has_recoverable_error_` **depois** de `worker_.enqueue()` aceitar. Se o
  handoff rejeita (nenhum worker vivo), retorna `false` com o canal preservado e `state_ = error`;
  `navigate()` então retorna `false` sem mover `current_index_`/target, e a UI mantém o cromo que já
  está na tela (sem canal órfão). No caminho aceito a ordem continua `request_mutex_` →
  `publication_mutex_` → `xSemaphoreGive`, sem inversão.
- F8 (comentário, verificado por inspeção): o comentário de `classify_image_key` em
  `cyberdeck_image_list.h` agora diz que a posse segue a **superfície** existente (imagem publicada
  ou cromo de erro recuperável) e que, sem superfície (rejeição inicial), o shell mantém o teclado;
  `Esc` continua consumido. Isso corresponde exatamente à função (`foreground` → `escape` → `exit`;
  `!image_ready` → `pass_through`).
- Contrato cobrindo os dois (adicionado pelo coder, revisado por mim — aditivo, sem afrouxar):
  `request_body.count("recoverable_error_ = viewer_chrome{};") == 1`;
  `enqueue_position < clear_position`; o trecho entre `worker_.enqueue()` e o clear contém
  `return false;` e **não** contém o clear (rejeição preserva o canal). Minhas asserções anteriores
  (clear no aceite, `fail_recoverable` publica canal e preserva `directory_`/`directory_path_`, guard
  `image_viewer_foreground()`, cromo `recovered` aplicado, gate de digitação em `exists()`) continuam
  presentes e passando. Não vi necessidade de novo teste: o gap material de F9 já está fechado pelo
  contrato e o de F8 é documental (sem comportamento a executar).
- Commands | exit | result (snapshot `cb017eac…`, executados 16:04–16:05):
  - `make -C tests/host/keymap test_image_viewer_contract` | 0 | `0 failures` (inclui as asserções
    de F9 e as minhas)
  - `... test_image_navigation` | 0 | `78 checks, 0 failures`
  - `... test_image_view` | 0 | `108 checks, 0 failures` (ASan+UBSan)
  - `... test_image_list` | 0 | `1247 checks, 0 failures`
  - `... test_image_stack_frame_contract` | 0 | `0 failures`; host chain 160 B/6144 B; ELF
    `fail=16B, fail_initial=368B, fail_recoverable=384B, navigate=448B, publish=368B, run_job=800B,
    start=240B, stop=368B, worker_entry=448B, decode_jpeg=288B, decode_png=784B`; miniz vendorizado
  - `git diff --check` | 0 | limpo
  - Não executado: suíte completa, gates globais, build e flash; nenhuma edição de produção ou teste
    feita por mim nesta rodada. Os únicos artefatos tocados foram binários/gcda de teste (ignorados).
- Snapshot após a rodada: inalterado em relação ao confirmado (`git diff` = `cb017eac…`, status =
  `a90b19b8…`, binário = `68556364…`); a nota é untracked e não altera esses hashes.
- Classificação: **F8 e F9 FECHADOS** (F8 por comentário correto; F9 por código + contrato
  determinístico). Nenhum defeito novo, nenhuma lacuna material remanescente no escopo host.
  A aprovação independente do host da entrada anterior permanece válida; a validação física
  `F21..F27`/MAN-01..07 continua pendente e fora desta rodada.
- Supersedes or discards: substitui os itens abertos F8/F9 da entrada anterior e o snapshot
  `9cf22da0…` como referência de evidência. Nada da matriz (`TEST/NEG-IMGF-RNAV`, D9..D14, 4:1:1,
  D1..D3, limites, upload) é revogado.
- Próximo passo: `reviewer` (revisão de desenvolvimento); depois a etapa física autorizada antes do
  aceite final.

### 2026-11-09 16:16 -03:00 - etapa física (Tab5 1.18.0): flash + g.png inicial + f128; setas humanas pendentes

- `evidence_kind: medido` (dispositivo real). Escopo: setup físico autorizado (`idf.py -p
  /dev/ttyACM0 app-flash`), confirmação de `esp_app_desc`/SHA-256, boot/safe_mode/uptime, abertura
  inicial inválida `g.png` e abertura válida `f128.png`. **Nenhuma seta foi simulada e nenhum texto
  foi injetado depois de o viewer abrir**. Não finalizei revisão ampla; `MAN-01..07` continua
  incompleta.
- Snapshot de produção confirmado antes do flash (o mesmo da validação host):
  HEAD `0f4ab3a3dd43`;
  `git diff | sha256sum` = `cb017eac4028392852b15af22a3b384164a0a65efcbf9acee58dc3271a79cb01`;
  `git status --porcelain | sha256sum` = `a90b19b8d9adcf30dd85bab95eb1d722b66ce7449f72a7f86906aba17e5b5af3`;
  `version.txt` = `1.18.0`; manifesto image = `1.0.3`;
  `build/cyberdeck5.bin` sha256 = `68556364154dc0eb4d334392007356c653706e58aaf93da12dad5a0f7de50fcf`
  (2.790.768 B); `esp_app_desc` lido do bin: projeto `cyberdeck5`, versão `1.18.0`, built
  `Nov 9 2026 15:52:23`; `find components main -newer build/cyberdeck5.bin` = 0.
- Flash/boot:
  - Antes: `ping` ok; `sys.info` = fw **1.17.0**, uptime `01:03:34`, `safe_mode=false`,
    `interrupted_boots=0`; `/dev/ttyACM0` presente e usuário em `uucp`; nenhum monitor concorrente.
  - `source ~/esp/esp-idf/export.sh && idf.py -p /dev/ttyACM0 app-flash` | **exit 0** | "Hash of
    data verified", hard reset. `flasher_args` confirma app em `0x20000`.
  - Read-back independente: `esptool read-flash 0x20000 2790768` → sha256
    **`68556364154dc0eb4d334392007356c653706e58aaf93da12dad5a0f7de50fcf`**, idêntico ao bin
    (confirma que o slot `factory` recebeu exatamente o binário validado; o offset `0xe3000` que
    aparece no resumo do esptool é apenas o último bloco escrito).
  - Pós-boot: `sys.info` = fw **1.18.0**, `safe_mode=false`, `interrupted_boots=0`, uptime 6 s; depois
    `00:00:19`, `00:00:50`, `00:03:03`, `00:04:24` monotônico — **sem panic/WDT/reboot**.
    `ui.dump` mostrou header `CYBERDECK5 1.18.0`.
- Fixtures (somente leitura; nenhum `fs.write`/delete): `screen.status` off=false,
  `ui.type "ls /cd5t-1435"` + `term.dump` listaram os 13 arquivos conhecidos
  (`a.bmp b.jpg c.jpeg d.jpg e.png f.png f128.png g.png h.png i.jpg j.png o.bmp p.png`), incluindo
  `f128.png` e `g.png`. Diretório e arquivos preservados.
- Abertura inicial inválida (AC-IMGF-08):
  - `ui.type "image /cd5t-1435/g.png"` → `term.dump` =
    `/$ image /cd5t-1435/g.png` seguido de **`image: invalid PNG signature`** e novo prompt `/$ `;
    app voltou ao shell; `sys.info` com uptime contínuo (sem reboot).
- Abertura válida e preparação (REQ-IMGF-06/AC-IMGF-09):
  - `ui.type "image /cd5t-1435/f128.png"` → `ui.dump` mostrou viewer com título **`f128.png`**,
    indicador **`7/13`**, rodapé `left/right browse  Esc close` e widget de imagem **128x128** em
    `(576,322)`. Isso confirma f128 na janela esperada.
  - **Desvio de estado observado (não causado por comandos desta sessão)**: segundos depois, sem
    nenhum `ui.type`/seta enviada por mim, o viewer já estava em **`p.png`, `13/13`**, com widget
    **640x480** em `(320,146)`; `ui.dump`/captura atuais confirmam. O uptime permaneceu monotônico e
    o event log SD não mostra `event=stop`/novo `start` do `cyberdeck.image_viewer` entre f128 e
    p.png — ou seja, houve **navegação dentro do app**, cujo único caminho é `LV_KEY_RIGHT` físico
    (`image_navigate`). A explicação consistente é **setas físicas pressionadas externamente**
    (provável usuário no dispositivo), não um comando da ponte; não consigo provar a autoria sem o
    usuário. Consequência inferida (não capturada): a travessia 7→13 cruzou `g.png` (8, assinatura),
    `h.png` (9, transparência), `i.jpg` (10, 4:1:1) e `j.png` (11, dimensões), recuperando em
    `o.bmp` (12) e p.png (13), **com o viewer ainda aberto** — indício de que o erro recuperável não
    devolveu o shell, mas **sem captura do estado de erro**.
  - Capturas: `/tmp/opencode/f128-1.18.bmp` (na verdade já mostra p.png; nome enganoso) e
    `/tmp/opencode/p-13of13-1.18.bmp` (p.png 13/13). A última, lida conforme a spec BMP (B,G,R e
    `biHeight>0` bottom-up), tem vermelho em y146..385 e azul em y386..625, 640x480 em `(320,146)` —
    corresponde a `p.png` em escala 1:1, **sem frame stale de f128**.
- Estado atual do dispositivo (deixado assim; sem teclas): **viewer em `p.png`, 13/13**, superfície
  visível, aviso/erro ausente, tela acordada (o timeout de 1 min pode apagá-la; `screen.wake` é a
  única ação permitida antes de capturar). Fixtures em `/cd5t-1435` intactas.
- Pendência física (bloqueia declarar `MAN-01..07` completa): faltam **setas humanas** e `Esc`
  físico. Instrução exata para o usuário/developer (não automatizar; não digitar no viewer):
  1. A partir de `p.png` **13/13**, pressione **LEFT uma vez por vez, devagar, seis vezes**:
     `12/13 o.bmp` (imagem válida), `11/13 j.png` (erro de dimensões), `10/13 i.jpg` (erro 4:1:1),
     `9/13 h.png` (erro de transparência), **`8/13 g.png` (erro de assinatura: título `g.png`,
     indicador `8/13`, mensagem `image: invalid PNG signature` na área de conteúdo, imagem anterior
     ausente, nada no prompt)**, `7/13 f128.png` (imagem volta, mensagem some). Em cada erro o
     viewer deve permanecer e as setas devem continuar; no 8 não deve haver texto no shell.
  2. Estando em `7/13 f128.png` (estado preparado), pressione **RIGHT uma vez** → deve cair em
     `8/13 g.png` com o erro acima. Pressione **RIGHT** novamente e mais vezes: `9/13`, `10/13`,
     `11/13` continuam em erro tipado e `12/13 o.bmp` deve **recuperar a imagem**, limpando a
     mensagem; `LEFT` volta ao erro anterior; `Esc` fecha o viewer e devolve o shell. Teste de
     setas rápidas: se fizer manualmente, registrar se aparece frame/erro obsoleto; não é
     obrigatório nesta rodada.
  3. Quando estiver em **`8/13` (erro)**, avisar o developer para rodar (sem enviar teclas)
     `screen.wake` se necessário, `ui.dump`, `term.dump`, `screen.dump --out <arquivo>` e `sys.info`
     — é a evidência que falta para o erro recuperável.
- Commands | exit | result (resumo): `ping` 0; `sys.info` pré 0 (1.17.0); `idf.py app-flash` 0
  (hash verified); `esptool read-flash` 0 (sha idêntico); `sys.info` pós 0 (1.18.0, uptime
  monotônico); `screen.status`/`screen.wake` 0; `ui.type ls` + `term.dump` 0 (13 fixtures);
  `ui.type g.png` + `term.dump` 0 (`invalid PNG signature` + prompt); `ui.type f128.png` + `ui.dump`
  0 (`f128.png`, `7/13`, 128x128); `ui.dump` posterior 0 (`p.png`, `13/13`, 640x480); `screen.dump`
  0. Sem suíte/build adicionais; sem `fs.write`, sem delete, sem setas/Esc simulados.
- Classificação: **flash e verificação inicial OK; MAN física ainda INCOMPLETA**. O desvio
  f128→p.png impediu deixar o aparelho exatamente em f128 e impediu capturar o erro recuperável; não
  declaro a etapa física concluída. Nenhum panic/reboot/mismatch de binário. A aprovação host
  permanece; a revisão final ampla do reviewer continua **não** autorizada por esta entrada.
- Supersedes or discards: substitui o estado "físico pendente" apenas no que foi medido (flash,
  abertura inicial inválida, f128 7/13, p.png 13/13). Não revoga a matriz host nem F8/F9; não marca
  MAN como completo.
- Próximo passo: `developer` repassar a instrução de setas ao usuário e, quando o erro estiver na
  tela, acionar o `tester` para capturar (sem teclas); depois `reviewer` em modo final.

### 2026-11-09 16:20 -03:00 - relato humano: navegação com imagem inválida aceita (`evidence_kind: relato`)

- `evidence_kind: relato` — **não medido**. Hora local estimada; sem comando de relógio nesta
  entrada ("sem comandos"). Origem: mensagem do usuário recebida agora.
- Relato do usuário: ele declarou que **fez a validação física**, **conseguiu navegar entre as
  imagens, inclusive as inválidas**, **recebeu a mensagem de retorno** e concluiu **"Está tudo
  correto."** Ele **aceitou** o comportamento observado.
- AC manual relacionada: navegação com imagem inválida (REQ-IMGF-06 / AC-IMGF-07/AC-IMGF-09;
  `TEST-IMGF-RNAV-MAN-01/02` e o gate humano correspondente). O relato satisfaz **o gate humano
  aplicável a esse comportamento**: ao navegar por imagens inválidas o viewer não devolveu o shell
  e houve retorno/mensagem, com aceite explícito do usuário.
- Não atribuo firmware, versão ou SHA ao relato: o usuário não reportou SHA, versão nem arquivo
  específico. As medições anteriores no Tab5 com o firmware 1.18.0 (flash/read-back `68556364…`,
  boot, `g.png` inicial inválido, `f128.png` 7/13, `p.png` 13/13) permanecem **separadas**, como
  `medido` da entrada anterior; esta entrada não as revalida, não as converte em relato e não
  transfere o SHA para a fala do usuário.
- Cenários MAN secundários **não cobertos / não executados** por este relato (sem marcar falha):
  captura do cromo de erro em `8/13` (título/indicador/mensagem na tela), mensagem exata por
  arquivo inválido, setas rápidas, clamp em `1/N` e `N/N+`, `Esc` físico fechando o viewer,
  recuperação explícita até `o.bmp` 12/13, imagem 1280×1280/tempo de decode, ciclos de
  heap/uptime e monitor serial. Permanecem não executados; não há evidência de defeito neles.
- Não declaro a tarefa completa nem a etapa física item-a-item concluída; registro apenas que o
  gate humano de aceite da navegação com imagem inválida foi satisfeito pelo relato.
- Supersedes or discards: substitui a pendência de setas humanas **somente no que o relato cobre**
  (aceite humano da navegação, inclusive por imagens inválidas); não revoga as medições anteriores
  nem a matriz host. `MAN-01..07` **não** passa a "completa"; `Esc` e os demais cenários
  secundários continuam pendentes.
- Próximo passo: `reviewer` em modo final (revisão ampla), mantendo os cenários secundários
  registrados como não executados.
