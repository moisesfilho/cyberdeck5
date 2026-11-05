# Validação manual do player de música

**Dispositivo:** M5Stack Tab5 / ESP32-P4
**Porta usual:** `/dev/ttyACM0`
**Firmware integrado atual:** `version.txt` (confirmar hash da imagem no registro)

**Ordem de gates:** testes focados independentes precedem a validação deste
roteiro; para mudanças de interface/dispositivo, a validação no Tab5 deve ocorrer
antes da revisão final ampla, quando segura e autorizada. Se bloqueada/deferida,
registre a limitação e mantenha o gate físico pendente; revisão host não prova
comportamento no dispositivo.

Este roteiro não autoriza repetir recuperação sem limite. Registre horário,
versão/hash, porta, resposta NDJSON, logs seriais e `sys.info` antes/depois.
Qualquer safe mode, ausência de resposta, reboot inesperado, panic ou WDT encerra
o cenário; não repetir clear/reset/playback automaticamente.

## 1. Recovery e boot normal — gate obrigatório

`events.read` também exporta o journal NVS de recovery como um frame
`recovery_log` (além do log textual do SD). Capture-o sem abrir um monitor
serial concorrente:

```bash
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 events.read
```

O journal registra `load` e as transições `boot_begin`, `clear` e `checkpoint`,
com estado anterior/posterior e resultado de persistência. Ele é diagnóstico
best-effort e não substitui `sys.info` nem altera o latch.

1. Grave a imagem construída para o snapshot atual e capture o boot serial
   completo. Registre as linhas `HOSTED_DIAG recovery`, `hosted`, `wifi`, SD e
   bateria.
2. Consulte `sys.info`. Se `safe_mode=true`, consulte o estado e os logs, envie
   `sys.safe_mode.clear` no máximo uma vez e confirme `cleared=true`. Espere o
   boot atual terminar; então faça no máximo um reset controlado e capture o
   próximo boot. O clear explícito também conclui o checkpoint pendente e zera
   `interrupted_boots`, para que reset provocado pelo fechamento/reabertura da
   Serial-JTAG não conte o mesmo boot de recovery como nova interrupção. Não
   faça clear adicional.
3. Só prossiga após observar no log serial `HOSTED_DIAG recovery
   stage=checkpoint result=ESP_OK persisted=1 safe_mode=0 pending=0` e
   `CYBERDECK5 iniciado`. Depois confirme em duas consultas `sys.info`, separadas
   por alguns segundos, `safe_mode=false`, `interrupted_boots=0` e uptime
   crescente. Um `sys.info` antecipado pode retornar valores aparentemente
   normais enquanto `app_main` ainda inicializa serviços; não envie comandos de
   app/player antes do checkpoint. O boot normal também deve apresentar
   `HOSTED_DIAG hosted stage=power_enable`, `esp_hosted_init`, `connect_to_slave`,
   montagem SD e init/start do Wi-Fi.
4. Se os marcadores Hosted não surgirem, houver RPC timeout/CP não pronto, safe
   mode reaparecer ou `sys.info` não responder, pare. Não iniciar o player nem
   atribuir a falha ao decoder/áudio; preserve a captura para diagnóstico.

## 2. Abertura, scan e inspeção — modo estrito sem playback

**Escopo desta rodada:** abrir o player e consultar estado/scan sem playback.
`ui.type "music"` é permitido **somente antes** da transição para foreground.
Depois que o player abriu, **não use `ui.type`, nem para `music status` ou
`music list`**: ele injeta Enter, que o player interpreta como Play/Pause,
independentemente do texto. `ok:true` da ponte não prova execução pelo shell.
Use `music.status` (NDJSON read-only), `music.diagnostics`, `term.dump` e
`ui.dump`. Não toque em Play/Pause, Next, Enter, N, P ou Space. Se o foco/estado
tornar uma ação ambígua, não a execute.

Pré-condição: boot normal comprovado e sem reboot/WDT durante a captura.

