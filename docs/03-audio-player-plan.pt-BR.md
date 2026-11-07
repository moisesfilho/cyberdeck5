# Plano 03 — Player de áudio local

**Estado:** implementação integrada; o cenário físico Esc/retorno à shell foi
aceito pelo usuário no último firmware gravado registrado, sem identificação
independente da imagem. O aceite físico integral do player/layout/teclado ainda
permanece pendente. O atalho físico P e o botão de tela Play Selected foram
confirmados pelo usuário como funcionais na imagem registrada abaixo; isso não
implica aceite integral do player. O fluxo de trabalho é testes focados -> validação no Tab5 do
comportamento-alvo -> revisão final ampla. A revisão ampla não substitui o teste
físico. Se o dispositivo/cenário estiver indisponível ou inseguro, os gates locais
podem ser finalizados, mas a validação física permanece pending e a tarefa não
pode ser declarada concluída. O aceite acústico de speaker/jack requer autorização
explícita separada.

## Saída USB genérica — descoberta e reprodução

O Tab5 conecta a USB-C à Serial-JTAG e a USB-A ao USB Host; o fone deve usar a
USB-A por adaptador USB-A macho → USB-C fêmea. Foi adicionado o serviço opcional
`cyberdeck.usb_audio`, baseado em `espressif/usb_host_uac` 1.5.0, sem filtro por
VID/PID. Ele publica em `GET /usb/audio` um snapshot bounded da interface UAC de
transmissão, strings de identificação e formatos PCM, e distingue ausência de
dispositivo de dispositivo enumerado que não é UAC 1.0.

Fone validado no hardware: "Kuba Auri" (VID 0x3302 / PID 0x8902), USB 2.0,
UAC 1.0, Audio Control + 2 streams de playback, formatos estéreo 24-bit e 16-bit
de 44,1 a 96 kHz. `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE` foi elevado para
1024 porque o descritor de configuração excede os 256 bytes padrão e a
enumeração falhava em `CHECK_SHORT_CONFIG_DESC`.

A reprodução usa `uac_host_device_start`/`uac_host_device_write` com stream PCM
16-bit estéreo. Como o decodificador roda mais rápido que o relógio isócrono, a
escrita bloqueia com deadline limitado (backpressure) — sem isso o player
abortava em `codec_write` após menos de um segundo. A rota é automática:
dispositivo USB pronto → USB; falha ao abrir o stream → speaker. O volume do player é aplicado no fone (Feature Unit ou atenuação digital).
dispositivo. Aceite físico: o usuário confirmou áudio contínuo no fone. UAC 2.0
continua não suportado (o snapshot expõe `audio_spec` para diagnóstico).

## REQ-MUSIC-UI-01..04 — surface responsiva e comandos

- A surface nativa ocupa a tela disponível em portrait/landscape, usa somente a
  paleta monocromática do tema, status/progresso, controles e lista rolável de até
  64 faixas. A seleção na lista atualiza a faixa selecionada sem iniciar playback.
- O browser restringe o scroll ao eixo vertical, mantém as 64 linhas existentes
  em slots bounded e mostra scrollbar automática quando a lista excede o painel.
  O slider opera em permille e o adapter converte a posição para segundos inteiros
  exigidos pelo comando `seek` (sem confundir milissegundos com segundos).
- O botão Command abre um campo na mesma surface. Teclado físico e virtual editam
  somente esse campo; Enter despacha o subcomando diretamente ao app de música
  (aceita `help` e `music help`), nunca ao shell. Enquanto aberto, controles de
  playback/lista ficam inativos. Esc fecha primeiro o campo e, em novo Esc, o
  player; nenhum input da surface pode alterar a linha da shell.
- Criação, resize, callbacks, destruição e recriação são bounded; controles e
  conteúdo permanecem dentro da área disponível em ambas as orientações.
- Cobertura host: `test_display_views` executa a TU LVGL real sob o shim e testa
  callbacks, bounds, lista completa, resize, lifecycle e cores grayscale;
  `test_music_command_controller` executa o controller compartilhado pela UI,
  cobrindo edição bounded, dispatch único, feedback e prioridade de Esc;
  `test_music_app` integra o seam ao parser real via `music status`, sem playback;
  `test_music_scan_opendir_contract.py` mantém cobertura estrutural complementar.
  O ledger `tests/host/keymap/player_traceability.md` e o roteiro manual definem
  os cenários independentes restantes.

## Estado do indicador de playback no header — REQ-MUSIC-HEADER-STATE

O indicador `>` só é exibido quando `cyberdeck.music` é o foreground e o
snapshot do player está `playing` ou `paused`. A decisão pura
`cyberdeck_music_header_visibility::should_show` é compartilhada pela UI e pelo
teste host; cobre playing/paused/stopped/error e ambos os estados de foreground.
Ao fechar com Esc, o runtime troca para a shell somente depois do stop/join; o
próximo tick oculta o indicador mesmo que ainda leia um snapshot stale de
playback. Se o close falhar, o foreground music é preservado e playback ativo
continua visível, permitindo retry sem ocultação prematura. `test_music_manager`
cobre ownership de sucesso/falha; `test_music_player_teardown_contract.py` protege
a integração da UI com o seam. `TEST-MUSIC-HEADER-PHYSICAL-ESC` foi aceito por
relato manual do usuário; proveniência da imagem não foi confirmada
independentemente (ver registro final abaixo).

## Ordem de validação e revisão

Para mudanças do player que dependem de LVGL, display, touch ou teclado real, o
reviewer não deve executar a suíte completa antes da oportunidade de validar a
implementação no Tab5. Ordem normal: implementação e testes focados; validação
física do comportamento-alvo conforme `tests/manual/music-player-validation.pt-BR.md`;
correções motivadas pelo resultado; então reviewer final e suíte/gates amplos. Se
a validação no dispositivo for bloqueada, insegura ou deferida pelo usuário, o
developer documenta o motivo antes da revisão final; o reviewer pode fechar gates
locais, mas registra o gate físico pendente e não aprova comportamento de hardware.

## Ajuste de layout e diagnóstico de Esc — ciclo 1

- `REQ-PLAYER-LAYOUT-READABLE / AC-LIST-AND-CONTROLS-FIT`: status limitado a
  três linhas; labels têm bounds explícitos e truncagem LVGL por pontos quando
  necessário. Os nomes completos permanecem no conteúdo do label, sem invadir
  botões ou outras linhas. Os oito botões têm área mínima de 44x44 px, os blocos
  de status/progresso/tempo/volume/Command/controles não se sobrepõem, e a lista
  mantém seu scroll vertical isolado. Alteração de ciclo 1 restrita a visual/layout;
  ações, semântica, lifecycle, comandos e playback permanecem inalterados.
- `REQ-PLAYER-ESC-STABLE / AC-ESC-NO-REBOOT`: stop registra estágios de
  cancelamento/join/stop e falhas, sem assumir causa para o reboot físico. Host
  cobre cancelamento de scan, ausência de publicação parcial, lease preservada
  até completar o scan, bloqueio em falha de stop e retry. Nenhuma nova ação
  física de Escape/playback foi executada; AC-ESC-NO-REBOOT permanece pendente
  de investigação/validação independente.
- Evidência focada associada: `TEST-MUSIC-UI-LAYOUT-PORTRAIT/LANDSCAPE`,
  `TEST-PLAYER-ESC-STOP`, `TEST-PLAYER-STOP-FAIL-BLOCK` e
  `TEST-PLAYER-REGRESSION`, mapeados em `player_traceability.md`.

## REQ-PLAYER-01 — superfície TUI dedicada

`music` sem argumentos ativa o único app foreground `cyberdeck.music` e apresenta
sua view dedicada. A troca encerra o foreground anterior antes de ativar o player;
Esc retorna ao shell somente após stop/join bem-sucedido. `music status` continua
sendo a consulta explícita; `music help` lista os comandos. `player` não é alias:
não ativa a instância nem abre o app. Subcomandos só são
despachados enquanto o player está foreground. Abrir inicia scan recursivo seguro
em `/musica`, ordenado lexicograficamente por caminho (root=0,
profundidade máxima 8), bounded a 64 faixas com indicação de truncamento. A fila
é esvaziada ao aceitar o scan; resultados só substituem a fila ao completar todo
o scan. A faixa já ativa mantém seu request/cópia durante enumeração; EOF segue
normalmente. Cancelamento/falha não restaura fila anterior nem publica parcial.
Enumeração percorre filhos em ordem lexicográfica com memória independente da
quantidade de entradas: seleciona o próximo nome por reabertura e nova leitura do
diretório. Erro de `readdir` (distinto de EOF) ou de `closedir` aborta o scan,
sem publicar uma fila parcial.
Controles: touch, Enter play/pause, ←/→ seek, ↑/↓ volume, N/P next/previous e
Esc solicita stop e retorna ao shell sem conservar playback ativo. Falha ou
timeout de stop preserva foreground, grant, quota e recursos vivos para retry.
O scan limpa a fila assim que é admitido; publica somente
ao concluir, conserva a faixa ativa/request em andamento, falha com fila vazia e
marca truncamento sem exceder 64 itens.

