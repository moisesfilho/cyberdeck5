# Arquitetura

O `cyberdeck5` é um firmware monolítico. A aplicação é inicializada diretamente
por `app_main`, monta uma única tela LVGL e mantém os módulos de infraestrutura
necessários para a primeira ferramenta.

## Princípios

- Usar o hardware diretamente quando isso reduzir camadas e alocações.
- Manter operações bloqueantes fora da thread da UI.
- Não criar extensibilidade antes de existir uma segunda ferramenta real.
- Preferir buffers limitados e descarte explícito de dados antigos.
- O `cat` local valida o caminho e o tamanho antes da saída, abre cada componente
  por descritor sem seguir symlinks, lê somente do descritor validado e rejeita
  conservadoramente VFS sem essas garantias; sua saída é sanitizada antes do LVGL
  regulares em chunks bounded de 1024 bytes e delega o I/O a um worker com fila
  bounded; somente o resultado é entregue à UI por `lv_async_call`.

## Organização semântica

O componente ESP-IDF único é organizado por contexto, não por componentes
ESP-IDF adicionais. `src/features/` contém os fluxos de produto (shell, Wi-Fi,
SSH e screenshot), enquanto `src/platform/` contém as integrações de entrada,
display, sensores, logging e networking. Os headers espelham essa árvore em
`include/features/` e `include/platform/`. A lógica pura deve permanecer
host-testável; `app_main` é o ponto de composição das partes concretas.

`managed_components/`, incluindo `m5stack_tab5` e `sock_utils`, permanece fora
dessa reorganização e continua sendo gerenciado pelo ESP-IDF.

## Fluxo de boot

1. Inicializar NVS.
2. Inicializar display e LVGL pelo BSP do Tab5.
3. Criar a tela TUI monocromática.
4. Sob lock do display: criar a UI e inicializar a protecao de tela (`screen_off_init` com timeout padrao de 2 minutos e brilho 20%); restaurar o timeout persistido do NVS antes de iniciar o timer.
5. Iniciar o reader INA226; falha de inicializacao registra log e nao aborta o restante do boot; ausencia no probe publica `absent`, enquanto falha de leitura apos o startup publica `unavailable`.
6. Inicializar Wi-Fi e reconexao a partir do SD; o header recebe estados de Wi-Fi por callback e atualiza somente o icone: claro quando Wi-Fi esta habilitado, conectado e possui IP, e escuro nos demais estados. O SSID nao e renderizado no header.
7. Aguardar conexao SSH iniciada pelo usuario.
8. Iniciar a ponte manual USB Serial-JTAG (`bridge_start`): task propria, iniciada por ultimo; falha aqui registra aviso e nao derruba o boot.

## Bateria

`battery_status.cpp` concentra a lógica pura: recebe a tensão medida do
barramento em `bus_voltage_mv` e satura a faixa aprovada `6000..8230` mV em
`0..100`. Corrente positiva é descarregando, negativa é carregando e corrente
zero ou indeterminada é neutral. No estado neutral, a UI conserva somente o
percentual numérico, sem glyph ou texto de estado. Ausência é produzida apenas
por `present == false`; falha de leitura é `unavailable`, distinta de neutral,
e nenhum percentual é fabricado. O módulo não depende de ESP-IDF, FreeRTOS,
I2C ou LVGL.

`ina226_reader.cpp` é a integração separada de hardware. Ela usa o barramento
I2C do BSP no endereço `0x41`, lê o registro de tensão `0x02` (1,25 mV/LSB),
grava configuração `0x4527` e calibração `0x0D55`, e executa as leituras de
tensão e corrente numa task FreeRTOS a cada
1000 ms. Um mutex protege o snapshot copiado consultado pela UI. Falha de
probe/identificação publica `absent` somente por presença explícita; uma leitura
que falha depois do startup publica `unavailable`; em `app_main` a falha de
inicialização é apenas registrada. O fluxo não persiste estado de carga e
não implementa controle ou proteção de bateria. Nenhuma transação I2C ocorre na
UI, e o reader não registra timer LVGL.

`cyberdeck_battery_protection.cpp` é a política pura de proteção de carga,
host-testável e sem dependências de ESP-IDF, FreeRTOS, I2C, NVS, LVGL ou BSP.
Ela define os estados `battery`, `external`, `charging`, `absent` e `unknown`,
com threshold de corrente ±15 mA, `external` >= 7900 mV e `absent` >= 8330 mV
exigindo 5 votos consecutivos. A proteção só atua quando as três condições
estão satisfeitas: estado `charging`, percentual >= 90 e tensão >= 8200 mV.
Uma vez ativa, permanece travada pela histerese e libera apenas quando o
percentual cai para <= 85 (ou quando o estado deixa de ser `charging`). A única
opção persistida no NVS é `protection_enabled` (default `true`); desabilitá-la
religa o carregador imediatamente e exige reativação explícita. Falhas de
leitura não desligam `CHG_EN` nem perdem o último estado seguro.

