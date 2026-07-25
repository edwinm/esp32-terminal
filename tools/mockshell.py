#!/usr/bin/env python3
"""A pretend login shell on the host end of the terminal's USB link.

This stands in for a real Linux getty so the board can be driven end to end
before there is a server to plug into: it echoes what you type on the on-screen
keyboard, edits the line, keeps history, and answers a handful of commands —
including a fake `htop` that exercises colour, box drawing and the alternate
screen buffer.

    ./tools/mockshell.py [port]

With no argument it picks the only CDC device it can find. Ctrl-C in *this*
terminal quits; Ctrl-C on the board's keyboard just cancels the current line,
the way a real shell does.
"""

import glob
import math
import random
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("This needs pyserial:\n"
             "    pip install pyserial\n"
             "or, if you built the firmware with PlatformIO, use the Python it\n"
             "already installed it into:\n"
             "    ~/.platformio/penv/bin/python tools/mockshell.py")

PROMPT = "\x1b[1;32muser@esp32\x1b[0m:\x1b[1;34m~\x1b[0m$ "
PROMPT_LEN = len("user@esp32:~$ ")

COLS, ROWS = 80, 24


def find_port():
    for pattern in ("/dev/cu.usbmodem*", "/dev/ttyACM*"):
        found = sorted(glob.glob(pattern))
        if found:
            return found[0]
    sys.exit("no CDC device found; pass the port explicitly")