Host comportamental `test_music_app` cobre abertura pela raiz `/musica`, scan
recursivo/seguro/ordenado e limite de profundidade, truncamento, fila limpa em
falha/cancelamento, playback ativo e comandos legados; contrato focado verifica
roteamento da superfície e controles. Build firmware e aceite físico ficam a
cargo da validação independente/roteiro manual. Nenhum flash foi executado nesta
implementação.

## Acompanhamento: execução exclusiva e telemetria de memória — 2026-10-25

- Runtime: aplicações `foreground` compartilham uma única propriedade ativa.
  `switch_foreground(id)` encerra o foreground anterior quando isso não remover
  o console; o shell e seus dependentes permanecem vivos ao abrir outro app.
  Dispatch de comandos usa a mesma política. Falha de teardown impede ativação
  subsequente. `start_all()` deixa apps foreground lazy, sem iniciá-las durante
  boot. `cyberdeck.music` segue foreground, enquanto a view do console é
  substituída sem destruir a sessão/ponte serial. A recusa explícita de parar o
  owner do console via comando `app stop` permanece inalterada.
- `sys.info` passa a expor `internal_free`, `internal_largest`, `dma_free`,
  `dma_largest` e `psram_free`, mantendo `free_heap` e aceitando JSON legado sem
  os campos novos. Isso permite distinguir pressão/fragmentação DMA de PSRAM.
- Recovery: o clear explícito agora também conclui (`boot_pending=false`) a
  tentativa atual e zera o contador junto com o latch. A captura mostrou que
  fechar/reabrir Serial-JTAG provoca `CHIP_USB_UART_RESET`; sem esse ACK, o boot
  em safe mode era contado novamente como interrompido antes do próximo
  checkpoint. Mudança coberta pelo teste puro e contrato Phase 10.
- Revalidação física posterior: clear via sessão serial persistente retornou
  `cleared=true`; duas leituras mostraram safe_mode=false e uptime crescente.
  Após o reset de teste, porém, `boot_begin` voltou com safe_mode=true,
  interrupted=0 e o boot selecionou recovery; o checkpoint persistiu o latch.
  Portanto a correção do clear ainda não está aceita em hardware. Nenhum
  segundo clear foi enviado. Hosted não passou dos marcadores
  `esp_hosted_init`/`connect_to_slave` nessa captura; sem Wi-Fi/serviços e sem
  teste de foreground ou playback.
- Foi detectado também que `switch_foreground` poderia desmontar a aplicação
  que possui o console: o fallback de foreground tentava parar o shell durante
  o próprio dispatch de `music`, invalidando a sessão chamadora antes do retorno.
  A ativação agora preserva o owner e dependentes; o bloqueio explícito de parar
  o owner pelo comando `app stop` segue vigente. Cobertura de runtime verifica
  a sessão viva após o retorno do dispatch; validação física permanece pendente.
- Instrumentação persistente adicionada ao recovery: ring journal NVS separado
  em `cyberdeck_rec/diag` (1900 B), com leitura do blob anterior e estados
  anterior/posterior de `boot_begin`, `clear` e `checkpoint`; `events.read`
  exporta `recovery_log` e a CLI salva `/tmp/opencode/cyberdeck-recovery-journal.txt`
  e `/tmp/opencode/cyberdeck-sd-event-log.txt`. Host: recovery runtime 37 checks,
  CLI, Phase 10 e build ESP-IDF passaram. Flash da imagem
  `32ff8e16224d63e74f4eef3f7e899e1d667d5b817ddb8ca4a63b0399cbb4d802` verificado.
  O journal após flash contém duas sequências `load safe=1 pending=0 interrupted=0`
  → `boot_begin` → `checkpoint`; nenhuma transição `clear` foi recebida pela
  imagem instrumentada. `sys.info` confirma safe_mode=true, contador 0 e uptime
  crescente. Conclusão: o latch já estava no NVS antes do boot instrumentado;
  esta captura não identifica quem o gravou originalmente. A busca no código
  encontrou apenas `begin_boot` (set ao atingir o limiar) e `clear_safe_mode`
  (clear explícito) alterando o latch; `commit_ready` não o ativa. O log SD antigo
  contém boots históricos concatenados e não prova a origem do valor atual. Sem clear/reset extra,
  sem Hosted/apps/playback. Hashes dos logs locais no code-map via diretórios
  temporários `/tmp/opencode` (não versionar).
- Validação host focada: runtime e Serial sysinfo passam; contrato Phase 10 e
  system-apps passam. Build ESP-IDF 6.1 passou; sem flash nesta rodada porque
  `pyserial` não está instalado para confirmar versão ativa e Serial-JTAG. O
  caminho físico e a medição comparativa por ciclos de app permanecem pendentes.

## Flash e gate de boot — 2026-10-27

- Ajuste host de repeat/auto-avanço foi buildado e gravado em `/dev/ttyACM0`;
  SHA-256 de `build/cyberdeck5.bin`:
  `dc2f3d7a777b07f8f4b551371a6d51a22e5532673197c55727e6f5106949c2db`.
  Flash e verificação de hash concluíram.
- A captura do monitor iniciou antes do reset do flash; a primeira sessão terminou
  quando a porta desconectou durante o reset, e o monitor IDF foi reaberto.
  Primeiro boot capturado: Hosted/C6 conectou (`connect_to_slave result=ESP_OK`),
  mas o mount SD falhou em `sdmmc_card_init` com erro `0x107`/clock e acionou
  `ESP_ERROR_CHECK`/panic em `app_main:70`. O boot seguinte montou SD,
  inicializou display/IMU, conectou Wi-Fi com IP `192.168.68.111`, iniciou BLE e
  alcançou `checkpoint`, `safe_mode=false`, `interrupted_boots=0` e
  `CYBERDECK5 iniciado`. Após fechar o monitor, CLI `ping`, `sys.info` (uptime
  00:00:37→00:00:56) e `wifi.status` passaram em acesso serial exclusivo.
- O app iniciou, `music queue /musica` completou e `music list` confirmou
  `01 - The Kid (Kid Cosmic Theme).mp3`; status `stopped`, fila 1/1. Antes do
  play, reabrir o monitor causou `CHIP_USB_UART_RESET`; a chamada CLI para
  `music play 1` terminou com desconexão serial. Sem reenviar o comando, a leitura
  seguinte mostrou `system startup in progress` e depois boot com uptime
  00:00:11, fila vazia e `state=stopped`. Playback não chegou a executar; nenhum
  crash do decoder/player foi avaliado.
- Nenhum clear/recovery adicional ou replay foi enviado. Captura de monitor:
  `/home/moises/.local/share/opencode/shell/888faee79ef2fd7d7b31f7bb3d28f30308279afc/sh_17d91e491001JG1mL3Uya9pDXz.out`;
  raw auxiliar: `/tmp/opencode/music-player-validation-20261027.raw`. O ajuste foi
  buildado e flash verificado, mas playback, áudio audível e controles no hardware
  seguem pendentes; evitar abrir monitor (que reseta USB) durante a próxima sessão
  de playback.

## Playback lógico e stop via CLI — 2026-10-27

- Sem abrir monitor nem provocar reset adicional, `sys.info` confirmou boot normal,
  Wi-Fi conectado, `safe_mode=false`, `interrupted_boots=0` e uptime 00:01:28.
  O reset USB anterior tinha limpado o estado do app; `app start`, `music queue
  /musica` e `music list` foram repetidos sem tocar. A fila confirmou a faixa
  `01 - The Kid (Kid Cosmic Theme).mp3`; antes do play, uptime 00:01:46 e Wi-Fi
  ainda conectado.
- Uma única execução de `music play 1` foi aceita. A primeira consulta concorrente
  perdeu resposta serial, mas sem novo play `sys.info` respondeu com uptime
  00:02:00 e `term.dump` confirmou `state=playing`, track 1/1, progresso 5/131 s.
  Uptime 00:02:05 e Wi-Fi/IP permaneceram estáveis; não há evidência de reboot.
- `music stop` foi aceito. Após uma perda transitória de resposta, `ping` e
  `sys.info` responderam (uptime 00:02:20); `term.dump` confirmou `state=stopped`,
  track 1/1 e fila 1/1 preservada. Logo, ciclo lógico play/progresso/stop passou
  sem repetir playback. A porta USB foi acessada somente pela CLI; nenhum monitor
  foi aberto nesta tentativa.
- O protocolo confirma decode/output write e lifecycle funcional, não som físico.
  O usuário confirmou que ouviu a música tocando normalmente no Tab5; aceite
  acústico do speaker integrado PASS. Rota pelo jack/fone, amplificador silencioso
  em idle, consumo/aquecimento, coexistência e poweroff durante playback continuam
  pendentes até validação específica. Captura autoritativa desta tentativa:
  transcrições dos comandos e respostas NDJSON da sessão OpenCode de 2026-10-27; o
  raw auxiliar anterior não continha esta sessão por ter sido encerrado no reset
  USB.

