# Validacao Manual: Menu Inicial TUI e Shell de Entrada

**Projeto:** cyberdeck5 (M5Stack Tab5)  
**Escopo:** uma única tela com terminal TUI, teclado fisico/virtual (shell de
entrada) e fluxo SSH que exercita o roteamento de entrada no mesmo terminal.
**Estado sob validacao:** header compacto com `CYBERDECK5` a esquerda, relogio
`DD/MM/YYYY HH:MM` (GMT-3 fixo, sem DST) no centro e indicador de Wi-Fi a
direita; NENHUM status SSH no header — os estados SSH aparecem SOMENTE como
linhas `[ESTADO] mensagem` no terminal unificado e como eventos no log (`log`);
terminal direto, sem fileira de botoes; SSH iniciado pelo comando `ssh`;
digitacao por teclado fisico e virtual, inclusive durante SSH; scroll do
terminal apenas interno (sem scrollbar externa). Não há tela SSH separada nem
ação de retorno.
**Firmware:** `components/cyberdeck/src/platform/display/cyberdeck_ui.cpp`, `src/platform/input/tab5_keyboard_keys.cpp`,
`src/platform/input/tab5_keyboard.cpp`, `src/features/ssh/ssh_client.cpp`, `src/features/shell/cyberdeck_shell_utils.cpp`.
**Duracao estimada:** 60-90 min por execucao.

> Este plano NAO substitui codigo de producao nem altera firmware. Qualquer
> falha encontrada deve ser registrada na tabela abaixo e reportada ao
> developer/reviewer.

---

## 1. Pre-condicoes (reproducibilidade)

| # | Item | Procedimento |
|---|------|--------------|
| P1 | Firmware atual | `idf.py build && idf.py -p /dev/ttyACM0 flash monitor` |
| P2 | Servidor SSH alvo | `ssh -V` no PC; usuario/senha validos (ex.: `root@<ip>`) |
| P3 | Teclado fisico | M5Tab5 Keyboard (A164) na Ext.Port1; LED direito roxo = modo Character |
| P4 | Sem teclado fisico | Teclado virtual LVGL deve aparecer ao focar o terminal, inclusive durante SSH |
| P5 | WLAN | Rede salva no SD (persistencia) ou roteador aberto p/ teste de reconexao |
| P6 | Rotacao | Sensor BMI270 habilitado; virar a tela deve girar a UI |

Registre em cada execucao: `device_id`, `data`, `commit`/`hash` do firmware,
`ssid`, `host_ssh`, `observador`.

---

## 2. Menu inicial TUI (renderizacao)