`battery_protection.cpp` é o adaptador exclusivo de hardware: inicializa o
Expander B via `bsp_io_expander1_init()` (endereço I2C 0x44), configura
`CHG_STAT` no pin 6 como entrada active-low com pull-up e `CHG_EN` no pin 7
como saída push-pull com valor inicial alto (carregador habilitado). Integra o
reader INA226 (sensor-only), a política pura, a persistência NVS da opção
`enabled` e expõe snapshots para a UI via timer LVGL de 1 s. Falhas de I2C,
NVS ou leitura de `CHG_STAT` são registradas em log sem desligar `CHG_EN` nem
perder o último snapshot seguro.

## Battery Protection

O firmware implementa **battery protection** (proteção de carga) para prolongar a vida útil da bateria. A política pura (`cyberdeck_battery_protection`) define os estados `battery`, `external`, `charging`, `absent` e `unknown`, com threshold de corrente ±15 mA, `external` ≥ 7900 mV e `absent` ≥ 8330 mV exigindo 5 votos consecutivos. A proteção ativa somente quando as três condições são satisfeitas: estado `charging`, percentual ≥ 90 e tensão ≥ 8200 mV. Uma vez ativa, permanece travada pela histerese e libera apenas quando o percentual cai para ≤ 85 (ou quando a carga para). A única opção persistida no NVS é `protection_enabled` (default `true`); desabilitá-la religa o carregador imediatamente e exige reativação explícita. Falhas de leitura não desligam `CHG_EN` nem perdem o último estado seguro.

A rastreabilidade desse fluxo é mantida em `code-map.md`: REQ-BAT-001 e
REQ-BAT-002 fixam a matemática e os estados, REQ-BAT-003 fixa a apresentação
neutral, REQ-BAT-004 fixa boot não fatal e ausência de controle de carregador,
REQ-BAT-005 fixa a integração INA226, REQ-BAT-006 exige que esta documentação
e o mapa permaneçam alinhados aos contratos host, REQ-BAT-007 fixa o Expander B
e pinos CHG_STAT/CHG_EN, REQ-BAT-008 fixa os estados e thresholds, REQ-BAT-009
fixa a histerese 90/85 e fail-safe, e REQ-BAT-010 fixa NVS, timer UI, shell e
`ui.type`.

## Screen Protection

The firmware protects the display from burn-in and saves power by
turning it off after an inactivity timeout (default 2 minutes,
configurable 0..1440). The implementation has two layers:

- `cyberdeck_screen_protection`: pure policy/state, host-testable.
  Manages the timeout value, the on/off state, and the NVS
  persistence of both `effective_minutes` and `last_positive_minutes`.
  Setting the timeout to 0 disables the auto-off without losing the
  last positive value.
- `screen_off`: LVGL/BSP adapter. Creates a full-screen black LVGL
  object, pauses/resumes a 1-second inactivity timer, and handles
  double-tap (400 ms window) to turn the display back on. A dedicated
  FreeRTOS task (`screen_nvs`) writes the timeout to NVS outside the
  LVGL task. The boot order restores the persisted timeout before the
  timer starts.

The `screen` shell commands route to this layer:

- `screen on` — calls `screen_off_turn_on()` (restores the previous
  screen and brightness).
- `screen off` — calls `screen_off_turn_off()` (loads the black
  overlay).
- `screen timeout <0-1440>` — calls `screen_off_set_timeout_minutes()`
  and persists the value; 0 disables the timer.

The screen is also turned off/on by Wi-Fi state transitions and the
SSH session lifecycle. The screenshot endpoint works while the screen
is off because it captures the LVGL framebuffer directly.

## Wi-Fi: dispatch e persistência

O callback do evento `GOT_IP` não executa trabalho pesado na task `sys_evt`.
Ele apenas publica um snapshot do evento no dispatch de Wi-Fi. O coordinator
processa esse snapshot no worker próprio, serializa as operações de persistência
e coordena os efeitos derivados da conexão, mantendo a thread da UI e a task de
eventos livres de I/O, mutexes longos e chamadas de rede.

Cada operação assíncrona tem ownership explícito: o produtor transfere a
mensagem ao dispatch/coordinator, que a confirma (`ack`) ou a libera conforme o
resultado. Falhas de enfileiramento e operações retryable seguem a política de
retry do coordinator; itens que não podem ser processados são descartados sem
deixar ownership ambíguo. Tokens de conexão, scan e persistência identificam a
geração corrente, portanto callbacks atrasados ou de uma tentativa cancelada
não alteram o estado atual.

O scan é assíncrono: o callback copia o snapshot dos resultados e o entrega ao
contexto que controla a UI, onde SSIDs são deduplicados e ordenados. A busca
também respeita cancelamento, timeout e a geração da tentativa. Persistência só
ocorre após `connected` e `has_ip`; em falhas de conexão ou de persistência, o
coordinator executa rollback do estado transitório. O fluxo de esquecimento
usa wipe explícito da credencial persistida, sem exibir ou registrar a senha.

Essa separação corrige o reboot observado logo após `GOT_IP`: o trabalho pesado
foi removido de `sys_evt`, enquanto callbacks tardios, retries e cancelamentos
passaram a ser filtrados por tokens e processados pelo coordinator.

## Screenshot HTTP