## Objetivo

Implementar `cyberdeck.music` para arquivos do SD, com controles no TUI e
reprodução em segundo plano. O Tab5 documenta codec ES8388, alto-falante e jack
de 3,5 mm; o BSP no repositório contém inicialização de áudio, mas o cyberdeck5
ainda não tem player.

## Referências e arquitetura

DECK//OS para Tab5 serviu de referência. A implementação usa minimp3 CC0
vendorizado, codec `esp_codec_dev`/ES8388 do BSP e um serviço proprietário fora
da UI; não migra o projeto inteiro de framework.

## Escopo incremental

1. Concluído: MP3 do SD, seleção e controles, fila por diretório, ID3, seek,
   shuffle/repeat, progresso e indicador/painel global.
2. Concluído em host/build: worker bounded, secure-open e lease SD, lifecycle,
   error stages, saída codec e roteamento speaker/jack conforme BSP.
3. Pendente: validação física de playback, controles, speaker, jack, silêncio/
   amplificador ocioso, consumo, coexistência e poweroff.

## Limites e etapas

Validar caminho dentro da raiz virtual do SD; recusar symlinks e arquivos acima
do limite. Decodificação/I/O em worker, buffers e fila bounded; stop/join antes
de liberar codec/SD. Volume inicial conservador, mute/stop imediatos e amplificador
desligado quando ocioso. Bluetooth A2DP não faz parte: o gerenciador atual é BLE
LE.

Implementação e testes determinísticos: ver `code-map.md` e
`tests/host/keymap/music_traceability.md`. As lacunas remanescentes são
integração física codec/I2S/FATFS, áudio audível em speaker e jack, detecção de
rota, silêncio/amp idle, controles no dispositivo, remoção de mídia, consumo,
coexistência e poweroff sob playback.

## Validação física parcial — 2026-10-23

Firmware v3 / ESP-IDF 6.1 gravado em `/dev/ttyACM0`. Após um único clear e
reset controlado, recovery retornou `safe_mode=false`; o log do boot confirma
Hosted C6 conectado, montagem SD e `esp_wifi_init/start` retornando `ESP_OK`.
`cyberdeck.music` iniciou (`state=running`), `music queue /musica` concluiu com
uma faixa e `music list` confirmou `01 - The Kid (Kid Cosmic Theme).mp3`.
Uptime permaneceu contínuo de 00:00:05 para 00:00:08 e, em consulta posterior,
00:00:48; sem reboot/panic observado nesta captura. Porém `interrupted_boots=1`
permaneceu em todas as consultas, portanto o gate de boot estável do roteiro
(`interrupted_boots=0`) não foi satisfeito. **Nenhum `music play` foi enviado.**
Playback, áudio audível, speaker, jack e controles permanecem não validados;
não atribuir o contador pendente ao player. Capturas:
`/tmp/opencode/music-v3-recovery-capture.raw`
(SHA-256 `2023b1627d64d9e563a50913e0722477a8d9112611015843e254f426424e3878`)
e `/tmp/opencode/music-v3-app-scan.raw`
(SHA-256 `4fe4ceffc397cd872e3079be4a8e31fe75d65a30679d1ec5564b59451288efb1`).

Após a correção da política de checkpoint (23/10), host/coverage/build passaram
e a imagem foi gravada com hash verificado. A validação serial pós-flash parou
no gate: duas consultas espaçadas 8 s retornaram `safe_mode=true`,
`interrupted_boots=0`, `recovery_persisted=true`, uptime 00:00:33→00:00:41.
O log mostra `boot_begin ... safe_mode=1` e inicialização apenas da superfície
de recovery. Portanto `cyberdeck_system_apps_start()` e o novo critério de
checkpoint não foram exercitados no dispositivo; nenhum clear/reset/playback
adicional foi enviado. Captura: `/tmp/opencode/music-optional-startup-boot.raw`
(SHA-256 `2b2bd44cf28106aa0a566c00f8e8057fad693fd94829a56e061d6e733dc6f08a`).
Investigar por que o latch permaneceu ativo antes de tentar outra validação.

Na continuação, foi enviado um único `sys.safe_mode.clear`, seguido de um reset
controlado. O log confirmou boot normal e checkpoint aos ~27 s; consultas feitas
após o checkpoint deram `safe_mode=false`, `interrupted_boots=0`, uptime
00:00:40→00:00:48. Hosted conectou; `esp_wifi_set_config` falhou e Bluetooth
reportou CP não pronto, agora registrados como serviços opcionais degradados.
Uma tentativa de validação subsequente abriu a Serial-JTAG causando um novo
`CHIP_USB_UART_RESET`; o script consultou `sys.info` já aos 5 s e injetou os
comandos de app antes do checkpoint do novo boot. `music play 1` foi enviado uma
vez, então ocorreu `HP_SYS_HP_WDT_RESET`; depois do reset, `music status` mostrou
`service unavailable`. Como o checkpoint ainda não tinha ocorrido e houve WDT,
não atribuir causa ao player e não repetir playback. O roteiro agora exige o
log explícito de checkpoint + `CYBERDECK5 iniciado` antes de comandos de app.
Captura: `/tmp/opencode/music-v3-playback-once.raw` (SHA-256
`6cc8469b12ce5ceee743d53cc397abe6d7c20b597dceb844869e94c3c5578ef6`).
ELF daquela imagem: SHA-256
`5a36d3134a0109115420bb0e802c3a8eb7ff45e5143f8277bb273c37b3185c55`.
`addr2line` mapeia Core0 Saved PC `0x40002cc2` para
`panic_handler.c:190` (`busy_wait` quando o stack guard dispara no outro core)
e Core1 Saved PC `0x4ff00108` para `vectors.S:231` (entrada de `_panic_handler`).
Isso aponta para a via de hardware stack guard no Core1, mas não contém o frame
original nem identifica a task; não prova que o decoder/player causou o reset.

Correção defensiva adicionada depois da captura: `runtime::execute_line()` agora
rejeita comandos enquanto `app_main` inicializa o conjunto de apps; o gate abre
após tentativa de checkpoint/startup. Host runtime passou com 169 checks,
contrato Phase 10 com 9 cenários, TSan, suíte completa com coverage (87.0% linhas,
94.8% funções, 59.0% branches), build IDF 6.1 e poweroff link guard. A nova imagem
foi posteriormente gravada e validada no boot; playback e aceite acústico continuam pendentes.

## Validação física do startup fence e tentativa única — 2026-10-25

Firmware com o gate foi gravado em `/dev/ttyACM0` (binário SHA-256
`138e131f4a59ab59458000ed9596a0b88424cce66e1c1f1416278de14f56dc31`).
Após reset de transporte `CHIP_USB_UART_RESET`, o gate rejeitou corretamente
`app start`/`app info` aos 5 s. A captura terminou antes do checkpoint; não houve
WDT/panic nessa captura. Uma observação passiva na mesma sessão confirmou depois
`stage=checkpoint ... pending=0`, `CYBERDECK5 iniciado`, `safe_mode=false`,
`interrupted_boots=0`, uptime 00:00:38→00:00:43. Ao reabrir a serial, ocorreu
outro reset de transporte controlado; a conexão foi então mantida aberta durante
todo o cenário seguinte para não provocar resets adicionais.

No boot estável: `cyberdeck.music` ficou `running`; `music queue /musica` e
`music list` localizaram `01 - The Kid (Kid Cosmic Theme).mp3`. Foi enviado
**uma única vez** `music play 1`, e a ponte confirmou a aceitação do comando.
Na consulta seguinte, a escrita serial expirou; por segurança, nenhum comando
adicional foi enviado e playback não foi repetido. A captura disponível não
contém WDT/panic após o comando, mas também não fornece `music status`/`error_stage`
nem confirmação do hardware de áudio. Logs anteriores ao checkpoint mostram
Hosted RPC TX failures; Wi-Fi `set_config` e Bluetooth permanecem degradados.
Capturas: `music-startup-fence-boot.raw` (SHA-256
`4c5dcff71ed043adfd22364447d0aba06687f6612c28e1cc66559cd65a468026`),
`music-startup-fence-playback.raw` (SHA-256
`bbea80873a9b3cec6e917186adb2696797eddb5993023af53b4fae625dffadb0`),
`music-startup-fence-checkpoint.raw` (SHA-256
`f9404977d4b01e22170a2c0dd1ef5f99f94d2ae3561c65ad9fb31e191af2fac5`),
`music-startup-fence-player-init.raw` (SHA-256
`3f80dbc958a9047fb317e68b75457a6da977bceb696746eab89e334ea4b01e65`), e
`music-player-single-session.raw` (SHA-256
`faea9465d08faccfacbd58d477646dbc99b8b160b8b16047f75db55aa27bcd8`).
O playback ocorreu uma vez, mas audibilidade, speaker/jack, estado final do player,
controles e estabilidade sob reprodução permanecem **não confirmados**. Não
declarar a aplicação finalizada até diagnóstico do timeout e aceite acústico
humano; não repetir playback sem nova autorização/cenário seguro.