`music` sem argumentos é o único ponto de entrada do player: ativa o app
foreground `cyberdeck.music`, abre a surface dedicada e inicia o scan recursivo
da queue e o scan direto do browser em `/musica` (pastas e arquivos MP3 em ordem
lexicográfica, até 64 entradas reais). A raiz do browser é `/musica`; `..` é uma
linha sintética apenas dentro de subpastas e nunca aparece na raiz nem consome um
dos 64 itens. O browser mostra somente pastas e MP3; navegar não altera a queue.
Não é necessário — e não basta — `app start cyberdeck.music`: o start isolado
apenas sobe o app, sem abrir a surface nem iniciar o scan. `player` não é alias.

Subcomandos (`music status|list|play|...`) pertencem ao app, mas não são
consultáveis via `ui.type` no player em foreground. Fora dele, o shell responde
`app: command requires active foreground`.

O console e a ponte Serial-JTAG permanecem vivos com o player aberto. Isso é
requisito do contrato: `term.dump`, `music.status` e `sys.info` devem continuar
respondendo enquanto a surface está na tela. A resposta de `ui.type` não
confirma o efeito, e Enter pode iniciar reprodução.

```text
sys.info
ui.type "music"
term.dump          # esperado: music: opening player
ui.dump            # surface do player com status, lista rolável e controles
music.status       # state, queue_scanning, track_count, position_ms, duration_ms
music.diagnostics  # contadores read-only, sem título/caminho da faixa
```

Para observar estado e heap no mesmo intervalo, use **somente uma sessão**
Serial-JTAG: `python3 tools/collect_music_status_series.py --seconds 60
--out /tmp/opencode/music-status.csv`. A ferramenta nunca sobrescreve o CSV,
registra uma amostra por segundo e não coleta títulos/caminhos de faixas.

Aguarde `queue_scanning=false` em `music.status` antes de avaliar a contagem
de fila. Com o scan em andamento, `music` e `music queue <dir>` são rejeitados com
`scan unavailable or another scan is active`; não repetir durante a coleta. A
surface mostra status, browser rolável de pastas/MP3 e controles; a consulta
NDJSON informa `track_count` sem expor títulos ou caminhos.
Esse limite da queue recursiva é independente do limite de 64 entradas do browser
por pasta.

Não use `music queue /musica` no fluxo normal: esse já é o diretório padrão do
scan. Nesta rodada, não use `queue <diretório>` nem comandos por `ui.type`
depois de abrir o player. Registre contagem/estado sem títulos ou caminhos;
não tente reproduzir faixas.

Após consultar status/lista, use apenas dumps/captura para inspecionar a surface,
o header e o estado visual. Se surgir erro, WDT, reboot, serviço indisponível ou
uptime reiniciado, pare; não repita nem tente playback.

## 3. Surface e inspeção visual — sem acionar controles

A surface contém status, lista rolável de até 64 faixas e controles de toque:
`Previous`, `Play/Pause`, `Next`, `Seek -10s`, `Seek +10s`, `Volume -`,
`Volume +`, `Command` e `Play Selected`. A lista deve mostrar scrollbar quando exceder o painel;
inspecione a lista e seus limites por `ui.dump`/captura, sem interagir com ela.
Em portrait e landscape, confirme que os labels permanecem dentro de seus
controles/linhas; nomes excessivos podem terminar em pontos, mas não devem
invadir outros widgets nem aumentar a altura da linha. Somente a lista deve
rolar verticalmente.

Com o player aberto, confirme que o header global permanece visível e fixo.
Não faça arrastes, gestos, toques ou navegação por teclado nesta rodada. A
validação limita-se à inspeção passiva da surface, header, lista e controles.

Não abra o campo `Command` nem use o mapa de teclado nesta rodada: Enter executa
ações no contexto do player, e N/P/Space ou controles de tela podem disparar
reprodução. Evite toda entrada interativa no player após as consultas `status` e
`list` autorizadas acima.

### 3.1. Captura de tela e entrada durante a inspeção

Em uma rodada interativa autorizada, verifique a navegação do browser sem
confundir a posição visual da linha com o índice da queue recursiva:

1. Na raiz `/musica`, confirme que não há `..`; selecione uma pasta e pressione
   Enter ou P. Ambos devem abrir a pasta sem iniciar/alterar playback.
