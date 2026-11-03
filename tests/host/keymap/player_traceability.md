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
| REQ-MUSIC-HEADER-STATE / AC-MUSIC-HEADER-ESC-HIDDEN, AC-MUSIC-HEADER-ESC-FAIL-PRESERVE | TEST-MUSIC-HEADER-CLOSE-STATE, TEST-MUSIC-HEADER-CLOSE-FAIL, TEST-MUSIC-HEADER-PHYSICAL-ESC | O indicador `>` segue playing/paused somente enquanto music é foreground; após close bem-sucedido, foreground shell oculta no próximo tick mesmo com snapshot stale de playback. Falha de close preserva foreground e indicador para retry. | `test_music_header_visibility` cobre as oito combinações de foreground × playing/paused/stopped/error e os casos de snapshot stale após saída e playback visível após falha de close; `test_music_manager` cobre ownership em close success/failure; `test_music_player_teardown_contract.py` protege a integração UI→seam. Esc físico permanece manual. |
| REQ-PLAYER-01 / AC-PLAYER-08 switch e ordem | TEST-PLAYER-SWITCH-ORDER | Troca para outro app observa stop+join concluídos antes do start destino; console owner e dependentes são preservados e destino recebe ownership/input. | `test_app_runtime` + novo teste adapter/runtime com eventos ordenados. |
| REQ-PLAYER-01 / AC-PLAYER-09 stop failure | TEST-PLAYER-STOP-FAIL-BLOCK | Falha/timeout de stop/join bloqueia switch; player segue foreground, running e com worker/lease/view válidos; destino não inicia. Retry após sanar falha completa a troca. | `test_app_runtime` (falha lifecycle) + novo teste adapter/runtime com injeção de falha. |
| REQ-PLAYER-01 / AC-PLAYER-10 reopen | TEST-PLAYER-REOPEN-RESCAN | Depois de stop completo, reabrir inicia exatamente um scan; fixture alterada aparece e callback/dado do ciclo anterior não contamina a sessão nova. | `test_music_app`; app/adapter real. |
| REQ-PLAYER-01 / AC-PLAYER-11 scan íntegro | TEST-PLAYER-SCAN-BOUNDS-SECURITY | Manter ordem lexicográfica, MP3 válido, rejeição de symlink/traversal/não-regular/secure-open, depth 8 e limite 64/truncation; readdir/closedir falho, cancelamento/root ausente não publica parcial. | `test_music_app` + `test_music_scan_opendir_contract`; negativos e limites preservados. |
| REQ-PLAYER-01 / AC-PLAYER-12 controles | TEST-PLAYER-CONTROLS | Touch e teclas Enter, setas, N/P acionam toggle/play-pause, prev/next, seek e volume na instância ativa; limites de volume/seek e input não reconhecido. | `test_display_views` valida os sete callbacks reais da view; `test_music_manager` cobre política de input e `test_music_player` efeitos. Manual Tab5 pendente. |
| REQ-PLAYER-01 / AC-PLAYER-13 regressão | TEST-PLAYER-REGRESSION | Preservar testes existentes de model, decoder, áudio, player, app, segurança, bounds, help e runtime; novas suites entram nos alvos focados/agregado sem remover nem afrouxar asserts. | Alvos focados existentes e Makefile; suíte ampla fica para reviewer. |

## REQ-MUSIC-KEYS-01..05 / AC-MUSIC-KEYS-01..06