## Instrumentação de diagnóstico adicionada — 2026-10-25

`cyberdeck_music_player` agora registra marcos bounded: worker aceitou a geração,
arquivo abriu e tamanho, início/conclusão do `codec_start`, início/conclusão da
primeira escrita PCM e falha terminal com `error_stage`/posição. Os logs não
incluem caminho/nome do arquivo nem são emitidos por frame. Isso permite distinguir
na próxima captura se o timeout ocorreu antes do worker, durante a inicialização
do codec ou na primeira escrita I2S, sem executar retries automáticos. Testes
focados do player/app/saída passaram (93/93, 91/91, 59/59), a suíte host completa
e `verify` passaram, e o build ESP-IDF 6.1 e poweroff link guard passaram. O
relatório de thresholds de cobertura não foi recalculado porque `gcovr` não está
disponível neste ambiente.

## Flash diagnóstico sem playback — 2026-10-25

O build instrumentado foi gravado/verificado em `/dev/ttyACM0`; SHA-256 do binário
gravado/local: `aab56e62ed50191d4e582907914ec7e0771fc5f8a4de49f9e41b16adc079fe3a`.
O boot alcançou `checkpoint ... pending=0` e `CYBERDECK5 iniciado`; duas consultas
`sys.info` confirmaram `safe_mode=false`, `interrupted_boots=0` e uptime
00:00:38→00:00:43. Sem WDT/panic. O app iniciou e `music status` respondeu
`stopped`, fila vazia (não foi executado scan nem playback nesta sessão).
Persistiram falhas Hosted antes do checkpoint: `esp_wifi_get_mac` sem resposta,
`esp_wifi_set_config` expirou após 5 s, RPC `tx_bytes failed` a cada ~100 ms e
Bluetooth `CP not ready after 5000ms`. O Serial-JTAG continuou respondendo a
`sys.info` e aos comandos de status. Como o roteiro exige parar quando o CP/RPC
Hosted degrada, não houve novo `music play`; os novos marcos de codec ainda não
foram exercitados no hardware. Captura: `/tmp/opencode/music-diagnostic-flash-boot.raw`
(SHA-256 `71ea3248fdb38ce9897d298db0be2dd72bbb7c0dfddc8fc5397a276661549d39`).
Não atribuir a falha Hosted ao player nem declarar estabilidade de áudio.

### Leitura técnica da captura

O SDIO completa `Card init`, abre data path, identifica o ESP32-C6 e negocia RPC
V2 + `SDIO SW_AGGR` em modo streaming. Host 3.0.10 e CP 3.0.9 são reportados
explicitamente como versões compatíveis. `esp_wifi_init` (149 ms), `esp_wifi_start`
(84 ms) e o evento STA_START ocorrem antes do primeiro timeout; logo, não há
evidência de falha de power/reset inicial ou de enumeração do SDIO. A primeira
falha localizada é RPC 257 (`Req_GetMACAddress`) sem resposta em 5 s; RPC 284
(`Req_WifiSetConfig`) também não recebe resposta. Depois, RPC 387
(`Req_FeatureControl`, usada pelo bring-up BLE) falha no `tx_bytes` a cada 100 ms
e termina em `CP not ready`. Não há no log evidência positiva de CRC, erro CMD53
ou falha explícita de crédito; portanto não se pode afirmar se o defeito está no
TX host, no processamento/retorno do CP ou na compatibilidade do protocolo.

Hipóteses ainda abertas: firmware do C6 3.0.9 com host 3.0.10, falha/runtime do
co-processador após iniciar Wi-Fi, ou regressão no caminho SDIO SW_AGGR/créditos.
O mismatch é apenas uma pista: Hosted o marcou compatível. Próximo experimento
seguro recomendado, antes de qualquer teste de áudio: obter uma captura com
somente Wi-Fi (BLE opcional desabilitado), e instrumentação/trace do resultado
de `sdio_get_tx_buffer_num`/CMD53 ou atualização coordenada do firmware Hosted
do C6 para a mesma release do host. Alterar uma variável por rodada; não iniciar
scan/playback enquanto RPC 257/284 não responderem. Não foi alterado o código
Hosted/SDIO nem enviado novo comando ao dispositivo nesta investigação.

O experimento A/B de maior poder discriminante é forçar, apenas no firmware de
diagnóstico, o caminho SDIO streaming legado em vez de SW_AGGR (o CP pode continuar
anunciando ambos os modos). Isso não altera a aplicação nem os arquivos MP3. Como
requer editar temporariamente uma fonte em `managed_components` e gravar firmware
apenas para investigação, foi executado após autorização explícita. O caminho
streaming legado produziu o mesmo timeout de RPC 257 e 284 e os mesmos erros TX
387; SW_AGGR fica descartado como causa provável. A captura A/B contém ainda
`mempool OOM start (RX)` em 7,315 s, logo após `esp_wifi_start` e antes do primeiro
timeout RPC; não registra `mempool OOM end`. Essa evidência nova aponta para
esgotamento/indisponibilidade de buffers DMA RX como hipótese prioritária.

No ESP-Hosted local, `sdio_buffer_alloc()` chama `eh_host_port_dma_alloc()`, cuja
implementação atual usa `heap_caps_malloc(n, MALLOC_CAP_DMA)` (sem tentativa
SPIRAM). A variante `eh_host_port_dma_alloc_aligned()` é que tenta
`MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA`, condicionada a
`CONFIG_EH_HOST_PORT_DMA_PREFER_SPIRAM`; o arquivo defaults ativa essa opção.
O boot registrou `dma_free=347791` antes de iniciar UI/Wi-Fi e depois a alocação
RX falhou, apesar do heap total grande em PSRAM. Isso é consistente com pressão
no heap interno DMA, mas ainda não prova se o OOM é causa primária ou consequência
de buffers RX retidos. A segunda A/B chamou `eh_host_port_dma_alloc_aligned`
somente para os buffers RX (1536 B, múltiplo do alinhamento L2 configurado, com
fallback para DMA interno) e registrou free/largest DMA + PSRAM no OOM. Resultado:
nenhum OOM RX apareceu; contudo, `esp_wifi_set_config` falhou imediatamente no TX
(RPC 284, `io_ops.tx_bytes failed`, duração 0 ms), sem `GetMACAddress` nem resposta
de rede. Persistiram erros do RPC 387 e CP not ready; os `sys.info` em 41s e 46s
responderam, sem reboot/panic. Isso confirma que o RX OOM foi evitado nesta
variante, mas não estabiliza o transporte. O caminho TX ainda aloca por
`eh_host_port_dma_alloc` e precisa de diagnóstico separado (falha de alocação,
estado `s_inited`/transporte ou fila); não presumir que migrar TX para PSRAM é
seguro, pois payloads podem não satisfazer alinhamento de cache L2.

Fonte Hosted temporária restaurada byte a byte após os testes. Captura A/B streaming:
`/tmp/opencode/music-diagnostic-streaming-ab.raw` (SHA-256
`5d73e1dee496e2b12147bff7dadb56ac16ee80c983bdda0b741680175909810a`). Captura
PSRAM RX A/B: `/tmp/opencode/music-diagnostic-psram-ab.raw` (SHA-256
`30c8dfa3848d705c8ab58b1f9d1a237844bac22dde016488258e1a7f0f541132`).

Uma captura separada com instrumentação de causa no TX, sem alterar o transporte,
confirmou que aos 22,3 s a RPC 387 falha por alocação DMA interna: requisição de
38 bytes, `dma_free=703` e maior bloco `112` bytes; o erro se repete a cada 100 ms
até `CP not ready`. Assim, a falha BLE tardia é definitivamente explicada por
esgotamento/fragmentação do heap DMA interno. Na mesma captura, RPC 257 e 284
seguiram como `no response` (5 s); não houve `tx DMA alloc failed` registrado para
elas, portanto a causa dos timeouts Wi-Fi ainda não está fechada (possível perda
das respostas por RX OOM). Boot/checkpoint e `sys.info` 38→43 s estáveis, sem
panic/WDT. Captura: `/tmp/opencode/music-diagnostic-tx-trace.raw` (SHA-256
`cfc395677a51c21a98f46ef060580b861ad4e7b327a029b5dd7e8139591878e9`). Fonte de
instrumentação restaurada; depois foi gravada a variante CPU-staging descrita abaixo.

