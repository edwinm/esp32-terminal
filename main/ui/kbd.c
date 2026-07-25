// kbd.c — on-screen keyboard: layout, hit testing, drawing, byte emission.
#include <string.h>

#include "board_pins.h"
#include "glyph.h"
#include "kbd.h"
#include "lcd.h"
#include "render.h"
#include "term.h"
#include "usb_cdc.h"
#include "utf8.h"

// --- Key codes --------------------------------------------------------------

enum {
    K_NONE = 0,
    K_SHIFT = 0x100, K_CTRL, K_LAYER, K_HIDE, K_ZOOM,
    K_ENTER, K_BKSP, K_TAB, K_ESC,
    K_UP, K_DOWN, K_LEFT, K_RIGHT,
};

#define KF_REPEAT 0x01     // auto-repeats when held

typedef struct {
    const char *label;     // UTF-8; NULL means "draw the character itself"
    uint16_t    code;
    uint16_t    shift_code;
    uint8_t     units;
    uint8_t     flags;
} kbd_key_t;

#define A(c)        { NULL, (c), (c), 1, 0 }               // same either way
#define P(c, s)     { NULL, (c), (s), 1, 0 }               // shifted variant
#define R(c, s)     { NULL, (c), (s), 1, KF_REPEAT }
#define S(l, k, u, f) { (l), (k), (k), (u), (f) }

// Letters shift to uppercase; everything else has an explicit pair.
#define L(c)        { NULL, (c), (c) - 32, 1, 0 }

static const kbd_key_t k_letters[KBD_ROWS][12] = {
    { L('q'), L('w'), L('e'), L('r'), L('t'), L('y'), L('u'), L('i'), L('o'), L('p'),
      S("⌫", K_BKSP, 1, KF_REPEAT), S("⌄", K_HIDE, 1, 0) },
    { L('a'), L('s'), L('d'), L('f'), L('g'), L('h'), L('j'), L('k'), L('l'),
      P(';', ':'), S("⏎", K_ENTER, 2, 0), {0} },
    { S("⇧", K_SHIFT, 1, 0),
      L('z'), L('x'), L('c'), L('v'), L('b'), L('n'), L('m'),
      P(',', '<'), P('.', '>'), P('/', '?'), P('-', '_') },
    { S("123", K_LAYER, 1, 0), S("Ctl", K_CTRL, 1, 0), S("Esc", K_ESC, 1, 0),
      S("⇥", K_TAB, 1, 0), S("␣", ' ', 3, KF_REPEAT),
      S("1x", K_ZOOM, 1, 0),
      S("←", K_LEFT, 1, KF_REPEAT), S("↓", K_DOWN, 1, KF_REPEAT),
      S("↑", K_UP, 1, KF_REPEAT), S("→", K_RIGHT, 1, KF_REPEAT),
      {0}, {0} },
};

static const kbd_key_t k_symbols[KBD_ROWS][12] = {
    { P('1', '!'), P('2', '@'), P('3', '#'), P('4', '$'), P('5', '%'),
      P('6', '^'), P('7', '&'), P('8', '*'), P('9', '('), P('0', ')'),
      S("⌫", K_BKSP, 1, KF_REPEAT), S("⌄", K_HIDE, 1, 0) },
    { A('='), A('+'), A('-'), A('_'), A('['), A(']'), A('{'), A('}'), A('|'),
      A('\\'), S("⏎", K_ENTER, 2, 0), {0} },
    { S("⇧", K_SHIFT, 1, 0),
      A('`'), A('~'), A('\''), A('"'), A('<'), A('>'), A('?'), A(':'),
      A('*'), A('('), A(')') },
    { S("abc", K_LAYER, 1, 0), S("Ctl", K_CTRL, 1, 0), S("Esc", K_ESC, 1, 0),
      S("⇥", K_TAB, 1, 0), S("␣", ' ', 3, KF_REPEAT),
      S("1x", K_ZOOM, 1, 0),
      S("←", K_LEFT, 1, KF_REPEAT), S("↓", K_DOWN, 1, KF_REPEAT),
      S("↑", K_UP, 1, KF_REPEAT), S("→", K_RIGHT, 1, KF_REPEAT),
      {0}, {0} },
};

// --- Colours (byte-swapped RGB565) -----------------------------------------

