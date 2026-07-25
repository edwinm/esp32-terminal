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
`htop` and `ls --color` come out monochrome. Fix it:

```bash
sudo systemctl edit serial-getty@ttyACM0.service
```

Put this in the editor that opens:

```ini
[Service]
Environment=TERM=xterm-256color
ExecStart=
ExecStart=-/sbin/agetty -o '-p -- \\u' --keep-baud --noclear %I xterm-256color
```

Then:

```bash
sudo systemctl daemon-reload
sudo systemctl restart serial-getty@ttyACM0.service
```

That is the whole setup. Log in on the board and run `htop`.

### Optional — a stable name if the server has other ACM devices

`ttyACM0` is assigned in plug order, so a second device can steal it and orphan
the getty. Pin this board by its chip ID (the hex from step 2):

```bash
sudo tee /etc/udev/rules.d/99-esp32-term.rules <<'EOF'
SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", ATTRS{idProduct}=="4001", SYMLINK+="esp32term"
EOF
sudo udevadm control --reload && sudo udevadm trigger
```

With several of these boards, add `ATTRS{serial}=="A1B2C3D4E5F6"` (your own hex)
to the rule. Then run the getty on the symlink instead:

```bash
sudo systemctl disable --now serial-getty@ttyACM0.service
sudo systemctl enable --now serial-getty@esp32term.service
```

### Troubleshooting

| symptom | cause |
|---|---|
| no `/dev/ttyACM*` at all | wrong socket — you are in the CP2104 one (`ttyUSB*`) |
| screen is dark, nothing enumerates | the cable is charge-only; use a data cable |
| board shows the splash but a getty is running | it booted after the getty opened the port, so the banner was missed — press Enter |
| text appears but `htop` is monochrome | `TERM` is still `vt220`; do step 5 |
| permission denied opening the port | add yourself to `dialout`, or use `sudo` |
| garbage on screen at power-on | the ROM bootloader prints on the *other* (UART) port at boot; harmless |

Two things worth knowing:

- **Baud rate is meaningless** over USB CDC-ACM — the `--keep-baud` in the getty
  line is inherited cargo from real serial ports and any speed "works". The
  `stty` line settings (`ICRNL`, `echo`, `onlcr`) do still apply and do matter.
- **Window size:** CDC-ACM ttys have no winsize, so they default to 24×80, which
  is exactly this grid. Nothing extra is needed. `resize` works too if you want
  to be certain.

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