Na A/B combinada RX-PSRAM + trace TX, o OOM RX desapareceu, e a falha foi
localizada diretamente no envio da RPC 284: `tx DMA alloc failed: bytes=66`
com `dma_free=867`, bloco maior 200 B; `set_config` falhou em 1 ms, sem esperar
resposta. Portanto a RPC 284 não chega ao CP nesta variante porque o host não
consegue reservar o buffer DMA TX. BLE repetiu o mesmo problema para 38 B, com
691 B livres e bloco maior 72 B. `sys.info` continuou respondendo; sem panic/WDT.
Isso torna o esgotamento/fragmentação do heap DMA interno causa confirmada da
falha de envio Hosted sob essa carga; não prova ainda que o co-processador esteja
saudável nem que todos os timeouts 257 na configuração original tenham a mesma
causa. Captura: `/tmp/opencode/music-diagnostic-rx-psram-tx-trace.raw` (SHA-256
`5e218cbe3f9f2eb714692c8331bf277c7bd1a9cdc68cf670283ae1808ba96048`).
Fontes temporárias restauradas após a captura.

## CPU-staging: primeiro boot e reabertura serial — 2026-10-25

Foi compilada e gravada uma variante experimental que mantém DMA somente nos
buffers SDIO de transferência; frames intermediários TX e cópias RX usam heap
CPU/PSRAM. No primeiro boot, checkpoint passou, não houve marcador RPC/RX de
falha em 48 s, `sys.info` respondeu em 46→51 s com `safe_mode=false`,
`interrupted_boots=0` e uptime crescente, e `wifi.status` reportou connected,
has_ip e `192.168.68.129`. `music queue /musica` mostrou scan iniciado; o primeiro
`music status` concluiu com `queue: scan complete`, `track: 1/1` e `state: stopped`.
Nenhum playback foi enviado.

Ao reabrir a serial para confirmar a fila, o dispositivo reiniciou com
`rst:0x17 (CHIP_USB_UART_RESET)`. Isso é reset induzido pela abertura USB/serial,
não evidência de panic/WDT do firmware. O segundo boot ainda estava em andamento
na captura; duas consultas subsequentes registraram uptime 00:00:05→00:00:10,
`safe_mode=false`, `interrupted_boots=0`, mas `wifi.status` desconectado nessa
janela inicial. Interromperam-se testes do app/player conforme o gate; não enviar
scan nem playback até observar checkpoint, Wi-Fi/RPC estáveis e fila novamente.

**Estado atual importante:** o dispositivo recebeu a variante CPU-staging do primeiro A/B (SHA-256 `e7ee8ddce54e2b27eaa4f711ec0ddc87ea1e68ed6affc515e06fc21e983857d4`). Após `idf.py reconfigure` o binário local foi recompilado (SHA-256 `50792ce1e712d1c5cd2336822087b80dc9572f1f176dac986a65da5d8410af34`); não foi regravado, e o segundo boot no dispositivo permanece inconclusivo.

A variante candidata está ativa em `managed_components` (ignorado), com patch/script versionados: `tools/esp-hosted-cpu-staging.patch` e `tools/prepare_esp_hosted_cpu_staging.py`. Após regenerar `managed_components`, executar `python3 tools/prepare_esp_hosted_cpu_staging.py apply` e `idf.py reconfigure` antes do build; `verify` confere hashes e a revisão exata 3.0.10; `restore` reverte apenas quando os hashes staged conferem. O `CMakeLists.txt` verifica após resolver componentes e antes do link do ELF em **cada build**, inclusive incremental: fontes ausentes/divergentes não geram novo firmware. A aplicação é explícita, não automática. `make -C tests/host/keymap test_esp_hosted_cpu_staging` cobre apply/restore, recusa de divergência e o gate; o agregado `make ... test` passou. Captura combinada: `/tmp/opencode/music-diagnostic-cpu-staging-ab.raw` (SHA-256 atual `5ea6728f0ec716a37f62f79170142d4aa949f7c963028706a7794e25e8095b29`). Não declarar áudio audível.

Gate validado também em execução real: `idf.py reconfigure` sem patch falhou,
`ninja -C build cyberdeck5.elf` sem patch bloqueou o link, e apply + reconfigure
+ build voltaram a passar (binário local `50792ce1…`). O teste host usa fixture
temporária, sem depender de `managed_components` na CI. A suíte completa passou;
uma rodada intermediária parou em `test_time_sync_bin` (asserção
`task_deletions == 1`), mas o alvo isolado e a repetição integral passaram sem
alterar o teste. O teste físico posterior ao reset USB segue pendente; não houve
flash nesta correção do gate de build.

O roteiro dispositivo-a-dispositivo fica em
`tests/manual/music-player-validation.pt-BR.md`; o boot normal é pré-condição
para qualquer teste de playback. Se o safe mode reaparecer, registrar `sys.info`
e parar, sem repetir clear/reset nem tentar tocar.

## Revalidação pontual I2S após correção — 2026-11-01

O firmware `build/cyberdeck5.bin` (SHA-256
`3ce90e1d24b68a771846b876a0e8fbde25ea6f7be25858286da55b04337aa9b4`) foi
gravado; hash da captura I2S: `d705e4588a3b55d11f70d9de1677c9e7f1e2ca944078dc55558aa980af3a5024`.
O usuário relatou que a única reprodução tocou até o fim. A captura não contém
evidência de log que demonstre o término; ela não permite atribuir essa conclusão
a logs. Não apareceu `channel not enabled yet`; apareceu também um warning
distinto de transição RX master→slave, que não deve ser confundido com aquele
erro. Isso valida somente esta tentativa de reprodução I2S conforme relato e
captura, não o aceite completo do player. Jack/fone, controles e poweroff durante
playback continuam fora desta revalidação e pendentes; demais critérios físicos
do player não são declarados aprovados.

## Aceite

## Browser musical — implementação faseada

### Fase 1: modelo puro e scan direto

Implementados `REQ-MUSIC-BROWSE-01,02,04` e `AC-MUSIC-BROWSE-01,02,04` no
módulo isolado `cyberdeck_music_browser`: entries directory/MP3 e snapshot de
até 64 itens, diretório virtual restrito a `/musica`, parent inerte na raiz e
seleção independente da fila/player. O seam enumera somente filhos diretos em
ordem lexicográfica por nome; MP3 aceita extensão `.mp3` sem distinção de caixa.
Mantém os 64 primeiros nomes elegíveis de forma determinística. Resolve a raiz
virtual a partir de `/sdcard` e abre cada componente/entrada com `openat`,
`O_NOFOLLOW`, `fstatat` e `fstat`; links, arquivos não elegíveis e não regulares
são ignorados. Falha de open/read/close retorna erro sem publicar snapshot parcial.
Os wrappers de sistema são injetáveis para testes comportamentais focados.

Tester confirmou no snapshot HEAD `a6f797bdca5bd8f4b3a9d43804dd86ee5d7927be` e
snapshot composto `0042e5655c3c1c4c3757b67039be74d6ea298f7d14e193daa323b7ef197fa4c9`:
`test_music_browser` 49 checks + ASan/UBSan, `test_music_app` 223 checks e
`test_music_scan_opendir_contract.py` 20/20. Esta fase inicial não conectou o
browser à fila/playback, não alterou UI/input nem substituiu o scan recursivo.
A leitura abre `/sdcard` como raiz confiável do volume e confina
subsequentes componentes por descritor; comportamento em FATFS/FreeRTOS e no
dispositivo ainda requer validação posterior.

### Fase 2: integração aditiva no application

O `application` expõe scan de pasta, snapshot de browser, seleção própria e
estado de scan ativo. Reutiliza o worker de scan existente (modo legacy ou
browser), sem worker concorrente adicional. O scan monta snapshot local e só o
publica completo sob o mutex de lifecycle após validar lease, grant e
cancelamento. Falha/cancelamento preserva snapshot anterior. `test_music_app`
testa a API real com scanner POSIX sobre fixture e fake player. `TEST-MUSIC-BROWSE-CANCEL-JOIN-10`
usa worker/scanner real, barreiras CV, join, lease e reopen sem stale; stop
failure/timeout injetável não foi testado.

### Fase 3: integração view/input/actions

A view renderiza o snapshot do browser na lista existente, mantendo seleção
visual por linha separada da browsing selection e da queue/player. `/musica` é a
raiz; `..` é uma linha sintética somente fora dela, não é entrada do filesystem
nem consome um dos 64 slots. Setas/cliques apenas selecionam; ao despachar,
remove-se o offset visual de `..` antes de chamar `browser_select`. Enter/P navegam
para parent/pasta; em MP3, Enter usa o toggle existente e P/Play Selected usa
play-selected existente. Playback recebe o path do item browser e procura a faixa
correspondente na queue recursiva; nunca interpreta índice visual como queue
index. Diretórios não entram na fila. Paths ou índices stale/invalid, scan ativo,
lista vazia e arquivos sem correspondência na fila não iniciam playback. A ação
exige que a view renderizada ainda corresponda ao diretório publicado. Previous,
Next, seek, volume, Command, Esc e comandos textuais permanecem inalterados.

