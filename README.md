# cyberdeck5

Firmware OTA over local Wi-Fi: after the one-time USB partition migration and
USB secret provisioning, run `python3 tools/update_firmware.py --host <TAB5-IP>
--firmware build/cyberdeck5.bin --confirm` for physical confirmation on the
device, or use `--silent` instead. See `AGENTS.md` for prerequisites, security
and recovery precautions.

**Languages:** [English](README.md) | [Português](README.pt-BR.md)

Device-specific embedded operating environment for the M5Stack Tab5, built on
the FreeRTOS kernel and ESP-IDF platform. It provides a touch TUI for compiled
command apps, including an interactive SSH client. The cyberdeck5 layers are
`platform/hardware`, `system runtime`, `SDK`, and `apps`; the Tab5 BSP is vendored.

[Implementation plan](docs/OS-IMPLEMENTATION-PLAN.pt-BR.md) ·
[Progress dashboard](docs/OS-IMPLEMENTATION-DASHBOARD.html)

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
- Runtime inicial de aplicações compiladas, com `app list`, `app info`,
  `app start` e `app stop`
- `wifi`, `ssh`, `bluetooth`, `battery`, `screen` and `log` are command
  applications with their own manifest, dispatched by the runtime supervisor
- Compiled apps use a bounded manifest/runtime contract; dynamic loaders and
  ELF execution are out of scope

## Requisitos

- M5Stack Tab5
- ESP-IDF 6.1
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
I2C in the UI. Its percentage is an estimated state of charge: coulomb counting
anchored on the open-circuit voltage (I×R compensation with the measured
330 mOhm series resistance at the seed and at the re-anchor), integrated with
the sample interval, clamped to 0..100 and corrected by at most one point per
sample. The approved 6000..8100 mV window maps that open-circuit voltage to
0..100 % and, together with the five votes, detects an absent pack; it is not a
direct reading of the terminal voltage. Its ceiling is the resting voltage
measured after the charger cut at 8200 mV, not a fully charged pack, so 100 %
means the maximum charge of this policy (about five points optimistic in the
middle of the curve). Positive current is
discharging, negative current is charging, and zero or indeterminate current
is neutral. The neutral state keeps the numeric percentage visible without a
state glyph or state text. One semantic icon marks charging or discharging,
with no battery-level glyph. An absent pack keeps the group visible with the
external glyph and no percentage; the group is hidden when the INA226 read is
invalid or the state is unknown. This is a read-only monitor: it does
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

The local file shell starts in the virtual root `/`; its prompt is
`/$ `. After a valid `cd`, the prompt shows the new current directory.
An invalid `cd` leaves both the directory and prompt unchanged. Use `pwd` to print the
current directory and `cd <path>` to change it. Paths may be relative to the
current directory or absolute under `/`. Only `cd` accepts `..`: it
normalizes `.` and `..` in relative, absolute, and mixed paths, clamping at
the `/` virtual root. All other commands reject path components `..`.

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

The file shell is sandboxed: `/` is the only exposed root, `cd ..` cannot
escape it, and symbolic links are not allowed. Other commands reject `..` path
components. `rm -r` cannot remove
the virtual root and refuses trees containing symbolic links. It is a small
local file interface, not a full POSIX shell: command parsing is whitespace-
based, and unsupported commands are passed through to the active terminal
context.

`cat` accepts only regular files under `/`. The size is checked before any
output, reads use bounded chunks, and file I/O runs in a worker before the
result is handed back to LVGL through a bounded asynchronous queue.

### Power off

In the local shell, enter `poweroff`, then exactly `y` and Enter to confirm.
Any other answer or Escape cancels. Save/close the editor and exit SSH first:
commands typed in an SSH session belong to the remote host.

Shutdown fences new I/O, quiesces SD/NVS owners, verifies persistence and
unmounts the SD before sending the Tab5 factory poweroff pulse. Errors or a
preparation timeout prevent the signal; failed attempts require a controlled
reboot rather than retry. USB disconnection or a dark display is not proof of
physical rail cut. See the [manual validation procedure](tests/manual/poweroff-validation.pt-BR.md).
The implementation is ready for manual validation; physical cut is not yet accepted.

### Compiled-in applications

The firmware starts directly in the terminal shell. Compiled applications are
registered in the firmware and can be inspected and controlled from the shell:

```text
app list
app info cyberdeck.demo
app start cyberdeck.demo
demo
app stop cyberdeck.demo
```

This first runtime does not load ELF files or scripts from the SD card. The SD
card remains data storage; dynamic application installation will require a
separate bounded application host and supervisor.

Apps are compiled against a predefined SDK. The first planned SDK surface is a
bounded storage read (`storage.bounded_read`) and bounded logger output. No
universal reply/output format is fixed until it is proven compatible with the
existing terminal and interactive SSH flows.

