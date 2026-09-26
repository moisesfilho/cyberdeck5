# cyberdeck5

**Idiomas:** [English](README.md) | [Português](README.pt-BR.md)

Firmware monolítico e leve para o M5Stack Tab5, com uma única tela TUI touch
orientada a ferramentas de computação. A primeira ferramenta é um cliente SSH
interativo no terminal.

## Características

- Interface dark monocromática baseada em preto, cinza e branco
- Tipografia monoespaçada
- Interface LVGL simples, com touch e teclado virtual
- Tela TUI única, com header `CYBERDECK5` e shell visual (`help`, `wifi`, `log`, `clear` e `ssh`)
- Wi-Fi com reconexão e persistência no cartão SD
- Header com status/percentual de bateria e um ícone Wi-Fi, claro somente quando
  o Wi-Fi está habilitado, conectado e possui IP; escuro nos demais estados
- Cliente SSH assíncrono com PTY remoto
- Autenticação por senha
- Rotação automática da tela pelo sensor BMI270
- Ponte manual USB Serial-JTAG NDJSON com CLI host (`tools/cyberdeck_cli.py`)
- Sem sistema de plugins, apps instaláveis, WASM ou desktop

## Requisitos

- M5Stack Tab5
- ESP-IDF 5.5.5
- Python 3.12
- Linux

## Build e flash

