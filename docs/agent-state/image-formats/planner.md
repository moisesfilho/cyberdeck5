# planner - image-formats

- Role: planner
- Updated: 2026-11-08
- Code state: estado de leitura do worktree; HEAD/diff não aferidos (execução de comandos proibida ao planner)

## Dispatch objective

Planejar, sem implementar, a expansão do visualizador `image` de BMP para JPEG e PNG com limites de memória, segurança, rastreabilidade e aceite físico; entregar proposta ao developer para aprovação explícita do usuário.

## Work done

Plano proposto na resposta ao developer: preservar REQ/AC-IMG-01..05 como baseline, criar REQ/AC-IMGF-01..04 e TEST-IMGF-* para despacho multiformato, rejeição segura, orçamento de memória e comportamento visual. Risco **high** pelo parsing de mídia não confiável, PSRAM e histórico de overflow de stack; matriz negativa de campos/limites antes da implementação e validação física antes da revisão final.

## Evidence

- Somente leituras, sem execução de testes/build/flash. `code-map.md:2562-2608` aponta o baseline; fonte atual consultada: `components/cyberdeck/{include,src}/apps/image/` (`bmp`, `decode`, `list`, `app`, `types`), `src/platform/display/cyberdeck_image_view.cpp`, `CMakeLists.txt`, `main/idf_component.yml`, `sdkconfig`, `tests/host/keymap/Makefile` e `tests/manual/image-viewer-validation.pt-BR.md`.
- `sdkconfig` desabilita os decoders LVGL LodePNG/libpng/TJPGD; há TJpgDec no LVGL local, com callback de stream/MCU (`tjpgd.h`) e saída RGB888 (`tjpgdcnf.h`); o driver JPEG do IDF exige bitstream inteiro e saída com dimensões eventualmente arredondadas (`jpeg_decode.h`), portanto não é equivalente a streaming bounded. Viabilidade de biblioteca PNG incremental/linha com alocador sob controle **não comprovada** pelas fontes consultadas.
- `docs/agent-state/image-viewer.md` registra aceite BMP e upload exclusivo `.bmp` em `/control/upload`; o novo escopo não pode pressupor que esse transporte receba JPEG/PNG.

## Decisions and assumptions

- Default reversível proposto: priorizar JPEG baseline via TJpgDec com entrada pelo descriptor confinado e saída RGB565; manter BMP intacto. PNG requer decoder por linha com limite demonstrado em host/build e licença/dependência avaliada antes de implementá-lo; NÃO ligar decodificação full-frame de LodePNG sem medir pico de PSRAM. Outras extensões, animação, transparência compositada e alteração do upload são melhorias adjacentes, não pré-requisitos do viewer; negociação humana se exigidas.
- Risco high por segurança e integração hardware; explicitar ao developer que JPEG-only não satisfaz o objetivo de PNG sem reaprovação humana. Não houve Jev: não há empate técnico fechado sustentado por evidência.

## Pending and risks

- Developer: submeter ao usuário objetivo, variantes JPEG/PNG aceitas, política de alpha/EXIF e plano para aprovação; verificar viabilidade de libpng incremental ou alternativa PNG em pesquisa limitada antes de firmar essa dependência. Se codec PNG/limites não forem sustentáveis, oferecer alternativa JPEG+BMP com novo checkpoint de escopo, nunca exclusão silenciosa.
- Acesso a firmware, medidas reais de PSRAM/stack, formatos PNG/JPEG específicos e transporte de fixtures JPEG/PNG ainda pendentes. Sem testes, sem flash e sem alegação de validação.

## References

- `code-map.md#visualizador-de-imagens-reqac-img-0105`; `docs/agent-state/image-viewer.md`; `tests/manual/image-viewer-validation.pt-BR.md`; plano proposto no retorno ao developer.

## Updates

### 2026-11-08 00:00 -03:00 - plano inicial

- Fact: leitura localizada e proposta de plano preparadas; nenhum código, teste ou configuração editado; somente esta nota escrita.
- Supersedes or discards: none.

### 2026-11-08 (hora não aferida) -03:00 - replanejamento B1/B2, retomada de sessão

