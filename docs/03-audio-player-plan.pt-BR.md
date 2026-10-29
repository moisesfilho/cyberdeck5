# Plano 03 — Player de áudio local

**Estado:** implementação integrada; aceite físico do player/layout/teclado ainda
pendente. O fluxo de trabalho é testes focados -> validação no Tab5 do
comportamento-alvo -> revisão final ampla. A revisão ampla não substitui o teste
físico. Se o dispositivo/cenário estiver indisponível ou inseguro, os gates locais
podem ser finalizados, mas a validação física permanece pending e a tarefa não
pode ser declarada concluída. O aceite acústico de speaker/jack requer autorização
explícita separada.

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
sendo a consulta explícita; `music help` lista os comandos. O alias legado
`player` ativa a mesma instância e não cria outro app. Subcomandos só são
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

## Aceite

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

Host/build aprovado não substitui o aceite físico. Para concluir, confirmar no
Tab5 que o app pode ser iniciado após boot normal, que uma faixa real toca e
que controles/lifecycle funcionam; testar speaker e jack, amp desligado em idle,
ausência de ruído/consumo anormal, coexistência e poweroff. Confirmar que a fonte
MP3 não é alterada. A tentativa fica bloqueada até boot normal estável e
diagnóstico Hosted/C6 suficiente; safe mode/recovery deve ser resolvido primeiro.