The compiled editor is opened with `edit <file>`. It uses the confined storage
facade, limits documents to 12000 bytes, rejects binary and invalid bytes, and
supports UTF-8, UTF-16 LE/BE, and Windows-1252. Existing files preserve codec,
BOM, and EOL; new files use UTF-8 without a BOM and LF. Editing, search,
undo/redo, directional input, and gesture scrolling are bounded. Saving uses a
temporary file, flush/fsync, and atomic rename; Save As requires confirmation.
The bounded LVGL editor surface owns the document, cursor, status and scroll
window. Arrow keys, physical/virtual keyboard, touch gestures, `Ctrl+S`,
`Ctrl+Q`, `Esc`, `Ctrl+F`/Enter/Esc search, and bounded undo/redo are routed
through the app; dirty close offers Save/Discard/Cancel.

The Tab5 system services are also registered applications: `cyberdeck.shell`,
`cyberdeck.wifi`, `cyberdeck.serial`, `cyberdeck.ssh`, `cyberdeck.screenshot`
and `cyberdeck.bluetooth`. They are started by the application supervisor in
dependency order; this keeps `app_main` as composition code instead of a list
of direct service starts.

### Image viewer (`image`)

`image <path>` opens one uncompressed 24-bit BMP from the SD card in an isolated
foreground app (`cyberdeck.image_viewer`):

- the path is absolute inside the virtual SD namespace (`/fotos/a.bmp`);
  `/sdcard/...`, relative paths and traversal are refused;
- only `.bmp` files with a `BI_RGB` 24-bit header are accepted, up to 5 MiB and
  1..1280 pixels per axis; PNG/JPEG, RLE compression, alpha and oversized files
  fail closed with an explicit message;
- the directory listing is bounded to 64 images, ordered by name, and
  left/right browse it with a one-based `i/N` (or `i/N+`) indicator; the index
  and the `+` describe the **window**, not the position inside the whole
  directory;
- opening a BMP that sorts beyond those first 64 names still works: the
  requested file replaces the last entry of the ordered window (63 smallest +
  the requested one), without raising the cap, duplicating entries or bypassing
  the path/format/size filters; a navigation scan keeps the ordinary window;
- the image is scaled with `min(area/image)` and never enlarged, so the whole
  bitmap stays visible with its aspect ratio; `Esc` returns to the shell.

Decoding runs on a dedicated worker task through the confined storage seam; the
LVGL surface only receives a finished RGB565 frame and owns it until the next
image or until the app is closed. Zoom and recursive directory traversal are out
of scope. Manual validation is documented in
`tests/manual/image-viewer-validation.pt-BR.md`; the host suites
(`test_image_bmp`, `test_image_fit`, `test_image_decode`, `test_image_list`,
`test_image_view`, `test_image_viewer_contract`) cover the pure layers and the
LVGL surface under the shim.

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

Protection activates only when both conditions are met:
- State is `charging` (via `CHG_STAT` low or current < -15 mA)
- Terminal bus voltage >= 8200 mV

The estimated state of charge no longer gates the cut. Once active, the
protection latches; the estimated state of charge only releases it at <= 90%
(accepted on the first sample, and never while still `charging`), together with
five consecutive samples without an external source, or when the option is
disabled. The only NVS-persisted
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

The `battery protection status` response is formatted by the pure battery
policy module, using one synchronized snapshot and bounded storage. Its exact
line format is:

```text
battery: state=%s charge=%s available=%s voltage_mv=%ld current_ma=%ld percentage=%ld protection=%s charger=%s\n
```

The formatter does not allocate, always respects the supplied capacity, and is
safe when the capacity is zero.

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

### Network diagnostics (`cyberdeck.netdiag`)

With Wi-Fi connected, run these commands in the local terminal shell (not in an
SSH session). Each network test runs only when explicitly requested:

```text
netdiag status                         # interface, IP, gateway, DNS and RSSI
netdiag dns example.com                # resolve an IPv4 address
netdiag tcp example.com 443            # test a TCP connection to this port
netdiag https https://example.com/     # send an HTTPS HEAD request
netdiag result                         # show the current or last test result
netdiag cancel                         # logically cancel an active test
```

`status` and `result` do not start network tests. After `dns`, `tcp` or `https`
prints `operation started`, use `netdiag result` to inspect the stage, duration,
error or HTTP status. Only one operation runs at a time, and new operations
must start at least 30 seconds apart. TCP resolves the specified host before
connecting; HTTPS uses its hostname directly and may reach a different IP.
Cancellation is logical: a DNS query can retain its slot until a late callback
(or reboot if none arrives). HTTPS validates TLS certificates, sends HEAD only,
does not follow redirects or retry with GET, and reports HTTP 405 as a final
result. There is no ping, scan, plaintext HTTP, export or automatic traffic.
See [network diagnostics](docs/NETDIAG.md) for limits and implementation notes.

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

The same server exposes read-only `GET /battery`, which returns the cached
battery telemetry as JSON (`application/json`, `Cache-Control: no-store`) so the
device can be validated over the LAN without the USB Serial-JTAG cable. It uses
the same LAN-only allowlist as `GET /screenshot`, never samples the I2C sensor
and never takes the display lock.

