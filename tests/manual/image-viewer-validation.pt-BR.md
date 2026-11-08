# Validação manual do visualizador de imagens (REQ/AC-IMG-01..05)

Recorte aprovado: aplicativo foreground isolado, BMP 24-bit `BI_RGB` sem
compressão nem transparência, arquivo de até 5 MiB, 1..1280 px por eixo, até 64
imagens do mesmo diretório, navegação por setas com indicador `i/N`, proporção
preservada e `Esc` de volta ao shell. PNG/JPEG, zoom e varredura recursiva
ficam **fora** do escopo.

Os testes host (`test_image_bmp`, `test_image_fit`, `test_image_decode`,
`test_image_list`, `test_image_view`, `test_image_viewer_contract`) validam a
camada pura e a costura LVGL sob o shim. Eles **não** substituem os itens
abaixo, que exigem o Tab5 em execução.

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
   - `g.png` (formato fora do escopo → deve falhar);
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
| A7 | `image /fotos/g.png` | falha explícita (não é `.bmp`); volta ao shell |
| A8 | arquivo de 5 MiB exato | abre normalmente |
| A9 | `image /fotos/sumiu.bmp` (remover depois de listar) | erro explícito, sem indicador obsoleto |

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
| E4 | `image help` | bloco `image [app]`, identidade/versão, `usage: image <path>` e linha de parâmetros indentada (`help image` NÃO serve: responde `usage: help`) |
| E5 | `app start cyberdeck.image_viewer` | não inicia em segundo plano; comportamento de foreground |
| E6 | `image` sem argumento | `usage: image <path>`; volta ao shell |

## Evidência a registrar

- build (commit/`version.txt`), SHA-256 do `.bin` gravado e slot de OTA;
- `sys.info` antes e depois (uptime, heap livre);
- comandos enviados, respostas e trechos relevantes do monitor;
- para os itens visuais, arquivos `screen.dump` em `/tmp` com o nome do cenário;
- qualquer desvio classificado como defeito de implementação, defeito de teste
  ou limitação de ambiente — sem remover cenários.