| ID | Acao | Resultado esperado | Pass/Fail | Obs |
|----|------|--------------------|-----------|-----|
| T1 | Boot sem teclado | Header no plano aprovado: `CYBERDECK5` a esquerda, relogio `DD/MM/YYYY HH:MM` no centro e um unico icone Wi-Fi a direita; o icone fica claro somente quando o Wi-Fi esta habilitado, conectado e possui IP, e escuro nos demais estados; nenhum SSID nem status/indicador SSH no header; terminal direto mostra `CYBERDECK5 READY` e o prompt `$ `, sem botoes de conexao |  |  |
| T1a | Relogio do header | Centro do header mostra `DD/MM/YYYY HH:MM` exato (GMT-3 fixo); avanca de minuto na virada do minuto; se o RTC ainda nao sincronizou (epoch < 2020), o campo fica vazio sem quebrar o layout; NENHUM rotulo SSH aparece ao lado do relogio |  |  |
| T2 | Terminal direto | Terminal unificado ocupa a area abaixo do header (sem fileira de botoes), com a linha de comando pronta para entrada |  |  |
| T3 | Tema monocromatico | Fundo preto (#000), superficies #0A0A0A/#121212, bordas #2A2A2A, texto #F2F2F2, texto fraco #8A8A8A; sem cor de destaque |  |  |
| T4 | Entrada unificada | A linha de comando do terminal recebe foco e aceita entrada por toque/teclado |  |  |
| T5 | Rotacao automatica | Girar o Tab5 90/180/270°: UI acompanha (BMI270) sem glitch |  |  |
| T6 | Teclado virtual | Tocar no terminal: teclado LVGL aparece ancorado embaixo (altura 300px); some ao digitar tecla fisica |  |  |
| T7 | Layout sem overflow | Com teclado virtual aberto, o terminal continua acessivel (sem sobreposicao ilegivel) |  |  |
| T8 | Terminal direto | Não há botão entre o header e o terminal; tocar nessa região mantém o foco no terminal e não inicia SSH |  |  |

## 3. Shell de entrada (teclado fisico -> terminal focado)

> Pre-condicao: focar o terminal apos o boot (sem SSH ativo).

| ID | Acao | Resultado esperado | Pass/Fail | Obs |
|----|------|--------------------|-----------|-----|
| S1 | Digitar `a`..`z`, `A`..`Z` | Texto inserido na linha do terminal, case preservado |  |  |
| S2 | Digitar `0`..`9` e simbolos `!@#$%^&*()[]{}`, `/?.>-_+=` | Caracteres corretos na linha do terminal |  |  |
| S3 | `Espaco` (tecla fisica e `space`) | Insere espaco |  |  |
| S4 | `Enter` | Nao insere newline no terminal; executa a linha conforme o estado da sessao (ver R2) |  |  |
| S5 | `Backspace` | Apaga o caractere a esquerda do cursor |  |  |
| S6 | `Del` | Apaga o caractere a direita do cursor |  |  |
| S7 | Setas esq/dir | Movem o cursor dentro da linha do terminal |  |  |
| S8 | `Esc` | Nenhum texto inserido; nao derruba a sessao (ver L4 quando online) |  |  |
| S9 | Caracteres multibyte NAO listados (ex. acento) | Primeiro byte >= 0x20 injetado como char OU log `Evento nao mapeado` no monitor; nunca crash |  |  |
| S10 | Tecla `Ctrl` + `a` | Bit0 do modifier set; roteado como tecla especial (ver R4) |  |  |
| S11 | Tecla `Alt` + letra | Byte ESC (0x1B) enviado antes do char (bit2 do modifier) |  |  |
| S12 | Inserir no meio da linha | Mover o cursor com setas esq/dir para o meio e digitar: o caractere e inserido NA posicao do cursor (texto a direita desloca); cursor avanca uma posicao |  |  |
| S13 | Cursor permanece na posicao apos edicao | Apos inserir/apagar (Backspace/Del) no meio da linha, o cursor permanece na posicao real (nao pula para o fim); setas seguem refletindo a posicao real |  |  |
| S14 | Cursor apos historico | Apos Up/Down (historico), o cursor vai para o FIM da linha resgatada (paridade com `move_history`) e a digitacao continua a partir dai |  |  |
| S15 | Cursor visivel | A barra de cursor (caret) esta visivel na posicao correta da linha de edicao mesmo com o terminal preenchido por saida longa; tocar no terminal posiciona o cursor no ponto tocado, quando aplicavel ao controle |  |  |
| S16 | Prompt do shell local | Ao iniciar, o prompt mostra `/sdcard$ `. Após `cd` válido mostra o diretório atual (por exemplo, `/sdcard/child$ `); `cd` inválido preserva o diretório e o prompt anteriores |  |  |

## 3.1 Comandos internos do shell (roteamento do menu)

> Pre-condicao: tela TUI unica visivel, terminal no foco, sem sessao SSH ativa.
> Estes itens cobrem `execute_line()` e `append_output()` em
> `cyberdeck_ui.cpp`, que NAO sao testaveis host-side (ver secao 10).

| ID | Acao | Resultado esperado | Pass/Fail | Obs |
|----|------|--------------------|-----------|-----|
| CMD1 | Digitar `help` + `Enter` | Ecoa `$ help`; imprime exatamente `help - show this help`<br>`wifi - show network status`<br>`log - show recent events`<br>`clear - clear the terminal`<br>`ssh [user@]host[:port] - start an SSH session` (com newline final); permanece no menu |  |  |
| CMD2 | Digitar `clear` + `Enter` | Limpa o texto de saida do terminal unificado |  |  |
| CMD3 | Digitar `wifi` + `Enter` | Imprime `wifi: enabled connected <ip>` / `wifi: enabled disconnected` / `wifi: disabled disconnected`, ou `wifi: unavailable` se `wifi_mgr_get_status` falhar; sem crash |  |  |
| CMD3a | Digitar `log` + `Enter` repetidamente | Imprime os eventos recentes, ou `(nenhum evento disponivel)`, sem reiniciar o dispositivo nem gerar panic |  |  |
| CMD4 | Digitar `ssh` (sem args) + `Enter` | Imprime `usage: ssh [user@]host[:port]`; permanece no menu |  |  |
| CMD5 | Digitar `ssh host:abc` + `Enter` | Usage (porta invalida); permanece no menu |  |  |
| CMD6 | Digitar `ssh user @host` + `Enter` | Usage (parser rejeita espacos no alvo); permanece no menu |  |  |
| CMD7 | Digitar `ssh alice@192.168.1.50:2222` + `Enter` | Inicia a sessão SSH no mesmo terminal (status de senha ou host-key conforme o servidor) |  |  |
| CMD8 | Digitar comando desconhecido (ex. `foo`) + `Enter` | Imprime `unknown command; type help` |  |  |
| CMD9 | `Enter` com linha vazia ou apenas espacos | Nada e impresso (sem `$ `); linha permanece vazia |  |  |
| CMD10 | Digitar `HELP` (maiusculas) + `Enter` | Desconhecido (case-sensitive): `unknown command; type help` |  |  |
| CMD11 | Gerar saida do shell > 12 KiB (ex. repetir `help` varias vezes) | Buffer do shell sofre rollover para 12288 bytes (mantem a cauda); UI continua responsiva, sem estouro de memoria |  |  |

## 4. Roteamento / navegacao (TUI)

| ID | Acao | Resultado esperado | Pass/Fail | Obs |
|----|------|--------------------|-----------|-----|
| R1 | `ssh` sem alvo | Imprime `usage: ssh [user@]host[:port]`; permanece no menu |  |  |
| R2 | `Enter` no terminal com SSH online | Envia o conteudo como comando |  |  |
| R3 | `Enter` durante pedido de senha | Envia a senha digitada no terminal e limpa a linha |  |  |
| R4 | `Ctrl+letra` com SSH online | Byte de controle (`1..26`) enviado ao remoto (ex.: Ctrl+C) |  |  |
| R5 | Foco do teclado virtual | Teclado LVGL segue o terminal focado (`lv_keyboard_set_textarea`) |  |  |
| R6 | Alvo SSH sem porta | Conexao usa porta 22 (default) |  |  |

## 5. Fluxo SSH (integracao do shell)

| ID | Acao | Resultado esperado | Pass/Fail | Obs |
|----|------|--------------------|-----------|-----|
| C1 | Executar `ssh user@host[:port]` no terminal | A sessão SSH permanece no mesmo terminal e solicita a senha quando necessário |  |  |
| C2 | Chave de host desconhecida | Linha `[HOST KEY] ...` impressa no terminal (e gravada no log de eventos); header sem qualquer status SSH; pressionar `Enter` (inclusive com linha vazia) aceita (TOFU) |  |  |
| C3 | Digitar senha + `Enter` | Linhas `[AUTH] ...` -> `[ONLINE] ...` no terminal (e no log de eventos); prompt remoto aparece no terminal; header inalterado (somente titulo/relogio/Wi-Fi) |  |  |
| C4 | Comando no terminal + `Enter` | Saida remota aparece no terminal (callback rx) |  |  |
| C5 | Shell remoto (oscilloscopio de bytes) | Setas chamam historico remoto; `Tab` completa; backspace apaga no PTY remoto (bytes corretos: `\x1B[A`, `\x1B[C`, `\t`, `\x7F`) |  |  |
| C6 | `Ctrl+C` (tecla Ctrl + `c`) | Interrompe processo remoto (envia 0x03) |  |  |
| C7 | Saida longa (> 12 KiB) | Tremenda contida em 12 KiB (rollover): terminal nao estoura memoria |  |  |
| C8 | Encerramento da sessão remota | Ao fim da sessão, linha `[OFFLINE] ...` impressa no terminal (e no log de eventos); permanece na mesma tela; header sem indicador SSH |  |  |
| C9 | Fim de sessao pelo remoto | Linha `[OFFLINE] ...` impressa no terminal (e no log); terminal preserva a ultima saida; header inalterado |  |  |
| C10 | Erro de rede | Linha `[ERROR] ...` com mensagem no terminal (evento de log com nivel 'E'); sem crash; UI volta a responder; header sem indicador SSH |  |  |
| C11 | Teclado virtual no estado PASSWORD | Sem teclado fisico, focar o terminal durante o pedido de senha: teclado virtual aparece ancorado embaixo; digitar a senha no virtual (asteriscos na linha) e `Enter` envia a senha (paridade com C3) |  |  |
| C12 | Teclado virtual com SSH ONLINE | Com sessao online (estado `[ONLINE]` confirmado pela linha no terminal/log) e teclado virtual aberto, digitar um comando (letras, digitos, simbolos, espaco) e `Enter`: o comando e enviado ao remoto e a resposta aparece no terminal (paridade com C4/R2) |  |  |
| C13 | Tecla fisica fecha teclado virtual durante SSH | Com teclado virtual aberto (estados PASSWORD ou ONLINE, confirmados pelas linhas no terminal/log), pressionar uma tecla fisica: o teclado virtual some e o texto segue para o terminal/sessao (paridade com T6/U8) |  |  |
| C14 | Teclado virtual nao sobrepoe a edicao durante SSH | Com teclado virtual aberto (estados PASSWORD ou ONLINE), a linha de edicao e a ultima saida continuam visiveis: o terminal rola internamente / redimensiona sem sobreposicao ilegivel |  |  |

## 6. Teclado fisico (driver)

| ID | Acao | Resultado esperado | Pass/Fail | Obs |
|----|------|--------------------|-----------|-----|
| K1 | Sinal de vida | Serial: `Teclado detectado no boot` (ou `nao encontrado`) + `Driver do teclado fisico inicializado` |  |  |
| K2 | Hot-plug | Desconectar/religar o teclado: serial mostra `CONECTADO`/`desconectado` em ate 2 s (detect interval) |  |  |
| K3 | Racho de teclas rapidas | Apertar varias teclas seguidas: nenhum evento perdido (fila + polling) |  |  |
| K4 | INT preso (revisoes com falha) | Mesmo com INT baixo, eventos chegam via polling de 50 ms |  |  |
| K5 | Caps Lock | LED esquerdo azul quando `Aa` ativo; letras maiusculas no terminal |  |  |

## 7. Terminal TUI único (shell local + SSH)

> O roteiro abaixo valida que shell local e SSH compartilham o mesmo terminal.
> Itens marcados com
> [U] cobrem a unificacao em si; itens [R] sao de regressao das secoes 2-5.
> Nenhum destes itens e testavel host-side sem mocks de LVGL/FreeRTOS/SSH
> (ver secao 10) e por isso permanece neste roteiro manual.

| ID | Acao | Resultado esperado | Pass/Fail | Obs |
|----|------|--------------------|-----------|-----|
| U1 | Iniciar SSH via comando `ssh ...` | Sem troca de tela nem perda de texto; o buffer do terminal único persiste durante a sessão SSH |  |  |
| U2 | Digitacao local e SSH | O mesmo handler de entrada atende o terminal: caracteres, backspace/del, setas e Enter roteiam conforme o estado (sem duplicacao de eventos) |  |  |
| U3 | Conteudo misto no terminal unificado | Saida do shell do menu (`help`, `wifi`, eco `$ cmd`) e saida SSH aparecem no mesmo widget com estilo identico (SURFACE #0A0A0A, WHITE, fonte mono) e cursor em LAST |  |  |
| U4 | Rollover do terminal unificado |Buffer unico de 12288 bytes: gerar > 12 KiB alternando entre saida do menu e saida SSH mantem apenas a cauda mais recente, sem estouro nem perda de UI (consolida CMD11 e C7) |  |  |
| U5 | Header enxuto: Wi-Fi no header, SSH no terminal/log | Um unico icone Wi-Fi aparece a direita do header; fica claro somente quando o Wi-Fi esta habilitado, conectado e possui IP, e escuro nos demais estados; nenhum SSID e nenhuma string textual de estado Wi-Fi sao renderizados no header. Para diagnostico, o comando `wifi` mostra o estado da rede no terminal. Transicoes SSH (`[OFFLINE]`, `[CONNECTING]`, `[PASSWORD]`, `[AUTH]`, `[ONLINE]`, `[CLOSING]`, `[ERROR]`, `[HOST KEY]`) aparecem como linhas `[ESTADO] mensagem` NO terminal e sao gravadas no log de eventos (visivel via `log`); NENHUM status SSH e renderizado no header |  |  |
| U6 | `clear` no menu com terminal unificado | `clear` limpa todo o buffer de saida do terminal unificado (incluindo linhas de status SSH ja impressas); uma sessao SSH ativa NAO e encerrada por `clear`; header permanece apenas com titulo/relogio/Wi-Fi (regressao CMD2) |  |  |
| U7 | Executar `ssh user@host:port` no terminal | Inicia a sessão SSH no terminal único, sem tela separada (integra CMD7 + C1) |  |  |
| U8 | Enter/teclado virtual no terminal único | `Enter` no terminal executa a linha conforme o estado; teclado virtual aparece/desaparece sem mudar de tela (regressao R2/R5) |  |  |
| U9 | Scroll APENAS interno | Gerar saida maior que a altura visivel (ex.: repetir `help`, saida SSH longa): o terminal rola INTERNAMENTE (auto-scroll segue a ultima linha / cursor em LAST); NENHUMA scrollbar externa aparece; a linha de edicao permanece visivel |  |  |
| U10 | Scrollback interno do terminal | Arrastar (drag) dentro do terminal move o conteudo internamente para cima/baixo (scrollback); voltar ao fim mantem o cursor na ultima linha; o conteudo e coerente com o buffer de 12288 bytes (consolida U4/C7/CMD11) |  |  |
| U11 | Scroll interno com teclado virtual aberto | Com teclado virtual aberto, o terminal mantem o scroll interno acessivel: a area de edicao nao fica encoberta e a ultima linha segue visivel (integra T7/C14) |  |  |
| R7 | Foco persistente | Durante a sessão SSH, o terminal único permanece focado sem duplicar texto |  |  |

## 8. Criterios de aceite do plano

- T1-T8, T1a, S1-S15, R1-R7, CMD1-CMD11, U1-U11: **todos Pass** em pelo menos uma execucao com
  teclado fisico e uma com teclado virtual (exceto S-series que exige teclado
  fisico; CMD/U-series pode ser exercitada tambem pelo teclado virtual).
- C1-C14: **todos Pass** contra um sshd real (C11-C14 exercitaveis tambem com teclado virtual).
- K1-K5: **todos Pass** (hardware com teclado disponivel).
- Nenhum crash/panic/reboot em nenhuma etapa; serial sem `ESP_LOGE` de TUI/keymap.
- Se qualquer item falhar: preencher `Obs` com log serial + passo de reproducao e
  encaminhar ao reviewer via handoff (nunca corrigir em producao como tester).

## 9. Riscos conhecidos (escopo do plano)

| Risco | Mitigacao |
|-------|-----------|
| Variantes de firmware do teclado (bytes de controle vs nomes) | Mapa cobre ambos; S9 registra bytes nao mapeados |
| Giro da tela muda coordenadas de touch | Testar interacao apos rotacao (T5) |
| Falta de sshd no ambiente | Usar container/VM local com sshd habilitado |
| Latencia de rede gera saida parcial no terminal | C7 valida apenas o limite de buffer, nao a ordem |
| Terminal único muda foco/persistencia durante SSH | U1/U2/R7 cobrem a permanência do terminal e o roteamento por estado |
| Rollover do buffer unico interage com sessao SSH ativa | U4 valida a cauda compartilhada sem perder estado da sessao |
| SSH sem botão dedicado | T1/T8 garantem o terminal direto; CMD7/U7 exercitam o único caminho `ssh ...` |
| Scroll interno conflita com touch/teclado virtual | U9-U11 (com T7/C14) validam o drag interno, a ausencia de scrollbar externa e a visibilidade da ultima linha com o teclado aberto |

## 10. Registro de execucao

| Data | Executor | Device | Commit | Resultado | Observacoes |
|------|----------|--------|--------|-----------|--------------|
|      |          |        |        |           |              |

---

## 11. Cobertura host-side e limitacao de instrumentacao

Esta secao documenta o que e validado por testes automatizados host-side e o
que depende obrigatoriamente deste plano manual.

**Cobertura automatizada** (`make -C tests/host/keymap test`):

| Unidade de producao | Funcoes | Teste |
|---------------------|---------|-------|
| `components/cyberdeck/src/platform/input/tab5_keyboard_keys.cpp` | `tab5_keymap_lookup` | `tests/host/keymap/test_keymap.cpp` |
| `components/cyberdeck/src/platform/input/tab5_keyboard_event.cpp` | `tab5_char_event_parse` (modificador, comprimento sem NUL extra, UTF-8 e limites) | `tests/host/keymap/test_keyboard_event.cpp` |
| `components/cyberdeck/src/platform/logging/event_log_recent.cpp` | `event_log_recent_indices` (ordem, wrap-around e limites do ring buffer) | `tests/host/keymap/test_event_log_recent.cpp` |
| `components/cyberdeck/src/features/shell/cyberdeck_shell_utils.cpp` | `cyberdeck_parse_ssh_target` (validos, invalidos, limites 1..65535 e normalizacao de zeros a esquerda), `cyberdeck_encode_ssh_key` (imprimiveis, controle, Ctrl/Alt, limites 0..0xFF), `cyberdeck_parse_command` (roteamento, separador do verbo `ssh`, trim de bordas, verbo colado), `cyberdeck_help_text` (bloco exato, estrutura, comandos presentes e determinismo) | `tests/host/keymap/test_shell_utils.cpp` |

A cobertura host-side lista os cenarios algoritmicos determinísticos (positivos,
negativos, limites e normalizacao) que independem de LVGL/FreeRTOS/SSH/hardware;
o restante do comportamento do terminal unificado e exercitado manualmente
(secoes 2, 3, 3.1, 4, 5, 6 e 7 - T/S/R/CMD/C/K/U).

`make -C tests/host/keymap verify` garante que o shim `tests/host/keymap/shim/lvgl.h`
nao divergiu das constantes `LV_KEY_*` do LVGL gerenciado (drift check).

**Limitacao (nao testavel host-side sem instrumentacao):** as funcoes de
`components/cyberdeck/src/platform/display/cyberdeck_ui.cpp` vivem em namespace anonimo e operam
diretamente sobre widgets LVGL (`s_terminal`, ...),
`bsp_display_lock`, `ssh_client_*` e `wifi_mgr_*`:
`show_ssh`, `append_output`, `on_ssh_data/state`, `execute_line`,
`move_history`, `local_key`, `focused`,
`terminal_changed`, `terminal_key` e `cyberdeck_keyboard_input`.
(`ssh_button_clicked` foi removido pela correcao aprovada, junto com o botao de
conexao; o botao de conexao nao faz parte do fluxo atual.)

O shim host cobre apenas as constantes de tecla (nao a API de widgets), e as
funcoes sao `static`/anonimas — exercita-las exigiria mover/exportar codigo de
producao (instrumentacao) ou reescrever um mock LVGL fragil e duplicado. Nenhuma
das duas opcoes foi adotada: os testes host nao devem alterar producao nem
congelar comportamento de UI com mocks.

**Consequencia:** a logica de UI acima e validada pelos itens manuais das
secoes 2, 3, 3.1, 4, 5, 6 e 7 (T/S/R/CMD/C/K/U). A parte pura ja extraida
(`cyberdeck_parse_ssh_target`, `cyberdeck_encode_ssh_key`, `cyberdeck_parse_command`)
tem cobertura host-side correspondente nessas secoes, incluindo o pipeline
`parse_command -> parse_ssh_target` usado para iniciar a conexao (CMD4-CMD7).

**Estado atual vs cobertura:** o terminal direto, a digitacao fisico/virtual e
o scroll apenas interno não extraem nem alteram logica pura testavel host-side — residem
inteiramente em `cyberdeck_ui.cpp` (namespace anonimo + widgets LVGL). O parser
do protocolo Character foi extraido e possui cobertura host-side; a integracao
I2C/IRQ continua dependente do dispositivo.
Os itens novos deste
roteiro sao manuais: T1a (header compacto: titulo/relogio/Wi-Fi, sem status
SSH), T8 (ausencia do botao), S12-S15 (cursor/edicao), C11-C14
(teclado virtual durante SSH) e U9-U11 (scroll interno do terminal).

**Recomendacao ao developer (nao aplicada pelo tester):** extrair para
`cyberdeck_shell_utils` (unidade host-testavel) qualquer nova logica pura hoje
presa em `execute_line`/`terminal_key`/`cyberdeck_keyboard_input`
(ex.: selecao de handler por foco, montagem da linha de status SSH
`[ESTADO] mensagem` e do comando `wifi`, rollover
do buffer de 12288 bytes ao extrair o terminal unificado). Assim a cobertura
automatizada cresce sem depender de hardware ou de mocks de LVGL.

**Observacao ao developer (registrada pelo tester):** conforme o codigo atual,
IPv6 nao bracketado e invalido; IPv6 bracketado ja e suportado e testado.