#define C_KEY        lcd_rgb(0x2a, 0x2f, 0x38)
#define C_KEY_SPECIAL lcd_rgb(0x1e, 0x22, 0x2a)
#define C_KEY_DOWN   lcd_rgb(0x2f, 0x74, 0xb5)
#define C_MOD_ARMED  lcd_rgb(0x6a, 0x54, 0x18)
#define C_MOD_LOCKED lcd_rgb(0xb0, 0x7a, 0x10)
#define C_LABEL      lcd_rgb(0xe4, 0xe4, 0xe4)
#define C_GAP        lcd_rgb(0x08, 0x09, 0x0c)
#define KEY_GAP      2      // pixels of gap drawn inside each key's rectangle

// --- State ------------------------------------------------------------------

enum { MOD_OFF = 0, MOD_ARMED, MOD_LOCKED };

static bool    s_visible;
static int     s_layer;                // 0 = letters, 1 = symbols
static uint8_t s_shift, s_ctrl;
static int     s_down_row = -1, s_down_col = -1;

// The widest key (space, 3 units) bounds the scratch buffer.
static uint16_t s_keybuf[3 * KBD_UNIT_W * KBD_KEY_H];

static const kbd_key_t (*layout(void))[12]
{
    return s_layer ? k_symbols : k_letters;
}

// --- Geometry ---------------------------------------------------------------

// Walk a row to find the key at unit `u`, returning its index and x extent.
static int key_at_unit(int row, int u, int *out_x, int *out_units)
{
    const kbd_key_t (*rows)[12] = layout();
    int x = 0;
    for (int i = 0; i < 12; i++) {
        const kbd_key_t *k = &rows[row][i];
        if (k->units == 0) break;
        if (u >= x && u < x + k->units) {
            if (out_x) *out_x = x;
            if (out_units) *out_units = k->units;
            return i;
        }
        x += k->units;
    }
    return -1;
}

// --- Drawing ----------------------------------------------------------------

// Blit one glyph into the key scratch buffer, clipping to the key.
static void draw_glyph(uint16_t *buf, int stride, int x, int y,
                       uint16_t cp, uint16_t fg, int scale)
{
    const uint8_t *g = font_glyph(cp, false);
    for (int gy = 0; gy < FONT_CELL_H; gy++) {
        uint8_t bits = g[gy];
        if (!bits) continue;
        for (int gx = 0; gx < FONT_CELL_W; gx++) {
            if (!(bits & (0x80 >> gx))) continue;
            for (int sy = 0; sy < scale; sy++) {
                for (int sx = 0; sx < scale; sx++) {
                    int px = x + gx * scale + sx;
                    int py = y + gy * scale + sy;
                    if (px < 0 || px >= stride || py < 0 || py >= KBD_KEY_H) continue;
                    buf[py * stride + px] = fg;
                }
            }
        }
    }
}

static int label_codepoints(const kbd_key_t *k, int row, int col, uint16_t *out, int max)
{
    (void)row; (void)col;
    if (!k->label) {
        uint16_t c = (s_shift != MOD_OFF) ? k->shift_code : k->code;
        out[0] = c;
        return 1;
    }
    if (k->code == K_ZOOM) {
        out[0] = (render_get_scale() > 1) ? '2' : '1';
        out[1] = 'x';
        return 2;
    }

    utf8_state_t st;
    utf8_reset(&st);
    int n = 0;
    for (const char *p = k->label; *p && n < max; p++) {
        uint32_t cp;
        if (utf8_decode(&st, (uint8_t)*p, &cp)) out[n++] = (uint16_t)cp;
    }
    return n;
}

static uint16_t key_background(const kbd_key_t *k, bool down)
{
    if (down) return C_KEY_DOWN;
    if (k->code == K_SHIFT) {
        if (s_shift == MOD_LOCKED) return C_MOD_LOCKED;
        if (s_shift == MOD_ARMED)  return C_MOD_ARMED;
    }
    if (k->code == K_CTRL) {
        if (s_ctrl == MOD_LOCKED) return C_MOD_LOCKED;
        if (s_ctrl == MOD_ARMED)  return C_MOD_ARMED;
    }
    return (k->code >= K_SHIFT) ? C_KEY_SPECIAL : C_KEY;
}