```bash
curl --fail http://DEVICE_IP/battery
```

`GET /usb/audio` reports bounded USB Audio Class discovery on the USB-A Host
port, including VID/PID and PCM formats when found. The initial implementation
uses the generic UAC 1.0 driver without a vendor filter. The Tab5 USB-C
connector remains dedicated to USB Serial-JTAG; a USB-A male to USB-C female
adapter is required to attach a USB-C headset.

```bash
curl --fail http://DEVICE_IP/usb/audio
```

`POST /control/type` injects text plus Enter into the active UI, exactly like
`ui.type` on the USB bridge, so commands can be sent while `idf.py monitor`
keeps the serial port. The channel is authenticated with its own HMAC-SHA256
key (NVS, provisioned once over USB via `control.key`; the OTA key is never
reused) and a single-use nonce per request. HTTP provides no confidentiality:
use it only on a trusted LAN. A command is accepted into a bounded queue and
applied by a dedicated task, never inside the HTTP handler.

```bash
python3 tools/cyberdeck_http_cli.py --port /dev/ttyACM0 provision
python3 tools/cyberdeck_http_cli.py --host DEVICE_IP type "help"
python3 tools/cyberdeck_http_cli.py --host DEVICE_IP terminal
```

`POST /control/upload` carries one `.bmp` file (up to 3 MiB) to
`/sdcard/screenshots`, which the viewer sees as `/screenshots/<name>`. It exists
because the USB `fs.write` payload is limited to 2048 bytes and cannot transport
a full-resolution fixture. The client computes the local SHA-256 and signs an
upload-specific message (domain + nonce + method + URI + target basename +
declared size + digest); the device authenticates that metadata - including the
same basename it will publish - before opening anything,
re-verifies the digest while streaming with a fixed buffer, holds a WRITE lease
on the SD coordinator and publishes through an exclusive same-directory
temporary, never overwriting an existing file. Each transfer also has an
absolute per-request deadline (120 s) that progress cannot extend, so a peer
that trickles bytes cannot hold the single HTTP task, the SD lease and the
temporary open; the screen-off overlay does not block an upload.

```bash
python3 tools/cyberdeck_http_cli.py --host DEVICE_IP upload fixture.bmp
python3 tools/cyberdeck_http_cli.py --host DEVICE_IP upload fixture.bmp --name viewer-full.bmp
```

When a UAC 1.0 playback device is present and ready, the player routes its PCM
to that device instead of the on-board codec; if the USB stream cannot be
started the player falls back to the speaker, so playback never regresses.
The player volume (0-50) is applied to the headset: through its Feature Unit
when the device supports it, or by attenuating the PCM otherwise, so the
control always has an audible effect. The stream is paced to the isochronous
clock by bounded blocking writes. Unplugging the headset is detected even while
paused (the bus is polled because the host library only reports a removal to a
client that has a device open).

Inserting or removing a headset - USB-A or the 3.5 mm jack - while a track is
playing **pauses** it: playback never switches output by itself and never
continues on the speaker. The log records why
(`playback paused reason=audio_route_change|output_removed`) and resuming is
always explicit: pressing Play/P picks whatever output exists then (ready USB,
otherwise the board codec, with the jack when connected). A headset already
plugged when the track starts does not pause anything, and changes made while
paused are absorbed instead of pausing again. Detection requires two consecutive
equal readings, so intermittent jack contact does not interrupt audio, and a
failed read (I2C expander) counts as unknown, never as "no headset".

The server stops automatically when the Wi-Fi address is lost.

## Serial bridge (USB Serial-JTAG NDJSON)

The firmware exposes a manual NDJSON bridge on the USB Serial-JTAG port: one
JSON object per line, bounded to 4096 bytes and correlated by `rid`
(`{"rid","ok":true,"result":...}` on success, `{"rid","ok":false,"error",
"error_code"}` on error). ESP_LOG output interleaves on the same stream and is
filtered by the client.

Commands: `ping`, `sys.info`, `wifi.status`, `wifi.scan`, `ui.echo`,
`ui.click X Y`, `ui.tap <target>`, `ui.type <text>`, `ui.escape`, `ui.dump`, `ui.clear`,
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
`sys.info` also reports optional heap low-water sums by memory capability and
the serial task's minimum free stack in bytes. These do not measure other tasks
or prove a simultaneous whole-device minimum.
With the music app in the foreground, use `music.status` for read-only playback
and queue-scan telemetry. Do not send `ui.type "music status"`: its Enter key
toggles playback instead of querying the shell.
`music.scan` reports the last queue/browser scan duration and minimum free
scan-task stack in bytes, without revealing track names or paths.

## Estrutura

```text
main/                       # Boot e inicialização da interface
components/cyberdeck/       # apps, runtime, SDK and platform integration
components/m5stack_tab5/    # vendored BSP for the only Tab5 target
```

## Licença

MIT.
