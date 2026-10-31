# Player foreground — matriz de rastreabilidade

## Contrato aprovado

`music` sem argumentos ativa o app foreground `cyberdeck.music`, abre a view
própria e inicia um scan recursivo de `/musica`. Subcomandos são despachados
somente enquanto o player está foreground; `player` é alias da mesma instância.
Manager, adapter e input fazem a mediação entre app/runtime, view e serviços. O
console e a ponte serial permanecem vivos enquanto a view do player está ativa.
Esc e troca de app cancelam/juntam o scan e param playback antes de liberar a
view ou iniciar o próximo foreground. Falha/timeout de stop bloqueia a troca e
mantém foreground, grant, quota e recursos vivos para retry. Reabertura inicia scan novo.
Preservar scan recursivo seguro, controles, consumo de input e limites/casos
negativos existentes.

## REQ/AC e cenários

| REQ / AC | TEST | Cenário e asserts obrigatórios | Alvo/seam comportamental |
| --- | --- | --- | --- |
| REQ-PLAYER-01 / AC-01, AC-06 open/input readiness | TEST-MUSIC-OPEN-INPUT-RACE-01..03 | Enter durante abertura é consumido sem toggle/play; após view pronta um Enter segue funcionando. Abrir sem input cria exatamente uma view e um scan, sem playback; falha de create desativa manager e não deixa view parcial/controles ativos. UI roteia input pelo foreground music antes do status timer e a surface visível sobe à frente da textarea da shell. | `test_music_manager`, `test_music_app`, `test_display_views` e `test_music_scan_opendir_contract.py`. |
| REQ-PLAYER-01 / AC-PLAYER-01 lazy | TEST-PLAYER-LAZY-START | Boot, registro e start lazy não criam scan nem playback; `music` explícito abre e inicia exatamente um scan. | `test_music_app` + `test_app_runtime`; manager/adapter real ou seam compartilhado. |
| REQ-PLAYER-01 / AC-PLAYER-02 foreground único | TEST-PLAYER-OPEN-FOREGROUND | `music` ativa a mesma instância foreground; repetição não duplica app/scan. Console/sessão e Serial-JTAG permanecem vivos sob a view; nenhum modal/surface paralela. | `test_app_runtime` + `test_music_app`; adapter/runtime. |
| REQ-PLAYER-01 / AC-PLAYER-03 comandos ativos | TEST-PLAYER-COMMANDS-ACTIVE | Todos comandos válidos, inclusive `status`/`queue`, chegam à instância ativa; argumentos inválidos são rejeitados sem efeitos. | `test_music_app` + `test_shell_app`; dispatch/adapter. |
| REQ-PLAYER-01 / AC-PLAYER-04 comandos inativos | TEST-PLAYER-COMMANDS-INACTIVE | Antes de abrir e após Esc/stop, `status`, `queue` e comando mutável não são executados, não iniciam serviço nem reabrem UI. | `test_music_app` + `test_app_runtime`; dispatch real. |
| REQ-PLAYER-01 / AC-PLAYER-05 stop/cancel/join | TEST-PLAYER-STOP-CANCEL | Reter scan em secure-open; stop solicita cancelamento e não retorna antes de worker/join. Não publica resultado parcial nem libera lease/handle cedo; playback recebe stop. Cobrir também stop sem scan. | `test_music_app`; worker/adapter real com gate determinístico. |
| REQ-PLAYER-01 / AC-PLAYER-06 view e input | TEST-PLAYER-VIEW-INPUT | View tem ciclo create/render/input/destroy próprio; eventos de teclado/touch traduzem para ações do player. Input não reconhecido é consumido e não altera/executa linha do shell. Teardown destrói view após quiescência. | `test_display_views` compila o TU real `cyberdeck_music_view.cpp` no shim LVGL e valida ciclo/render/visibilidade; `test_music_manager` verifica política de input/close; contrato scanner verifica wiring. Touch/teclado LVGL real segue manual. |
| REQ-PLAYER-01 / AC-PLAYER-07 Esc | TEST-PLAYER-ESC-STOP | Esc ativo aciona close uma vez; cancel/join e stop precedem liberação da view/foreground. Falha mantém ownership; Esc inativo não repete ações. | `test_music_manager` + `test_app_runtime`; contrato estrutural complementar; hardware manual. |
| REQ-PLAYER-01 / AC-PLAYER-08 switch e ordem | TEST-PLAYER-SWITCH-ORDER | Troca para outro app observa stop+join concluídos antes do start destino; console owner e dependentes são preservados e destino recebe ownership/input. | `test_app_runtime` + novo teste adapter/runtime com eventos ordenados. |
| REQ-PLAYER-01 / AC-PLAYER-09 stop failure | TEST-PLAYER-STOP-FAIL-BLOCK | Falha/timeout de stop/join bloqueia switch; player segue foreground, running e com worker/lease/view válidos; destino não inicia. Retry após sanar falha completa a troca. | `test_app_runtime` (falha lifecycle) + novo teste adapter/runtime com injeção de falha. |
| REQ-PLAYER-01 / AC-PLAYER-10 reopen | TEST-PLAYER-REOPEN-RESCAN | Depois de stop completo, reabrir inicia exatamente um scan; fixture alterada aparece e callback/dado do ciclo anterior não contamina a sessão nova. | `test_music_app`; app/adapter real. |
| REQ-PLAYER-01 / AC-PLAYER-11 scan íntegro | TEST-PLAYER-SCAN-BOUNDS-SECURITY | Manter ordem lexicográfica, MP3 válido, rejeição de symlink/traversal/não-regular/secure-open, depth 8 e limite 64/truncation; readdir/closedir falho, cancelamento/root ausente não publica parcial. | `test_music_app` + `test_music_scan_opendir_contract`; negativos e limites preservados. |
| REQ-PLAYER-01 / AC-PLAYER-12 controles | TEST-PLAYER-CONTROLS | Touch e teclas Enter, setas, N/P acionam toggle/play-pause, prev/next, seek e volume na instância ativa; limites de volume/seek e input não reconhecido. | `test_display_views` valida os sete callbacks reais da view; `test_music_manager` cobre política de input e `test_music_player` efeitos. Manual Tab5 pendente. |
| REQ-PLAYER-01 / AC-PLAYER-13 regressão | TEST-PLAYER-REGRESSION | Preservar testes existentes de model, decoder, áudio, player, app, segurança, bounds, help e runtime; novas suites entram nos alvos focados/agregado sem remover nem afrouxar asserts. | Alvos focados existentes e Makefile; suíte ampla fica para reviewer. |