static void draw_key(int row, int col, bool down)
{
    const kbd_key_t (*rows)[12] = layout();
    const kbd_key_t *k = &rows[row][col];
    if (k->units == 0) return;

    int ux = 0;
    for (int i = 0; i < col; i++) ux += rows[row][i].units;

    int w = k->units * KBD_UNIT_W;
    int x = ux * KBD_UNIT_W;
    int y = KBD_Y + row * KBD_KEY_H;

    uint16_t bg = key_background(k, down);

    // 2px gap all round: at 40px wide, a 1px border does not separate adjacent
    // keys enough to read as distinct targets.
    for (int py = 0; py < KBD_KEY_H; py++) {
        bool edge_y = (py < KEY_GAP) || (py >= KBD_KEY_H - KEY_GAP);
        for (int px = 0; px < w; px++) {
            bool edge = edge_y || (px < KEY_GAP) || (px >= w - KEY_GAP);
            s_keybuf[py * w + px] = edge ? C_GAP : bg;
        }
    }

    uint16_t label[4];
    int n = label_codepoints(k, row, col, label, 4);

    // Word labels ("Ctl", "Esc", "123") need three glyphs, which at 2x would
    // fill the key edge to edge and run into its neighbours. Drop those to 1x.
    int scale = (n <= 2) ? 2 : 1;
    int label_w = n * FONT_CELL_W * scale;
    int lx = (w - label_w) / 2;
    int ly = (KBD_KEY_H - FONT_CELL_H * scale) / 2;
    for (int i = 0; i < n; i++) {
        draw_glyph(s_keybuf, w, lx + i * FONT_CELL_W * scale, ly, label[i], C_LABEL, scale);
    }

    lcd_raw_push(s_keybuf, x, y, w, KBD_KEY_H);
}

void kbd_draw(void)
{
    if (!s_visible) return;
    const kbd_key_t (*rows)[12] = layout();
    for (int r = 0; r < KBD_ROWS; r++) {
        for (int c = 0; c < 12; c++) {
            if (rows[r][c].units == 0) break;
            draw_key(r, c, false);
        }
    }
}

// --- Byte emission ----------------------------------------------------------

static void send(const char *bytes, int len)
{
    usb_cdc_write((const uint8_t *)bytes, (size_t)len);
}

// Ctrl-<char> for the non-letter cases. Ctrl-letter is just c & 0x1f.
static int ctrl_byte(uint16_t c)
{
    if (c >= 'a' && c <= 'z') return c & 0x1F;
    if (c >= 'A' && c <= 'Z') return c & 0x1F;
    switch (c) {
    case '@': case ' ': case '2': return 0x00;
    case '[': case '3': return 0x1B;
    case '\\': case '4': return 0x1C;
    case ']': case '5': return 0x1D;
    case '^': case '6': return 0x1E;
    case '_': case '7': case '-': return 0x1F;
    case '?': case '8': return 0x7F;
    default: return -1;
    }
}

static void emit_arrow(char final)
{
    // vim, less and anything using keypad(TRUE) put the terminal in
    // application-cursor mode; sending CSI there gives the classic "arrows work
    // in bash but not in vim".
    char buf[3] = { 0x1B, term_app_cursor_mode() ? 'O' : '[', final };
    send(buf, 3);
}

static void emit_key(const kbd_key_t *k)
{
    bool shift = (s_shift != MOD_OFF);
    bool ctrl  = (s_ctrl != MOD_OFF);

    switch (k->code) {
    case K_ENTER: send("\r", 1); break;            // CR, not LF: the tty's
                                                   // ICRNL converts it and
                                                   // readline expects CR
    case K_BKSP:  send("\x7f", 1); break;
    case K_TAB:   if (shift) send("\x1b[Z", 3); else send("\t", 1); break;
    case K_ESC:   send("\x1b", 1); break;
    case K_UP:    emit_arrow('A'); break;
    case K_DOWN:  emit_arrow('B'); break;
    case K_RIGHT: emit_arrow('C'); break;
    case K_LEFT:  emit_arrow('D'); break;
    default: {
        if (k->code >= K_SHIFT) return;            // handled by the caller
        uint16_t c = shift ? k->shift_code : k->code;
        if (ctrl) {
            int b = ctrl_byte(c);
            if (b >= 0) { char ch = (char)b; send(&ch, 1); return; }
        }
        char ch = (char)c;
        send(&ch, 1);
        break;
    }
    }
}

// --- Modifier bookkeeping ---------------------------------------------------

static uint8_t cycle_mod(uint8_t m)
{
    // off -> armed (one shot) -> locked -> off
    return (m == MOD_OFF) ? MOD_ARMED : (m == MOD_ARMED ? MOD_LOCKED : MOD_OFF);
}

