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

**Escopo desta rodada:** abrir o player, consultar status/lista e inspecionar a
surface. Não reproduza áudio. São permitidos somente `music` sem argumentos,
`music status`, `music list` e inspeção não interativa por `term.dump`, `ui.dump`
ou captura de tela. Não envie `music play`, `play`, `resume` ou qualquer comando
equivalente; não toque em `Play/Pause`, `Next` ou outro controle de playback; não
pressione fisicamente/virtualmente Enter, N, P ou Space no player, nem execute
qualquer ação que possa iniciar ou retomar a reprodução. Use `ui.type` somente
para os três comandos permitidos acima; não digite no campo Command. Se o
foco/estado tornar uma ação ambígua, não a execute.

Pré-condição: boot normal comprovado e sem reboot/WDT durante a captura.

`music` sem argumentos é o único ponto de entrada do player: ativa o app
foreground `cyberdeck.music`, abre a surface dedicada e inicia um scan recursivo
de `/musica` (profundidade máxima 8, ordem lexicográfica, limite de 64 faixas).
Não é necessário — e não basta — `app start cyberdeck.music`: o start isolado
apenas sobe o app, sem abrir a surface nem iniciar o scan. `player` é alias do
mesmo fluxo.

Subcomandos (`music status|list|play|...`) só são aceitos enquanto o player é o
foreground ativo; fora disso a resposta é
`app: command requires active foreground`.

O console e a ponte Serial-JTAG permanecem vivos com o player aberto. Isso é
requisito do contrato: `ui.type`, `term.dump` e `sys.info` devem continuar
respondendo enquanto a surface está na tela.

```text
sys.info
ui.type "music"
term.dump          # esperado: music: opening player
ui.dump            # surface do player com status, lista rolável e controles
ui.type "music status"
term.dump          # esperado: queue: scan complete + queue: N tracks
ui.type "music list"
term.dump          # lista limitada a 64 faixas
```

Aguarde `queue: scan complete` em `music status` antes de qualquer outro comando
de fila. Com o scan em andamento, `music` e `music queue <dir>` são rejeitados com
`scan unavailable or another scan is active`; não repetir durante a coleta. A
surface mostra o status, lista rolável de até 64 faixas e controles; `music
status` acrescenta `queue: truncated at 64 tracks` quando o limite é atingido.

Não use `music queue /musica` no fluxo normal: esse já é o diretório padrão do
scan. Nesta rodada, não use `queue <diretório>` nem qualquer comando além de
`music`, `music status` e `music list`. Registre o resultado do scan e a
identidade das faixas somente como inspeção; não tente reproduzi-las.

Após consultar status/lista, use apenas dumps/captura para inspecionar a surface,
o header e o estado visual. Se surgir erro, WDT, reboot, serviço indisponível ou
uptime reiniciado, pare; não repita nem tente playback.

## 3. Surface e inspeção visual — sem acionar controles

A surface contém status, lista rolável de até 64 faixas e controles de toque:
`Previous`, `Play/Pause`, `Next`, `Seek -10s`, `Seek +10s`, `Volume -`,
`Volume +` e `Command`. A lista deve mostrar scrollbar quando exceder o painel;
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

Com a surface do player aberta, **Enter inicia playback** (play/pause): ele não
serve para "acordar" a tela. Se a proteção de tela apagar o display, religue com
injeção vazia (`ui.type ""`) e só então retome a inspeção — esse evento é
consumido apenas para religar e não executa comando.

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
| ← / → | seek -10 s / +10 s |
| ↑ / ↓ | volume +1 / -1 (faixa 0..50) |
| N / P | próxima / anterior — **proibido nesta rodada** |
| Esc | fecha o player: foreground volta ao shell com stop/join e halt do playback |

Os itens de interação abaixo estão fora do escopo desta rodada e não devem ser
executados:

- toque: `ui.tap "Play/Pause"`, `ui.tap "Next"`, `ui.tap "Volume +"` e conferir o
  efeito em `music status`/`ui.dump`;
- teclado: Enter, ←/→, ↑/↓, N/P e Esc. Esc deve retornar ao shell **na mesma
  sessão** (prompt/linha anterior preservados) e sem desmontar a ponte serial;
- `sys.info` antes e depois de cada bloco: uptime crescente, sem reboot/panic/WDT;
- reabrir com `music` inicia novo scan e não restaura a fila anterior;
- trocar de app com o player aberto encerra via stop/join; falha de stop mantém o
  player ativo e bloqueia a troca de foreground;
- scan cancelado ou falho não pode publicar fila parcial.

**Estado deste ciclo:** não enviar Escape nem fazer nova ação física. O reboot
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

1. `ui.type "music"`, aguarde `queue: scan complete` e `music status` (estado
   `stopped`).
2. Registre `sys.info`.
3. Envie Escape (`ui.type ""` não envia ESC; use o teclado físico ou
   `ui.type` com o comando de shell equivalente ao Escape usado na rodada).
4. Confirme `term.dump` com o prompt de volta, `music status` recusado (app não
   está em foreground) e `sys.info` sem reboot.

### 4.2. ESC tocando

1. Reabra `music`, aguarde o scan e envie `music play 1`.
2. Confirme `music status` com estado `playing` e progresso crescendo.
3. Envie Escape durante a reprodução.
4. Critérios da seção + áudio silencioso depois do Esc (sem necessidade de
   medir qualidade) e `sys.info` com uptime monotônico.

### 4.3. ESC com scan em voo

1. Feche e reabra o player, disparando um novo scan.
2. Envie Escape imediatamente após `ui.type "music"` (antes de
   `queue: scan complete`).
3. Confirme `stop stage=cancel_scan`, ausência de publicação de fila parcial
   (`music status` recusado após o fechamento), prompt de volta e uptime
   monotônico.

### 4.4. Ciclo ESC → reabrir → tocar, 5 vezes

Repita cinco vezes: abrir `music`, aguardar o scan, `music play 1`, confirmar
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