class Shell:
    def __init__(self, port):
        self.p = serial.Serial(port, 115200, timeout=0.05)
        self.line = ""
        self.cur = 0                 # cursor index within self.line
        self.history = []
        self.hist_idx = None
        self.htop_until = 0
        self.running = True

    # --- output ------------------------------------------------------------

    def w(self, s):
        self.p.write(s.encode("utf-8"))
        self.p.flush()

    def prompt(self):
        self.w("\r\n" + PROMPT)
        self.line = ""
        self.cur = 0
        self.hist_idx = None

    def redraw_line(self):
        """Repaint from the cursor to end of line, then park the cursor.

        Uses EL 0 rather than clearing the whole line, which is what a real
        readline does and which exercises the same sequence.
        """
        self.w("\r" + PROMPT + self.line + "\x1b[K")
        back = len(self.line) - self.cur
        if back:
            self.w(f"\x1b[{back}D")

    # --- commands ----------------------------------------------------------

    def run(self, cmd):
        cmd = cmd.strip()
        if not cmd:
            return
        self.history.append(cmd)
        name, _, rest = cmd.partition(" ")
        fn = getattr(self, "cmd_" + name, None)
        if fn:
            fn(rest.strip())
        else:
            self.w(f"\r\n\x1b[31m{name}: command not found\x1b[0m")

    def cmd_help(self, _):
        self.w("\r\nAvailable: \x1b[1mhelp ls pwd date echo uname whoami "
               "free uptime colors clear htop exit\x1b[0m")

    def cmd_ls(self, _):
        entries = [
            ("\x1b[1;34m", "Documents"), ("\x1b[1;34m", "Downloads"),
            ("\x1b[1;34m", "src"), ("\x1b[0m", "notes.txt"),
            ("\x1b[0m", "README.md"), ("\x1b[1;32m", "build.sh"),
            ("\x1b[1;31m", "archive.tar.gz"), ("\x1b[0m", "config.yaml"),
        ]
        self.w("\r\n" + "  ".join(f"{c}{n}\x1b[0m" for c, n in entries))

    def cmd_pwd(self, _):
        self.w("\r\n/home/user")

    def cmd_date(self, _):
        self.w("\r\n" + time.strftime("%a %d %b %Y %H:%M:%S %Z"))

    def cmd_echo(self, rest):
        self.w("\r\n" + rest)

    def cmd_uname(self, _):
        self.w("\r\nLinux esp32 6.8.0-generic #1 SMP x86_64 GNU/Linux")

    def cmd_whoami(self, _):
        self.w("\r\nuser")

    def cmd_uptime(self, _):
        self.w("\r\n 09:41:22 up 7 days,  3:12,  1 user,  load average: 0.14, 0.09, 0.03")

    def cmd_free(self, _):
        self.w("\r\n              total        used        free      shared   available"
               "\r\nMem:        7.7Gi       2.1Gi       4.3Gi       268Mi       5.2Gi"
               "\r\nSwap:       2.0Gi          0B       2.0Gi")

    def cmd_colors(self, _):
        self.w("\r\n")
        for row in range(16):
            line = "".join(f"\x1b[48;5;{row * 16 + col}m  " for col in range(16))
            self.w("  " + line + "\x1b[0m\r\n")
        self.w("\x1b[A")

    def cmd_clear(self, _):
        self.w("\x1b[2J\x1b[H")

    def cmd_exit(self, _):
        self.w("\r\nlogout\r\n")
        self.running = False

    # --- fake htop ---------------------------------------------------------

    def cmd_htop(self, _):
        # Alternate screen, so leaving it restores the shell exactly — which is
        # the round trip worth testing.
        self.w("\x1b[?1049h\x1b[?25l")
        self.htop_until = time.time() + 600
        self.htop_frame = 0

    def htop_draw(self):
        f = self.htop_frame
        self.htop_frame += 1
        cpus = [(50 + 45 * math.sin(f / 6.0 + i)) for i in range(4)]
        out = ["\x1b[H"]

        for i, pct in enumerate(cpus):
            filled = int(pct / 100 * 40)
            bar = ""
            for j in range(40):
                if j < filled:
                    colour = "\x1b[32m" if j < 24 else ("\x1b[33m" if j < 34 else "\x1b[31m")
                    bar += colour + "|"
                else:
                    bar += "\x1b[2m "
            out.append(f"  \x1b[1;36m{i}\x1b[0m \x1b[34m[\x1b[0m{bar}\x1b[0m"
                       f"\x1b[34m]\x1b[0m \x1b[1m{pct:5.1f}%\x1b[0m\x1b[K\r\n")

        mem = 27 + int(6 * math.sin(f / 9.0))
        membar = "\x1b[32m" + "|" * mem + "\x1b[2m" + " " * (40 - mem)
        out.append(f"  \x1b[1;36mM\x1b[0m \x1b[34m[\x1b[0m{membar}\x1b[0m"
                   f"\x1b[34m]\x1b[0m \x1b[1m2.11G/7.7G\x1b[0m\x1b[K\r\n")
        out.append("\x1b[K\r\n")

        out.append("\x1b[30;46m  PID USER      PRI  NI  VIRT   RES  S CPU% MEM%   "
                   "TIME+  Command                 \x1b[0m\x1b[K\r\n")
        procs = [
            (1, "root", "  1234", " 8912", 0.3, 0.4, "0:04.21", "/sbin/init"),
            (412, "root", " 91234", "23112", 1.7, 1.1, "0:31.08", "/usr/lib/systemd/systemd-journald"),
            (901, "user", "412344", "98112", 0.0, 4.2, "2:12.55", "/usr/bin/python3 server.py"),
            (1044, "user",  "212344", "48112", 0.0, 2.1, "0:52.10", "node /srv/app/index.js"),
            (1190, "postgres", "312344", "78112", 0.0, 3.4, "5:02.77", "postgres: writer process"),
            (2201, "user",  "  9234", " 4112", 0.0, 0.2, "0:00.31", "htop"),
            (2202, "user",  "  8234", " 3912", 0.0, 0.2, "0:00.02", "-bash"),
        ]
        for pid, user, virt, res, cpu, memp, t, cmd in procs:
            cpu = max(0.0, cpu + random.uniform(-0.2, 6.0) if pid in (901, 1190) else cpu)
            hl = "\x1b[1;32m" if cpu > 3 else ""
            out.append(f"{pid:5d} \x1b[36m{user:<9}\x1b[0m 20   0 {virt} {res}  S "
                       f"{hl}{cpu:4.1f}\x1b[0m {memp:4.1f} {t:>8}  \x1b[1m{cmd[:30]}\x1b[0m"
                       "\x1b[K\r\n")

        for _ in range(ROWS - 6 - len(procs) - 1):
            out.append("\x1b[K\r\n")
        out.append("\x1b[30;46mF1Help  F2Setup  F3Search  F4Filter  F9Kill  "
                   "F10Quit   \x1b[0m\x1b[1m tap q to quit \x1b[0m\x1b[K")
        self.w("".join(out))

    def htop_quit(self):
        self.htop_until = 0
        self.w("\x1b[?25h\x1b[?1049l")
        self.prompt()

    # --- input -------------------------------------------------------------

    def handle(self, data):
        i = 0
        while i < len(data):
            b = data[i]
            i += 1

            if self.htop_until:
                if b in (ord("q"), 0x03):
                    self.htop_quit()
                continue

            if b == 0x1B:                                  # escape sequence
                seq = data[i:i + 2]
                i += len(seq)
                self.arrow(seq)
                continue

            if b in (0x0D, 0x0A):                          # Enter (device sends CR)
                cmd = self.line
                self.run(cmd)
                if self.running:
                    self.prompt()
                continue

            if b == 0x7F or b == 0x08:                     # Backspace
                if self.cur > 0:
                    self.line = self.line[:self.cur - 1] + self.line[self.cur:]
                    self.cur -= 1
                    self.redraw_line()
                continue

            if b == 0x03:                                  # Ctrl-C
                self.w("^C")
                self.prompt()
                continue

            if b == 0x04:                                  # Ctrl-D
                if not self.line:
                    self.cmd_exit("")
                continue

            if b == 0x15:                                  # Ctrl-U
                self.line = ""
                self.cur = 0
                self.redraw_line()
                continue

            if b == 0x01:                                  # Ctrl-A
                self.cur = 0
                self.redraw_line()
                continue

            if b == 0x05:                                  # Ctrl-E
                self.cur = len(self.line)
                self.redraw_line()
                continue

            if b == 0x0C:                                  # Ctrl-L
                self.w("\x1b[2J\x1b[H" + PROMPT + self.line)
                continue

            if b == 0x09:                                  # Tab: complete a command
                stem = self.line
                opts = [c[4:] for c in dir(self)
                        if c.startswith("cmd_") and c[4:].startswith(stem)]
                if len(opts) == 1:
                    self.line = opts[0] + " "
                    self.cur = len(self.line)
                    self.redraw_line()
                elif len(opts) > 1:
                    self.w("\r\n" + "  ".join(sorted(opts)) + "\r\n" + PROMPT + self.line)
                continue

            if 0x20 <= b < 0x7F:                           # printable
                ch = chr(b)
                self.line = self.line[:self.cur] + ch + self.line[self.cur:]
                self.cur += 1
                if self.cur == len(self.line):
                    self.w(ch)                             # fast path: plain echo
                else:
                    self.redraw_line()
                continue

    def arrow(self, seq):
        if len(seq) < 2:
            return
        final = chr(seq[1])
        if final == "A":                                   # up: history back
            if self.history:
                self.hist_idx = (len(self.history) - 1 if self.hist_idx is None
                                 else max(0, self.hist_idx - 1))
                self.line = self.history[self.hist_idx]
                self.cur = len(self.line)
                self.redraw_line()
        elif final == "B":                                 # down: history forward
            if self.hist_idx is not None:
                self.hist_idx += 1
                if self.hist_idx >= len(self.history):
                    self.hist_idx = None
                    self.line = ""
                else:
                    self.line = self.history[self.hist_idx]
                self.cur = len(self.line)
                self.redraw_line()
        elif final == "C":                                 # right
            if self.cur < len(self.line):
                self.cur += 1
                self.w("\x1b[C")
        elif final == "D":                                 # left
            if self.cur > 0:
                self.cur -= 1
                self.w("\x1b[D")

    # --- main --------------------------------------------------------------

    def loop(self):
        self.w("\x1b[2J\x1b[H")
        self.w("\x1b[1;36mESP32-S3 Serial Terminal\x1b[0m — mock host shell\r\n")
        self.w("\x1b[2mTap the screen for the keyboard. Try: "
               "help, ls, colors, htop\x1b[0m\r\n")
        self.prompt()

        last_htop = 0
        while self.running:
            data = self.p.read(256)
            if data:
                self.handle(data)
            if self.htop_until:
                now = time.time()
                if now > self.htop_until:
                    self.htop_quit()
                elif now - last_htop > 1.0:
                    last_htop = now
                    self.htop_draw()
        self.p.close()


if __name__ == "__main__":
    port = sys.argv[1] if len(sys.argv) > 1 else find_port()
    print(f"mock shell on {port} — Ctrl-C here to quit", flush=True)
    try:
        Shell(port).loop()
    except KeyboardInterrupt:
        print("\nbye", flush=True)
    except serial.SerialException as e:
        sys.exit(f"serial error: {e}")
