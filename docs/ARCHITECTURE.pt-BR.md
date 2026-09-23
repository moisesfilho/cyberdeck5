# Arquitetura

O `cyberdeck5` é um firmware monolítico. A aplicação é inicializada diretamente
por `app_main`, monta uma única tela LVGL e mantém os módulos de infraestrutura
necessários para a primeira ferramenta.

## Princípios

- Usar o hardware diretamente quando isso reduzir camadas e alocações.
- Manter operações bloqueantes fora da thread da UI.
- Não criar extensibilidade antes de existir uma segunda ferramenta real.
- Preferir buffers limitados e descarte explícito de dados antigos.

## Organização semântica

O componente ESP-IDF continua único e é organizado por contexto, sem criar
componentes ESP-IDF adicionais. `src/features/` contém os fluxos do produto
(shell, Wi-Fi, SSH e screenshot), enquanto `src/platform/` contém as
integrações de entrada, display, sensores, logging e networking. Os headers
espelham essa árvore em `include/features/` e `include/platform/`. A lógica
pura deve permanecer testável no host; `app_main` é o ponto de composição das
partes concretas.

`managed_components/`, incluindo `m5stack_tab5` e `sock_utils`, permanece fora
desta reorganização e continua sendo gerenciado pelo ESP-IDF.

## Fluxo de boot

1. Inicializar NVS.
2. Inicializar display e LVGL pelo BSP do Tab5.
3. Criar a tela TUI monocromática.
4. Inicializar Wi-Fi e reconexão a partir do SD; o header recebe estados de
   Wi-Fi por callback e atualiza somente o ícone: claro quando o Wi-Fi está
   habilitado, conectado e possui IP, e escuro nos demais estados. O SSID não
   é renderizado no header.
5. Aguardar conexão SSH iniciada pelo usuário.
6. Iniciar a ponte manual USB Serial-JTAG (`bridge_start`): task própria,
   iniciada por último; falha aqui registra aviso e não derruba o boot.

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

## SSH

`ssh_client` usa uma task FreeRTOS dedicada, filas de entrada e senha e
callbacks de saída/estado. O callback de UI é executado sob o lock do display
para que widgets LVGL não sejam alterados concorrentemente.

## Header de estado

O header contém um único ícone Wi-Fi desenhado com primitivas LVGL. O ícone fica claro
somente quando o Wi-Fi está habilitado, conectado e possui IP; fica escuro nos
demais estados. O SSID nunca é renderizado. Estados e erros de SSH são
exibidos no terminal e registrados no log de eventos. Diagnósticos de rede
continuam disponíveis pelo comando `wifi` do shell, fora do header. Atualizações
de LVGL originadas pelos eventos do Wi-Fi usam `bsp_display_lock`. O header
mantém a grade 30/40/30 (título/relógio/Wi-Fi); o ícone é alinhado
dinamicamente à direita da terceira célula, com os pixels visíveis a
aproximadamente 2 px da borda. O layout reage a `LV_EVENT_SIZE_CHANGED`,
acompanhando o tamanho real do header.

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
