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

The header keeps a direct 30/40/30 grid (title/clock/right cell). Inside the
right cell, the Wi-Fi icon is rendered first and the battery group follows. The
Wi-Fi icon uses LVGL primitives and is light only when Wi-Fi is enabled,
connected, and has an IP address; it is muted in every other state. The SSID is
never rendered in the header. SSH state and errors are shown in the terminal
and recorded in the event log. Use the `wifi` shell command for diagnostics.

The battery group consumes synchronized INA226 snapshots without performing
I2C in the UI. Its percentage is derived from the measured bus voltage and
saturates across the approved 6000..8230 mV window; positive current is
discharging, negative current is charging, and zero or indeterminate current
is neutral. The neutral state keeps the numeric percentage visible without a
state glyph or state text. One semantic icon marks charging or discharging,
with no battery-level glyph. The entire group stays hidden while the sensor is
absent or its latest read is unavailable. This is a read-only monitor: it does
not expose charger control. Wi-Fi remains dynamically aligned within its
allocated part of the right cell and recalculates on `LV_EVENT_SIZE_CHANGED`.
The complete requirement-to-test mapping is maintained in `code-map.md` under
REQ-BAT-001 through REQ-BAT-006.

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

O shell visual do menu também oferece `help`, `wifi`, `clear`, `screen` e `ssh`.

### Local file shell

The local file shell starts in the virtual root `/sdcard`; its prompt is
`/sdcard$ `. After a valid `cd`, the prompt shows the new current directory.
An invalid `cd` leaves both the directory and prompt unchanged. Use `pwd` to print the
current directory and `cd <path>` to change it. Paths may be relative to the
current directory or absolute under `/sdcard`. Only `cd` accepts `..`: it
normalizes `.` and `..` in relative, absolute, and mixed paths, clamping at
the `/sdcard` virtual root. All other commands reject path components `..`.

Supported commands are:

```text
ls [path]       list visible entries
ls -a [path]    include dot entries
cat <file>      print a regular file (up to 12288 bytes)
touch <file>    create an empty file
mkdir <dir>     create a directory
rm <path>       remove a file
rm -r <path>    remove a directory tree recursively
rmdir <dir>     remove an empty directory
```

Run `help` (or `help -h` / `help --help`) for the general command list. The
local file commands also accept `-h` and `--help` (for example, `ls --help`) to
show their usage.

The file shell is sandboxed: `/sdcard` is the only exposed root, `cd ..` cannot
escape it, and symbolic links are not allowed. Other commands reject `..` path
components. `rm -r` cannot remove
the virtual root and refuses trees containing symbolic links. It is a small
local file interface, not a full POSIX shell: command parsing is whitespace-
based, and unsupported commands are passed through to the active terminal
context.

`cat` accepts only regular files under `/sdcard`. The size is checked before any
output, reads use bounded chunks, and file I/O runs in a worker before the
result is handed back to LVGL through a bounded asynchronous queue.

### Screen Protection

The firmware turns the display off after a configurable idle timeout
(default 2 minutes) to save power. While the screen is off, a black
LVGL overlay covers the display; tapping it twice within 400 ms turns
the display back on. The timeout persists across reboots via NVS.

From the terminal:

```text
screen on            # turn the display back on immediately
screen off           # turn the display off immediately
screen timeout 0     # disable the auto-off timer
screen timeout 5     # set the timeout to 5 minutes (0..1440)
```

`screen timeout <minutes>` accepts an integer from 0 to 1440. Setting
0 disables the auto-off without turning the screen off immediately.
The last positive value is preserved when the timeout is disabled and
restored on the next boot.

### Battery Protection

The firmware implements charge protection to extend battery lifespan.
It reads `CHG_STAT` (active-low, Expander B pin 6) and controls `CHG_EN`
(Expander B pin 7). The protection policy is pure and host-testable,
separate from the INA226 sensor reader.

Protection activates only when all three conditions are met:
- State is `charging` (via `CHG_STAT` low or current < -15 mA)
- Battery percentage >= 90%
- Bus voltage >= 8200 mV

Once active, protection latches through hysteresis and releases only
when percentage drops to <= 85% (or charging stops). The only NVS-persisted
option is `protection_enabled` (default `true`). Disabling it re-enables
the charger immediately; re-enabling requires explicit action. Read
failures never turn off `CHG_EN` or lose the last safe state.

From the terminal:

```text
battery protection on      # enable charge protection
battery protection off     # disable charge protection
battery protection status  # show protection state
```

The commands are also reachable via the serial bridge `ui.type`
transitively (e.g., `ui.type "battery protection off"`).

Use `wifi search` to scan networks, ordered by signal strength. In the list,
Up/Down navigates, Enter selects, and Escape cancels. Open networks connect
immediately; saved credentials are reused. A new protected network prompts for
a masked password. Connection attempts time out after 15 seconds, and the
configuration is persisted only after association and an IP address are
confirmed.

Use `wifi saved` to list saved SSIDs without showing passwords. Up/Down
navigates, Enter starts the forget flow, and Escape exits. A second Enter
confirms forgetting; Escape cancels the confirmation.

`wifi audit` reports only the current local association (no scan or probe) and
prints status, SSID, BSSID and IP directly in the terminal. It does not write a
file. `wifi audit save` is the explicit persistence command; after its real
completion ACK, the timestamped file is under `/sdcard/wifi-audit/` as
`wifi-audit-YYYYMMDD-HHMMSS.txt`. The directory is created
automatically, existing files are never overwritten, and the retired export
spelling is rejected.

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

## Serial bridge (USB Serial-JTAG NDJSON)

The firmware exposes a manual NDJSON bridge on the USB Serial-JTAG port: one
JSON object per line, bounded to 4096 bytes and correlated by `rid`
(`{"rid","ok":true,"result":...}` on success, `{"rid","ok":false,"error",
"error_code"}` on error). ESP_LOG output interleaves on the same stream and is
filtered by the client.

Commands: `ping`, `sys.info`, `wifi.status`, `wifi.scan`, `ui.echo`,
`ui.click X Y`, `ui.tap <target>`, `ui.type <text>`, `ui.dump`, `ui.clear`,
`screen.shot` (frame metadata) and `screen.dump`, which streams the LVGL
screen as 1024-byte Base64 BMP chunks with per-chunk and total CRC32.

```bash
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 ping
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 sys.info
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 ui.click 120 300
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 screen.dump --out screen.bmp
```

`pyserial` is required on the host (`pip install pyserial`). See
`tests/manual/serial-bridge-validation.pt-BR.md` for the on-device checklist.

## Estrutura

```text
main/                       # Boot e inicialização da interface
components/cyberdeck/       # TUI, SSH, Wi-Fi e persistência
components/m5stack_tab5/    # BSP local do Tab5
```

## Licença

MIT.