Testes focados cobrem `TEST-MUSIC-BROWSE-ENTER-05`, `PLAY-SELECTED-06`,
`SELECTION-07` e `REGRESSION-12`: view LVGL real verifica parent/labels e seleção
separada; app real verifica path→queue e ações; contratos protegem wiring.
Validação independente e física permanecem pendentes.

## Correção local do transaction I2S/codec — cycle 1

O dependency `espressif/esp_codec_dev` está pinado em 1.6.2 e resolvido pelo
override `../components/esp_codec_dev` em `main/idf_component.yml`. O fork
preserva o manifest upstream (commit `af7b72fb2b73d4f513d3cede01c13518ab216735`)
e LICENSE Apache-2.0. A correção propaga erros de configuração I2S em `open`,
mantém o primeiro erro ao fazer rollback e mantém estado/handles para retry na
falha de cleanup. `esp_codec_dev_delete` agora retorna o resultado do close e
só desaloca o handle após close bem-sucedido; a mudança upstream de retorno
`void` para `int` é uma quebra de compatibilidade de fonte intencional para
consumidores que assumiam `void`.

Evidência do warning I2S: a versão upstream de `esp_codec_dev.c` descartava o
retorno de `data_if->set_fmt`/`enable` durante `open`, e o caminho de cleanup
precisava preservar ownership ao falhar. O TU real
`platform/audio_codec_data_i2s.c` também é exercitado com erros injetados por
operação. Para teardown, após limpar observers e trace, a falha injetada de I2S
disable produz trace
`[CODEC_DISABLE, I2S_DISABLE(falha), I2S_DISABLE(retry)]`; `codec_disable_calls`
permanece 1. Assim, o disable do codec foi concluído uma vez e não deve ser
omitido do oráculo nem repetido na tentativa de cleanup. Os alvos focados
`test_music_codec_i2s_vendor` e `test_music_codec_i2s_vendor_acceptance`
compilam e executam o fork local; o primeiro caracteriza/controla o comportamento
corrigido do fork e o segundo executa a aceitação (181 assertions). Nenhum deles
executa o pacote upstream original sem patch nem estabelece comparação upstream.
O comportamento original (descarte dos retornos de `set_fmt`/`enable` em `open`)
foi identificado por inspeção prévia do código upstream; essa inspeção é evidência
histórica separada dos testes atuais. Os alvos são focados e não executam a suíte
global.
O harness usa TUs reais do fork com shims; não modela FreeRTOS/I2S hardware,
DMA ou registradores do codec. Build ESP-IDF local é verificação de resolução e
compilação, não validação de hardware. Sem flash/playback nesta rodada; aceite
físico continua pendente.

Após teste manual sem serial aberta, o usuário confirmou Wi-Fi, fila com uma faixa
e status `stopped`, mas **uma única tentativa** com `music play 1` travou e
reiniciou o dispositivo. Nenhum replay automático foi executado. Diagnóstico do
build ESP32-P4 com `-fstack-usage`: `mp3dec_decode_frame` reservava 17.200 B na
stack de uma task `music_player` de apenas 8.192 B. Adaptação local do minimp3
preserva a API original e recebe scratch alocado pelo player no heap;
`mp3dec_decode_frame_with_scratch` mede 976 B, e o worker mede 1.152 B no
cross-compiler. Falha de alocação de scratch vira `error_stage=decode`. Teste
host focado (93 checks), paridade de dois frames entre wrapper upstream e scratch
externo sob ASan/UBSan, suíte host integral, contrato de stack e build ESP-IDF
passam; binário local
SHA-256 `0bcb094320d7563f2d6b8d590a5220f417ba1b25f7ac3ff76550107b324c116d`.
O binário corrigido foi gravado/verificado em `/dev/ttyACM0`. Uma única captura
após flash confirmou checkpoint persistido, `sys.info` em 48→53 s com
`safe_mode=false`, `interrupted_boots=0`, Wi-Fi conectado/com IP e nenhum marcador
de RPC timeout, RX OOM, WDT ou panic. A abertura serial ainda registrou
`CHIP_USB_UART_RESET`; captura `/tmp/opencode/music-scratch-boot.raw` (SHA-256
`6ccc56f89d66484d60a3657f20cc17c4e6afe2371a0250048f6abd26c2bec6db`).
**Não houve nova tentativa de playback; não declarar áudio audível nem atribuir
o reset anterior a causa exclusiva sem backtrace.**

### Trace temporal bounded — investigação de stutter

O usuário aprovou instrumentação observacional apenas no worker; não há fix
funcional nem causa comprovada. `music.diagnostics` permanece schema v1
inalterado. Novo `music.trace` é somente leitura, pagina até 16 eventos por
resposta e fornece cursor/next/truncated/lost explícitos. Ring de 64 registros
(≤4 KiB), timestamp monotônico, geração, tipo, resultado, duração e bytes onde
há valor já disponível. Só o worker grava: início/retirada de geração, leitura
SD, decode, codec start, output write e falha. Não observa ISR/underrun/DMA real.
Cada campo concorrente é atomic uint32 lock-free; publication sequence por slot
permite snapshot bounded sem espera, omitindo slots instáveis. P4 build e testes
host direcionados são necessários antes de qualquer alegação de segurança.
Esta telemetria correlaciona durações/cadência, mas não prova causalidade; a
validação física do novo trace continua pendente e nenhum playback/flash é
autorizado por este trabalho.

### Regressão manual após correção de scratch

O usuário repetiu o teste no firmware corrigido, diretamente no Tab5, e relatou
novo travamento seguido de reinício. A correção do orçamento de stack eliminou
uma falha demonstrável, mas **não resolveu o playback**. Nenhum replay, reset,
flash ou tentativa via serial foi feito após este relato. `sdkconfig` tem
`CONFIG_ESP_COREDUMP_ENABLE_TO_NONE=y`, `ESP_SYSTEM_PANIC_PRINT_REBOOT=y` e
`ESP_TASK_WDT_PANIC=n`; `sys.info` não fornece backtrace da reprodução anterior.
Sem captura contemporânea do console, não é possível identificar se a falha
restante ocorre no decode, init do codec, I2S ou outra task. Próxima etapa depende
de diagnóstico com captura passiva já aberta antes de uma eventual tentativa
**explicitamente autorizada**; não repetir playback às cegas.

### Correção do escopo da métrica de rota — ciclo 1

O snapshot físico mostrou `route_check_calls=0`, mas `start()` também chama
`update_route()` (abertura forçada e reentrada same-format após throttle), enquanto
a métrica anterior só era propagada por `write()`. Portanto zero não demonstrava
ausência de checks I2C/expander e não sustentava atribuição ao backend/I2S. A
instrumentação agora recebe o resultado opcional de timing em ambos os caminhos;
conta uma vez cada chamada real, inclui tentativas que falham e exclui o retorno
rápido throttled. `codec_start_*` e `write_*` permanecem intactos. Sem alteração
de playback/I2S/DMA/codec e sem nova reprodução nesta rodada. A nova imagem exige
validação posterior pelo usuário; diagnóstico acústico permanece pendente.

Host/build aprovado não substitui o aceite físico. Para concluir, confirmar no
Tab5 que o app pode ser iniciado após boot normal, que uma faixa real toca e
que controles/lifecycle funcionam; testar speaker e jack, amp desligado em idle,
ausência de ruído/consumo anormal, coexistência e poweroff. Confirmar que a fonte
MP3 não é alterada. A tentativa fica bloqueada até boot normal estável e
diagnóstico Hosted/C6 suficiente; safe mode/recovery deve ser resolvido primeiro.

### Observação manual de trocas e stutter — 2026-11-02

Snapshot de trabalho: HEAD `d2236a4b94bc7a2ada4482991732dcd6d7a00d66` + diff
`1b1710ba0bf7a87d8eb60570d2b74bfcccee0784b46fff0ec0e2f23105028f49`.
Firmware testado: SHA-256 `67a2358a14fd3130df0564d272cf68e441ab4bc2ceaf32f43583bb26e8013daa`.
O usuário relatou várias trocas de faixa, esperas de segundos e trocas rápidas;
nenhum engasgo foi ouvido. Diagnóstico read-only posterior: generation=7,
generations queued/started/retired=7/7/6, failures=0; máximos read=5465 us,
decode=11603 us, codec-start=18720 us, write=27827 us; route checks=35/max
1341 us; backend writes=734/max 27803 us. `sys.info` uptime avançou de
00:20:03 para 00:20:18, `safe_mode=false`, `interrupted_boots=0`,
`recovery_persisted=true`.

Trace `/tmp/opencode/music-trace-user-run-20261102.jsonl`, SHA-256
`224166835af56b2e39e585dfdd79869152131d1cac63ca9af151754d9efda7d6`:
quatro páginas/64 eventos; primeira página `truncated=true`, `lost=10176`,
somente sequências 10176..10239 da generation 7. Eventos de trocas anteriores
foram sobrescritos; a observação confirma ausência de stutter percebido nessa
rodada, mas não permite correlacionar cada troca nem comprovar underrun DMA.
O teste manual direcionado de stutter nas trocas relatadas fica PASS conforme
observação do usuário; causa raiz permanece não confirmada e nenhuma correção
funcional foi feita. O usuário considera o problema resolvido, mas isso não
substitui aceite técnico causal nem os gates físicos restantes do player.

