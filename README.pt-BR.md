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
- Header com apenas um ícone Wi-Fi, claro somente quando o Wi-Fi está habilitado,
  conectado e possui IP; escuro nos demais estados
- Cliente SSH assíncrono com PTY remoto
- Autenticação por senha
- Rotação automática da tela pelo sensor BMI270
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

O header possui um único ícone Wi-Fi desenhado com primitivas LVGL. Ele fica claro somente
quando o Wi-Fi está habilitado, conectado e possui endereço IP; fica escuro em
todos os demais estados. O SSID nunca é renderizado no header. Estados e erros
de SSH são exibidos no terminal e registrados no log de eventos. Use o comando
`wifi` do shell para diagnóstico da rede; esses detalhes permanecem fora do
header. O header mantém uma grade 30/40/30 (título/relógio/Wi-Fi), e o ícone é
alinhado dinamicamente à direita da terceira célula, com os pixels visíveis a
aproximadamente 2 px da borda. A posição é recalculada em
`LV_EVENT_SIZE_CHANGED`.

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

O shell visual do menu também oferece `help`, `wifi` e `clear`.

### Shell local de arquivos

O shell local de arquivos inicia no diretório raiz virtual `/sdcard`. Use
`pwd` para exibir o diretório atual e `cd <caminho>` para alterá-lo. Os caminhos
podem ser relativos ao diretório atual ou absolutos dentro de `/sdcard`.

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

O shell de arquivos é isolado em sandbox: `/sdcard` é a única raiz exposta,
`..` não pode ser usado para escapar dela e links simbólicos não são permitidos.
`rm -r` não pode remover a raiz virtual e recusa árvores que contenham links
simbólicos. Essa é uma interface local de arquivos, não um shell POSIX completo:
o parsing dos comandos é baseado em espaços, e comandos não suportados são
encaminhados ao contexto de terminal ativo.

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

## Screenshot HTTP

Quando o Wi-Fi possui endereço IP, o firmware inicia `GET /screenshot` na porta
80. O endpoint aceita apenas endereços privados/da rede local e retorna um BMP
24-bit bottom-up (`biHeight` positivo) da tela LVGL atual. As requisições são
serializadas.

```bash
curl --fail --output screenshot.bmp http://IP_DO_DISPOSITIVO/screenshot
```

O servidor é parado automaticamente quando o Wi-Fi perde o endereço IP.

## Estrutura

```text
main/                       # Boot e inicialização da interface
components/cyberdeck/       # TUI, SSH, Wi-Fi e persistência
components/m5stack_tab5/    # BSP local do Tab5
```

## Licença

MIT.