- Fact: objetivo condicionado permanece BMP/JPEG/PNG com limites existentes, sem exclusão de PNG. A matriz high-risk `docs/agent-state/image-formats/tester.md` agora especifica `REQ-IMGF-01..05`, `AC-IMGF-01..06`, `C1..C7`, `NEG-IMGF-*`, fixtures e roteiro físico; é especificação, não execução. Estado do código: HEAD `0f4ab3a3dd43` e diff vazio **conforme nota do tester**; não aferido independentemente pelo planner (comandos proibidos). Workflow, instrução de entrega e modelo canônico foram lidos do agent-toolkit; nenhum comando, teste, build ou flash executado.
- Decision: B1 tem caminho plausível, ainda não validado: fonte miniz MIT vendorizada/pinada, apenas API de inflate incremental com janela de 32 KiB e linhas bounded, integração host + firmware via TU pura, sem `libz` externo (C7); parse de chunks/CRC/zlib/Adler32/cancelamento e orçamento medido antes de fixar teto. B2: TJpgDec standalone vendorizado/pinado com notice original, `JD_FORMAT=RGB565`, callbacks de descriptor/MCU e pool bounded; não habilitar wrapper LVGL, pois importa headers/gates e não satisfaz C7. Pin por release/commit/hash e conservar licença/notice e relatório de alterações; licença/proveniência da cópia exata, API de streaming miniz e footprint ficam sujeitos a verificação técnica antes de implementação. A escolha não implica afirmar licença/versão verificadas nesta sessão.
- Decision: premissas técnicas reversíveis: PNG 8-bit sem interlace tipos 0/2/3, JPEG SOF0 8-bit 1/3 componentes; extensão autoritativa, sem EXIF rotation/ICC, conteúdo inválido na navegação falha fechado; `POST /control/upload` continua BMP-only. `alpha` (PNG 4/6 e `tRNS`) propõe rejeição tipada, **não** descarte silencioso de pixels nem pretensão de suporte a todas as variantes PNG. A descrição pública deve dizer explicitamente o subconjunto suportado. Não usar resultado Jev anterior (0,63 < 0,90) como desempate/aprovação; nenhum Jev nesta execução.
- Risk/checkpoint: miniz não está no registry e passa a ser dependência third-party mantida localmente; a cópia TJpgDec também exige política de atualizações/notice. Dependências mínimas para C7 e streaming, não melhoria adjacente; se o usuário não aceitar vendorização/variantes/alpha ou se o spike não provar inflate bounded e host-linkável, PARAR e pedir reaprovação humana da alternativa, nunca entregar JPEG-only. Antes do coder, developer deve obter aceite explícito dos recortes PNG/JPEG observáveis, tratamento de alpha, dependências/licenças e eventual rejeição de fotos grandes por manter 1280 px. Gate físico final permanece humano.
- Supersedes or discards: a pendência anterior de 'pesquisar alternativa PNG' como caminho inteiramente desconhecido (linhas 18, 23, 28) foi substituída por hipótese concreta miniz + gate de viabilidade; B1/B2 continuam pendentes de prova, não de investigação genérica. Plano executável e associação REQ→TEST são entregues no retorno ao developer; detalhes da matriz permanecem na nota do tester.

### 2026-11-09 00:00 -03:00 - delta de aprovação humana (hora não aferida)

- Fact: usuário autorizou bibliotecas open-source permissivas para uso pessoal/não comercial. A dependência técnica proposta (miniz e TJpgDec standalone vendorizados) deixa de exigir aprovação genérica de uso de bibliotecas permissivas; permanecem obrigatórias a verificação da licença/notice e proveniência de cada versão efetivamente copiada, o pin/hash, o cumprimento das condições da licença e a prova de C7, memória e build. Esta autorização não aprova automaticamente biblioteca de licença incompatível nem mudança de escopo ou comportamento.
- Fact: usuário escolheu explicitamente **rejeitar JPEG 4:1:1**. REQ-IMGF-02/AC-IMGF-01 passam a aceitar somente JPEG SOF0 8-bit grayscale ou YCbCr com sampling 4:4:4, 4:2:2 ou 4:2:0; sampling 4:1:1 deve falhar fechado com `unsupported_sampling`, sem frame publicado. Adicionar ao cenário negativo `NEG-IMGF-JPG-16` fixture 4:1:1 com esse status e posse/alocações balanceadas; positivos `TEST-IMGF-JPG-01..08` permanecem para as variantes aprovadas. O tester deve incorporar este delta na sua matriz antes do despacho ao coder; não executar ou remover testes neste passo.
- Supersedes or discards: a exigência de obter aprovação genérica das dependências permissivas na atualização de 2026-11-08 foi satisfeita pela autorização humana, sem dispensar verificação técnica/licenciamento específico; a inclusão anterior de 4:1:1 na lista JPEG positiva em `tester.md` foi expressamente descartada, assim como qualquer pendência de decisão humana sobre essa variante. Permanecem para o developer os outros checkpoints de comportamento ainda não aprovados (política alpha/`tRNS`, subconjunto PNG e EXIF/ICC, conforme aplicável); não inferir aceite desses itens a partir deste delta.
- Code state: nenhum código, teste ou configuração foi editado; HEAD/diff não aferidos nesta execução (sem comandos). Apenas esta nota foi atualizada.