| REQ / AC | TEST | Cenário e asserts | Alvo/seam |
| --- | --- | --- | --- |
| REQ-MUSIC-KEYS-01..05 / AC-MUSIC-KEYS-01..06 | TEST-MUSIC-KEYS-MAPPING / BOUNDS / LIST / REMOVED / REGRESSION / HINT | ←/→ acionam faixa anterior/próxima; ↑/↓ movem a seleção/highlight e scroll da lista sem playback; +/- alteram volume; limites da lista não passam de 0..N-1; N permanece sem binding; P é Play Selected conforme REQ-MUSIC-PLAY-SELECTED; Enter/touch/comandos textuais preservados; dica ASCII atualizada por orientação. | `test_music_manager`, `test_display_views`, `test_music_scan_opendir_contract.py`, `test_music_player_ui_contract.py`; validação física no roteiro manual. |
| REQ-ENTER-PLAYPAUSE / AC-ENTER-PLAYPAUSE, AC-ENTER-SELECTED-START, AC-ENTER-INVALID-NOOP | TEST-ENTER-TOGGLE-STATES / SELECTED-START / EMPTY-INVALID / SNAPSHOT-MISMATCH / UNKNOWN-STATE / REGRESSION / TAB5 | Enter consulta snapshot real: playing pausa, paused retoma; stopped inicia somente faixa selecionada válida. Consistência da fila/snapshot e índice são validados antes de qualquer efeito. Fila vazia, índice inválido, mismatch em playing/paused/stopped, error ou enum desconhecido são no-op, sem player call nem mutação de seleção. Atalhos, touch e comandos textuais permanecem. | `test_music_manager` (roteamento Enter existente), `test_music_app` (estados, mismatch por estado, error e enum fora do domínio), Tab5 manual. |

## REQ-MUSIC-UI-01..04 / AC-MUSIC-UI-01..04

| REQ / AC | TEST | Cenário e asserts | Alvo/seam comportamental |
| --- | --- | --- | --- |
| REQ-MUSIC-UI-02 / AC-MUSIC-UI-02 | TEST-MUSIC-UI-ACTIONS-BOUNDS / LIST-NAVIGATION | Oito ações visuais anteriores mais Play Selected chamam callback real; nove botões incluem Command; 64 linhas têm scroll vertical e permanecem alcançáveis; selecionar faixa não dispara playback; ações e lista não operam enquanto comando está aberto. | `test_display_views`; `test_music_app` testa `select <index>` válido/inválido sem play. |
| REQ-MUSIC-PLAY-SELECTED / AC-MUSIC-PLAY-SELECTED | TEST-MUSIC-PLAY-SELECTED-01..10 | Ação explicitamente separada toca a faixa destacada desde zero nos estados playing/paused/stopped; Enter preserva toggle pause/resume e stopped-start-selected. P e botão touch despacham uma vez; selection-only não toca; fila/snapshot/index inválidos falham fechados; comandos textuais e shell submit preservados; nove botões atendem bounds e layout. | `test_music_manager`, `test_music_app`, `test_display_views` e integração UI inspecionada. Manual: usuário confirmou P físico e botão de tela Play Selected na imagem SHA-256 `67a2358a14fd3130df0564d272cf68e441ab4bc2ceaf32f43583bb26e8013daa` (produção inalterada desde então); não inferir P virtual nem aceite integral. Ver roteiro de 2026-11-02. |
| TEST-MUSIC-PLAY-SELECTED-01..10 | 01 manager action; 02..04 app play-from-zero playing/paused/stopped; 05 fail-closed invalid/empty/inconsistent/error/unknown enum (including `0xff`, no player calls or selection mutation); 06 Enter toggle regression; 07 P key path and N absent; 08 touch callback exactly once; 09 selection-only and pending snapshot reconciliation; 10 nine-control layout/44 px/no-overlap plus textual command and shell preservation | Cobertura host mantém o mapeamento comportamental; UI key routing é parcialmente estrutural. Evidência manual reportada pelo usuário: P físico e botão de tela Play Selected funcionaram na imagem SHA-256 `67a2358a14fd3130df0564d272cf68e441ab4bc2ceaf32f43583bb26e8013daa`; P virtual, outros controles/layout e aceite integral não são inferidos. |
| REQ-ENTER-SELECTED-START / AC-ENTER-SELECTED-START | TEST-MUSIC-SELECT-RENDER-ENTER-01..03 | Em stopped, seleção por seta ou clique seguida de render de status atrasado mantém o highlight e dispara exatamente um play no caminho selecionado; status antigo não reverte a seleção. Fila substituída/reduzida não destaca item oculto e uma seleção válida posterior converge após status atualizado. Pause/resume permanecem cobertos pelo teste da aplicação. | `test_display_views` integra view LVGL real e fake app/player; `test_music_app` mantém os asserts de toggle por estado. Rejeição real da seleção não é injetável pela view TU; requer integração app/manager. |
| REQ-MUSIC-UI-03 / AC-MUSIC-UI-03 | TEST-MUSIC-UI-COMMAND-OPEN/EDIT/SUBMIT/ERROR/ESC/KEY-ISOLATION/SHELL-PRESERVE | Overlay Command abre na mesma surface; texto bounded, Enter encaminha só ao app, aceita subcomando com/sem prefixo `music`, erros retornam ao painel; primeiro Esc fecha campo, segundo fecha player; input sem overlay é consumido sem alterar shell. | `test_display_views`, `test_music_app`, `test_music_manager`, `test_music_command_controller` e contrato de integração `test_music_scan_opendir_contract.py`; execução física fica manual. |
| REQ-MUSIC-UI-03 / AC-MUSIC-UI-03 behavioral dispatch | TEST-MUSIC-COMMAND-CONTROLLER | Controller compilado pela produção e host cobre cursor/inserção/backspace/delete, limite 96, dispatch Enter exatamente uma vez, erro visível e Escape prioritário; integração host envia `music status` pelo parser real do app. O controller possui exclusivamente callback do app, não uma rota de shell. | `test_music_command_controller` + integração em `test_music_app`; seam compartilhado `cyberdeck_music_command_input`. |
| REQ-MUSIC-UI-04 / AC-MUSIC-UI-04 | TEST-MUSIC-UI-DESTROY-RECREATE / STOP-JOIN / SCAN-ATOMIC | Preservar lifecycle existente: view não sobrevive teardown, callbacks não duplicam em reabertura, stop/join precedem liberação; scan continua sem publicar parcial. | `test_display_views` e cenários existentes `test_music_app`/`test_app_runtime`; nenhum playback disparado pelos novos testes. |
| REQ-PLAYER-01 / AC-01, AC-06 + AC-ENTER-* | TEST-KEY-PLAYER-01..10 + TEST-KEY-PLAYER-11..14 | Dispatcher captura geração na submissão; callbacks stale não acessam fila nova nem chamam rota errada. TEST-KEY-PLAYER-11 verifica ordens submit→invalidate e invalidate→submit; 12 bloqueia receiver já claimado, executa stop/reopen e prova contexto/evento únicos sem consumo de B; 13 reinvoca callback concluído após reutilização do slot e exige que callback sem identidade ativa retorne antes de tocar fila/evento/ticket atual, que permanece intacto e é entregue uma vez. TEST-KEY-PLAYER-14 semeia a identidade em `UINTPTR_MAX`: a última submissão é entregue uma vez, submissões posteriores falham sem wrap/reuso, callback duplicado não toca fila/ticket, e limpeza confirma slots/envelope/fila sem pendências. Identidades monotônicas não reciclam; exaustão de `uintptr_t` falha fechado. | `test_keyboard_dispatch`, `test_music_app`, contratos e manual Tab5. |
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