Quando o Wi-Fi está conectado e possui endereço IP, o firmware disponibiliza
`GET /screenshot` na porta 80. O endpoint aceita somente clientes da rede
privada/local (incluindo IPv4 mapeado em IPv6 e endereços locais IPv6) e retorna
um BMP 24-bit da tela LVGL. O arquivo usa o formato bottom-up do BMP: `biHeight`
é positivo e as linhas são emitidas da última para a primeira. As requisições
são serializadas, e o servidor é parado quando o Wi-Fi perde o endereço IP.

Para salvar a captura a partir de um host na mesma LAN:

```bash
curl --fail --output screenshot.bmp http://DEVICE_IP/screenshot
```

## Ponte manual USB Serial-JTAG NDJSON

A ponte roda na task `serial_brg` (prio 3, 8192 bytes de stack), fora da
stack do LVGL, iniciada por `bridge_start()` ao fim de `app_main`. O driver
USB Serial-JTAG so e instalado se ainda nao estiver ativo — o console
`ESP_LOG` compartilha a mesma porta —; frames e logs são serializados por um
mutex de escrita, e `esp_log_set_vprintf` redireciona o log para a mesma
porta com conversão `\n` → `\r\n` (o driver direto não passa pela conversão
do VFS).

O protocolo é NDJSON com uma linha por mensagem, limite de 4096 bytes e
correlação por `rid`. O parser JSON é próprio (sem cJSON), valida UTF-8
estrito, escapes `\uXXXX` com pares sintéticos e object/alvo de campos de
topo (`rid`, `type`, `text`, `x`, `y`, `target`, `symbol`). Sob
`bsp_display_lock`, `ui.click`/`ui.tap` resolvem o alvo (hit-test por
coordenada, ou texto visível no ancestral clicável mais próximo) e agendam
`LV_EVENT_CLICKED` com `lv_async_call`; `ui.type`/`ui.clear` injetam eventos
na fila bounded do teclado (capacidade 8) com 30 ms entre eventos;
`wifi.scan` usa o scan assíncrono do `wifi_mgr` com semáforo estático e
timeout de 8 s (cancelamento no timeout); `screen.dump` captura RGB565 por
`lv_snapshot_take`, monta o BMP 24-bit e transmite chunks de 1024 bytes com
CRC IEEE nos frames `start`/`chunk`/`end`.

`fs.write` grava bytes Base64 no `/sdcard` para testes e automação. O payload
decodificado é limitado a 2048 bytes, o caminho é confinado ao cartão e a
escrita usa temporário exclusivo; no host o commit usa `renameat`, enquanto no
FATFS o firmware não substitui destinos existentes sem garantia de atomicidade.
O CLI aceita `--input`, `--stdin` ou `--data` e retorna tamanho e CRC32.

A lógica pura (parse/envelopes/BMP/CRC/`LineAssembler`) é host-testável e
autocontida: fora de `ESP_PLATFORM` ela não referencia funções de
`screenshot_bmp_*` — stride/size/header/conversão são espelhados localmente
com fórmulas idênticas — porque três dos quatro testes host linkam apenas a
ponte.

## SSH

`ssh_client` usa uma task FreeRTOS dedicada, filas de entrada e senha e
callbacks de saída/estado. O callback de UI é executado sob o lock do display
para que widgets LVGL não sejam alterados concorrentemente.

## Header de estado

The header uses a direct 30/40/30 title/clock/right grid. The right cell owns
two children in order: the Wi-Fi icon and then the battery group. The Wi-Fi icon
is drawn with LVGL primitives and is light only when Wi-Fi is enabled,
connected, and has an IP address; it is muted otherwise. The SSID is never
rendered. Wi-Fi-driven LVGL updates hold the BSP display lock. SSH state and
errors are rendered in the terminal and written to the event log. Network
diagnostics remain available through the `wifi` shell command, outside the
header.

The battery group displays one semantic state icon (`LV_SYMBOL_MINUS` while
discharging, `LV_SYMBOL_CHARGE` while charging) and the numeric clamped
percentage. In the neutral state it keeps the percentage visible while rendering
no state glyph or state text. It never selects an LVGL battery-level glyph. It
is refreshed from the reader's synchronized snapshot and is
hidden as a whole for an absent sensor or failed read. The Wi-Fi layout responds
to `LV_EVENT_SIZE_CHANGED` within its allocated portion of the right cell, with
visible pixels approximately 2 px from that subcell's edge.

### Wi-Fi icon geometry

The Wi-Fi indicator keeps its existing size, state semantics, and colors. The
lower visual limit of the upper arcs is `center_y - outer_radius * (1 -
sin(45°))`; the dot is placed 1.5 px below that limit (an accepted range of
1–2 px). The same geometry calculation is
used when the icon is created and when it is repositioned after
`LV_EVENT_SIZE_CHANGED`; resize therefore changes placement only, not the
indicator's appearance or state. The approved vertical anchor is
`center_y = box_height - 12.5px`; for the 42px header this is `29.5px`, which
optically aligns the icon with the title and clock. This anchor preserves the
existing X position, size, gap, colors, and state semantics; resize continues
to update placement without changing those properties.
