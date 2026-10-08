# Arquitetura

O `cyberdeck5` é um firmware para um único alvo, o M5Stack Tab5, que está sendo
organizado como um OS embarcado simplificado: `platform/hardware`,
`kernel/runtime`, `SDK` e `apps`. O runtime e as apps são compilados no
firmware. Consulte o [plano de transformação](OS-TRANSFORMATION-PLAN.pt-BR.md)
para fases, backlog e limites de escopo.

## Princípios

- Usar o hardware diretamente quando isso reduzir camadas e alocações.
- Manter operações bloqueantes fora da thread da UI.
- Não criar extensibilidade antes de existir uma segunda ferramenta real.
- Preferir buffers limitados e descarte explícito de dados antigos.
- Compilar apps contra um SDK predefinido, começando por `storage` com leitura
  bounded e `logger`; não expor VFS, LVGL ou handles de hardware diretamente.
- Preservar output/reply compatível com apps atuais, sobretudo SSH, antes de
  fixar um contrato universal de command apps.

## Organização semântica

O componente ESP-IDF continua único e é organizado por contexto, sem criar
componentes ESP-IDF adicionais. `src/apps/` contém os fluxos do produto
(shell, Wi-Fi, SSH e screenshot), enquanto `src/platform/` contém as
integrações de entrada, display, sensores, logging e networking. Os headers
espelham essa árvore em `include/apps/` e `include/platform/`. A lógica
pura deve permanecer testável no host; `app_main` é o ponto de composição das
partes concretas.

### Camada visual

Os widgets LVGL são mantidos separados dos serviços e comandos. `cyberdeck_ui.cpp`
é a fachada de composição e orquestra callbacks, estado de entrada e serviços;
`cyberdeck_header_view.cpp` cria e atualiza exclusivamente o header; e
`cyberdeck_terminal_view.cpp` cria o textarea e o teclado virtual. Esses views
recebem dados já resolvidos e callbacks de interação, mas não incluem
`wifi_mgr`, `ssh_client`, `ble_mgr`, shell, NVS, I2C ou persistência. Serviços e
modelos de produto permanecem em `src/apps/`, sem dependência de LVGL.

O header mantém uma única `cyberdeck_font` em todos os labels de texto e uma
métrica de line-height compartilhada. A célula da bateria possui três labels:
ícone semântico, percentual base e uma cópia sobreposta do percentual para o
peso visual. O terceiro label usa a mesma fonte, fica deslocado 1 px e é
ignorado pelo layout; acompanha texto e visibilidade sem alterar a geometria.
Essa é a implementação de `REQ-HEADER-FONT-01..03`, coberta por
`AC-HEADER-FONT-01` em `tests/host/keymap/test_display_views.cpp`.

O handoff do teclado físico fica isolado em `cyberdeck_keyboard_dispatch.cpp`.
Ele possui a fila bounded de snapshots, o mutex de enqueue/rollback e o
agendamento por `lv_async_call`. O dispatcher não conhece sessões de shell,
SSH, Wi-Fi ou BLE.

O estado de sessão e as decisões de entrada ficam em
`src/apps/shell/cyberdeck_shell_session.{h,cpp}`. A `session` é livre de
LVGL e possui a linha corrente, o cursor, o histórico, o SSID Wi-Fi pendente, o
`cyberdeck_edit_line` de cada contexto (menu, SSH, senha, host key), a chave de
estado SSH e o buffer de passkey BLE. Ela decide o que cada tecla faz e executa
os comandos, sempre através da interface `host`: saída de terminal, pumps,
serviços de Wi-Fi/SSH/cat, shell local e BLE. `cyberdeck_ui.cpp` fica como
fachada: constrói e atualiza widgets, traduz `lv_key_t` para
`cyberdeck_shell_session::key`, mantém o pump de eventos e implementa o `host`.

O estado da sessão também é dela por completo: linha, cursor, histórico, SSID
pendente, editor por contexto, buffer de passkey BLE e os dois tokens de
conexão Wi-Fi. Nenhuma API de plataforma é chamada diretamente; a sessão usa
apenas as portas declaradas em `host`. Isso permite linká-la no host contra um
`host` falso, e `tests/host/keymap/test_shell_session.cpp` exercita o
comportamento de `handle_key`, `execute_line` e da inserção de texto dessa
forma.