## Registro de validação física — 2026-11-02

Para `REQ-MUSIC-HEADER-STATE` / `AC-MUSIC-HEADER-ESC-HIDDEN` /
`TEST-MUSIC-HEADER-PHYSICAL-ESC`, a primeira tentativa gravou e verificou a
imagem v3 SHA-256 `6427e1c7df8ee3e0724a12d67683fa5a4443e3718be6c1c1c3cff34c2aa48b48`,
mas o controller abortou em `wait_boot`, antes de qualquer comando de UI/player,
playback ou Esc. Captura analisada localmente:
`/tmp/opencode/music-header-esc-20261102-190216-idfenv.raw`, SHA-256
`36abb1c0dcfad48d50d4b704761a3547d3ad7a4e4f8834f8570ae7c001b9998b`. O motivo
foi falso positivo do harness: regex case-insensitive `FAULT` encontrou
`Backtrace` na linha de comando do `idf_monitor.py` (`--decode-panic backtrace`).
Não há evidência de panic/WDT/reset. Embora a captura contenha
`HOSTED_DIAG wifi stage=set_config result=ESP_OK`, faltam o conjunto completo de
marcadores/checkpoint e `sys.info`; o gate de boot não foi comprovado. Nenhum
resultado físico de player/Esc pode ser inferido. `manual_validation: pending`;
intenção de retomar após corrigir uma cópia temporária do helper fora do repo.