## REQ-MUSIC-UI-01..04 / AC-MUSIC-UI-01..04

| REQ / AC | TEST | Cenário e asserts | Alvo/seam comportamental |
| --- | --- | --- | --- |
| REQ-MUSIC-UI-01 / AC-MUSIC-UI-01 | TEST-MUSIC-UI-LAYOUT-PORTRAIT / LANDSCAPE | Surface usa espaço disponível, lista e controles ficam dentro dos bounds, layout recalcula no `LV_EVENT_SIZE_CHANGED`; paleta grayscale. | `test_display_views`: TU real, 720x1280, 1280x720, resize, bounds/assertions. |
| REQ-MUSIC-UI-02 / AC-MUSIC-UI-02 | TEST-MUSIC-UI-ACTIONS-BOUNDS / LIST-NAVIGATION | Sete ações visuais chamam callback real; oito botões incluem Command; 64 linhas têm scroll vertical e permanecem alcançáveis; selecionar faixa não dispara playback; ações e lista não operam enquanto comando está aberto. | `test_display_views`; `test_music_app` testa `select <index>` válido/inválido sem play. |
| REQ-MUSIC-UI-03 / AC-MUSIC-UI-03 | TEST-MUSIC-UI-COMMAND-OPEN/EDIT/SUBMIT/ERROR/ESC/KEY-ISOLATION/SHELL-PRESERVE | Overlay Command abre na mesma surface; texto bounded, Enter encaminha só ao app, aceita subcomando com/sem prefixo `music`, erros retornam ao painel; primeiro Esc fecha campo, segundo fecha player; input sem overlay é consumido sem alterar shell. | `test_display_views`, `test_music_app`, `test_music_manager`, `test_music_command_controller` e contrato de integração `test_music_scan_opendir_contract.py`; execução física fica manual. |
| REQ-MUSIC-UI-03 / AC-MUSIC-UI-03 behavioral dispatch | TEST-MUSIC-COMMAND-CONTROLLER | Controller compilado pela produção e host cobre cursor/inserção/backspace/delete, limite 96, dispatch Enter exatamente uma vez, erro visível e Escape prioritário; integração host envia `music status` pelo parser real do app. O controller possui exclusivamente callback do app, não uma rota de shell. | `test_music_command_controller` + integração em `test_music_app`; seam compartilhado `cyberdeck_music_command_input`. |
| REQ-MUSIC-UI-04 / AC-MUSIC-UI-04 | TEST-MUSIC-UI-DESTROY-RECREATE / STOP-JOIN / SCAN-ATOMIC | Preservar lifecycle existente: view não sobrevive teardown, callbacks não duplicam em reabertura, stop/join precedem liberação; scan continua sem publicar parcial. | `test_display_views` e cenários existentes `test_music_app`/`test_app_runtime`; nenhum playback disparado pelos novos testes. |
| REQ-MUSIC-UI-05 / AC-MUSIC-UI-05 | TEST-MUSIC-UI-SCROLL-ISOLATION | Player aberto mantém system bar fixa e separada do content; superfície, cards, controles, parent e faixas não rolam horizontalmente nem encadeiam scroll; somente a lista rola verticalmente com scrollbar automática. | `test_display_views` exercita flags LVGL reais da view e geometria root/bar/content; `test_ui_resource_contract.py` protege o wiring do content e o scroll desligado no parent. |
| REQ-PLAYER-LAYOUT-READABLE / AC-LIST-AND-CONTROLS-FIT | TEST-MUSIC-UI-LAYOUT-PORTRAIT / LANDSCAPE / TEST-MUSIC-FOOTER-TEXT / TEST-MUSIC-FOOTER-LAYOUT / TEST-MUSIC-FOOTER-FIT | Status limitado a três linhas; nomes/labels preservam conteúdo e truncam visualmente por pontos sem clipping vertical; oito botões têm no mínimo 44x44 px; blocos de informação, Command e controles não se sobrepõem; 64 faixas ficam bounded e somente a lista rola em portrait, landscape e resize nos dois sentidos. O rodapé de atalhos, cuja dica plana tem 71 caracteres (767,7 px a 10,8125 px/char) e não cabe nos 680 px de conteúdo do retrato, ocupa uma linha em paisagem (`kMusicControlsHint`, sem `\n`) e duas linhas em retrato com quebra explícita após `seek` (`kMusicControlsHintPortrait`), reservando `line` / `2 * line + 2` sem medir texto em runtime; os dois cartões terminam acima dessa faixa nas duas orientações e na volta a paisagem. | `test_display_views` executa TU LVGL real, verifica bounds dos painéis/cartões/labels/botões, matriz de não-sobreposição em portrait/landscape, truncagem e resize portrait→landscape→portrait→landscape; exige o texto exato por orientação, a altura `1×line`/`2×line+2` do label, `cartão_y + cartão_height ≤ footer->y` e que cada linha renderizada caiba na largura do label (`TEST-MUSIC-FOOTER-TEXT`/`LAYOUT`/`FIT`). O `FIT` mede com o avanço do shim (11 px/char, 1,7% acima dos 10,8125 px/char reais), logo é proxy conservador: sem ele, ampliar os atalhos atualizando os dois literais passaria pelas asserções de texto exato. Shim verifica propriedades/layout e largura aproximada, não renderização física dos pixels nem a truncagem DOTS real. `test_music_player_ui_contract` resolve a cobertura real de `cyberdeck_font` e de cada `LV_SYMBOL_*` usado na UI, rejeitando code points sem glifo nos textos do player, e fixa os dois literais, a reserva `footer_height` e a aplicação em `layout()` pelo flag `landscape`, com provas de mutação para faixa de uma linha, texto plano no retrato, quebra removida e texto aplicado em `create()`. |
| REQ-PLAYER-ESC-STABLE / AC-ESC-NO-REBOOT | TEST-PLAYER-ESC-STOP / STOP-FAIL-BLOCK / REGRESSION | Stop cancela scan e aguarda sua conclusão antes de chamar stop do player; scan cancelado não publica parcial nem libera lease cedo; falha de player-stop preserva foreground/grant para retry; Esc repetido inativo não repete close. Logs identificam estágios de stop para diagnóstico; a causa física do reboot não é presumida. | `test_music_app`, `test_music_manager` e `test_app_runtime`; stop-stage logs no adapter/app. Reboot físico permanece manual/pending. |
| REQ-HEADER-FONT-01..03 / AC-HEADER-FONT-01 glyph coverage | TEST-HEADER-FONT-GLYPH-COVERAGE | Texto estático pintado com `cyberdeck_font` não pode usar code point fora dos cmaps reais: o botão de música do header desenha ASCII `>` (`LV_SYMBOL_PLAY` U+F04B não tem glifo) e a dica de controles é ASCII, definida uma vez em `kMusicControlsHint` na view e renderizada como rodapé fixo (o `cyberdeck_ui.cpp` não concatena mais a dica ao texto de status). | `test_music_player_ui_contract` deriva a cobertura de `cyberdeck_font.c` (cmaps FORMAT0 + `unicode_list_*` esparsas), resolve cada `LV_SYMBOL_*` pelo mapa versionado `lv_symbol_codepoints.json` (derivado do UTF-8 de `lv_symbol_def.h`, com `verify_lv_symbol_codepoints` checando drift fora do agregado) e isenta só `strcmp` contra texto de `lv_keyboard_get_button_text`; exige o literal exatamente uma vez entre UI+view, o footer criado após `list_` com `kMuted`/`DOTS`, `footer_y` reservado e `card_height` medido por ele, com provas de mutação para texto-concatenado/cartões de altura cheia/rodapé no topo. `test_display_views` mantém 26x36, a fonte `cyberdeck_font` do rótulo e o clique do botão (o shim não implementa `lv_obj_center`, então a centralização não é verificada) e valida o rodapé sem sobreposição nos dois layouts. Desenho físico do `>` durante playback é manual. |