O agendamento da reconexão BLE em background fica isolado em
`cyberdeck_ble_background.cpp` (`scheduler`): restauração de bonds no boot,
janela periódica de 10 s, matching por endereço+`addr_type`, teto de 3
tentativas por ciclo e preempção manual. Ele opera sobre o
`state_machine` BLE e enfileira comandos `ble_mgr`, sem tocar LVGL ou
terminal. A UI mantém apenas o pump de eventos (`on_ble_event`,
`process_ble_events`, `ble_submit_actions`) e roteia eventos e preempção
para o scheduler.

`components/m5stack_tab5/` é o BSP vendored do único alvo Tab5. Componentes
gerenciados pelo ESP-IDF permanecem fora desta reorganização. Não há
portabilidade dos Drivers do Tactility nesta fase.

## Fluxo de boot atual

O estado confirmado ainda concentra em `app_main` a montagem do SD, a
inicialização de NVS e recovery, o registro e o logging dos system apps, o
display, IMU/UI, screenshot, `screen_off`, bateria, teclado, brilho e o
startup normal ou safe mode dos serviços, seguido do checkpoint de recovery.

1. Montar o SD e validar o handle do BSP.
2. Inicializar NVS e recovery, registrar os system apps e iniciar o event log
   quando possível; falhas de NVS/recovery/logging preservam o diagnóstico.
3. Inicializar display/LVGL pelo BSP do Tab5.
4. Sob lock do display, iniciar IMU, UI, screenshot e proteção de tela
   (`screen_off_init`); falha nessas etapas interrompe o boot.
5. Inicializar bateria, teclado e brilho; falhas de bateria são não fatais.
6. Iniciar os serviços pelo supervisor no caminho normal ou a superfície de
   safe mode; falhas preservam o latch e adiam o checkpoint.
7. Registrar o checkpoint de recovery somente quando o startup estiver pronto.

### Alvo/backlog de composição

O alvo arquitetural é reduzir `app_main` a apenas mount do SD, NVS,
register/start dos system apps e checkpoint de recovery. Essa redução ainda é
backlog: não descreve o estado atual nem autoriza remover a composição
existente sem migrar cada responsabilidade e preservar seus contratos.

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
testável no host e sem dependências de ESP-IDF, FreeRTOS, I2C, NVS, LVGL ou BSP.
Ela define os estados `battery`, `external`, `charging`, `absent` e `unknown`,
com threshold de corrente ±15 mA, `external` >= 7900 mV e `absent` >= 8330 mV
exigindo 5 votos consecutivos. A proteção só atua quando as três condições
estão satisfeitas: estado `charging`, percentual >= 90 e tensão >= 8200 mV.
Uma vez ativa, permanece travada pela histerese e libera apenas quando o
percentual cai para <= 85 (ou quando o estado deixa de ser `charging`). A única
opção persistida no NVS é `protection_enabled` (default `true`); desabilitá-la
religa o carregador imediatamente e exige reativação explícita. Falhas de
leitura não desligam `CHG_EN` nem perdem o último estado seguro.

Duas regras de precedência tornam essa política segura para uma bateria
removível. Primeiro, somente uma leitura INA226 **inválida** preserva o snapshot
anterior: uma leitura válida atualiza tensão, corrente, percentual e
disponibilidade mesmo quando `CHG_STAT` não pôde ser lido, e o sinal ausente é
publicado como `unknown` (nunca como `not_charging`). Uma falha de leitura do
expander, portanto, não congela mais o indicador nem o oculta. Segundo, a
precedência de ausência/presença é resolvida antes do sinal do carregador e da
corrente: com o barramento em `absent` (>= 8330 mV voteado) o estado é
`absent`, de modo que um `CHG_STAT` preso em low não fabrica uma bateria
carregando. Estados, thresholds e número de votos permanecem os aprovados. O
snapshot também publica o sinal `charge` já decodificado.