2. Dentro da pasta, confirme `..` no topo; Enter ou P nessa linha retorna ao pai
   (até `/musica`). Selecione um MP3 e confirme que setas/touch só movem o
   highlight do browser, sem mudar a faixa selecionada/em reprodução.
3. Em um MP3, Enter mantém o toggle existente: playing pausa, paused retoma e
   stopped inicia o MP3 destacado. P físico e o botão `Play Selected` iniciam o
   MP3 destacado mesmo quando outra faixa está tocando/pausada. A resolução deve
   corresponder pelo path do arquivo, não pelo índice visual do browser.
4. Confirme que Previous, Play/Pause, Next, seek, volume, Command, Esc e os
   comandos textuais existentes continuam operando como antes. Registre
    `music.status` antes/depois; não use `ui.type` para a consulta nem repita uma ação se o resultado
   for ambíguo ou houver erro/reboot.

### 3.2. Cenário Play Selected (registro de validação manual)

Este cenário não faz parte da inspeção passiva acima e requer autorização
específica para playback. **Registro 2026-11-02:** o usuário confirmou que tanto
o atalho físico `P` quanto o botão de tela `Play Selected` funcionaram
corretamente na imagem SHA-256
`67a2358a14fd3130df0564d272cf68e441ab4bc2ceaf32f43583bb26e8013daa`. Desde essa
compilação, somente follow-up de teste/documentação `09bdd39` mudou; produção
permaneceu igual. Evidência por relato do usuário, limitada a esses dois caminhos;
não inferir P virtual, outros periféricos/controles ou aceite integral.

Em uma execução autorizada, selecione outra faixa enquanto uma estiver
tocando/pausada e use `P` ou o botão `Play Selected`: a faixa destacada deve
iniciar do começo, uma única vez. Enter continua sendo toggle da faixa corrente (playing→pause,
paused→resume); em stopped inicia a faixa destacada. Alterar highlight sozinho
não deve iniciar áudio. Em fila vazia/seleção inválida, a ação deve falhar sem
    substituir playback. Verifique status/posição via `music.status`, sem enviar
comandos de controle duplicados. N permanece sem binding; comandos textuais
`music select` e `music play` devem manter o comportamento existente.

Com a surface do player aberta, **Enter alterna playback** (pausa/resume; inicia
a seleção somente quando stopped): ele não
serve para "acordar" a tela. Se a proteção de tela apagar o display, religue com
`screen.wake` e só então retome a inspeção — `ui.type` é recusado com
`screen_off` quando a tela está apagada, e Enter ativo pode iniciar playback.

A inspeção visual pode usar `screen.dump` pela ponte serial ou o endpoint HTTP
`GET /screenshot`:

```bash
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 screen.dump --out /tmp/player.bmp
curl -s -o /tmp/player-http.bmp http://<ip>/screenshot   # IP de wifi.status
```

As duas vias entregam o mesmo BMP bottom-up (`biHeight > 0`); não é preciso
inverter a imagem. Compare `sys.info` antes e depois de cada captura: uptime
reiniciado ou contadores zerados indicam reboot e encerram o cenário.

Mapeamento de teclado (físico e virtual):

| Tecla | Efeito |
| --- | --- |
| Enter | play/pause, alternando conforme o estado atual — **proibido nesta rodada** |
| ← / → | faixa anterior / próxima |
| ↑ / ↓ | move seleção/highlight na lista; não inicia playback |
| P | navega em `..`/pasta; em MP3 inicia o item destacado (Play Selected) |
| + / - | volume +1 / -1 (faixa 0..50) |
| Esc | fecha o player: foreground volta ao shell com stop/join e halt do playback |

Os itens de interação abaixo estão fora do escopo desta rodada e não devem ser
executados:

- toque: `ui.tap "Play/Pause"`, `ui.tap "Next"`, `ui.tap "Volume +"` e conferir o
  efeito em `music.status`/`ui.dump`;
- teclado: Enter, ←/→, ↑/↓, +/− e Esc. Esc deve retornar ao shell **na mesma
  sessão** (prompt/linha anterior preservados) e sem desmontar a ponte serial;