O slider reporta posição em permille; o adaptador converte para milissegundos e
envia segundos inteiros ao parser do app. `test_music_scan_opendir_contract.py`
protege essa conversão contra seek incorreto por fator 1000.

## Mapeamento concreto e limites

- Já existentes no Makefile: `test_music_app` (app/scan comportamental),
  `test_music_player` (worker/player e falhas de inicialização),
  `test_music_model`, `test_music_audio_output`,
  `test_music_decoder_scratch`, `test_music_decoder_stack_contract`,
  `test_music_scan_opendir_contract`, `test_app_runtime`, `test_shell_app`,
  `test_window_manager` e `test_display_views`.
- `test_app_runtime` cobre exclusividade, ordem e falha de teardown; em falha a
  aplicação permanece `running` e grant/quota válidos até retry bem-sucedido.
  `test_music_app` cobre scan/commands/lifecycle de domínio;
  `test_music_manager` cobre consumo de teclas, retenção após falha de close e
  Escape inativo sem ação duplicada. O teste do manager usa callbacks host e não
  prova execução física da view.
- `test_music_scan_opendir_contract.py` verifica a branch ESP/FATFS e os pontos
  de integração atuais manager/view/input/adapter, preservando asserções de
  ownership, comandos, controles, cancelamento e segurança. Não prova
  comportamento LVGL.
- `test_display_views` links the production music-view TU, covering create,
  rendering, visibility, all seven callback labels/context, scroll isolation,
  root/system-bar/content geometry, destroy/recreate and scope-guard coverage.
  Shim callbacks do not replace physical input tests.
- Testes host que só usam fakes/contratos não provam a execução física de LVGL,
  touch/teclado, codec ou áudio. `manual_validation: pending` até validar no
  Tab5 Esc, troca de app, controles e playback parado sem scan ativo.
- Executar somente alvos criados/alterados; comandos exatos devem ser registrados
  depois que os novos alvos/seams forem implementados. Não executar suíte global
  nem cobertura nesta etapa de especificação.