// Redraw whichever modifier keys are on screen (their colour encodes state).
static void redraw_modifiers(void)
{
    const kbd_key_t (*rows)[12] = layout();
    for (int r = 0; r < KBD_ROWS; r++) {
        for (int c = 0; c < 12; c++) {
            if (rows[r][c].units == 0) break;
            uint16_t code = rows[r][c].code;
            if (code == K_SHIFT || code == K_CTRL) draw_key(r, c, false);
        }
    }
}

// Shift also changes every letter's label, so a shift change is a full repaint.
static void consume_oneshots(void)
{
    bool shift_was_armed = (s_shift == MOD_ARMED);
    if (s_shift == MOD_ARMED) s_shift = MOD_OFF;
    if (s_ctrl == MOD_ARMED)  s_ctrl = MOD_OFF;
    if (shift_was_armed) kbd_draw(); else redraw_modifiers();
}

// --- Public -----------------------------------------------------------------

void kbd_init(void)
{
    s_visible = false;
    s_layer = 0;
    s_shift = s_ctrl = MOD_OFF;
    s_down_row = s_down_col = -1;
}

bool kbd_visible(void) { return s_visible; }

void kbd_show(void)
{
    if (s_visible) return;
    s_visible = true;
    render_set_keyboard_visible(true);
    render_frame();                 // repaint the clipped grid before the keys
    kbd_draw();
}

void kbd_hide(void)
{
    if (!s_visible) return;
    s_visible = false;
    s_down_row = s_down_col = -1;
    // The model has not changed, so the shadow diff would repaint nothing —
    // the rows the keyboard covered have to be invalidated explicitly.
    render_set_keyboard_visible(false);
    render_invalidate_all();
}

bool kbd_handle_touch(const touch_event_t *ev)
{
    if (ev->type == TOUCH_NONE) return false;

    if (!s_visible) {
        if (ev->type == TOUCH_PRESS) { kbd_show(); return true; }
        return false;
    }

    if (ev->type == TOUCH_RELEASE) {
        if (s_down_row >= 0) {
            int r = s_down_row, c = s_down_col;
            s_down_row = s_down_col = -1;
            draw_key(r, c, false);
        }
        return true;
    }

    if (ev->type == TOUCH_REPEAT) {
        if (s_down_row >= 0) {
            const kbd_key_t *k = &layout()[s_down_row][s_down_col];
            emit_key(k);
        }
        return true;
    }

    // TOUCH_PRESS
    if (ev->y < KBD_Y) {            // tap outside the keyboard dismisses it
        kbd_hide();
        return true;
    }

    int row = (ev->y - KBD_Y) / KBD_KEY_H;
    if (row < 0 || row >= KBD_ROWS) return true;
    int unit = ev->x / KBD_UNIT_W;
    if (unit >= KBD_UNITS) unit = KBD_UNITS - 1;

    int col = key_at_unit(row, unit, NULL, NULL);
    if (col < 0) return true;

    const kbd_key_t *k = &layout()[row][col];

    s_down_row = row;
    s_down_col = col;
    draw_key(row, col, true);
    touch_enable_repeat((k->flags & KF_REPEAT) != 0);

    switch (k->code) {
    case K_SHIFT:
        s_shift = cycle_mod(s_shift);
        kbd_draw();                 // every letter label changes
        return true;
    case K_CTRL:
        s_ctrl = cycle_mod(s_ctrl);
        redraw_modifiers();
        draw_key(row, col, true);   // keep the press highlight
        return true;
    case K_LAYER:
        s_layer ^= 1;
        s_down_row = s_down_col = -1;
        kbd_draw();
        return true;
    case K_HIDE:
        kbd_hide();
        return true;
    case K_ZOOM:
        render_set_scale(render_get_scale() > 1 ? 1 : 2);
        draw_key(row, col, true);
        return true;
    default:
        break;
    }

    emit_key(k);
    consume_oneshots();
    draw_key(row, col, true);       // consume_oneshots may have repainted it
    return true;
}

bool kbd_shift_active(void)  { return s_shift != MOD_OFF; }
bool kbd_ctrl_active(void)   { return s_ctrl != MOD_OFF; }
bool kbd_shift_locked(void)  { return s_shift == MOD_LOCKED; }
bool kbd_ctrl_locked(void)   { return s_ctrl == MOD_LOCKED; }
