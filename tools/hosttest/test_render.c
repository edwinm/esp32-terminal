// test_render.c — run the real rasteriser on the build machine and write the
// resulting 480x320 framebuffer to a PNG, so the font indexing, byte order,
// attributes and box-drawing alignment can be checked by eye without flashing.
//
//     ./tools/hosttest/render.sh   ->  /tmp/.../screen.png
//
// lcd.c is replaced by the fake panel below; everything else — term_screen.c,
// vt_parser.c, render.c, glyph.c and the generated font — is the firmware's own
// code, compiled unmodified.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kbd.h"
#include "lcd.h"
#include "render.h"
#include "status.h"
#include "term.h"
#include "touch.h"
#include "usb_cdc.h"

// --- Fake panel -------------------------------------------------------------

static uint16_t s_fb[LCD_W * LCD_H];      // byte-swapped RGB565, as on the wire

int64_t esp_timer_get_time(void)
{
    static int64_t t = 0;
    t += 16000;                            // 16 ms per call: one render tick
    return t;
}

esp_err_t lcd_init(void) { return ESP_OK; }
int  lcd_width(void)  { return LCD_W; }
int  lcd_height(void) { return LCD_H; }
void lcd_backlight_set(int p) { (void)p; }
int  lcd_backlight_get(void) { return 100; }
bool lcd_get_touch(uint16_t *x, uint16_t *y) { (void)x; (void)y; return false; }
bool lcd_get_touch_raw(uint16_t *x, uint16_t *y) { (void)x; (void)y; return false; }
const char *lcd_touch_name(void) { return "none"; }
uint8_t lcd_touch_addr(void) { return 0; }
void lcd_touch_set_offset_rotation(uint8_t o) { (void)o; }
uint8_t lcd_touch_get_offset_rotation(void) { return 0; }
void lcd_raw_start(void) {}
void lcd_raw_end(void) {}

void lcd_raw_push(const uint16_t *buf, int x, int y, int w, int h)
{
    for (int r = 0; r < h; r++) {
        for (int c = 0; c < w; c++) {
            int px = x + c, py = y + r;
            if (px < 0 || px >= LCD_W || py < 0 || py >= LCD_H) continue;
            s_fb[py * LCD_W + px] = buf[r * w + c];
        }
    }
}

void lcd_raw_push_dma(const uint16_t *buf, int x, int y, int w, int h)
{
    lcd_raw_push(buf, x, y, w, h);
}

void lcd_raw_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    for (int r = 0; r < h; r++) {
        for (int c = 0; c < w; c++) {
            int px = x + c, py = y + r;
            if (px < 0 || px >= LCD_W || py < 0 || py >= LCD_H) continue;
            s_fb[py * LCD_W + px] = color;
        }
    }
}

void lcd_raw_fill(uint16_t color)
{
    lcd_raw_fill_rect(0, 0, LCD_W, LCD_H, color);
}

void term_reply(const char *s, int len) { (void)s; (void)len; }

// Keystrokes the on-screen keyboard would have sent, so the tests can assert on
// them instead of needing a USB host.
static char     s_sent[256];
static int      s_sent_len;
void usb_cdc_write(const uint8_t *d, size_t n)
{
    for (size_t i = 0; i < n && s_sent_len < (int)sizeof(s_sent) - 1; i++) {
        s_sent[s_sent_len++] = (char)d[i];
    }
    s_sent[s_sent_len] = '\0';
}
usb_link_state_t usb_cdc_link_state(void) { return USB_LINK_OPEN; }
uint32_t usb_cdc_rx_count(void) { return 0; }
esp_err_t usb_cdc_init(void) { return ESP_OK; }
size_t usb_cdc_read(uint8_t *b, size_t m, uint32_t t) { (void)b; (void)m; (void)t; return 0; }

// --- PPM output (converted to PNG by render.sh) ------------------------------