### Retentativas do controller e estado atual — 2026-11-02

O firmware ativo permaneceu v3, SHA-256
`6427e1c7df8ee3e0724a12d67683fa5a4443e3718be6c1c1c3cff34c2aa48b48`; não houve
reflash nem envio de comandos de UI/player/playback/Esc. Ambas as tentativas
falharam na instrumentação local e não constituem defeito ou validação do firmware.

1. Cópia temporária do helper removeu `Backtrace` de `FAULT`, mas abortou no
   warning `W (10563) NimBLE: Failed to persist local IRK (rc=8)`, porque a regex
   genérica `ERROR` também reconhecia warning W. Exploração local: `rc=8` é
   `BLE_HS_ENOTSUP`; em `ble_hs_pvcy.c` o caminho registra WARN e retorna, sendo
   provável que a IRK não persista entre boots. Não é panic nem prova de
   estabilidade. Raw `/tmp/opencode/music-header-esc-20261102-retry.raw`, SHA-256
   `688a2234b63bc8297fc782bd8794bbf7b15bd603ae43644e3adf6747b8d3a255`; sem
   checkpoint completo ou `sys.info`.
2. Cópia temporária gates-only ignorou `Backtrace` e restringiu `ERROR` a
   severidade `E (`. Após aguardar `BOOT_MARKERS` por 35 s, fallback `events.read`
   falhou localmente antes de qualquer escrita/envio de frame:
   `TypeError: 'int' object is not a mapping` (o helper passou inteiro em `fields`).
   Raw `/tmp/opencode/music-header-esc-20261102-final.raw`, SHA-256
   `5793e02addb02b860d25ec6c3a31c1f9d8d7656d456b6a8da1e1aa05aa62cf7b`, 10 linhas
   somente com eventos de tela por inatividade/wake. Nenhum `events.read`,
   `sys.info` ou interação foi enviado. Sem reset/panic comprovado e sem conclusão
   de estabilidade.

Discovery local posterior: `cyberdeck_serial_bridge.cpp::exec_events_read` não
aceita parâmetros extras e usa chunk fixo 1024; envelope correto apenas
`{rid,type}`, coberto por `test_event_projection_contract.py`. Próxima rota
planejada: corrigir somente a cópia privada para `controller.request("events.read",
{})` e tentar uma vez se o gate de boot passar. A tentativa subsequente e final
está registrada abaixo; não classificar falhas do harness como bugs firmware.

### Terceira tentativa do controller — 2026-11-02

A terceira cópia privada corrigida foi
`/tmp/opencode/music-monitor-controller-esc-final.py`, SHA-256
`ee92d34742f73877ee6118d265cef5e1c0654dea8d264647306a1f54e4422f9f`; alterou
somente regex de Backtrace, severidade de erro e fallback `events.read` com
`fields={}` conforme contrato. Imagem anteriormente gravada/verificada: v3,
SHA-256 `6427e1c7df8ee3e0724a12d67683fa5a4443e3718be6c1c1c3cff34c2aa48b48`,
`version.txt=3`; versão ativa não confirmada por `sys.info` nesta execução.

`wait_boot` expirou sem `BOOT_MARKERS`; fallback enviou `events.read`, sem resposta
no deadline. Raw `/tmp/opencode/music-header-esc-20261102-final-once.raw`, SHA-256
`2dfeff5b58bc7f7bda2fe62ca12b2ad354f39f5a9d94179ab9f7e2951159f563` (3 linhas,
419 B), não contém resposta/checkpoint. Nenhum `sys.info`, UI, player, playback ou
Esc; não há evidência suficiente de crash/reset nem de boot estável. Warning
NimBLE `rc=8` foi identificado localmente como `BLE_HS_ENOTSUP`, não fatal, mas
não satisfaz o boot gate. O erro anterior de `fields` inteiro foi corrigido com
`{}`, porém o terceiro retorno continuou sem resposta. Tentativa encerrada conforme
instruções; não repetir.