### 2026-11-09 16:00 -03:00 - nova sessão, delta de navegação inválida (hora de referência; relógio não aferido)

- Fact: reset de sessão após a implementação física anterior: objetivo novo e explícito do usuário é **manter o viewer aberto** quando uma seta seleciona imagem inelegível pelo conteúdo/formato, exibir motivo tipado na superfície, permitir seguir até imagem válida e fechar só com Esc. A decisão D4 anterior (`tester.md:277-280`), `AC-IMGF-02`/`NEG-IMGF-PUB-02/03` no ramo de navegação e `test_image_viewer_contract`/manual que pressupõem retorno ao shell são substituídos **apenas nesse ramo**; abertura inicial `image <path>` inválida e baseline de formatos/limites/segurança permanecem. Plano e matriz de rastreabilidade novos constam no retorno ao developer desta execução; ainda requerem aprovação humana antes de qualquer despacho ao coder.
- Evidence/code state: somente leitura de `code-map.md` seção imagem, notas coder/tester/reviewer/planner e fontes atuais `cyberdeck_image_app.{h,cpp}`, `cyberdeck_image_types.h`, `cyberdeck_ui.cpp`, `cyberdeck_image_view.{h,cpp}`, manual e template do toolkit; nenhum comando, teste, build ou flash executado; HEAD/diff não aferidos. Evidência física histórica em `tester.md:920-983` pertence ao binário anterior e não prova o novo delta.
- Decision: risco **high** (mudança de posse do frame, geração assíncrona, retorno foreground sob erro de arquivo não confiável). O ramo `by_index` deve preservar snapshot da página e posição do item inválido, publicar evento recuperável tipado com índice/nome/geração, descartar frame antigo sem ponteiro LVGL pendente e permitir seta seguinte sem re-listar; sucesso limpa mensagem e mostra frame; erro inicial/falha de display mantém abandono para shell. A mensagem persiste somente até uma nova navegação aceita ou Esc; entre pedido aceito e resultado, não mostrar imagem/erro do item anterior como atual. Planejar prova host comportamental de estado/evento e LVGL shim, testes negativos de stale/cancel/ownership e roteiro físico de setas; preservar gates completos anteriores e atualizar mapa/documentação/versionamento conforme política local.
- Pending/checkpoint: developer apresentar os REQ/AC e a mudança D4 ao usuário e obter **nova aprovação explícita** antes de codificação/flash; após aprovação, coder/tester/reviewer executam testes focados, build, série física 1 Hz por 2 minutos com setas físicas, extremos de indicador quando aplicável, e revisão final ampla. Nenhum Jev: objetivo e comportamento especificados, sem empate técnico fechado. Contingência se o caminho de evento/frame seguro não couber no orçamento: parar, comparar modelo puro de estado + contrato de UI versus evento integrado sob lock, pedir reaprovação caso se altere comportamento/risco; jamais manter imagem antiga com nome/erro do item inválido.
- Supersedes or discards: substitui a premissa anterior deste papel de que navegação inválida deve falhar fechado **retornando ao shell**; não revoga evidências validadas de decode BMP/JPEG/PNG, ROM prefix, licença, limites ou decisão humana de rejeitar 4:1:1. A nota de teste anterior é histórica, não critério atual para navegação.