- `sys.info` antes e depois de cada bloco: uptime crescente, sem reboot/panic/WDT;
- reabrir com `music` inicia novo scan e não restaura a fila anterior;
- trocar de app com o player aberto encerra via stop/join; falha de stop mantém o
  player ativo e bloqueia a troca de foreground;
- scan cancelado ou falho não pode publicar fila parcial.

**Estado dos demais gates:** esta confirmação não valida Escape nem outras ações
físicas; consulte as seções próprias e não infira o aceite completo do player. O reboot
reportado após Esc ainda não tem causa demonstrada. Os logs `music: stop
stage=cancel_scan`, `scan_join_complete`, `player_stop` (ou `stop failed
stage=...`) são evidência diagnóstica para uma rodada autorizada futura; host
tests não provam ausência de reboot no Tab5. A regressão de ESC tem cenário
próprio e autorização explícita na seção 4.

### Fora do escopo desta rodada: autorização explícita obrigatória

Os testes acústicos/de playback abaixo só podem ser executados em outra rodada,
após autorização explícita. A confirmação “Sim, sem playback” vale somente para
a rodada atual e não autoriza nenhum desses passos.

- verificar áudio audível no speaker integrado;
- confirmar ausência de cortes/engasgos audíveis durante uma faixa inteira,
  inclusive com leitura de SD, Wi-Fi e redraw da UI ativos (correção do DMA de
  I2S para `8 x 512`, ~85 ms; o usuário já confirmou em fw `9bfd6410` que os
  cortes pararam);
- verificar inserção/remoção de fone e rota pelo jack de 3,5 mm;
- testar pause/resume/seek/volume/stop e observar status/progresso;
- confirmar amplificador desligado quando ocioso, sem ruído persistente;
- observar aquecimento/consumo anormal, coexistência com Wi-Fi/BLE e stop;
- verificar que o MP3 original permanece byte-idêntico;
- poweroff durante playback só em cenário separado e explicitamente aprovado.

Não marcar aceite físico completo sem registrar confirmação humana de speaker e
jack. Host tests/build não substituem este roteiro.

## 4. ESC e reabertura do player — regressão do reboot `should not return`

Cenário autorizado apenas com confirmação explícita. Ele cobre a correção do
reboot `FreeRTOS Task "music_player" should not return, Aborting now!` após Esc
(fw `26499c37`, corrigido e validado em fw `627f9499`): o worker agora parkeia depois de sinalizar completion e o
`stop()` é o dono único do teardown (`vTaskDeleteWithCaps`).

**Pré-requisito:** firmware construído a partir deste snapshot, seção 1 concluída
(`safe_mode=false`, `interrupted_boots=0`, uptime crescente).

**Critérios objetivos por cenário** (todos obrigatórios):

- `sys.info` depois do Esc com uptime **maior** que o anterior e sem reset de
  contadores; nenhum reboot, panic ou WDT;
- log serial sem `should not return`, `panic_abort`, `Guru Meditation` ou
  `Backtrace`;
- log serial mostra `music: stop stage=cancel_scan`, `scan_join_complete`
  (quando havia scan), `player_stop` e `stop result=complete`;
- `term.dump` mostra o prompt do shell (`/$ `) depois do Esc;
- a ponte Serial-JTAG continua respondendo (`ping`) sem reconectar a porta.

### 4.1. ESC com o player parado

Os cenários de playback abaixo são históricos/condicionados. Para novas
execuções, consulte estado/progresso por `music.status` (não `ui.type`) e
use somente a interface de controle explicitamente autorizada. `ui.type`
enquanto o player está aberto injeta Enter e pode iniciar/alternar áudio;
ele não envia comandos de shell nem Escape.

1. `ui.type "music"`, aguarde `queue_scanning=false` em `music.status` (estado
   `stopped`).
2. Registre `sys.info`.
3. Envie Escape com o teclado físico; `ui.type` não envia Escape.
4. Confirme `term.dump` com o prompt de volta, `music.status` recusado (app não
   está em foreground) e `sys.info` sem reboot.

### 4.2. ESC tocando

1. Reabra `music`, aguarde o scan e inicie uma faixa pelo controle autorizado
   (não por `ui.type`).