### Confirmação manual de Play Selected — 2026-11-02

O usuário confirmou que tanto o atalho físico `P` quanto o botão de tela
`Play Selected` funcionaram corretamente. A imagem gravada tinha SHA-256
`67a2358a14fd3130df0564d272cf68e441ab4bc2ceaf32f43583bb26e8013daa`; desde
essa compilação, somente o follow-up de teste/documentação `09bdd39` mudou, sem
alteração de produção. Esta evidência cobre especificamente esses dois meios de
acionar Play Selected, por relato do usuário; não infere validação virtual de P,
outras teclas, touch/controles distintos, speaker/jack ou aceite integral.
Separadamente, o usuário relatou várias trocas normais e rápidas sem engasgos,
conforme observação de stutter acima. Causa-raiz do stutter continua desconhecida
e não houve fix funcional; o trace bounded não prova ausência de underrun DMA.
Os demais gates físicos do player, incluindo speaker/jack, ESC/lifecycle e os
critérios de coexistência/idle/poweroff aplicáveis, continuam pendentes.

### Refinamento da cobertura do indicador do header — ciclo 1

O tester reportou a inclusão do caso ausente `foreground=false` com estado
`error` em `test_music_header_visibility.cpp`. A matriz agora verifica as oito
combinações de foreground × playing/paused/stopped/error, além de snapshot
`playing` stale após saída bem-sucedida e playing/paused visíveis em foreground
após falha de close. O tester executou `make -C tests/host/keymap
test_music_header_visibility` e `make -C tests/host/keymap
test_music_player_teardown_contract`; ambos retornaram exit 0. `test_music_manager`
foi executado anteriormente pelo coder e passou, cobrindo ownership após falha
de close; o tester não repetiu esse alvo. Evidência detalhada em
`tests/host/keymap/player_traceability.md`. Esta é evidência host, não valida
Escape no dispositivo; `manual_validation: pending` permanece.

Build ESP-IDF do coder neste follow-up: `source ~/esp/esp-idf/export.sh &&
idf.py build` passou em ESP32-P4; imagem `build/cyberdeck5.bin`, tamanho
`0x2757b0`, com 39% da partição livre. Houve avisos de depreciação Kconfig e
aviso do linker de `kill` não implementado na dependência libssh; sem falha de
build. Nenhum flash ou teste físico foi executado.

No Tab5-OS o app de música é UI e o engine de áudio pertence ao OS; não há
correção de stutter diretamente transferível identificada. A correção prévia
específica do cyberdeck5 permanece o ring I2S DMA `8 x 512` (b90f191); esta
rodada não demonstra causalidade nova.

### Tentativa física interrompida pelo harness — 2026-11-02

O firmware v3 foi gravado e a imagem foi verificada com SHA-256
`6427e1c7df8ee3e0724a12d67683fa5a4443e3718be6c1c1c3cff34c2aa48b48`. O
controller encerrou em `wait_boot` antes de enviar qualquer comando de UI; não
houve abertura do player, playback ou interação com Esc. A captura
`/tmp/opencode/music-header-esc-20261102-190216-idfenv.raw` tem SHA-256
`36abb1c0dcfad48d50d4b704761a3547d3ad7a4e4f8834f8570ae7c001b9998b`. Sua
análise local identificou falso positivo do harness: a regex case-insensitive
`FAULT` casou com `Backtrace` na linha de comando de `idf_monitor.py` contendo
`--decode-panic backtrace`. A captura não fornece evidência de panic, WDT ou
reset; contém `HOSTED_DIAG wifi stage=set_config result=ESP_OK`, mas não o
conjunto completo de marcadores/checkpoint nem `sys.info`, portanto o gate de
boot não foi comprovado. Esta tentativa não valida comportamento do player nem
Esc; `TEST-MUSIC-HEADER-PHYSICAL-ESC` e a validação física permanecem pendentes.
Retomar somente após corrigir o helper em cópia temporária fora do repositório;
nenhuma alteração no helper original foi feita.

### Retentativas de instrumentação do controller — 2026-11-02

As duas retentativas abaixo são falhas/limitações de instrumentação, não bugs do
firmware. O firmware em execução continuou sendo v3, SHA-256
`6427e1c7df8ee3e0724a12d67683fa5a4443e3718be6c1c1c3cff34c2aa48b48`; não houve
reflash nem comando de UI/player/playback/Esc em nenhuma delas.

1. Na cópia temporária do helper, `Backtrace` foi removido do padrão `FAULT`, mas
   a execução abortou no aviso `W (10563) NimBLE: Failed to persist local IRK
   (rc=8)`: a regex genérica `ERROR` também casa texto de warning `W`. Exploração
   local identificou `rc=8` como `BLE_HS_ENOTSUP`; `ble_hs_pvcy.c` apenas registra
   WARN e retorna. Isso provavelmente significa que a IRK local não persiste entre
   boots; não é panic nem evidência de estabilidade. Captura
   `/tmp/opencode/music-header-esc-20261102-retry.raw`, SHA-256
   `688a2234b63bc8297fc782bd8794bbf7b15bd603ae43644e3adf6747b8d3a255`.
   Não houve gate completo de boot nem `sys.info`.
2. Outra cópia temporária, gates-only, ignorou `Backtrace` e restringiu o padrão
   de erro a severidade `E (`. Após esperar `BOOT_MARKERS` por 35 s, o fallback
   `events.read` falhou localmente antes de escrever/enviar qualquer frame:
   `TypeError: 'int' object is not a mapping`, pois o helper passou um inteiro
   como `fields`. Captura `/tmp/opencode/music-header-esc-20261102-final.raw`,
   SHA-256 `5793e02addb02b860d25ec6c3a31c1f9d8d7656d456b6a8da1e1aa05aa62cf7b`,
   contém 10 linhas, somente eventos de tela por inatividade/wake. Nenhum
   `events.read`, `sys.info`, UI, player, playback ou Esc foi enviado. Não houve
   reset/panic comprovado e não se pode concluir estabilidade.

Discovery local posterior confirmou que `exec_events_read` não aceita parâmetros
adicionais e usa chunk fixo de 1024 bytes; o envelope correto é somente
`{rid,type}`. `test_event_projection_contract.py` cobre esse contrato. A terceira
tentativa corrigiu apenas a cópia privada do controller (detalhes e resultado
abaixo); não modificou helper original nem arquivos de produção/teste.

### Terceira tentativa do controller interrompida — 2026-11-02

A terceira cópia privada foi
`/tmp/opencode/music-monitor-controller-esc-final.py`, SHA-256
`ee92d34742f73877ee6118d265cef5e1c0654dea8d264647306a1f54e4422f9f`; corrigiu
somente regex de Backtrace, severidade de erro e fallback `events.read` com
`fields={}` conforme contrato local. Imagem anteriormente gravada/verificada:
v3, SHA-256 `6427e1c7df8ee3e0724a12d67683fa5a4443e3718be6c1c1c3cff34c2aa48b48`,
`version.txt=3`; versão ativa não confirmada por `sys.info` nesta execução.

`wait_boot` expirou sem `BOOT_MARKERS`; fallback enviou `events.read`, sem resposta
no deadline. Raw `/tmp/opencode/music-header-esc-20261102-final-once.raw`, SHA-256
`2dfeff5b58bc7f7bda2fe62ca12b2ad354f39f5a9d94179ab9f7e2951159f563` (3 linhas,
419 B), não contém resposta/checkpoint. Nenhum `sys.info`, UI, player, playback ou
Esc; não há evidência suficiente de crash/reset nem de boot estável. Warning
NimBLE `rc=8` foi analisado no código local como `BLE_HS_ENOTSUP`, não fatal, mas
não satisfaz o boot gate. O erro anterior de `fields` inteiro foi corrigido com
`{}`, porém esta terceira execução ainda não obteve resposta. A tentativa foi
encerrada conforme instruções; não repetir.

Registro histórico: o gate boot/serial do controller ficou inconclusivo; isso não
era bug confirmado no firmware nem constituía resultado do cenário físico. O
aceite posterior reportado pelo usuário está registrado abaixo.

### Aceite manual reportado pelo usuário — 2026-11-02

