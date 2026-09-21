# cyberdeck5

**Languages:** [English](README.md) | [Português](README.pt-BR.md)

Firmware monolítico e leve para o M5Stack Tab5, com uma única tela TUI touch
orientada a ferramentas de computação. A primeira ferramenta é um cliente SSH
interativo no terminal.

## Características

- Interface dark monocromática baseada em preto, cinza e branco
- Tipografia monoespaciada
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

The header contains one Wi-Fi icon drawn with LVGL primitives. It is light only when
Wi-Fi is enabled, connected, and has an IP address; it is muted in every other
state. The SSID is never rendered in the header. SSH state and errors are shown
in the terminal and recorded in the event log. Use the `wifi` shell command for
network diagnostics; those details remain outside the header. The header keeps a
30/40/30 grid (title/clock/Wi-Fi), and the icon is dynamically right-aligned in
the third cell, with visible pixels approximately 2 px from the edge. Its
position is recalculated on `LV_EVENT_SIZE_CHANGED`.

The lower visual limit of the upper arcs is `center_y - outer_radius * (1 -
sin(45°))`; the dot is placed 1.5 px below that limit (accepted range: 1–2 px).
Creation and resize share the same geometry
calculation; resize does not change the icon size, state semantics, or colors.
The approved vertical anchor is `center_y = box_height - 12.5px` (`29.5px` in
the 42px header), optically aligning the icon with the title and clock while
preserving X, size, gap, colors, and state.

1. No terminal TUI, execute `ssh [user@]host[:port]`.
2. Informe a senha e confirme a chave do host quando solicitado.
3. Digite comandos no mesmo terminal e pressione Enter. Não há tela SSH separada nem botão de conexão.

O shell visual do menu também oferece `help`, `wifi` e `clear`.

### Local file shell

The local file shell starts in the virtual root `/sdcard`. Use `pwd` to print the
current directory and `cd <path>` to change it. Paths may be relative to the
current directory or absolute under `/sdcard`.

Supported commands are:

```text
ls [path]       list visible entries
ls -a [path]    include dot entries
touch <file>    create an empty file
mkdir <dir>     create a directory
rm <path>       remove a file
rm -r <path>    remove a directory tree recursively
rmdir <dir>     remove an empty directory
```

Run `help` (or `help -h` / `help --help`) for the general command list. The
local file commands also accept `-h` and `--help` (for example, `ls --help`) to
show their usage.

The file shell is sandboxed: `/sdcard` is the only exposed root, `..` cannot be
used to escape it, and symbolic links are not allowed. `rm -r` cannot remove
the virtual root and refuses trees containing symbolic links. It is a small
local file interface, not a full POSIX shell: command parsing is whitespace-
based, and unsupported commands are passed through to the active terminal
context.

### Wi-Fi

Use `wifi search` to scan networks, ordered by signal strength. In the list,
Up/Down navigates, Enter selects, and Escape cancels. Open networks connect
immediately; saved credentials are reused. A new protected network prompts for
a masked password. Connection attempts time out after 15 seconds, and the
configuration is persisted only after association and an IP address are
confirmed.

Use `wifi saved` to list saved SSIDs without showing passwords. Up/Down
navigates, Enter starts the forget flow, and Escape exits. A second Enter
confirms forgetting; Escape cancels the confirmation.

A sessão SSH roda em uma task FreeRTOS dedicada para não bloquear a UI.

## Screenshot HTTP

When Wi-Fi has an IP address, the firmware starts `GET /screenshot` on port 80.
The endpoint is available only to private/local network addresses and returns a
24-bit bottom-up BMP (positive `biHeight`) of the current LVGL screen. Requests
are serialized.

```bash
curl --fail --output screenshot.bmp http://DEVICE_IP/screenshot
```

The server stops automatically when the Wi-Fi address is lost.

## Estrutura

```text
main/                       # Boot e inicialização da interface
components/cyberdeck/       # TUI, SSH, Wi-Fi e persistência
components/m5stack_tab5/    # BSP local do Tab5
```

## Licença

MIT.
