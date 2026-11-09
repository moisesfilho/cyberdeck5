# Validação manual do visualizador de imagens (REQ/AC-IMG-01..05, REQ-IMGF-01..05)

Recorte aprovado: aplicativo foreground isolado, arquivo de até 5 MiB, 1..1280 px
por eixo, até 64 imagens do mesmo diretório, navegação por setas com indicador
`i/N`, proporção preservada e `Esc` de volta ao shell. A extensão escolhe o
decoder e é autoritativa: BMP 24-bit `BI_RGB` (inalterado), JPEG baseline `SOF0`
8-bit grayscale/YCbCr com amostragem 4:4:4, 4:2:2 ou 4:2:0, e PNG 8-bit sem
interlace tipos 0/2/3. Ficam **fora** do escopo: JPEG progressivo/aritmético/
lossless/12-bit/CMYK/**4:1:1**, PNG com alfa (`tRNS`, tipos 4/6), PNG 16-bit ou
entrelaçado, zoom, varredura recursiva e upload de JPEG/PNG (`POST
/control/upload` continua BMP-only). EXIF/ICC não são aplicados.

Os testes host (`test_image_bmp`, `test_image_fit`, `test_image_formats`,
`test_image_jpeg`, `test_image_png`, `test_image_decode`, `test_image_list`,
`test_image_view`, `test_image_decoders_contract`, `test_image_viewer_contract`)
validam a camada pura, os decoders e a costura LVGL sob o shim. Eles **não**
substituem os itens abaixo, que exigem o Tab5 em execução; a seção F cobre o
recorte multiformato.

Para uma fixture em resolução completa (1280x720x24 = 2.764.854 B), que não
cabe no limite de 2048 bytes do `fs.write`, use o transporte autenticado da LAN:
`python3 tools/cyberdeck_http_cli.py --host <IP> upload fixture.bmp` publica em
`/sdcard/screenshots` (visível como `/screenshots/fixture.bmp`) e imprime o
comando `image` correspondente. Cenários e limites do transporte estão em
`tests/manual/http-control-validation.pt-BR.md` seção 6; a validação do viewer
continua sendo a deste roteiro.

## Preparação

1. Grave o firmware com o binário compilado (`idf.py -p /dev/ttyACM0 flash`) e
   registre `sys.info` (versão, heap, uptime) antes do roteiro.
2. Copie arquivos de teste para o cartão SD, por exemplo em `/fotos/`:
   - `a.bmp` 640x480 (paisagem, cabe inteira);
   - `b.bmp` 480x640 (retrato);
   - `c.bmp` 1280x1280 (caso máximo);
   - `d.bmp` 2000x100 (fora do limite por eixo → deve falhar);
   - `e.bmp` 32x32 com cor sólida (não deve ser ampliada);
   - `f.bmp` de 6 MiB (acima do teto → deve falhar);
   - `g.gif`/`h.webp` (extensão não reconhecida → deve falhar);
   - `i.png` (PNG truecolor válido), `j.jpg` (JPEG 4:4:4), `k.jpeg` (4:2:0),
     `l.png` (paleta) e `m.jpg` (grayscale) — ver seção F;
   - `n.png` com bytes de JPEG e `o.jpg` com bytes de PNG (conteúdo divergente
     da extensão → deve falhar com motivo tipado);
   - `p.png` RGBA e `q.jpg` progressivo (variantes recusadas → devem falhar);
   - um arquivo com 5 MiB exatos e geometria válida (limite inclusivo);
   - `sub/` com um BMP dentro (não deve entrar na listagem).
   Use `fs.write` da ponte serial (≤2048 B), o cartão no PC ou
   `tools/cyberdeck_http_cli.py upload` (até 3 MiB, só basename `.bmp`) para
   `/screenshots/`; registre tamanho e SHA-256
   de cada arquivo.
3. Feche o `idf.py monitor` ou mantenha-o apenas como observador de logs;
   comandos de UI podem ser enviados pela ponte serial
   (`tools/cyberdeck_cli.py`) ou pela LAN (`tools/cyberdeck_http_cli.py type`).

## A. Abertura e limites (AC-IMG-01)

| Passo | Ação | Resultado esperado |
| --- | --- | --- |
| A1 | `image /fotos/a.bmp` | imagem exibida inteira, barra de título com nome e `i/N`, sem sobrepor o header de 42 px |
| A1b | `image /screenshots/print_20261020_172101.bmp` (BMP existente que ordena **depois** dos 64 primeiros nomes do diretório) | abre normalmente: a janela vira "63 menores + este arquivo", indicador `64/64+`, seta esquerda navega para o 63º; sem panic |
| A2 | `image fotos/a.bmp` | mensagem de caminho inválido; nenhuma imagem; prompt do shell ativo |
| A3 | `image /sdcard/fotos/a.bmp` | caminho inválido (namespace virtual não aceita `/sdcard`) |
| A4 | `image /fotos/../fotos/a.bmp` | caminho inválido |
| A5 | `image /fotos/d.bmp` | falha explícita por dimensão; volta ao shell |
| A6 | `image /fotos/f.bmp` | falha explícita por tamanho (> 5 MiB); volta ao shell |
| A7 | `image /fotos/g.gif` | falha explícita (extensão não reconhecida); volta ao shell; um `.png` válido abre (seção F) |
| A8 | arquivo de 5 MiB exato | abre normalmente |
| A9 | `image /fotos/sumiu.bmp` (remover depois de listar) | erro explícito, sem indicador obsoleto |
| A10 | arquivo de 0 byte `empty.bmp`/`empty.png` numa pasta com outras imagens, depois `image /fotos/empty.png` | o vazio **não** aparece na listagem nem é promovido pelo pin; a abertura explícita responde `image: not found in directory` (o seam de storage rejeita `st_size <= 0`); numa pasta só com o vazio a resposta é `image: no supported image in directory`. Não existe mensagem dedicada a arquivo vazio |

## B. Exibição e proporção (AC-IMG-02)

| Passo | Ação | Resultado esperado |
| --- | --- | --- |
| B1 | abrir `b.bmp` (retrato) | imagem completa, sem corte e sem distorção |
| B2 | girar o dispositivo (retrato ↔ paisagem) | a imagem é reencaixada e continua inteira; sem artefato |
| B3 | abrir `c.bmp` (1280x1280) | imagem reduzida proporcionalmente, sem corte |
| B4 | abrir `e.bmp` (32x32) | mantém 32x32 px (sem ampliação) |
| B5 | observar as bordas da imagem | sem moldura, fundo ou sombra do tema sobre o bitmap |

## C. Navegação (AC-IMG-03)

As setas **não** são injetáveis pela ponte serial: use o teclado físico (ou o
toque nos botões, se presentes) e confirme o efeito com `screen.dump`
(preferencial) ou `ui.dump`.

| Passo | Ação | Resultado esperado |
| --- | --- | --- |
| C1 | com `a.bmp` aberto, seta direita | próxima imagem em ordem de nome; indicador `2/N` |
| C2 | seta direita até a última | para na última (`N/N`), sem voltar para a primeira |
| C3 | seta esquerda na primeira | permanece na primeira (`1/N`) |
| C4 | diretório com 65+ BMPs (abertura ordinária) | indicador `i/64+` e apenas as 64 primeiras; o `+` indica que há arquivos fora da **janela**, não a posição no diretório |
| C4b | abrir explicitamente um BMP fora dos 64 primeiros e navegar com setas | o pedido aparece como `64/64+`; esquerda vai para `63/64+`; direita a partir dele **não** dá wrap; a abertura seguinte feita por navegação volta a mostrar a janela ordinária |
| C5 | diretório com subpasta e PNG | apenas `.bmp` do diretório aparecem |
| C6 | repetir `image /fotos/a.bmp` | mesma ordem e mesmo índice |

## D. Ciclo de vida e isolamento (AC-IMG-04)

| Passo | Ação | Resultado esperado |
| --- | --- | --- |
| D1 | `Esc` com imagem aberta | volta ao shell; prompt `/$ ` responde |
| D2 | abrir/fechar 10 vezes | `sys.info` sem queda de heap monotônica; uptime contínuo |
| D3 | abrir A, seta para B, seta de volta para A | conteúdo correto em cada troca |
| D4 | seta e `Esc` imediatamente | nenhuma imagem tardia aparece no shell |
| D5 | `music` tocando e `image /fotos/a.bmp` | música para; visualizador abre |
| D6 | `Esc`, depois `music` | player reabre normalmente |
| D7 | tentar falha (A5/A6/A7) e digitar em seguida | o texto vai para o shell, não para um foreground zumbi |
| D8 | monitor serial durante todo o roteiro | sem panic, WDT ou reboot |

## E. Manifesto, registro e help (AC-IMG-05)

| Passo | Ação | Resultado esperado |
| --- | --- | --- |
| E1 | `app list` | 21 aplicativos, incluindo `cyberdeck.image_viewer` |
| E2 | `app info cyberdeck.image_viewer` | `type: foreground`, recursos `display input storage`, comando `image` |
| E3 | `help` | exatamente uma linha `image [app]`, na ordem de registro |
| E4 | `image help` | bloco `image [app]`, identidade/versão (`1.0.3` ou superior), `usage: image <path>` e linha de parâmetros indentada citando `.bmp/.jpg/.jpeg/.png` (`help image` NÃO serve: responde `usage: help`) |
| E5 | `app start cyberdeck.image_viewer` | não inicia em segundo plano; comportamento de foreground |
| E6 | `image` sem argumento | `usage: image <path>`; volta ao shell |

## F. Multiformato BMP/JPEG/PNG (REQ-IMGF-01..05)

Pré-requisito: firmware da `1.18.0` ou superior e as fixtures da seção
"Preparação". Amostre as cores com `screen.dump` (BMP 24-bit) e confira o valor
RGB565 esperado; para JPEG use tolerância (±16 por canal nos centros de
quadrante chapado).

| Passo | Ação | Resultado esperado |
| --- | --- | --- |
| F1 | `image /fotos/i.png` | abre; `screen.dump` mostra o padrão de cores da fixture, sem corte nem inversão de linha |
| F2 | `image /fotos/j.jpg` | abre 4:4:4; título e `i/N` coerentes; cores próximas das originais |
| F3 | `image /fotos/k.jpeg` | abre 4:2:0; sem "shear" horizontal/vertical nas bordas de quadrante |
| F4 | `image /fotos/l.png` | paleta mapeada corretamente (4 cores exatas) |
| F4b | PNG paleta 128x128 com 4 faixas horizontais (~200 B, tipo 3) | decodifica inteiro; faixas na ordem do arquivo (topo = primeira cor), sem `PNG decode failed`; regressão host: `test_image_png::test_palette_128_bands` |
| F5 | `image /fotos/m.jpg` | grayscale: R=G=B em toda a imagem |
| F6 | `image /fotos/n.png` (bytes JPEG) | falha tipada (`invalid PNG signature`) e volta ao shell |
| F7 | `image /fotos/o.jpg` (bytes PNG) | falha tipada (`invalid SOI marker`) e volta ao shell |
| F8 | `image /fotos/p.png` (RGBA) | falha tipada (`unsupported PNG transparency`), sem frame parcial |
| F9 | `image /fotos/q.jpg` (progressivo) | falha tipada (`unsupported JPEG variant`) |
| F10 | fixture JPEG com `Orientation = 6` | **não** é rotacionada (pixels na ordem crua do arquivo) |
| F11 | JPEG/PNG 1280x1280 | decodifica inteiro; registre o tempo até o frame aparecer |
| F12 | navegar `bmp → jpg → png → bmp` com as setas | cada passo decodifica o formato da extensão; o frame anterior é liberado uma vez; `i/N` correto |
| F13 | 10 ciclos abrir/fechar por formato | `sys.info` antes/depois sem queda monotônica de heap; uptime contínuo |
| F14 | `Esc` durante decode de 1280x1280 | volta ao shell, prompt responde, sem panic/WDT/reboot |
| F15 | `image /fotos/x.png` 1281 px de largura | falha tipada (`invalid PNG dimensions`), volta ao shell |
| F16 | arquivo com 5 MiB + 1 byte | falha com `file larger than 5 MiB` antes de qualquer frame |
| F17 | nome `a.jpg.png` com conteúdo PNG | abre como PNG (a última extensão decide o formato) |
| F18 | `image` sem argumento, `image help`, `app info cyberdeck.image_viewer` | uso/help/manifesto citam `.bmp/.jpg/.jpeg/.png` e a versão `1.0.3` |
| F19 | `python3 tools/cyberdeck_http_cli.py --host <IP> upload foto.png` | recusado (upload permanece BMP-only) |
| F20 | monitor serial durante F1..F19 | zero panic/WDT; `sdkconfig` sem wrapper LVGL de decoder ligado |
| F21 | abra uma imagem válida e use a seta até um arquivo listado cujo conteúdo é inválido (ex.: `.png` com bytes JPEG) | o viewer **permanece**; o frame anterior some; o erro tipado (`image: invalid PNG signature`) aparece na área de conteúdo; título = arquivo que falhou; indicador = posição desse índice; nada é escrito no prompt `/$` |
| F22 | a partir do erro, avance para uma imagem válida e depois volte ao inválido | na válida: frame novo, erro ausente e `i/N` correto; ao voltar: o erro aparece de novo com o indicador do índice, sem resíduo do frame anterior |
| F23 | várias setas rápidas cruzando imagens válidas e inválidas | nenhum frame/erro obsoleto pisca; o estado final corresponde ao último alvo aceito; nenhum texto vaza para o shell |
| F24 | `Esc` durante o decode de uma imagem 1280x1280 e `Esc` com erro exibido | volta ao shell; o prompt responde; `sys.info` com uptime contínuo, sem panic/WDT/reboot |
| F25 | `image <caminho>` do **mesmo** arquivo inválido (abertura inicial) | mensagem tipada no terminal e volta ao shell, **sem** viewer — contraste com F21 |
| F26 | diretório com `k_max_directory_images + 1` arquivos; erro no último da janela | indicador `N/N+` no erro; a seta direita não faz nada (clamp) e a esquerda recupera para um índice anterior |
| F27 | monitor serial durante F21..F26 | zero panic/WDT; heap/PSRAM sem queda monotônica em 10 ciclos; `screen.dump` da área central após o erro não contém pixels do frame anterior |

## Evidência a registrar

- build (commit/`version.txt`), SHA-256 do `.bin` gravado e slot de OTA;
- `sys.info` antes e depois (uptime, heap livre);
- comandos enviados, respostas e trechos relevantes do monitor;
- para os itens visuais, arquivos `screen.dump` em `/tmp` com o nome do cenário;
- **leia as capturas conforme a especificação BMP**: `biHeight > 0` significa
  linhas gravadas de baixo para cima e cada pixel é o triplo **B,G,R** (azul,
  verde, vermelho). Ler os bytes como `R,G,B` troca vermelho por azul e pode
  parecer inversão vertical quando a fixture tem vermelho em um extremo e azul no
  outro; compare sempre com o frame do host (`test_image_capture_orientation`
  fixa esse invariante);
- qualquer desvio classificado como defeito de implementação, defeito de teste
  ou limitação de ambiente — sem remover cenários.