O usuário confirmou explicitamente que fez o teste físico e marcou todos os
critérios: playback estava ativo, pressionou Esc, o shell retornou, o ícone de
playback sumiu e não houve reinício. Este teste manual is user-reported and
firmware identity unconfirmed. Uma única consulta do tester a
`python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 sys.info` terminou com exit 2
(`nenhuma resposta do dispositivo`) e não foi repetida; portanto não foi possível
identificar a imagem ativa nem associar o relato a um hash. A última imagem
registrada como gravada e verificada foi firmware v3, SHA-256
`6427e1c7df8ee3e0724a12d67683fa5a4443e3718be6c1c1c3cff34c2aa48b48`; isso não
prova que foi a imagem usada no teste reportado. O build local mais recente após
a correção somente de formatação tem SHA-256
`4f329e4b371d0f406c1aa390f0ee189e19913da30b58b9953a02de894fee19e9` e não foi
gravado após essa correção. O aceite manual reportado resolve o gate
comportamental Esc/retorno ao shell, mas não é observação independente do agente
nem identificação de firmware. A revisão final continua `review_incomplete`:
gcovr 8.7 thresholds e scope guard não foram executados; CI remota não cobre o
diff dirty e não houve autorização de commit/push. Não declarar code review
completo nem tarefa concluída.

### Verificação independente do tester — ciclo 2

O handoff `test_ready` do tester verificou HEAD
`e8b064b1f88f59c02d81a855282e516573297bd4` + diff
`c07d0c054d0a3d37a449d762e1356b92509b5d98d9fc4d5037ff9d8505affb1a`.
Hashes verificados: fonte `cyberdeck_music_header_visibility.cpp`
`852f619a5d2eb5ea5af145a7193fa82d54d17cded997f8d86d12b71f034a81a` e teste
`test_music_header_visibility.cpp`
`2ee0ad52395b6e5ff6aa03682a85523d04cf80383646c1923aff7137c3027aed`.
Independente do coder: `clang-format --dry-run --Werror`,
`test_music_header_visibility` e `git diff --check` passaram. O tester confirmou
que os critérios de close success/failure estão documentados na matriz; Esc
físico continua pendente. A falta de formatação apontada na revisão ampla anterior
está corrigida e validada independentemente. Thresholds de coverage/gcovr e CI
remota seguem pendentes/inconclusivos, não aprovados. Próxima ação: reviewer final
no snapshot atualizado; `manual_validation: pending`, sem alegação de aceite físico.

### Correção de formatação após revisão — 2026-11-02

Reviewer `needs_correction` (cycle 1) encontrou somente falha do format gate
nas linhas 7 e 11 de `cyberdeck_music_header_visibility.cpp`; comportamento e
testes não tinham defeito acionável. Aplicado apenas clang-format nesse arquivo,
sem mudança semântica. `clang-format --dry-run --Werror` e
`make -C tests/host/keymap test_music_header_visibility` passaram, assim como
`git diff --check`. Os gates locais requeridos por esta correção estão completos;
coverage thresholds/gcovr e CI remota não foram conclusivos/verificados para o
snapshot dirty. O bloqueio do gate boot/serial e a validação física de Esc
permanecem inconclusivos/pending; não houve hardware, flash ou commit.

### Estado do review final local — 2026-11-02

O review final local permanece `review_incomplete`: gcovr 8.7 e o scope guard
não foram executados, portanto os gates de cobertura/escopo não estão aprovados.
No venv isolado `/tmp/opencode/gcovr-8.7-venv`, a instalação pip normal expirou
em 600 s enquanto coletava `lxml` (exit 124; log
`/tmp/opencode/gcovr-install-retry.log`). A tentativa bounded wheel-only contra
PyPI oficial também expirou em `Collecting lxml`, sem wheels encontrados (exit
124; log `/tmp/opencode/gcovr-wheel-download.log`). Não há processos pip/gcovr
ativos e gcovr continua ausente. Pesquisa externa confirma que gcovr 8.7 exige
`lxml`; disponibilidade de wheel compatível com CPython 3.14/x86_64 local segue
inconclusiva. Não realizar novas tentativas de instalação nesta tarefa sem nova
autorização/estratégia humana. CI remota não cobre este diff dirty.

Snapshot de referência do coder: HEAD
`e8b064b1f88f59c02d81a855282e516573297bd4` + diff anterior às atualizações
documentais do ciclo 3 `8f7328f5789896b63667925196dd0684c327aa71fcb4c8cd856fadb707bf34c4`.
O usuário autorizou revisão local, não commit/push. O aceite manual permanece
`completed` por relato do usuário conforme seção acima; a identidade do firmware
continua desconhecida. Isto não conclui a revisão: thresholds de coverage e scope
guard continuam pendentes e exigem execução independente do reviewer.

### Lacuna de instrumentação do coverage — tester cycle 3

O tester identificou que `test_music_header_visibility` já era executado no
agregado, mas sua receita compilava diretamente o teste e o TU real
`cyberdeck_music_header_visibility.cpp` sem `--coverage` e fora de `BINS`.
Consequentemente, o gcovr não recebia dados daquele TU de produção. A correção
do tester no `tests/host/keymap/Makefile` passou a compilar o TU de produção
instrumentado e incluí-lo no artefato do alvo; o `code-map.md` foi atualizado
para refletir o arranjo. Nenhuma fonte de produção ou assertion do teste mudou.

Evidência focada do tester: `test_music_header_visibility` PASS (8 assertions);
arquivos `.gcno`/`.gcda` foram gerados para teste e produção; `git diff --check`
PASS. Isto não demonstra os thresholds globais nem o scope guard. O gcovr
disponível foi extraído temporariamente e é `8.8.post1.dev0+g6e49f2f4d.d20261031`,
não a versão CI 8.7; os gates permanecem pendentes até o reviewer executar clean
coverage e scope guard no snapshot atualizado. A revisão final segue
`review_incomplete`; não inferir aprovação global.

### Revisão final aprovada e aceite manual — 2026-11-03

O commit funcional `0d26af2175b410a557a5d76383a4e29c27994142` foi aprovado
na revisão final. Quality Gate run `41553034162` terminou `success` em
2026-11-03 00:40:59Z: job quality e gates de format/lint/security, host +
coverage, TSan e build ESP-IDF verdes. O reviewer confirmou que HEAD local
correspondia ao SHA do run e que `git diff --check` estava limpo. Este resultado
é do commit funcional; a atualização documental posterior não foi coberta por
esse CI e não altera código nem os gates já aprovados.

O usuário esclareceu que o teste físico foi feito no último firmware gravado,
correspondente à última gravação registrada neste histórico (v3, SHA-256
`6427e1c7df8ee3e0724a12d67683fa5a4443e3718be6c1c1c3cff34c2aa48b48`). A
associação é baseada no relato do usuário, não em leitura independente da imagem.
Critérios reportados: playback ativo, Esc pressionado, retorno à shell, indicador
oculto e nenhum reboot. A consulta `sys.info` do tester falhou com exit 2/sem
resposta; portanto versão/hash ativos não foram lidos independentemente.
`TEST-MUSIC-HEADER-PHYSICAL-ESC` fica registrado como aceite manual reportado,
sem elevar os demais critérios físicos do player/layout/teclado a aprovados.

Rastreabilidade: `REQ-MUSIC-HEADER-STATE` e
`AC-MUSIC-HEADER-ESC-HIDDEN` passaram por relato; `AC-MUSIC-HEADER-ESC-FAIL-PRESERVE`
permanece coberto por host, não por este cenário físico.
`TEST-MUSIC-HEADER-CLOSE-STATE` e `TEST-MUSIC-HEADER-CLOSE-FAIL` permanecem
evidência host conforme matriz; `TEST-MUSIC-HEADER-PHYSICAL-ESC` tem resultado
manual reportado acima. A revisão final funcional está aprovada; gates físicos
independentes não verificados seguem pendentes.

### Rebuild, flash e boot verificados após o aceite — 2026-11-03

Após handoff válido do tester e autorização de flash do usuário, o firmware foi
recompilado e gravado sem alterações de código. HEAD documental:
`b5e86c779d90e28c8f18a26190abb4976bd3c639`; fontes de firmware idênticas ao
commit funcional aprovado `0d26af2175b410a557a5d76383a4e29c27994142`.
Com ESP-IDF v6.1 (a versão do Quality Gate aprovado), `idf.py build` passou. O
`build/cyberdeck5.bin` tem 2,578,352 bytes e SHA-256
`4f329e4b371d0f406c1aa390f0ee189e19913da30b58b9953a02de894fee19e9`. O comando
`idf.py -p /dev/ttyACM0 flash` passou, sem erase-total; hash gravado verificado e
hard reset executado. A única consulta posterior, `sys.info`, passou e reportou
chip `esp32p4`, `fw_version=3`, `idf_version=v6.1`, uptime `00:00:06`,
`safe_mode=false` e `interrupted_boots=0`.

Playback/Esc não foi repetido nesta ação. O usuário esclareceu que seu relato
anterior (playback ativo, Esc, retorno à shell, ícone oculto e sem reboot) foi
com o último firmware então gravado. A identidade relatada anteriormente não
foi verificada diretamente; agora a última imagem efetivamente gravada foi
confirmada como `fw_version=3` e pelo hash acima. Isto valida build, flash e boot
da imagem; não é nova observação independente do cenário playback/Esc nem fecha
os demais gates físicos do player.