2. Confirme `music.status` com estado `playing` e progresso crescendo.
3. Envie Escape durante a reprodução.
4. Critérios da seção + áudio silencioso depois do Esc (sem necessidade de
   medir qualidade) e `sys.info` com uptime monotônico.

### 4.3. ESC com scan em voo

1. Feche e reabra o player, disparando um novo scan.
2. Envie Escape imediatamente após `ui.type "music"` (antes de
   `queue: scan complete`).
3. Confirme `stop stage=cancel_scan`, ausência de publicação de fila parcial
   (`music.status` recusado após o fechamento), prompt de volta e uptime
   monotônico.

### 4.4. Ciclo ESC → reabrir → tocar, 5 vezes

Repita cinco vezes: abrir `music`, aguardar o scan, iniciar pelo controle
autorizado, confirmar
`playing`, Escape, confirmar prompt, `sys.info`. Critérios:

- uptime estritamente crescente em todas as leituras, sem reset de contadores;
- nenhuma linha `should not return`/`panic_abort`/`Guru` no log acumulado;
- cada reabertura executa novo scan (`queue: scan complete` com a lista atual) e
  o play volta a funcionar;
- heap livre em `sys.info` sem queda monotônica entre ciclos (variação pequena e
  sem tendência de esgotamento);
- ao final, `ping` e `ui.dump` respondem; encerrar com `music` fechado.

**Falha do cenário:** qualquer reboot, ausência de resposta NDJSON, prompt que
não volta, `close failed` persistente ou ausência de novo scan após reabrir.
Registre a captura e não repita automaticamente.

## 5. Duração da faixa e barra de progresso — REQ-MUSIC-06

Cenário de playback; exige a mesma autorização explícita da seção 4 e um boot
normal comprovado (seção 1). Cobre a correção em que o total da faixa era
recalculado a cada frame com o bitrate daquele frame, fazendo `elapsed/total`
(e a barra) pular para frente e para trás em arquivos VBR.

**Pré-requisito:** firmware construído a partir deste snapshot e faixa MP3
(preferencialmente VBR) presente em `/musica`.

1. Abra o player (`ui.type "music"`), aguarde `queue_scanning=false` e inicie a
   faixa pelo controle autorizado (não por `ui.type`).
2. Confirme no log serial uma única linha
   `music: playback duration generation=... duration_ms=... has_toc=... source=probe`
   (ou `source=pending_fallback`, seguida de
   `music: playback duration fallback generation=... duration_ms=...` quando o
   probe não resolveu o arquivo). Não deve haver nova linha de duração por frame.
   Em faixa VBR **sem** tag Xing/Info, o probe amostra 32 janelas de 2048 B
   distribuídas pelo arquivo inteiro (69632 B de I/O por abertura) em vez de só
   os primeiros 4 KiB: compare o total publicado com a duração real medida no
   host (`ffprobe -count_frames` × spf/hz sobre o mesmo arquivo). O total deve
   ficar dentro de 2 %; o estimador antigo (só o head) ficava 9–16 % abaixo.
3. Com a faixa tocando, amostre o rótulo `progress:` 10 vezes em 10 s (1 amostra
   por segundo) via `term.dump` ou `screen.dump`. Critérios:
   - o total (segunda parte de `elapsed/total`) é **idêntico** nas 10 amostras;
   - `elapsed` cresce de forma monotônica (nunca regride) e a barra avança no
     mesmo sentido;
   - nenhuma amostra mostra total alternando entre valores diferentes.
4. Deixe a faixa terminar naturalmente (sem seek). No EOF, a última amostra
   `progress:` deve satisfazer `elapsed == total` com tolerância de ±1 s; a
   reprodução para e `music.status` mostra `stopped`.
5. Registre `sys.info` antes/depois: uptime crescente, sem reboot/panic/WDT, e
   `ping` da ponte Serial-JTAG respondendo.

**Falha do cenário:** total variando entre amostras, `elapsed` regredindo,
divergência maior que 1 s no EOF, reboot ou ausência de resposta NDJSON.
Registre as amostras e o log e não repita automaticamente.

## 6. Enter no player — pausa, retomada e faixa selecionada