`cyberdeck_battery_view.cpp` é a camada pura de apresentação do indicador, sem
ESP-IDF, FreeRTOS, LVGL, I2C, NVS ou BSP. `resolve()` é um mapeamento total de
`battery_state` + `charge_signal` + disponibilidade + percentual para
`{visible, show_percentage, percentage, glyph}`: indisponível ou `unknown` fica
oculto; `absent` fica visível com o glyph externo e **sem** percentual;
`charging` (por estado ou por sinal) usa o glyph de carga com percentual; e
`battery`/`external` usam o glyph de bateria com percentual. O glyph nunca é
escolhido pelo nível: o percentual numérico ao lado é a única fonte de nível.

`battery_protection.cpp` é o adaptador exclusivo de hardware: inicializa o
Expander B via `bsp_io_expander1_init()` (endereço I2C 0x44), configura
`CHG_STAT` no pin 6 como entrada active-low com pull-up e `CHG_EN` no pin 7
como saída push-pull com valor inicial alto (carregador habilitado). Integra o
reader INA226 (sensor-only), a política pura, a persistência NVS da opção
`enabled` e expõe snapshots para a UI via timer LVGL de 1 s. Um único
`sample_and_publish` alimenta a política e publica as duas projeções: a
`cyberdeck_battery::snapshot` (`charge_class`, usada pelo shell/status) e o
snapshot puro da política em `battery_protection_get_policy_snapshot`, que é a
única entrada de bateria consumida pela view do header. Falhas de I2C,
NVS ou leitura de `CHG_STAT` são registradas em log sem desligar `CHG_EN` nem
perder o último snapshot seguro.

O header puro `cyberdeck_battery_protection.h` também concentra a representação
textual do status: `state_name`, `charge_signal_name` e `format_status_line`.
O último recebe um único `snapshot` e produz, sem alocação ou ESP-IDF, a linha
bounded `battery: state=%s charge=%s available=%s voltage_mv=%ld current_ma=%ld
percentage=%ld protection=%s charger=%s\n`, com terminação NUL e tratamento seguro
para capacidade zero. A UI usa esse formatador em `battery protection status`
e não consulta getters live adicionais.

## Proteção de Carga da Bateria

O firmware implementa **battery protection** (proteção de carga) para prolongar a vida útil da bateria. A política pura (`cyberdeck_battery_protection`) define os estados `battery`, `external`, `charging`, `absent` e `unknown`, com threshold de corrente ±15 mA, `external` ≥ 7900 mV e `absent` ≥ 8330 mV exigindo 5 votos consecutivos. A proteção ativa somente quando as três condições são satisfeitas: estado `charging`, percentual ≥ 90 e tensão ≥ 8200 mV. Uma vez ativa, permanece travada pela histerese e libera apenas quando o percentual cai para ≤ 85 (ou quando a carga para). A única opção persistida no NVS é `protection_enabled` (default `true`); desabilitá-la religa o carregador imediatamente e exige reativação explícita. Falhas de leitura não desligam `CHG_EN` nem perdem o último estado seguro.

A rastreabilidade desse fluxo é mantida em `code-map.md`: REQ-BAT-001 e
REQ-BAT-002 fixam a matemática e os estados, REQ-BAT-003 fixa a apresentação
neutral, REQ-BAT-004 fixa boot não fatal e ausência de controle de carregador,
REQ-BAT-005 fixa a integração INA226, REQ-BAT-006 exige que esta documentação
e o mapa permaneçam alinhados aos contratos host, REQ-BAT-007 fixa o Expander B
e pinos CHG_STAT/CHG_EN, REQ-BAT-008 fixa os estados e thresholds, REQ-BAT-009
fixa a histerese 90/85 e fail-safe, e REQ-BAT-010 fixa NVS, timer UI, shell e
`ui.type`.

### Indicador de energia removível (REQ-BAT-UI-001..005 / AC-BAT-UI-001..006)