Bloqueio atual: gate boot/serial inconclusivo, não bug confirmado no firmware.
`TEST-MUSIC-HEADER-PHYSICAL-ESC` não foi executado; `manual_validation: pending`.
Autorização era para uma tentativa controlada playback+Esc, não para revisão final
ampla.

### Evidência física adicional reportada pelo usuário — 2026-11-02

O usuário confirmou explicitamente todos os critérios do cenário: playback ativo,
pressionou Esc, shell retornou, ícone de playback sumiu e não houve reinício.
Este teste manual is user-reported and firmware identity unconfirmed. A consulta
única do tester a `python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 sys.info`
retornou exit 2, `nenhuma resposta do dispositivo`; não foi repetida. Assim, o
relato satisfaz o gate comportamental por aceite manual, mas a imagem em execução
permanece desconhecida. Última imagem registrada como gravada/verificada: v3,
SHA-256 `6427e1c7df8ee3e0724a12d67683fa5a4443e3718be6c1c1c3cff34c2aa48b48`;
não afirmar que corresponde ao teste. Build local posterior à correção
format-only: SHA-256 `4f329e4b371d0f406c1aa390f0ee189e19913da30b58b9953a02de894fee19e9`,
não gravado depois da correção. Portanto o resultado é relato humano, não
validação independente do firmware pelo agente. Reviewer final continua
`review_incomplete`: thresholds gcovr 8.7 e scope guard não executados; CI remota
não cobre o diff dirty; nenhum commit/push autorizado. Isto não fecha code review
nem autoriza marcar a tarefa concluída.

### Estado do review final local — 2026-11-02

Review final `review_incomplete`: gcovr 8.7 e o scope guard não foram executados;
coverage/scope não têm resultado PASS. Instalação pip normal no venv isolado
`/tmp/opencode/gcovr-8.7-venv` expirou após 600 s coletando `lxml` (exit 124;
`/tmp/opencode/gcovr-install-retry.log`). Tentativa wheel-only bounded usando
PyPI oficial também expirou em `Collecting lxml`, sem wheels encontrados (exit
124; `/tmp/opencode/gcovr-wheel-download.log`). Não há processos pip/gcovr ativos;
gcovr ausente. Pesquisa externa confirma `lxml` obrigatório no gcovr 8.7, mas a
compatibilidade de wheel CPython 3.14/x86_64 local permanece inconclusiva. Sem
novas tentativas de instalação nesta tarefa sem autorização/estratégia humana.
CI remota não cobre o diff dirty.

Snapshot de referência: HEAD `e8b064b1f88f59c02d81a855282e516573297bd4` + diff
tracked `cb24ed2063c771c83bf93337ab482664dbc9942dbee0b7443d3ae35fe2ea3e20`;
este follow-up é documental-only. Usuário autorizou revisão local, não
commit/push. `TEST-MUSIC-HEADER-PHYSICAL-ESC` não foi executado: nenhum
player/playback/Esc foi enviado e os device helpers não passaram boot/sysinfo;
`manual_validation: pending`. Não declarar aprovação/conclusão.

### Verificação independente do tester — ciclo 3

Snapshot base: HEAD `e8b064b1f88f59c02d81a855282e516573297bd4` + diff imediatamente
anterior a esta atualização documental `8f7328f5789896b63667925196dd0684c327aa71fcb4c8cd856fadb707bf34c4`.
O tester encontrou uma lacuna de coverage: o alvo `test_music_header_visibility`
já estava no agregado, mas compilava o teste e o TU real
`cyberdeck_music_header_visibility.cpp` diretamente, sem `--coverage` e fora de
`BINS`; portanto gcovr não produzia dados do TU de produção. O tester corrigiu a
receita do alvo no `tests/host/keymap/Makefile` para instrumentar e incluir o TU
de produção, e atualizou o `code-map.md`. Nenhum código de produção ou assertion
do teste foi alterado.

Evidência focada reportada: `test_music_header_visibility` PASS (8 assertions),
`.gcno` e `.gcda` de teste e produção gerados, `git diff --check` PASS. Isso não
fecha coverage global: o gcovr temporário é
`8.8.post1.dev0+g6e49f2f4d.d20261031` (não CI 8.7), e thresholds/scope guard
seguem pendentes. Reviewer deve executar clean coverage e scope guard no snapshot
atual; revisão continua `review_incomplete`.