Validação Tab5 pendente para `REQ-ENTER-PLAYPAUSE`, `REQ-ENTER-SELECTED-START`
e `REQ-ENTER-INVALID-NOOP`. Requer uma rodada que autorize playback, pois o
caso `stopped + faixa selecionada` inicia áudio. Com fila concluída, selecionar
uma faixa diferente da atual usando ↑/↓ e pressionar Enter deve iniciar essa
faixa; durante `playing`, Enter pausa; durante `paused`, Enter retoma. Com fila
vazia ou seleção inválida, Enter não deve iniciar faixa arbitrária. Confirmar
cada estado por `music.status`, observar uptime/log serial e parar conforme o
protocolo de validação autorizado. Preservar os limites de segurança e a regra
de não repetir automaticamente após panic/reboot.

Regressão host adicional: entre selecionar a faixa (por seta ou toque) e Enter,
o timer pode renderizar um snapshot de status produzido antes da seleção. Esse
snapshot não deve trocar o highlight nem a faixa tocada. A reprodução física
relatada anteriormente não deve ser repetida sem nova autorização; a correção
permanece pendente de validação independente no Tab5.

## 5.1. Seek em faixa VBR — AC-MUSIC-16

Cenário de playback; exige a mesma autorização explícita da seção 4, boot normal
comprovado (seção 1) e uma faixa **VBR** em `/musica`. Cobre o defeito em que
`seek <segundos>` fazia `lseek` proporcional por byte, caía no meio de um frame,
o minimp3 devolvia `samples=0` para o frame já consumido e a reprodução morria
com `state: error / error_stage: decode`.

1. Abra o player (`ui.type "music"`), aguarde `queue_scanning=false` e inicie a
   faixa pelo controle autorizado (não por `ui.type`).
2. Envie `seek 60` e confirme: a faixa **continua tocando**, `music.status`
   mostra `playing` (nunca `error`) e a posição fica próxima de 60 s (±1 s).
3. Repita com alvos **dentro da duração exibida** no rótulo `progress:
   <elapsed>/<total>`: `seek 60` (≈ metade da faixa curta), `seek 100` (≈ 80 %)
   e `seek <total-10>` (perto do fim). Alvos acima do total não são atingíveis:
   `player::seek` clampa no fim da faixa e o rótulo mostra ≈ o cap, não o alvo
   pedido — registre o alvo realmente efetivo. Em cada seek confirme que a
   reprodução **continua** (`state: playing`, sem `error_stage: decode`) e que a
   posição fica ≈ ao alvo (±1 s).
4. No log serial, cada seek deve emitir exatamente uma linha
   `music: playback seek generation=... target_ms=... byte=... toc_position_ms=... discard_ms=...`.
   Com TOC, `discard_ms` deve ser pequeno (≤ 5000); sem TOC, `discard_ms=0`.
5. Confirme em `term.dump`/`screen.dump` que o `progress:` mostra a posição no
    alvo e que o total não muda, e que `music.status` nunca exibe
    `state: error`; para o estágio específico, consulte `music.diagnostics`.
6. Registre `sys.info` antes/depois: uptime crescente, sem reboot/panic/WDT, e
   `ping` da ponte Serial-JTAG respondendo.

**Medição física já registrada (fw `c822b8cc`, `/dev/ttyACM0`):** na faixa longa
o total ficou constante em 271 s em 6 amostras e `seek 150` resultou em 152 s
ainda tocando; na faixa de 121 s, `seek 60`/`seek 100`/`seek 110` resultaram em
61 s/102 s/111 s e, após o fim natural, `music status` mostrou `stopped` com
`121s / 121 s`. Essa medição cobre a regra de clamping e a continuidade após o
seek; resta apenas o aceite visual (áudio/barra) no dispositivo.

**Falha do cenário:** `state: error`, `error_stage: decode`, posição que não
avança depois do seek, `discard_ms > 5000`, reboot ou ausência de resposta
NDJSON. Registre o alvo, as amostras e o log e não repita automaticamente.

## Snapshot de diagnóstico (read-only)

Com o app/player já aberto, envie pela ponte Serial-JTAG uma linha NDJSON:

```bash
{"rid":"audio-diag","type":"music.diagnostics"}
```