```bash
. ~/esp/esp-idf/export.sh
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## Shell e cliente SSH

### Indicadores do header

O header mantém a grade direta 30/40/30 (título/relógio/célula direita). Dentro
da célula direita, o ícone Wi-Fi é renderizado primeiro e o grupo de bateria vem
depois. O ícone usa primitivas LVGL e fica claro somente quando o Wi-Fi está
habilitado, conectado e possui endereço IP; fica escuro nos demais estados. O
SSID nunca é renderizado no header. Estados e erros de SSH são exibidos no
terminal e registrados no log de eventos. Use o comando `wifi` para diagnóstico.

O grupo de bateria consome snapshots INA226 sincronizados sem executar I2C na
UI. O percentual deriva da tensão medida no barramento e satura na faixa
aprovada de 6000..8230 mV; corrente positiva representa descarga, negativa
representa carga e corrente zero ou indeterminada representa neutral. No estado
neutral o percentual numérico permanece visível, sem glyph ou texto de estado.
Um único ícone semântico indica carga ou descarga, sem glyph de nível de bateria.
O grupo inteiro fica oculto enquanto o sensor está ausente ou a leitura mais
recente está indisponível. O fluxo é somente leitura e não expõe controle do
carregador. O Wi-Fi permanece alinhado dinamicamente dentro de sua parte alocada
na célula direita e recalcula em `LV_EVENT_SIZE_CHANGED`. O mapeamento completo
de requisitos para testes é mantido em `code-map.md`, de REQ-BAT-001 a
REQ-BAT-006.

O limite inferior visual dos arcos superiores é `center_y - outer_radius * (1 -
sin(45°))`; o ponto fica 1,5 px abaixo desse limite (faixa aceita: 1–2 px).
A criação e o resize compartilham o mesmo cálculo de
geometria; o resize não altera o tamanho, a semântica dos estados ou as cores
do ícone. A âncora vertical aprovada é `center_y = box_height - 12.5px`
(`29.5px` no header de 42px), alinhando opticamente o ícone ao título e ao
relógio e mantendo X, tamanho, gap, cores e estado.

1. No terminal TUI, execute `ssh [user@]host[:port]`.
2. Informe a senha e confirme a chave do host quando solicitado.
3. Digite comandos no mesmo terminal e pressione Enter. Não há tela SSH separada nem botão de conexão.

O shell visual do menu também oferece `help`, `wifi`, `clear`, `screen` e `ssh`.

### Shell local de arquivos

O shell local de arquivos inicia na raiz virtual `/`, com o prompt
`/$ `. Depois de um `cd` válido, o prompt mostra o novo diretório atual;
um `cd` inválido preserva o diretório e o prompt anteriores. Use `pwd` para
exibir o diretório atual e `cd <caminho>` para alterá-lo. Os caminhos
podem ser relativos ao diretório atual ou absolutos dentro de `/`.
Somente `cd` aceita `..`: ele normaliza `.` e `..` em caminhos relativos,
absolutos ou mistos, limitando tentativas de subir acima de `/` à raiz
virtual. Os demais comandos rejeitam componentes `..`.

Comandos disponíveis:

```text
ls [caminho]       lista entradas visíveis
ls -a [caminho]    inclui entradas ocultas
touch <arquivo>    cria um arquivo vazio
mkdir <diretório>  cria um diretório
rm <caminho>       remove um arquivo
rm -r <caminho>    remove uma árvore de diretórios recursivamente
rmdir <diretório>  remove um diretório vazio
```

Execute `help` (ou `help -h` / `help --help`) para ver a lista geral de
comandos. Os comandos do shell local também aceitam `-h` e `--help` (por
exemplo, `ls --help`) para exibir seu uso.

O shell de arquivos é isolado em sandbox: `/` é a única raiz virtual exposta
e corresponde ao cartão SD físico montado em `/sdcard`. `cd ..` não pode
escapar dela e links simbólicos não são permitidos. O caminho físico
`/sdcard` não é um alias aceito pelo shell virtual. Os demais
comandos rejeitam componentes `..` nos caminhos.
`rm -r` não pode remover a raiz virtual e recusa árvores que contenham links
simbólicos. Essa é uma interface local de arquivos, não um shell POSIX completo:
o parsing dos comandos é baseado em espaços, e comandos não suportados são
encaminhados ao contexto de terminal ativo.

### Proteção de Tela

O firmware desliga o display após um tempo configurável de inatividade
(padrao: 2 minutos) para economizar energia. Enquanto a tela estiver
desligada, uma sobreposição preta LVGL cobre o display; tocar duas
vezes em 400 ms a religa. O tempo de inatividade persiste entre reinicializações via NVS.

No terminal:

```text
screen on            # religar o display imediatamente
screen off           # desligar o display imediatamente
screen timeout 0     # desabilitar o timer de desligamento automatico
screen timeout 5     # definir o timeout para 5 minutos (0..1440)
```

`screen timeout <minutos>` aceita um inteiro de 0 a 1440. Definir 0
desabilita o desligamento automático sem apagar a tela imediatamente.
O último valor positivo é preservado quando o timeout é desabilitado e
restaurado no próximo boot.

### Proteção de Carga da Bateria

O firmware implementa proteção de carga para prolongar a vida útil da
bateria. Ele lê `CHG_STAT` (active-low, Expander B pin 6) e controla
`CHG_EN` (Expander B pin 7). A política de proteção é pura e testável no
host, separada do reader INA226 do sensor.

A proteção ativa somente quando as três condições são atendidas:
- Estado é `charging` (via `CHG_STAT` em nível baixo ou corrente < -15 mA)
- Percentual da bateria >= 90%
- Tensão do barramento >= 8200 mV

Uma vez ativa, a proteção permanece travada pela histerese e libera
apenas quando o percentual cai para <= 85% (ou quando a carga para).
A única opção persistida no NVS é `protection_enabled` (default `true`).
Desabilitá-la religa o carregador imediatamente; reativar exige ação
explícita. Falhas de leitura nunca desligam `CHG_EN` nem perdem o último
estado seguro.

No terminal:

```text
battery protection on      # habilita proteção de carga
battery protection off     # desabilita proteção de carga
battery protection status  # mostra estado da proteção
```

Os comandos também são alcançáveis via bridge serial `ui.type`
transitivamente (ex.: `ui.type "battery protection off"`).

### Wi-Fi

Use `wifi search` para procurar redes, ordenadas pela intensidade do sinal. Na
lista, Cima/Baixo navega, Enter seleciona e Escape cancela. Redes abertas
conectam imediatamente; credenciais salvas são reutilizadas. Uma nova rede
protegida solicita uma senha exibida apenas com asteriscos. As tentativas de
conexão expiram após 15 segundos, e a configuração só é persistida depois que
a associação e o endereço IP forem confirmados.

Use `wifi saved` para listar SSIDs salvos sem exibir senhas. Cima/Baixo navega,
Enter inicia o esquecimento e Escape sai. Um segundo Enter confirma o
esquecimento; Escape cancela a confirmação.

A sessão SSH roda em uma task FreeRTOS dedicada para não bloquear a UI.

### Bluetooth LE (ESP32-C6 via ESP-Hosted)

O radio BLE reside no coprocessor ESP32-C6, alcancado por `esp_hosted`
(VHCI/HCI). A stack NimBLE roda no ESP32-P4. Bluetooth Classic (BR/EDR) esta
fora do escopo.

Comandos disponiveis:

```text
bluetooth search      # scan assincrono de dispositivos BLE (timeout 10 s)
bluetooth paired      # lista de dispositivos pareados (bonds persistidos)
```

No resultado do `bluetooth search`, a lista mostra nome e tipo
(`Keyboard`, `Headset`, `Mouse`, `Unknown`). Navegacao com Cima/Baixo,
Enter inicia o pareamento, Escape cancela. Dispositivos nao conectaveis sao
listados mas nao podem ser pareados. O pareamento usa autenticacao
interativa quando o peer exige (passkey, numeric comparison ou confirmacao).
O passkey aparece apenas na linha de status da tela de autenticacao e nunca
em logs. Bonds sao persistidos (capacidade 16) e reconexoes automaticas sao
tentadas (maximo 3 falhas consecutivas; rearme manual via Enter).

Mensagens de terminal (uma por classe):
- Scan: `No Bluetooth devices found.` | `Bluetooth scan failed.` | `Bluetooth search timed out.` | `Bluetooth search cancelled.`
- Pair: `Bluetooth pairing rejected.` | `Bluetooth pairing failed.` | `Bluetooth pairing timed out.` | `Bluetooth pairing cancelled.`
- Connect: `Bluetooth connection failed.` | `Bluetooth connection timed out.` | `Bluetooth connection cancelled.` | `Bluetooth reconnection failed.`

## Screenshot HTTP

Quando o Wi-Fi possui endereço IP, o firmware inicia `GET /screenshot` na porta
80. O endpoint aceita apenas endereços privados/da rede local e retorna um BMP
24-bit bottom-up (`biHeight` positivo) da tela LVGL atual. As requisições são
serializadas.

```bash
curl --fail --output screenshot.bmp http://IP_DO_DISPOSITIVO/screenshot
```

O servidor é parado automaticamente quando o Wi-Fi perde o endereço IP.

## Ponte serial (USB Serial-JTAG NDJSON)

O firmware expõe uma ponte manual NDJSON na porta USB Serial-JTAG: um objeto
JSON por linha, limitado a 4096 bytes e correlacionado por `rid`
(`{"rid","ok":true,"result":...}` no sucesso, `{"rid","ok":false,"error",
"error_code"}` no erro). A saída do ESP_LOG chega intercalada no mesmo fluxo
e é filtrada pelo cliente.

Comandos: `ping`, `sys.info`, `wifi.status`, `wifi.scan`, `ui.echo`,
`ui.click X Y`, `ui.tap <alvo>`, `ui.type <texto>`, `ui.dump`, `ui.clear`,
`screen.shot` (metadados do frame) e `screen.dump`, que transmite a tela
LVGL como chunks BMP de 1024 bytes em Base64 com CRC32 por chunk e total.

```bash
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 ping
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 sys.info
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 ui.click 120 300
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 screen.dump --out screen.bmp
```

`pyserial` é necessário no host (`pip install pyserial`). Consulte
`tests/manual/serial-bridge-validation.pt-BR.md` para o roteiro no dispositivo.

## Estrutura

```text
main/                       # Boot e inicialização da interface
components/cyberdeck/       # TUI, SSH, Wi-Fi e persistência
components/m5stack_tab5/    # BSP local do Tab5
```

## Licença

MIT.