- **REQ-BAT-UI-001 / AC-BAT-UI-001..002** — falha de `CHG_STAT` não congela o
  snapshot: leitura INA válida atualiza tensão/corrente/percentual/
  disponibilidade; leitura INA inválida preserva o estado seguro da proteção,
  mas publica `available=false` para ocultar a UI e não renderizar um snapshot
  antigo.
- **REQ-BAT-UI-002 / AC-BAT-UI-003** — precedência de ausência/presença antes
  do sinal do carregador, preservando estados, thresholds e votos aprovados.
- **REQ-BAT-UI-003 / AC-BAT-UI-004** — camada pura (`cyberdeck_battery_view`)
  com mapeamento total para visível, percentual e glyph semântico, sem
  ESP-IDF, FreeRTOS ou LVGL.
- **REQ-BAT-UI-004 / AC-BAT-UI-005** — a UI mantém três labels (ícone,
  percentual base e overlay) e a grade 30/40/30 e apenas aplica a view, sem
  regra de negócio no LVGL e sem acesso
  direto a I2C/NVS/reader.
- **REQ-BAT-UI-005 / AC-BAT-UI-006** — `absent` mostra só o glyph externo e o
  glyph nunca é escolhido pelo percentual.

Limitação de hardware: a ausência é inferida pela tensão fixa do barramento
(< 6000 mV ou >= 8330 mV, 5 votos), sem sinal dedicado de presença. Limitação de recurso: a
fonte `cyberdeck_font.c` embarca apenas os codepoints FontAwesome `0xF067`
(mais), `0xF068` (menos), `0xF0E7` (carga), `0xF1EB` (Wi-Fi) e `0xF240..0xF244`
(bateria); não existe glyph de tomada, USB ou energia na fonte compilada e
regenerá-la está fora do escopo, então o caso de alimentação externa sem
bateria usa `LV_SYMBOL_MINUS` como marcador de "sem bateria".

## Proteção de Tela

O firmware protege o display contra burn-in e economiza energia
desligando-o após um tempo de inatividade (padrao: 2 minutos,
configuravel de 0 a 1440). A implementacao tem duas camadas:

- `cyberdeck_screen_protection`: politica/estado puro, testavel no
  host. Gerencia o valor do timeout, o estado on/off e a persistencia
  NVS de `effective_minutes` e `last_positive_minutes`. Definir o
  timeout como 0 desabilita o desligamento automatico sem perder o
  ultimo valor positivo.
- `screen_off`: adaptador LVGL/BSP. Cria um objeto LVGL preto que
  cobre toda a tela, pausa/retoma um timer de inatividade de 1 segundo
  e trata o duplo toque (janela de 400 ms) para religar o display.
  Uma task dedicada FreeRTOS (`screen_nvs`) escreve o timeout no NVS
  fora da task LVGL. A ordem de boot restaura o timeout persistido
  antes do timer iniciar.

Os comandos `screen` do shell roteiam para essa camada:

- `screen on` — chama `screen_off_turn_on()` (restaura a tela e o
  brilho anteriores).
- `screen off` — chama `screen_off_turn_off()` (carrega a sobreposicao
  preta).
- `screen timeout <0-1440>` — chama `screen_off_set_timeout_minutes()`
  e persiste o valor; 0 desabilita o timer.

A tela tambem e desligada/ligada por transicoes de estado do Wi-Fi e
pelo ciclo de vida da sessao SSH. O endpoint de screenshot funciona
enquanto a tela esta desligada porque captura o framebuffer LVGL
diretamente.

## Wi-Fi: dispatch e persistência

O callback do evento `GOT_IP` não executa trabalho pesado na task `sys_evt`.
Ele apenas publica um snapshot no dispatch de Wi-Fi. O coordinator processa
esse snapshot em seu worker, serializa as operações de persistência e coordena
os efeitos derivados da conexão, mantendo a task de eventos e a thread da UI
livres de I/O, mutexes longos e chamadas de rede.

Cada operação assíncrona tem ownership explícito: o produtor transfere a
mensagem ao dispatch/coordinator, que confirma (`ack`) ou a libera conforme o
resultado. Falhas de enfileiramento e operações retryable seguem a política de
retry do coordinator; itens que não podem ser processados são descartados sem
ownership ambíguo. Tokens de conexão, scan e persistência identificam a geração
corrente, portanto callbacks atrasados ou de uma tentativa cancelada não
alteram o estado atual.

