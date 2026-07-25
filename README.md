# ESP32-S3 Serial Terminal

A physical serial console for a headless Linux box, built on the
[**Makerfabs ESP32-S3 Parallel TFT with Touch 3.5" (ILI9488)**](https://www.makerfabs.com/esp32-s3-parallel-tft-with-touch-ili9488.html).

Plug the board's **native USB-C port** into the server, run a getty on
`/dev/ttyACM0`, and the screen becomes that machine's console: a full-colour
80×24 VT100/xterm terminal. Tap the screen for an on-screen keyboard and type
commands. `htop`, `vim` and `less` work.

The device is the terminal — it does not emulate a USB keyboard for the host.

## Quick start

```bash
pio run -t upload
```

with the board's **UART (CP2104)** socket connected to your build machine. Then
move the board's **native USB** socket to the server and run there:

```bash
sudo systemctl enable --now serial-getty@ttyACM0.service
```

Tap the screen for a keyboard and log in. Full walkthrough, including how to
tell the two sockets apart, is in
[Connecting it to a headless Linux server](#connecting-it-to-a-headless-linux-server).

## Hardware

The board is the **Makerfabs ESP32-S3 Parallel TFT with Touch 3.5"**, available
from the manufacturer:
<https://www.makerfabs.com/esp32-s3-parallel-tft-with-touch-ili9488.html>

This firmware targets the **v2.0** revision. Nothing else is needed — no
soldering, no extra parts, just the board and a USB-C cable.

- **MCU:** ESP32-S3-WROOM-1-N16R2 (16 MB quad flash, 2 MB quad PSRAM), 240 MHz
- **Display:** ILI9488 320×480, 16-bit Intel-8080 parallel bus, run landscape 480×320
- **Touch:** capacitive FT6236 @ 0x38 or resistive NS2009 @ 0x48 (auto-detected)
- **Two USB-C ports, two jobs:**
  - **native USB-OTG** → CDC-ACM, the terminal link (`/dev/ttyACM0` on the host)
  - **CP2104** → UART0, flashing and ESP-IDF logs

Keeping the two apart is deliberate: debug output can never end up in the
terminal stream, and USB CDC has no baud ceiling, so a full-screen `htop` redraw
is instant rather than the ~1 s it would take at 115200 baud.

The native USB socket also powers the board, so one cable to the server is
genuinely all it takes — confirmed on the v2.0 board this was developed on. If a
board revision turns up whose native socket does not supply power (no VBUS to
the 5 V rail, or no CC pull-downs), everything still works; it just also needs
the UART cable or a Y-cable for power.

## Terminal

- **80×24** in a 6×13 cell (480×312 of the panel), which is exactly the default
  size of a Linux serial getty, so no size negotiation is needed.
- Font is X11 **misc-fixed 6x13** and its real bold companion **6x13B** — public
  domain, vendored in `tools/fonts/`. ~2000 glyphs: Latin-1, Greek, Cyrillic,
  box drawing, block elements, arrows, maths.
- Colour: the 16 ANSI colours, the 256-colour cube, and 24-bit truecolour
  (`38;2;r;g;b`, including the `38:2::r:g:b` colon form that tmux and kitty emit).
- Attributes: bold (a genuinely different typeface, not a smear), dim,
  underline, reverse, strike, blink-as-bold. Italic is parsed and ignored —
  there is no room for it at 6 px.
- Alternate screen (`?1049`), scroll regions, insert/delete line and character,
  DEC special graphics (`ESC ( 0`), UTF-8 with correct double-width accounting,
  `REP` (which is how ncurses draws htop's meter bars), and cursor position
  reporting so `resize` works.
- The bottom 8 pixels are a status strip: link state, grid size, and the sticky
  modifier state — which has nowhere else to live once the keyboard is dismissed.

Not implemented: mouse reporting (the modes are accepted, events are simply
never sent), sixel, italics, and scrollback. `TERM=xterm-256color` promises some
of these; they degrade quietly.

## On-screen keyboard

Tap anywhere on the terminal to raise it; tap the `⌄` key or anywhere above the
keyboard to dismiss it.

While it is up the terminal is clipped to the rows above it **and pans to follow
the cursor**, so the prompt is always parked directly above the keys — you are
never typing blind.

- Two layers (letters / numbers-and-symbols) via the `123` / `abc` key.
- `⇧` and `Ctl` are sticky: tap once to arm for the next key, twice to lock,
  three times to clear. Armed and locked are drawn in different colours and
  mirrored in the status strip.
- Arrows, Tab, Esc, Enter, Backspace. Arrows send `ESC O A` under application
  cursor mode, so they work in `vim` as well as `bash`.
- Enter sends **CR**, not LF — the tty's `ICRNL` converts it and readline
  expects it that way.
- Ctrl+letter sends the control code (`Ctl` then `c` interrupts).
- `1x` / `2x` toggles zoom: at 2× the cells are 12×26 and the display becomes a
  40×12 window onto the same 80×24 terminal, panning to follow the cursor. At
  165 DPI, 6×13 is physically about 6-point type — legible up close, and this is
  the escape hatch when it is not.
- Arrows, Backspace and Space auto-repeat when held; letters do not.

## Connecting it to a headless Linux server

### Step 1 — plug the right port into the server

The board has **two USB-C sockets and they are not interchangeable.** Getting
this wrong is the single most common reason it "doesn't work".

| socket | chip behind it | shows up on Linux as | what it is for |
|---|---|---|---|
| **native USB** (wired to the ESP32-S3 itself) | none — direct | `/dev/ttyACM*` | **the terminal.** This is the one. |
| **UART** | CP2104 bridge | `/dev/ttyUSB*` | flashing the firmware and reading ESP-IDF logs |

Connect the **native USB** socket to the server. That one cable both powers the
board and carries the terminal, so nothing else is needed.

If the sockets are not labelled, tell them apart from the server: the terminal
port is the one that produces a `ttyACM` device. The other produces `ttyUSB`.

### Step 2 — find the device

```bash
ls -l /dev/serial/by-id/
```

You want the entry containing `Serial_Terminal`, for example:

```
usb-Makerfabs_ESP32-S3_Serial_Terminal_A1B2C3D4E5F6-if00 -> ../../ttyACM0
```

The trailing hex is this board's chip ID, so the `by-id` path is stable across
reboots and replugs. Use it wherever `/dev/ttyACM0` appears below if you have
more than one USB serial device.

If nothing appears, check the kernel saw it:

```bash
dmesg | tail -20      # expect: cdc_acm 1-1:1.0: ttyACM0: USB ACM device
lsusb | grep 303a     # expect: ID 303a:4001
```

### Step 3 — try it before setting anything up

```bash
sudo screen /dev/ttyACM0
```

Type in that `screen` session and the text appears on the board's display. This
confirms the link end to end. Press `Ctrl-A` then `k` to quit `screen`.

(No `screen`? `sudo picocom /dev/ttyACM0` or `sudo minicom -D /dev/ttyACM0` do
the same job. `sudo` is only needed because your user is probably not in the
`dialout` group — `sudo usermod -aG dialout $USER`, then log out and back in.)

### Step 4 — turn it into a login console

```bash
sudo systemctl enable --now serial-getty@ttyACM0.service
```

A login prompt appears on the board. Tap the screen for the keyboard and log in.

### Step 5 — make colours work

`systemd` runs serial gettys with `TERM=vt220`, which throws away colour, so
`htop` and `ls --color` come out monochrome.

The stock unit already ends its `ExecStart` with `$TERM`, so overriding just the
environment is enough. Write the drop-in directly rather than going through
`sudo systemctl edit` — on a headless box that drops you into whatever `$EDITOR`
happens to be, and an edit that is not saved fails silently:

```bash
sudo mkdir -p /etc/systemd/system/serial-getty@ttyACM0.service.d
sudo tee /etc/systemd/system/serial-getty@ttyACM0.service.d/term.conf >/dev/null <<'EOF'
[Service]
Environment=TERM=xterm-256color
EOF
sudo systemctl daemon-reload
sudo systemctl restart serial-getty@ttyACM0.service
```

**Log out on the board and log in again** — an existing session keeps whatever
`TERM` it started with. Then check on the board:

```bash
echo $TERM        # want: xterm-256color
tput colors       # want: 256
```

That is the whole setup. Run `htop`.

If `TERM` is still wrong, confirm the drop-in was actually picked up:

```bash
systemctl cat serial-getty@ttyACM0.service | tail -20
```

The `term.conf` contents should appear at the end. Failing all of that, set it
at login instead, which bypasses systemd entirely:

```bash
sudo tee /etc/profile.d/esp32-term.sh >/dev/null <<'EOF'
case "$(tty)" in /dev/ttyACM*) export TERM=xterm-256color ;; esac
EOF
```

### Step 6 — survive unplugging

Do this one. It is tempting to skip, but a terminal you carry to a server is
going to be unplugged and replugged constantly, and `serial-getty@ttyACM0` does
not survive that on its own:

- `ttyACM0` is assigned in plug order, so a replug can land on `ttyACM1` and
  leave the getty bound to a device that no longer exists.
- The stock serial getty is `BindsTo=` its device, so it *stops* when the board
  is unplugged and does not necessarily come back when the board returns.

The symptom on the board is the status strip reading **`USB idle … no getty
attached`** — the link is fine and the host has enumerated it, but nothing has
opened the port.

To get a prompt back immediately, point the getty at whatever device the board
is on now (`ls -l /dev/serial/by-id/` says which):

```bash
sudo systemctl restart serial-getty@ttyACM0.service
```

That lasts until the next unplug. For a fix that sticks, give the board a fixed
name and bind the getty to that instead:

```bash
sudo tee /etc/udev/rules.d/99-esp32-term.rules >/dev/null <<'EOF'
SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", ATTRS{idProduct}=="4001", \
  SYMLINK+="esp32term", TAG+="systemd", ENV{SYSTEMD_WANTS}="serial-getty@esp32term.service"
EOF
sudo udevadm control --reload && sudo udevadm trigger
```

`SYMLINK+` gives the stable name; `SYSTEMD_WANTS` is what actually starts the
getty each time the board is plugged in. Then move the getty over:

```bash
sudo systemctl disable --now serial-getty@ttyACM0.service
sudo systemctl enable --now serial-getty@esp32term.service
```

Redo the `TERM` drop-in from step 5 against the new name:

```bash
sudo mkdir -p /etc/systemd/system/serial-getty@esp32term.service.d
sudo tee /etc/systemd/system/serial-getty@esp32term.service.d/term.conf >/dev/null <<'EOF'
[Service]
Environment=TERM=xterm-256color
EOF
sudo systemctl daemon-reload
```

Now unplug and replug: the login prompt should come back on its own.

With more than one of these boards, add `ATTRS{serial}=="A1B2C3D4E5F6"` (your own
hex from step 2) to the rule and give each a distinct `SYMLINK+`.

### Troubleshooting

| symptom | cause |
|---|---|
| status strip says `no getty attached` | the host enumerated the board but nothing opened the port. Usually a replug that landed on a different `ttyACM*`, or a getty that stopped when the board was unplugged. `sudo systemctl restart serial-getty@ttyACM0.service` gets a prompt back now; step 6 stops it recurring |
| worked before, dead after replugging | same thing; step 6 is what fixes it permanently |
| no `/dev/ttyACM*` at all | wrong socket — you are in the CP2104 one (`ttyUSB*`) |
| screen is dark, nothing enumerates | the cable is charge-only; use a data cable |
| board shows the splash but a getty is running | it booted after the getty opened the port, so the banner was missed — press Enter |
| text appears but `htop` is monochrome | `TERM` is still `vt220`. Check `echo $TERM` on the board; if step 5's drop-in is missing from `systemctl cat serial-getty@ttyACM0.service`, it was never saved |
| `TERM` right in a new login, wrong in the current one | log out and back in — a live session keeps the `TERM` it started with |
| permission denied opening the port | add yourself to `dialout`, or use `sudo` |
| garbage on screen at power-on | the ROM bootloader prints on the *other* (UART) port at boot; harmless |

Two things worth knowing:

- **Baud rate is meaningless** over USB CDC-ACM — the `--keep-baud` in the getty
  line is inherited cargo from real serial ports and any speed "works". The
  `stty` line settings (`ICRNL`, `echo`, `onlcr`) do still apply and do matter.
- **Window size:** CDC-ACM ttys have no winsize, so they default to 24×80, which
  is exactly this grid. Nothing extra is needed. `resize` works too if you want
  to be certain.

## Running a dashboard instead of a login

If you want the board to *show* something — `htop`, a log tail, a status script —
rather than offer a login prompt, run that program on the tty instead of a getty.
The on-screen keyboard still reaches it, so `htop` stays interactive; there is
simply no login.

This assumes the `esp32term` symlink from step 6.

**Read the security note below before enabling this.** A dashboard has no login,
so whoever can touch the screen gets whatever the program offers.

```bash
sudo tee /etc/systemd/system/esp32-dashboard.service >/dev/null <<'EOF'
[Unit]
Description=htop on the ESP32 terminal
BindsTo=dev-esp32term.device
After=dev-esp32term.device

[Service]
Type=simple
# A serial tty has no window size of its own, so tell it — otherwise ncurses
# falls back to a guess and htop lays out for the wrong width.
ExecStartPre=/bin/stty -F /dev/esp32term rows 24 cols 80
ExecStart=/usr/bin/htop --readonly --no-function-bar --no-mouse

# Unprivileged. This, not --readonly, is the boundary that actually contains
# what someone at the screen can do: an unprivileged htop can still SEE every
# process, but cannot kill, renice or strace anything it does not own.
DynamicUser=yes
SupplementaryGroups=dialout          # needed to open the tty
NoNewPrivileges=yes
CapabilityBoundingSet=
RestrictSUIDSGID=yes
LockPersonality=yes
ProtectHome=yes
PrivateTmp=yes

StandardInput=tty-force
StandardOutput=tty
StandardError=journal
TTYPath=/dev/esp32term
TTYReset=yes
TTYVHangup=yes
Environment=TERM=xterm-256color
Environment=COLUMNS=80 LINES=24
Restart=always
RestartSec=2

[Install]
WantedBy=multi-user.target
EOF
```

If `DynamicUser=yes` gives trouble on your distro, `User=nobody` with the same
`SupplementaryGroups=dialout` does the same job. Do **not** set `ProtectProc=` —
it would hide the very processes the dashboard exists to show.

Point the udev rule at this service rather than the getty, so it starts whenever
the board is plugged in:

```bash
sudo tee /etc/udev/rules.d/99-esp32-term.rules >/dev/null <<'EOF'
SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", ATTRS{idProduct}=="4001", \
  SYMLINK+="esp32term", TAG+="systemd", ENV{SYSTEMD_WANTS}="esp32-dashboard.service"
EOF
sudo udevadm control --reload && sudo udevadm trigger
sudo systemctl disable --now serial-getty@esp32term.service
sudo systemctl enable --now esp32-dashboard.service
```

### Showing the boot log

The board cannot display boot messages *as they happen*. Firmware, the
bootloader and early kernel init all run before USB is enumerated, so the device
does not exist yet — that part is physics, not configuration. And
`console=ttyACM0` does not work either: the kernel's USB serial console lives in
the usb-serial layer and expects `console=ttyUSB0` (FTDI, CP210x and friends),
while this board is CDC-ACM, whose driver registers no console.

What does work is to follow the journal from the moment the board appears, after
dumping everything that happened before it. In practice you see the whole boot
log, a second or two late:

```
ExecStart=/usr/bin/journalctl -b -n all -f -o short-monotonic --no-hostname
SupplementaryGroups=dialout systemd-journal
```

Substitute those two lines into the unit above. `-b` limits it to this boot,
`-n all` dumps the backlog rather than the last ten lines, `--no-hostname` buys
back width on an 80-column screen, and `systemd-journal` group membership is
what lets an unprivileged service read the journal at all.

Shutdown is the mirror image: the host cuts USB power as it goes down, so the
final messages are lost. If you genuinely need console output across the whole
power cycle, that is what a real RS-232 console or a BMC/IPMI serial-over-LAN is
for — no USB device can do it.

Notes:

- **Reclaiming rows.** 24 rows is not many, so the htop flags above earn their
  place: `--no-function-bar` drops the `F1Help F2Setup …` strip, which under
  `--readonly` is a row spent advertising keys that do nothing. `--no-mouse`
  stops htop enabling mouse tracking the board will never report. Add
  `--no-meters` to drop the CPU/memory gauges if you only want the process
  list, and `-d 20` to refresh every 2 s instead of 1.5.
- Any program works, not just `htop`: `journalctl -f`, `watch -c -n5 …`, or your
  own script. Anything full-screen and ncurses-based behaves best.
- To go back to a login, re-enable `serial-getty@esp32term.service` and point
  `SYSTEMD_WANTS` back at it.
- `systemctl status esp32-dashboard` and `journalctl -u esp32-dashboard` are
  where errors go, since stderr is routed to the journal rather than the board.

### Security of a dashboard

A serial console is a console. With a getty (step 4) the screen offers a *login
prompt*, so it is no worse than a keyboard and monitor plugged into the machine.
A dashboard is different: **there is no authentication at all**, and whatever you
run is reachable by anyone who can touch the screen.

For `htop` specifically, an interactive session can:

| key | effect |
|---|---|
| `F9` / `k` | send any signal to any process — kill `sshd`, `auditd`, a database |
| `F7` / `F8` | renice |
| `s` | attach `strace` to a process, seeing its syscalls and their data |
| `l` | list a process's open files via `lsof` |

There is no way to spawn a shell from htop, so this is not arbitrary command
execution — but "kill anything" and "attach a tracer to anything" is bad enough.
`--readonly` is documented as disabling "all system and process changing
features", which covers kill and renice; the manual does not say what it does
about `s` and `l`, so do not lean on it as your only defence.

That is why the unit above runs unprivileged. The layers, in order of how much
they actually buy you:

1. **Run it as a non-root user** (`DynamicUser=yes`). An unprivileged htop can
   still see every process, but cannot signal, renice or ptrace anything it does
   not own. This is the boundary that holds even if htop grows a new feature.
2. **`--readonly`** on top, as defence in depth.
3. **Consider what is simply *visible*.** htop shows full command lines, and
   command lines routinely contain secrets (`--password=…`, tokens, API keys).
   A screen in a corridor or an open-plan office leaks those to anyone walking
   past, read-only and unprivileged or not. That is a property of the screen,
   not of the configuration.

Worth weighing against the alternative: if whoever can touch the screen can also
reach the machine's USB ports, they can already plug in a keyboard or boot media
and none of this matters. The dashboard genuinely raises risk in the case where
the *screen* is exposed but the *machine* is not — a board on the front of a
locked rack, or on a desk with the server elsewhere. If that is your situation,
prefer the getty, or point the dashboard at something that reveals less than a
process list (`watch` on a handful of metrics, say).

## Build & flash

Needs [PlatformIO](https://platformio.org/install). It downloads the ESP-IDF
toolchain itself on the first build; nothing else has to be installed.

```bash
pio run
```

```bash
pio run -t upload
```

Flashing and logs go over the **UART (CP2104)** socket — *not* the terminal one.
Connect that socket to your build machine before uploading.

```bash
pio device monitor
```

shows the ESP-IDF log at 115200, entirely separate from the terminal stream.

Once the terminal firmware is running, the board offers two serial devices, so
PlatformIO's auto-detection can pick the wrong one. If an upload fails or the
monitor stays silent, set `upload_port` / `monitor_port` in `platformio.ini` to
the CP2104 device (`/dev/ttyUSB*` on Linux, `/dev/cu.usbserial-*` on macOS).

## Tests

The parser and rasteriser are compiled and exercised on the build machine, where
they are far easier to debug than on the device. Nothing in the firmware is
modified for this — only a handful of FreeRTOS/IDF stub headers are added.

```bash
./tools/hosttest/run.sh
```

That covers deferred wrap, scroll regions, editing sequences, SGR (including the
colon-subparameter truecolour form), the alternate screen, `REP`, DA/CPR
replies, UTF-8 and DEC special graphics, tab stops and scrolling.

```bash
./tools/hosttest/render.sh screen.png
```

That runs the real rasteriser over a sample screen and writes a PNG, so font
indexing, colour byte order and box-drawing alignment can be checked by eye.

```bash
./tools/hosttest/sendtest.sh
```

Pushes that same sample at a connected board, byte for byte, so the panel and
the PNG can be compared directly.

## Driving the board without a server

`tools/mockshell.py` stands in for a Linux getty on the host end of the USB
link, so the board can be exercised end to end before there is a server to plug
into. It echoes what you type on the on-screen keyboard, edits the line, keeps
history, and answers `help`, `ls`, `colors`, `date`, `free`, `uname`, `uptime`,
`clear` and a fake animated `htop` (which is a good test of the alternate
screen: quitting it should restore the shell exactly).

```bash
python3 tools/mockshell.py
```

It needs `pyserial` (`pip install pyserial`); if you built the firmware with
PlatformIO, that already has it — `~/.platformio/penv/bin/python
tools/mockshell.py`.

It finds the CDC device on its own; pass a port to override. Ctrl-C on the
*host* quits it; Ctrl-C on the board's keyboard just cancels the line.

## Regenerating the font

```bash
python3 tools/genfont.py
```

Reads the BDFs in `tools/fonts/` and writes `main/term/font6x13.{c,h}` and
`main/term/font5x8.{c,h}`. The generated files are committed; the build never
runs this script and never touches the network.

## Layout

```
main/
  main.c            boot order, the term and disp tasks, splash, touch diagnostic
  lcd.cpp lcd.h     LovyanGFX panel/touch/backlight — hardware-verified, see below
  board_pins.h      every GPIO and the terminal geometry
  term/             screen model, VT parser, renderer, fonts
  ui/               touch state machine, on-screen keyboard, status strip
  io/usb_cdc.c      TinyUSB CDC-ACM
tools/
  genfont.py        BDF -> C font tables
  fonts/            vendored public-domain BDFs
  hosttest/         host-side tests for the terminal and renderer
```

Two tasks: `term` on core 0 drains the USB endpoint into the parser, `disp` on
core 1 renders, polls touch and draws the keyboard. **Only `disp` may touch
LovyanGFX** — it is not thread-safe, and keeping all of it on one task is what
makes a display mutex unnecessary.

The renderer keeps a shadow copy of what is on the glass and diffs the model
against it each frame (~20 µs for the whole grid). That is cheaper than it
sounds and removes the entire class of missed-invalidation bugs that per-cell
dirty flags invite.

## Notes on the display driver

This board is driven with **LovyanGFX**, not ESP-IDF's `esp_lcd`. The `esp_lcd`
i80 driver would not drive this panel (pins, init and geometry all verified —
the screen simply stayed blank). LovyanGFX's `Bus_Parallel16` talks to the same
LCD_CAM + GDMA silicon but configures it differently, and that is the
combination proven to work here. Touch goes through LovyanGFX too, using its
legacy I²C driver; mixing in the newer `i2c_master` driver aborts at boot.
LovyanGFX is vendored under `components/` (its bundled CJK fonts are stripped to
stubs, as they are unused).

Pixel buffers handed to `lcd_raw_push*` are **byte-swapped RGB565**, because
that is the one path through LovyanGFX with no per-pixel conversion. Build
colours with `lcd_rgb()` / `lcd_rgb565()` rather than writing `0xF800` and
expecting red.

**Board revision:** the display control pins on this unit are **WR=35, RS=36,
CS=37** (see `main/lcd.cpp`), which differs from the v2.0 schematic/factory
firmware set (18/17/46). If your panel stays blank, that pin set is the first
thing to check.

**Touch axes:** the default mapping (`offset_rotation = 0`, giving
`screen_x = raw_y`, `screen_y = 319 − raw_x`) is confirmed correct on the unit
this was developed on, with the FT6236. If a board with a differently-oriented
touch layer turns up, hold BOOT during reset for a crosshair diagnostic: it
draws where it thinks you touched and logs raw and mapped coordinates over the
UART port, and tapping the top edge cycles `Touch::offset_rotation` so the right
value can be found without reflashing. Whatever works goes into `lcd_init()`.

## Licence

MIT — see [LICENSE](LICENSE). Bundled third-party components keep their own
terms, listed below.

## Third-party components

| what | where | terms |
|---|---|---|
| **LovyanGFX** 1.1.12 — display and touch driver | vendored in `components/LovyanGFX/` | FreeBSD/2-clause BSD, with MIT-licensed Adafruit code; see `components/LovyanGFX/license.txt` |
| **X11 misc-fixed** `6x13`, `6x13B`, `5x8` — terminal and status fonts | `tools/fonts/`, converted into `main/term/font*.c` | public domain (`COPYRIGHT "Public domain font.  Share and enjoy."`) |
| **ESP-IDF** and **esp_tinyusb** | fetched by PlatformIO / the IDF component manager | Apache 2.0 |

LovyanGFX is vendored rather than fetched because this board needs its exact bus
configuration, and because its bundled CJK fonts are stripped to stubs here to
save flash. Its licence text is kept intact.

The firmware in `main/` and the tooling in `tools/` are this project's own work.