O aceite manual permanece `completed` por relato do usuário: playback ativo,
Esc, retorno à shell, ícone oculto e nenhum reboot. Identidade do firmware não
foi confirmada; não converter esse relato em validação independente. O usuário
autorizou revisão local, não commit/push.

### Correção de formatação após revisão — 2026-11-02

Reviewer `needs_correction` (cycle 1) apontou clang-format nas linhas 7 e 11 de
`cyberdeck_music_header_visibility.cpp`; sem falha de comportamento/teste. O
arquivo foi formatado sem alteração semântica. Gates desta correção completos:
dry-run clang-format, alvo `test_music_header_visibility` e `git diff --check`
passaram. Thresholds globais de coverage/gcovr e CI remota permanecem
inconclusivos para o snapshot dirty. `TEST-MUSIC-HEADER-PHYSICAL-ESC` segue
pendente: gate boot/serial inconclusivo, sem UI/player/play/Esc; manual pending.

### Verificação independente do tester — ciclo 2

Handoff `test_ready`, snapshot verificado: HEAD
`e8b064b1f88f59c02d81a855282e516573297bd4` + diff
`c07d0c054d0a3d37a449d762e1356b92509b5d98d9fc4d5037ff9d8505affb1a`.
Hashes: fonte `cyberdeck_music_header_visibility.cpp` =
`852f619a5d2eb5ea5af145a7193fa82d54d17cded997f8d86d12b71f034a81a`;
teste `test_music_header_visibility.cpp` =
`2ee0ad52395b6e5ff6aa03682a85523d04cf80383646c1923aff7137c3027aed`.
Validação independente: `clang-format --dry-run --Werror`,
`test_music_header_visibility` e `git diff --check` PASS. Critérios de close
success/failure documentados; `TEST-MUSIC-HEADER-PHYSICAL-ESC` permanece manual
pendente. A falta de formato reportada pelo reviewer no ciclo 1 foi corrigida e
validada. Coverage thresholds/gcovr e remote CI seguem pendentes/inconclusivos;
host regression focada/tester PASS não equivale a esses gates. Próxima ação:
reviewer final sobre snapshot atualizado. `manual_validation: pending`; nenhum
aceite físico alegado.

## Fechamento de rastreabilidade — revisão final, ciclo 4 (2026-11-03)

| REQ / AC | TEST | Resultado e evidência |
| --- | --- | --- |
| REQ-MUSIC-HEADER-STATE / AC-MUSIC-HEADER-ESC-HIDDEN | TEST-MUSIC-HEADER-CLOSE-STATE | Host coberto e aprovado no Quality Gate do commit funcional `0d26af2175b410a557a5d76383a4e29c27994142`; reviewer confirmou HEAD igual ao SHA do run. |
| REQ-MUSIC-HEADER-STATE / AC-MUSIC-HEADER-ESC-FAIL-PRESERVE | TEST-MUSIC-HEADER-CLOSE-FAIL | Caso de falha permanece validado por host (preservação do foreground/indicador); não foi cenário físico reportado. |
| REQ-MUSIC-HEADER-STATE / AC-MUSIC-HEADER-ESC-HIDDEN | TEST-MUSIC-HEADER-PHYSICAL-ESC | Aceite manual reportado pelo usuário: playback ativo → Esc → shell retornou, indicador sumiu, sem reboot. O usuário esclareceu que usou o último firmware gravado; vinculação à última gravação registrada é baseada nesse relato, sem leitura independente da identidade. `sys.info` falhou sem resposta (exit 2). |

Quality Gate run `41553034162` terminou `success` em 2026-11-03 00:40:59Z;
job quality, format/lint/security, host + coverage, TSan e build ESP-IDF passaram.
Reviewer confirmou HEAD local igual ao SHA do run e `git diff --check` limpo.
Essa evidência CI/revisão pertence ao commit funcional acima, não à atualização
documental posterior; não alegar CI para o novo commit. O aceite Esc é
`completed` por relato do usuário, não validação independente. Não inferir
aceitação de outros gates físicos do player.