static void write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", LCD_W, LCD_H);
    for (int i = 0; i < LCD_W * LCD_H; i++) {
        // Undo the byte swap, then expand RGB565 to 8 bits per channel exactly
        // the way the panel does, so the PNG shows the real on-screen colours.
        uint16_t s = s_fb[i];
        uint16_t v = (uint16_t)((s >> 8) | (s << 8));
        uint8_t r5 = (v >> 11) & 0x1F, g6 = (v >> 5) & 0x3F, b5 = v & 0x1F;
        uint8_t rgb[3] = {
            (uint8_t)((r5 << 3) | (r5 >> 2)),
            (uint8_t)((g6 << 2) | (g6 >> 4)),
            (uint8_t)((b5 << 3) | (b5 >> 2)),
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

// --- Sample content ---------------------------------------------------------

// Every byte of the sample is also captured, so the exact same stream can be
// piped at a real board (tools/hosttest/sendtest.sh). One definition, so the
// PNG and the panel can never drift apart.
static char  s_stream[16384];
static int   s_stream_len;

static void feed(const char *s)
{
    size_t n = strlen(s);
    if (s_stream_len + (int)n < (int)sizeof(s_stream)) {
        memcpy(s_stream + s_stream_len, s, n);
        s_stream_len += (int)n;
    }
    term_feed((const uint8_t *)s, n);
}

// A screenful that exercises the things most likely to be wrong: the 16 ANSI
// colours, the 256-colour cube, truecolour, every attribute, UTF-8 box drawing
// (which must tile seamlessly), block elements at every level, and a mock htop
// meter of the kind REP is used to draw.
static void draw_sample(void)
{
    feed("\033[2J\033[H");

    feed("\033[1;36m  ESP32-S3 Serial Terminal\033[0m  \033[2m80x24, misc-fixed 6x13\033[0m\r\n\r\n");

    feed("  ANSI  ");
    for (int i = 0; i < 8; i++) { char b[16]; snprintf(b, sizeof(b), "\033[%dm## ", 30 + i); feed(b); }
    feed("\033[0m\r\n  bright");
    for (int i = 0; i < 8; i++) { char b[16]; snprintf(b, sizeof(b), "\033[%dm ##", 90 + i); feed(b); }
    feed("\033[0m\r\n\r\n");

    feed("  256   ");
    for (int i = 16; i < 16 + 66; i++) { char b[24]; snprintf(b, sizeof(b), "\033[48;5;%dm ", i); feed(b); }
    feed("\033[0m\r\n  true  ");
    for (int i = 0; i < 66; i++) {
        char b[32];
        snprintf(b, sizeof(b), "\033[48;2;%d;%d;%dm ", i * 3, 255 - i * 3, 128);
        feed(b);
    }
    feed("\033[0m\r\n\r\n");

    feed("  attrs \033[1mbold\033[0m \033[2mdim\033[0m \033[4munderline\033[0m "
         "\033[7mreverse\033[0m \033[9mstrike\033[0m \033[1;31mbold red\033[0m\r\n\r\n");

    // Box drawing must tile with no seams at the corners or joins — which only
    // proves anything if the columns actually line up. Each cell is
    // " Xxx " (5) + METER_W meter characters + " " (1) wide.
#define METER_W  16
#define CELL_IN  (5 + METER_W + 1)

    feed("  \xe2\x94\x8c");                                        // upper left
    for (int i = 0; i < CELL_IN; i++) feed("\xe2\x94\x80");
    feed("\xe2\x94\xac");                                          // T down
    for (int i = 0; i < CELL_IN; i++) feed("\xe2\x94\x80");
    feed("\xe2\x94\x90\r\n");                                      // upper right

    // A mock htop meter: solid blocks in two colours, then the shade texture.
    feed("  \xe2\x94\x82 \033[32mCPU\033[0m ");
    for (int i = 0; i < 8; i++) feed("\033[32m\xe2\x96\x88\033[0m");
    for (int i = 0; i < 4; i++) feed("\033[33m\xe2\x96\x88\033[0m");
    for (int i = 0; i < 4; i++) feed("\033[2m\xe2\x96\x91\033[0m");
    feed(" \xe2\x94\x82 \033[36mMem\033[0m ");
    for (int i = 0; i < 10; i++) feed("\033[36m\xe2\x96\x88\033[0m");
    for (int i = 0; i < 6; i++) feed("\033[2m\xe2\x96\x91\033[0m");
    feed(" \xe2\x94\x82\r\n");

    feed("  \xe2\x94\x94");                                        // lower left
    for (int i = 0; i < CELL_IN; i++) feed("\xe2\x94\x80");
    feed("\xe2\x94\xb4");                                          // T up
    for (int i = 0; i < CELL_IN; i++) feed("\xe2\x94\x80");
    feed("\xe2\x94\x98\r\n\r\n");                                  // lower right

    feed("  blocks \xe2\x96\x81\xe2\x96\x82\xe2\x96\x83\xe2\x96\x84\xe2\x96\x85"
         "\xe2\x96\x86\xe2\x96\x87\xe2\x96\x88  shades \xe2\x96\x91\xe2\x96\x92"
         "\xe2\x96\x93\xe2\x96\x88  arrows \xe2\x86\x90\xe2\x86\x91\xe2\x86\x92"
         "\xe2\x86\x93  misc \xc2\xb1\xc2\xb0\xc3\xa9\xc3\xbc\xe2\x89\xa4\xe2\x89\xa5\r\n\r\n");

    // REP, the sequence htop's meters actually use.
    feed("  REP   \033[35m-\033[60b\033[0m\r\n\r\n");

    // A full-width line, to prove the deferred wrap does not insert a blank one.
    feed("\033[44;37m");
    for (int i = 0; i < 80; i++) feed("=");
    feed("\033[0m");
    feed("  user@server:~$ htop\033[5C\033[7m \033[0m");
}

// Tap a key by screen coordinate and report what it put on the wire. The byte
// is emitted on RELEASE, not press, so both halves are required.
static const char *tap(int x, int y)
{
    s_sent_len = 0;
    s_sent[0] = '\0';
    touch_event_t press = { TOUCH_PRESS, (uint16_t)x, (uint16_t)y };
    touch_event_t release = { TOUCH_RELEASE, (uint16_t)x, (uint16_t)y };
    kbd_handle_touch(&press);
    kbd_handle_touch(&release);
    return s_sent;
}

// Press one key, slide to another, then lift: the key under the finger at
// release is the one that counts. This is the whole point of emit-on-release —
// it lets you correct a mis-hit before committing to it.
static const char *tap_slide(int x0, int y0, int x1, int y1)
{
    s_sent_len = 0;
    s_sent[0] = '\0';
    touch_event_t press = { TOUCH_PRESS, (uint16_t)x0, (uint16_t)y0 };
    touch_event_t move = { TOUCH_MOVE, (uint16_t)x1, (uint16_t)y1 };
    touch_event_t release = { TOUCH_RELEASE, (uint16_t)x1, (uint16_t)y1 };
    kbd_handle_touch(&press);
    kbd_handle_touch(&move);
    kbd_handle_touch(&release);
    return s_sent;
}

static int s_failures;

// The box-drawing sample only demonstrates that glyphs tile if its columns
// actually line up. Assert that, rather than trusting a count done by hand:
// every row of the box must carry its verticals in exactly the same columns.
static void check_box_alignment(void)
{
    term_t *t = term_get();
    int rows[3], found = 0;

    for (int r = 0; r < TERM_ROWS && found < 3; r++) {
        for (int c = 0; c < TERM_COLS; c++) {
            uint16_t ch = t->row[r][c].ch;
            if (ch == 0x250C || ch == 0x2514 ||                 // corners
                (ch == 0x2502 && c == 2)) {                     // left wall
                rows[found++] = r;
                break;
            }
        }
    }
    if (found != 3) {
        printf("  FAIL box alignment: found %d box rows, expected 3\n", found);
        s_failures++;
        return;
    }

    for (int i = 0; i < 3; i++) {
        uint32_t mask_lo = 0, mask_hi = 0;
        for (int c = 0; c < TERM_COLS; c++) {
            uint16_t ch = t->row[rows[i]][c].ch;
            bool vertical = (ch == 0x250C || ch == 0x2510 || ch == 0x2514 ||
                             ch == 0x2518 || ch == 0x252C || ch == 0x2534 ||
                             ch == 0x2502);
            if (!vertical) continue;
            if (c < 32) mask_lo |= 1u << c; else mask_hi |= 1u << (c - 32);
        }
        static uint32_t first_lo, first_hi;
        if (i == 0) { first_lo = mask_lo; first_hi = mask_hi; continue; }
        if (mask_lo != first_lo || mask_hi != first_hi) {
            printf("  FAIL box alignment: row %d verticals at different columns "
                   "than the top rail\n", rows[i]);
            s_failures++;
        }
    }
}

static void expect(const char *got, const char *want, const char *what)
{
    if (strcmp(got, want) != 0) {
        printf("  FAIL %s: got \"", what);
        for (const char *p = got; *p; p++) {
            if (*p == 0x1B) printf("\\e");
            else if ((unsigned char)*p < 0x20 || (unsigned char)*p == 0x7F) printf("\\x%02x", (unsigned char)*p);
            else putchar(*p);
        }
        printf("\", want \"");
        for (const char *p = want; *p; p++) {
            if (*p == 0x1B) printf("\\e");
            else if ((unsigned char)*p < 0x20 || (unsigned char)*p == 0x7F) printf("\\x%02x", (unsigned char)*p);
            else putchar(*p);
        }
        printf("\"\n");
        s_failures++;
    }
}

// Centre of the key occupying units [u, u+span) of keyboard row `row`.
static int key_x(int u, int span) { return u * KBD_UNIT_W + span * KBD_UNIT_W / 2; }
static int key_y(int row) { return KBD_Y + row * KBD_KEY_H + KBD_KEY_H / 2; }

static void test_keyboard_bytes(void)
{
    // Layer 0, row 0: q w e r t y u i o p  BKSP HIDE
    expect(tap(key_x(0, 1), key_y(0)), "q", "tap q");
    expect(tap(key_x(10, 1), key_y(0)), "\x7f", "tap backspace -> DEL");

    // Row 1: a s d f g h j k l ;  ENTER(2 units)
    expect(tap(key_x(0, 1), key_y(1)), "a", "tap a");
    expect(tap(key_x(10, 2), key_y(1)), "\r", "tap enter -> CR, not LF");

    // Row 3: LAYER Ctl Esc Tab SPACE(3) ZOOM  arrows
    expect(tap(key_x(2, 1), key_y(3)), "\033", "tap Esc");
    expect(tap(key_x(3, 1), key_y(3)), "\t", "tap Tab");
    expect(tap(key_x(4, 3), key_y(3)), " ", "tap space");
    expect(tap(key_x(8, 1), key_y(3)), "\033[D", "left arrow (normal mode)");
    expect(tap(key_x(9, 1), key_y(3)), "\033[B", "down arrow (normal mode)");
    expect(tap(key_x(10, 1), key_y(3)), "\033[A", "up arrow (normal mode)");
    expect(tap(key_x(11, 1), key_y(3)), "\033[C", "right arrow (normal mode)");

    // Application cursor mode: vim, less and anything using keypad(TRUE).
    feed("\033[?1h");
    expect(tap(key_x(10, 1), key_y(3)), "\033OA", "up arrow (application mode)");
    feed("\033[?1l");

    // Sticky Shift: one shot, then it clears itself.
    tap(key_x(0, 1), key_y(2));                        // Shift
    expect(tap(key_x(0, 1), key_y(0)), "Q", "shift+q -> Q");
    expect(tap(key_x(1, 1), key_y(0)), "w", "shift is one-shot and cleared");

    // Sticky Ctrl. 'c' is row 2 unit 3 (after Shift, z, x); 'd' is row 1 unit 2.
    tap(key_x(1, 1), key_y(3));                        // Ctl
    expect(tap(key_x(3, 1), key_y(2)), "\x03", "ctrl+c -> ETX");
    tap(key_x(1, 1), key_y(3));
    expect(tap(key_x(2, 1), key_y(1)), "\x04", "ctrl+d -> EOT");

    // Double-tapping a modifier locks it.
    tap(key_x(1, 1), key_y(3));
    tap(key_x(1, 1), key_y(3));                        // -> locked
    expect(tap(key_x(0, 1), key_y(0)), "\x11", "ctrl locked: ctrl+q");
    expect(tap(key_x(1, 1), key_y(0)), "\x17", "ctrl stays locked: ctrl+w");
    tap(key_x(1, 1), key_y(3));                        // -> off
    expect(tap(key_x(0, 1), key_y(0)), "q", "third tap unlocks ctrl");

    // Shift+Tab is back-tab, not a plain tab.
    tap(key_x(0, 1), key_y(2));
    expect(tap(key_x(3, 1), key_y(3)), "\033[Z", "shift+tab -> CBT");

    // Symbol layer.
    tap(key_x(0, 1), key_y(3));                        // 123
    expect(tap(key_x(0, 1), key_y(0)), "1", "symbol layer: 1");
    tap(key_x(0, 1), key_y(2));                        // Shift
    expect(tap(key_x(0, 1), key_y(0)), "!", "shifted symbol layer: !");
    expect(tap(key_x(4, 1), key_y(1)), "[", "symbol layer: [");
    tap(key_x(0, 1), key_y(3));                        // back to abc
    expect(tap(key_x(0, 1), key_y(0)), "q", "back on the letter layer");

    // Tie drawing to hit testing: every point the byte tests above tap must
    // actually have a painted key under it, and the gaps between key rows must
    // stay dark. A keyboard that emits the right bytes but paints nothing (or
    // paints somewhere else) passes every check above and is still useless.
    kbd_draw();
    for (int row = 0; row < KBD_ROWS; row++) {
        int y = key_y(row);
        int lit = 0;
        for (int u = 0; u < KBD_UNITS; u++) {
            uint16_t px = s_fb[y * LCD_W + key_x(u, 1)];
            if (px != lcd_rgb(0, 0, 0)) lit++;
        }
        if (lit != KBD_UNITS) {
            printf("  FAIL keyboard row %d: only %d/%d tap points are painted\n",
                   row, lit, KBD_UNITS);
            s_failures++;
        }
        // The 2px gap at the top of each key row.
        uint16_t gap = s_fb[(KBD_Y + row * KBD_KEY_H) * LCD_W + key_x(0, 1)];
        if (gap != lcd_rgb(0x08, 0x09, 0x0c)) {
            printf("  FAIL keyboard row %d: no gap above the keys\n", row);
            s_failures++;
        }
    }
    // Nothing may be painted below the keyboard except the status strip.
    for (int y = KBD_Y + KBD_H; y < STATUS_Y; y++) {
        for (int x = 0; x < LCD_W; x++) {
            if (s_fb[y * LCD_W + x] != lcd_rgb(0, 0, 0)) {
                printf("  FAIL keyboard overruns its band at (%d,%d)\n", x, y);
                s_failures++;
                y = STATUS_Y; break;
            }
        }
    }

    // Slide-to-correct: the key under the finger at release wins, not the one
    // it landed on. On 6mm keys this is the difference between a typo and a
    // correction, so it is worth asserting rather than assuming.
    expect(tap_slide(key_x(0, 1), key_y(0), key_x(1, 1), key_y(0)),
           "w", "press q, slide to w, release -> w");
    expect(tap_slide(key_x(4, 1), key_y(1), key_x(4, 1), key_y(2)),
           "v", "sliding across rows retargets too");

    // Sliding off the keyboard entirely cancels: nothing is sent.
    expect(tap_slide(key_x(0, 1), key_y(0), 100, KBD_Y - 30),
           "", "sliding off the keyboard cancels the keypress");

    // The preview popup for a top keyboard row reaches up over the terminal.
    // Releasing must restore the terminal underneath it — and the invalidation
    // has to be in model-row space, not screen-row space, because the viewport
    // is panned while the keyboard is up. Getting that wrong leaves the upper
    // slice of the popup stranded on screen with nothing to repaint it.
    {
        // Put the cursor at the bottom of the buffer so the viewport is panned
        // as far as it goes; with view_top == 0 the bug is invisible.
        feed("\033[24;1H");
        render_frame();

        static uint16_t before[LCD_W * KBD_Y];
        memcpy(before, s_fb, sizeof(before));

        touch_event_t press = { TOUCH_PRESS, (uint16_t)key_x(0, 1),
                                (uint16_t)key_y(0) };
        kbd_handle_touch(&press);

        bool popup_drew_over_terminal = (memcmp(before, s_fb, sizeof(before)) != 0);
        if (!popup_drew_over_terminal) {
            printf("  FAIL top-row preview never reached the terminal area; "
                   "this check proves nothing\n");
            s_failures++;
        }

        touch_event_t release = { TOUCH_RELEASE, (uint16_t)key_x(0, 1),
                                  (uint16_t)key_y(0) };
        kbd_handle_touch(&release);
        render_frame();

        if (memcmp(before, s_fb, sizeof(before)) != 0) {
            int bad = -1;
            for (int i = 0; i < LCD_W * KBD_Y; i++) {
                if (before[i] != s_fb[i]) { bad = i; break; }
            }
            printf("  FAIL preview left pixels behind above the keyboard "
                   "(first at %d,%d)\n", bad % LCD_W, bad / LCD_W);
            s_failures++;
        }
    }

    // A tap above the keyboard dismisses it — on release, like everything else.
    tap(100, KBD_Y - 10);
    if (kbd_visible()) { printf("  FAIL tap above the keyboard did not dismiss it\n"); s_failures++; }

    // And a tap on the terminal brings it back. That press must be swallowed:
    // it must not also activate whatever key appears underneath it.
    const char *sent = tap(100, 100);
    if (!kbd_visible()) { printf("  FAIL tap on the terminal did not raise the keyboard\n"); s_failures++; }
    if (sent[0] != '\0') {
        printf("  FAIL the tap that opened the keyboard also typed \"%s\"\n", sent);
        s_failures++;
    }
}

int main(int argc, char **argv)
{
    const char *out = "screen.ppm";
    bool with_kbd = false, stream_only = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-k") == 0) with_kbd = true;
        else if (strcmp(argv[i], "-s") == 0) stream_only = true;
        else out = argv[i];
    }

    term_init();
    if (render_init() != ESP_OK) { fprintf(stderr, "render_init failed\n"); return 1; }
    touch_init();
    kbd_init();
    status_init();
    lcd_raw_fill(lcd_rgb(0, 0, 0));

    draw_sample();
    check_box_alignment();

    if (stream_only) {
        // Emit the raw byte stream for piping at a real board, so the panel
        // gets byte-for-byte what the PNG was rendered from.
        fwrite(s_stream, 1, (size_t)s_stream_len, stdout);
        return s_failures ? 1 : 0;
    }

    if (with_kbd) {
        kbd_show();
        test_keyboard_bytes();
        kbd_draw();
        // Leave a finger resting on a key so the preview popup is in the PNG.
        touch_event_t hold = { TOUCH_PRESS, (uint16_t)key_x(4, 1),
                               (uint16_t)key_y(1) };
        kbd_handle_touch(&hold);
    }

    render_frame();
    status_tick();

    write_ppm(out);
    printf("wrote %s (%dx%d)\n", out, LCD_W, LCD_H);
    if (s_failures) { printf("%d keyboard check(s) failed\n", s_failures); return 1; }
    if (with_kbd) printf("all keyboard checks passed\n");
    return 0;
}