O resultado deve ter `version:1`, `read_only:true`, geração e contadores/timings
RAM (microsegundos) de read/decode/codec-start/write, submétricas independentes
de route-check/backend-write e pause. `generation` é a
última solicitação de playback aceita na fila; campos de operação, pausa,
falhas e `failure_stage` pertencem estritamente a ela e são zerados no enqueue
aceito, antes de o worker começar. `generations_queued/started/retired` são
acumuladores da sessão e não são zerados ao trocar de faixa. Queue-full rejeitado
não avança geração nem queued count. Stop/halt invalidam trabalho ativo, mas não
criam geração enfileirada nem limpam os diagnósticos da última solicitação aceita;
trabalho superseded pode incrementar somente o retired acumulado e nunca publicar
métricas/erros na geração atual. Falha pré-PCM registra o estágio e contador da
geração nova, mantendo zero nas operações não realizadas. Para cada operação
read/decode/codec-start/write, `*_time_us` é o acumulado saturante e `*_max_us`
é a maior duração observada; cada máximo deve ser menor ou igual ao total.
`route_check_*` conta somente quando `update_route()` realmente é chamado (não
quando o throttle retorna cedo); `backend_write_*` conta somente se o callback
de escrita é chamado. Estes campos apenas separam tempos e não diagnosticam
causalidade nem alteram áudio.
A consulta nunca
inicia o app/player e deve retornar `player_not_running` se o worker não foi
inicializado. `esp_timer_get_time()` é consultado em torno de cada operação
medida; esse custo de timestamp ocorre no worker, não no caminho de snapshot/UI.
Os contadores saturam em `UINT64_MAX`. Não use esta verificação para autorizar
playback físico: flash e reprodução exigem autorização explícita separada.

## Trace temporal (read-only, sem playback)

Com o player aberto, solicitar páginas sem abrir monitor concorrente:

```bash
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 '{"rid":"audio-trace","type":"music.trace","limit":16}'
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 '{"rid":"audio-trace-next","type":"music.trace","cursor":16,"limit":16}'
```

Usar `next` retornado como cursor da próxima página. `truncated` indica cursor
anterior ao ring; `lost` conta sobrescritas/slots instáveis. Este trace mede
somente operações do worker e não atesta underrun/DMA físico nem identifica
causa isoladamente. Não executar playback para gerar eventos sem autorização
separada. Comparar timestamps monotônicos e durações de SD read/decode/codec
start/output write por geração, preservando a resposta e log de boot.

### Registro da rodada de trocas sem stutter — 2026-11-02

Firmware SHA-256 `67a2358a14fd3130df0564d272cf68e441ab4bc2ceaf32f43583bb26e8013daa`;
snapshot HEAD `d2236a4b94bc7a2ada4482991732dcd6d7a00d66` + diff
`1b1710ba0bf7a87d8eb60570d2b74bfcccee0784b46fff0ec0e2f23105028f49`.
O usuário fez várias trocas, esperou segundos e também trocou rapidamente; relatou
nenhum engasgo. Resultado: teste manual direcionado de stutter nas trocas relatadas
PASS por observação do usuário; não implica aceite completo do player.

Causa-raiz do stutter permanece desconhecida e não houve correção funcional.
O resultado é somente a observação subjetiva desta rodada, não prova ausência de
underrun DMA ou de stutter em outras condições.

`music.diagnostics`: generation=7; queued/started/retired=7/7/6; failures=0;
max read/decode/codec-start/write = 5465/11603/18720/27827 us; route
35/max 1341 us; backend 734/max 27803 us. `sys.info` uptime 00:20:03→00:20:18,
safe_mode=false, interrupted_boots=0, recovery_persisted=true.
Trace `/tmp/opencode/music-trace-user-run-20261102.jsonl`, SHA-256
`224166835af56b2e39e585dfdd79869152131d1cac63ca9af151754d9efda7d6`: 4 páginas,
64 eventos; primeira `truncated=true`, `lost=10176`, eventos capturados somente
seq 10176..10239 da generation 7. Transições anteriores foram sobrescritas;
trace do worker não detecta underrun DMA nem prova causalidade. Nenhuma correção
funcional foi feita. Causa raiz não confirmada; speaker/jack e demais gates de
aceite integral continuam separados.
