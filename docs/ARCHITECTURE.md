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
4. Inicializar Wi-Fi e reconexão a partir do SD; o header recebe estados de
   Wi-Fi por callback e atualiza somente o ícone: claro quando Wi-Fi está
   habilitado, conectado e possui IP, e escuro nos demais estados. O SSID não
   é renderizado no header.
5. Aguardar conexão SSH iniciada pelo usuário.
6. Iniciar a ponte manual USB Serial-JTAG (`bridge_start`): task própria,
   iniciada por último; falha aqui registra aviso e não derruba o boot.

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

The header contains one Wi-Fi icon drawn with LVGL primitives. The icon is light only
when Wi-Fi is enabled, connected, and has an IP address; it is muted otherwise.
The SSID is never rendered. Wi-Fi-driven LVGL updates hold the BSP display
lock. SSH state and errors are rendered in the terminal and written to the
event log. Network diagnostics remain available through the `wifi` shell
command, outside the header. The header uses a 30/40/30 title/clock/Wi-Fi grid;
the icon is dynamically right-aligned within the third cell, leaving its visible
pixels approximately 2 px from the edge. The layout responds to
`LV_EVENT_SIZE_CHANGED` so the alignment follows the actual header size.

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