O scan é assíncrono: o callback copia o snapshot dos resultados e o entrega ao
contexto que controla a UI, onde SSIDs são deduplicados e ordenados. A busca
respeita cancelamento, timeout e a geração da tentativa. Persistência só ocorre
após `connected` e `has_ip`; em falhas de conexão ou persistência, o coordinator
executa rollback do estado transitório. O fluxo de esquecimento usa wipe
explícito da credencial persistida, sem exibir ou registrar a senha.

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
curl --fail --output screenshot.bmp http://IP_DO_DISPOSITIVO/screenshot
```

## Ponte manual USB Serial-JTAG NDJSON

A ponte roda na task `serial_brg` (prio 3, 8192 bytes de stack), fora da
stack do LVGL, iniciada por `bridge_start()` ao fim de `app_main`. O driver
USB Serial-JTAG só é instalado se ainda não estiver ativo — o console
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
escrita usa temporário exclusivo. O CLI aceita `--input`, `--stdin` ou
`--data` e retorna tamanho e CRC32.

A lógica pura (parse/envelopes/BMP/CRC/`LineAssembler`) é host-testável e
autocontida: fora de `ESP_PLATFORM` ela não referencia funções de
`screenshot_bmp_*` — stride/size/header/conversão são espelhados localmente
com fórmulas idênticas — porque três dos quatro testes host linkam apenas a
ponte.

## SDK e aplicações

Apps compiladas implementam o contrato bounded de manifesto/lifecycle do
runtime e declaram seus recursos. A primeira superfície de SDK planejada é
`storage.bounded_read` e `logger`; a fachada de storage existente ainda é
somente uma autorização de recurso, portanto a leitura não deve ser inferida
como implementada. O contrato de saída deve continuar compatível com o shell e
com SSH antes de padronizar replies para command apps.

O `cyberdeck.editor` é uma command app foreground compilada. Seu modelo não
inclui LVGL, VFS ou descritores: recebe somente a fachada SDK de storage, com
leitura, temporário, `flush_or_fsync` e `rename_atomic`, todos confinados ao
namespace virtual. Documentos são limitados a 12000 bytes, rejeitam binário e
bytes inválidos e suportam UTF-8, UTF-16 LE/BE e Windows-1252. Arquivos
existentes preservam codec/BOM/EOL; novos usam UTF-8 sem BOM e LF. A escrita
segue temporário -> flush/fsync -> rename atômico, e o modelo concentra edição,
busca, undo/redo bounded e entrada direcional/gestual.
`cyberdeck_editor_view` é a única superfície LVGL do editor: usa slots fixos
para linhas, documento/cursor/status e uma janela de scroll bounded. Ela não
acessa VFS nem serviços. O window manager fornece o contexto opaco e a fachada
de input é revalidada a cada evento; teclado físico/virtual, setas, toque e
gesto chegam ao modelo. `Ctrl+S`, `Ctrl+Q`, `Esc` e `Ctrl+F`/Enter/Esc cobrem
salvar, fechar, confirmação Salvar/Descartar/Cancelar e busca. O teardown
remove a superfície antes da raiz LVGL para impedir callbacks tardios.

## SSH

`ssh_client` usa uma task FreeRTOS dedicada, filas de entrada e senha e
callbacks de saída/estado. O callback de UI é executado sob o lock do display
para que widgets LVGL não sejam alterados concorrentemente.

## Header de estado

O header usa a grade direta 30/40/30 (título/relógio/célula direita). A célula
direita possui dois filhos, nessa ordem: o ícone Wi-Fi e o grupo de bateria. O
ícone Wi-Fi é desenhado com primitivas LVGL e fica claro somente quando o Wi-Fi
está habilitado, conectado e possui IP; fica escuro nos demais estados. O SSID
nunca é renderizado. Estados e erros de SSH são exibidos no terminal e
registrados no log de eventos. Diagnósticos de rede continuam disponíveis pelo
comando `wifi` do shell, fora do header.

O grupo de bateria mantém exatamente três labels: um glyph semântico, o
percentual numérico saturado e uma cópia sobreposta desse percentual. A escolha
não acontece na camada LVGL; o overlay é apenas uma decisão de apresentação.
`refresh_battery_status` copia do adaptador de proteção o snapshot puro da
política, entrega-o a `cyberdeck_battery_view::resolve` e aplica o resultado:
`charging` renderiza o glyph de carga com o percentual, bateria presente
(`battery`/`external`) renderiza o pictograma constante de bateria com o
percentual, um `absent` voteado renderiza o glyph externo sem percentual, e
leitura indisponível ou estado `unknown` oculta o grupo inteiro. O glyph nunca
é selecionado pelo nível do percentual; o nível pertence ao número ao lado. Ele
é atualizado a partir do snapshot sincronizado do adaptador, sem acesso direto
a I2C, expander, NVS ou reader. O layout do
Wi-Fi reage a `LV_EVENT_SIZE_CHANGED` dentro de sua parte alocada da célula
direita, com pixels visíveis a aproximadamente 2 px da borda dessa subcélula.

### Geometria do ícone Wi-Fi

O indicador Wi-Fi mantém o tamanho, a semântica dos estados e as cores atuais.
O limite inferior visual dos arcos superiores é
`center_y - outer_radius * (1 - sin(45°))`; o ponto fica 1,5 px abaixo desse
limite (faixa aceita de 1–2 px). O mesmo cálculo de geometria
é usado na criação do ícone e no reposicionamento após
`LV_EVENT_SIZE_CHANGED`; portanto, o resize altera somente a posição, não a
aparência nem o estado do indicador. A âncora vertical aprovada é
`center_y = box_height - 12.5px`; no header de 42px, isso corresponde a
`29.5px` e alinha opticamente o ícone ao título e ao relógio. Essa âncora
mantém X, tamanho, gap, cores e semântica de estado; o resize continua
atualizando somente a posição.

## Bluetooth LE (ESP32-C6 via ESP-Hosted)

O radio BLE nao reside no ESP32-P4; ele pertence ao coprocessor ESP32-C6
alcancado por `esp_hosted` (VHCI/HCI), com a stack host (NimBLE) rodando no P4.
Nenhum modulo BLE puro toca `lvgl.h`, FreeRTOS, NVS, BSP ou ESP-IDF, exceto o
adaptador `ble_mgr.cpp`. Bluetooth Classic (BR/EDR) esta fora do escopo e e
rejeitado por contrato.

### Modulos puros (testaveis no host)

- `cyberdeck_ble_types.{h,cpp}`: tipos limitados, classificacao por `appearance`
  (teclado `0x03C1`, mouse `0x03C2`, fone `0x0401`/`0x0408`/`0x0418`/`0x0419`/`0x041A`/`0x041B`),
  sanitizador de nome bounded/UTF-8, normalizacao estrita de endereco, helpers
  de passkey (parse/formato/mascaracao `******`) e `device_list` deduplicado por
  endereco (RSSI mais forte, primeiro nome, `paired`/`connectable` monotonicos).
- `cyberdeck_ble_state_machine.{h,cpp}`: maquina de estados da tela
  (`idle`/`searching`/`results`/`paired`/`pairing`/`auth`/`connecting`/`connected`),
  deadlines exatos (scan 10 s, pair 30 s, auth 30 s, connect 20 s), tokens
  monotonicos para descartar callbacks obsoletos, acoes derivadas
  (`start_scan`/`pair`/`submit_auth`/`connect`/`reconnect`/...), quatro teclas
  (`up`/`down`/`enter`/`escape`), mensagens unicas por classe (vazio/falha/timeout/
  cancelamento para scan/pair/connect), reconexao automatica com orcamento de 3
  tentativas e rearme manual. O predicado puro `owns_input()` e a unica fonte
  de ownership das teclas: `searching` e fluxos ativos retêm BLE, enquanto
  `results`/`paired` so retêm as teclas quando a lista possui itens. Assim,
  uma conclusao vazia, falha ou timeout libera o terminal depois de anexar a
  mensagem/lista final, sem apagar a saida.
- `cyberdeck_ble_event_dispatch.{h,cpp}`: seam bounded (8 eventos) entre
  callbacks da stack e o modelo de tela; filas com overflow fail-closed,
  geracoes de token monotono para scan/pair/connect, `event_summary` sem
  segredos (passkey mascarado).
- `cyberdeck_ble_store.{h,cpp}`: persistencia logica de bonds (capacidade 16,
  registro `addr`+`name`+`kind`+`last` sem material de chave), encode
  deterministico (`CDB1;addr=...;name=...;kind=...;last=0|1`), decode estrito
  rejeitando campos duplicados/reordenados/desconhecidos (impede contrabando de
  LTK/IRK/passkey), deserialize atomico fail-closed, round-trip de reboot.

### Adaptador ESP-IDF (`ble_mgr.{h,cpp}`)

Unico modulo autorizado a falar com a stack BLE do C6 via `esp_hosted`.
`ble_mgr_start` executa em ordem estrita `esp_hosted_connect_to_slave()` ->
`esp_hosted_bt_controller_init()` -> `esp_hosted_bt_controller_enable()` ->
`nimble_port_init()`, registra cada retorno e encerra a tentativa sem iniciar
NimBLE quando transporte ou controlador falham. O boot permanece não fatal e
não há retry/reset loop; a versão do firmware C6 é consultada e registrada pela
API `esp_hosted_get_coprocessor_fwversion` quando disponível. Possui task FreeRTOS dedicada (`ble_mgr`, stack 4 KiB, prio 5), task
host nomeada `ble_host` com stack de 8 KiB e fila bounded de comandos (8). A UI apenas enfileira acoes; `ble_mgr_start` e
nao fatal no boot. O `event_dispatch` é protegido por mutex entre a task de
comandos e callbacks GAP. Usa NimBLE VHCI (`CONFIG_BT_NIMBLE_ENABLED=y`,
`CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE=y`, `CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI=y`).

### Integracao Shell e UI

- `cyberdeck_shell_utils.{h,cpp}`: roteia exatamente `bluetooth search` e
  `bluetooth paired` para `CYBERDECK_CMD_BLUETOOTH_SEARCH` e
  `CYBERDECK_CMD_BLUETOOTH_PAIRED`; o verbo nu e operando extra sao
  `CYBERDECK_CMD_UNKNOWN`.
- `cyberdeck_shell_help.h`: catalogo unico com 16 entradas, linha
  `bluetooth [search|paired]` entre `battery` e `ssh`.
- `cyberdeck_ui.cpp`: roteia `Up`/`Down`/`Enter`/`Escape` conforme o predicado
  `cyberdeck_ble::state_machine::owns_input()`, consome acoes
  derivadas e as encaminha ao `ble_mgr` via fila, processa eventos BLE em
  timer LVGL de 100 ms (`process_ble_events`), renderiza lista e mensagens
  do modelo puro.

### Configuracao

- `sdkconfig.defaults`: `CONFIG_BT_ENABLED=y`, `CONFIG_BT_CONTROLLER_DISABLED=y`,
  `CONFIG_BT_NIMBLE_ENABLED=y`, `CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE=y`,
  `CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI=y`.
- `main/idf_component.yml`: mantem `esp_hosted` para o link C6.
- `components/cyberdeck/CMakeLists.txt`: registra os 4 modulos puros +
  `ble_mgr.cpp`; depende de `bt`, `nimble`, `esp_hosted`.

### Rastreabilidade

REQ-BLE-001..014 / AC-BLE-001..014 mapeados em `code-map.md`; contratos host
em `tests/host/keymap/contracts/cyberdeck_ble_*.h`; alvos de teste
`test_ble_types`, `test_ble_state_machine`, `test_ble_event_dispatch`,
`test_ble_store`, `test_ble_command_parse`, `test_ble_integration_contract`.
O teardown de `nimble_port_freertos` não foi alterado nesta correção e permanece
como follow-up dedicado.
