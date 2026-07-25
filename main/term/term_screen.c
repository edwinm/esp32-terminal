// term_screen.c — the screen model: cells, cursor, scrolling, erasing.
//
// Everything here assumes the caller holds term_lock() (term_feed does).
#include <string.h>

#include "term.h"
#include "utf8.h"
#include "vt_parser.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static term_t            s_term;
static SemaphoreHandle_t s_lock;
static StaticSemaphore_t s_lock_buf;

uint16_t term_pal256[256];

// --- Palette ---------------------------------------------------------------

// xterm's standard 16. If dark blue (index 4) proves unreadable on this panel,
// this table is the one line to lift.
static const uint8_t k_ansi16[16][3] = {
    {  0,   0,   0}, {205,   0,   0}, {  0, 205,   0}, {205, 205,   0},
    {  0,   0, 238}, {205,   0, 205}, {  0, 205, 205}, {229, 229, 229},
    {127, 127, 127}, {255,   0,   0}, {  0, 255,   0}, {255, 255,   0},
    { 92,  92, 255}, {255,   0, 255}, {  0, 255, 255}, {255, 255, 255},
};

// Byte-swapped RGB565 — see the byte-order note in lcd.h.
static inline uint16_t rgb_to_swapped565(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t v = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((v >> 8) | (v << 8));
}

static void build_palette(void)
{
    for (int i = 0; i < 16; i++) {
        term_pal256[i] = rgb_to_swapped565(k_ansi16[i][0], k_ansi16[i][1], k_ansi16[i][2]);
    }
    for (int i = 0; i < 216; i++) {            // 6x6x6 colour cube
        int r = i / 36, g = (i / 6) % 6, b = i % 6;
        term_pal256[16 + i] = rgb_to_swapped565(r ? 55 + r * 40 : 0,
                                                g ? 55 + g * 40 : 0,
                                                b ? 55 + b * 40 : 0);
    }
    for (int i = 0; i < 24; i++) {             // greyscale ramp
        uint8_t v = (uint8_t)(8 + i * 10);
        term_pal256[232 + i] = rgb_to_swapped565(v, v, v);
    }
}

// --- Colour resolution ------------------------------------------------------

static uint16_t resolve(const term_color_t *c, uint32_t dflt, bool brighten)
{
    switch (c->kind) {
    case COLOR_INDEXED: {
        uint8_t idx = c->index;
        if (brighten && idx < 8) idx += 8;     // SGR 1 brightens 30-37 -> 90-97
        return term_pal256[idx];
    }
    case COLOR_RGB:
        return rgb_to_swapped565(c->r, c->g, c->b);
    default:
        return rgb_to_swapped565((dflt >> 16) & 0xFF, (dflt >> 8) & 0xFF, dflt & 0xFF);
    }
}

uint16_t term_resolve_fg(const term_t *t)
{
    return resolve(&t->pen.fg, TERM_DEFAULT_FG_RGB, (t->pen.attr & ATTR_BOLD) != 0);
}

uint16_t term_resolve_bg(const term_t *t)
{
    return resolve(&t->pen.bg, TERM_DEFAULT_BG_RGB, false);
}

// Erase fills with the *current* background but the default foreground and no
// attributes — that is what xterm does, and what `clear` on a coloured prompt
// relies on.
term_cell_t term_blank_cell(const term_t *t)
{
    term_cell_t c;
    c.ch   = ' ';
    c.attr = 0;
    c.fg   = resolve(&t->pen.fg, TERM_DEFAULT_FG_RGB, false);
    c.bg   = term_resolve_bg(t);
    return c;
}

static void blank_row(term_t *t, term_cell_t *row)
{
    term_cell_t b = term_blank_cell(t);
    for (int c = 0; c < TERM_COLS; c++) row[c] = b;
}

// --- Lifecycle --------------------------------------------------------------

static void reset_tabs(term_t *t)
{
    for (int c = 0; c < TERM_COLS; c++) t->tabs[c] = (c % 8) == 0 && c != 0;
}

void term_reset(term_t *t, bool hard)
{
    t->pen.fg.kind = COLOR_DEFAULT;
    t->pen.bg.kind = COLOR_DEFAULT;
    t->pen.attr    = 0;

    t->cx = t->cy = 0;
    t->pending_wrap = false;
    t->top = 0;
    t->bot = TERM_ROWS - 1;

    t->origin_mode     = false;
    t->autowrap        = true;
    t->insert_mode     = false;
    t->newline_mode    = false;
    t->cursor_visible  = true;
    t->cursor_blink    = true;
    t->reverse_video   = false;
    t->app_cursor      = false;
    t->app_keypad      = false;
    t->bracketed_paste = false;
    t->mouse_mode      = 0;

    t->charset[0] = 'B';
    t->charset[1] = 'B';
    t->gl = 0;

    t->last_graphic = 0;
    t->sco_cx = t->sco_cy = 0;
    memset(&t->saved, 0, sizeof(t->saved));
    t->saved.charset[0] = t->saved.charset[1] = 'B';

    reset_tabs(t);

    if (hard) {
        for (int r = 0; r < TERM_ROWS; r++) {
            t->row_primary[r] = t->cells_primary[r];
            t->row_alt[r]     = t->cells_alt[r];
        }
        t->alt_screen = false;
        for (int r = 0; r < TERM_ROWS; r++) {
            t->row[r] = t->row_primary[r];
            blank_row(t, t->row_primary[r]);
            blank_row(t, t->row_alt[r]);
        }
    }
}

void term_soft_reset(term_t *t)   // DECSTR
{
    bool alt = t->alt_screen;
    term_reset(t, false);
    t->alt_screen = alt;
}

void term_init(void)
{
    build_palette();
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    memset(&s_term, 0, sizeof(s_term));
    term_reset(&s_term, true);
    vt_parser_reset();
    s_term.generation = 1;
}

term_t *term_get(void)       { return &s_term; }
void    term_lock(void)      { xSemaphoreTake(s_lock, portMAX_DELAY); }
void    term_unlock(void)    { xSemaphoreGive(s_lock); }
uint32_t term_generation(void) { return s_term.generation; }

void term_feed(const uint8_t *data, size_t len)
{
    term_lock();
    vt_parser_feed(&s_term, data, len);
    s_term.generation++;
    term_unlock();
}

void term_snapshot_row(int r, term_cell_t *out)
{
    term_lock();
    memcpy(out, s_term.row[r], TERM_COLS * sizeof(term_cell_t));
    term_unlock();
}

void term_cursor_state(int *cx, int *cy, bool *visible, bool *blink, bool *reverse_video)
{
    term_lock();
    *cx = s_term.cx;
    *cy = s_term.cy;
    *visible = s_term.cursor_visible;
    *blink = s_term.cursor_blink;
    *reverse_video = s_term.reverse_video;
    term_unlock();
}

bool term_app_cursor_mode(void) { return s_term.app_cursor; }

// --- Scrolling --------------------------------------------------------------

// Reserved for scrollback: this is the one place a line leaves the top of the
// primary screen. v2 pushes it into a PSRAM ring here.
static inline void scrollback_push(const term_cell_t *row) { (void)row; }

void term_scroll_up(term_t *t, int n)
{
    int height = t->bot - t->top + 1;
    if (n <= 0) return;
    if (n > height) n = height;

    term_cell_t *stash[TERM_ROWS];
    for (int i = 0; i < n; i++) {
        stash[i] = t->row[t->top + i];
        if (t->top == 0 && !t->alt_screen) scrollback_push(stash[i]);
    }
    for (int r = t->top; r + n <= t->bot; r++) {
        t->row[r] = t->row[r + n];
    }
    for (int i = 0; i < n; i++) {
        t->row[t->bot - n + 1 + i] = stash[i];
        blank_row(t, stash[i]);
    }
}

void term_scroll_down(term_t *t, int n)
{
    int height = t->bot - t->top + 1;
    if (n <= 0) return;
    if (n > height) n = height;

    term_cell_t *stash[TERM_ROWS];
    for (int i = 0; i < n; i++) stash[i] = t->row[t->bot - n + 1 + i];
    for (int r = t->bot; r - n >= t->top; r--) {
        t->row[r] = t->row[r - n];
    }
    for (int i = 0; i < n; i++) {
        t->row[t->top + i] = stash[i];
        blank_row(t, stash[i]);
    }
}

// --- Cursor movement --------------------------------------------------------

void term_move_to(term_t *t, int row, int col)
{
    if (t->origin_mode) {
        row += t->top;
        if (row < t->top) row = t->top;
        if (row > t->bot) row = t->bot;
    } else {
        if (row < 0) row = 0;
        if (row >= TERM_ROWS) row = TERM_ROWS - 1;
    }
    if (col < 0) col = 0;
    if (col >= TERM_COLS) col = TERM_COLS - 1;
    t->cy = row;
    t->cx = col;
    t->pending_wrap = false;
}

void term_move_rel(term_t *t, int drow, int dcol)
{
    int lo = t->origin_mode ? t->top : 0;
    int hi = t->origin_mode ? t->bot : TERM_ROWS - 1;
    // Vertical movement is confined by the scroll region when the cursor is
    // already inside it, which is what CUU/CUD do in a real VT.
    if (t->cy >= t->top && t->cy <= t->bot) { lo = t->top; hi = t->bot; }

    int row = t->cy + drow;
    if (row < lo) row = lo;
    if (row > hi) row = hi;

    int col = t->cx + dcol;
    if (col < 0) col = 0;
    if (col >= TERM_COLS) col = TERM_COLS - 1;

    t->cy = row;
    t->cx = col;
    t->pending_wrap = false;
}

void term_carriage_return(term_t *t)
{
    t->cx = 0;
    t->pending_wrap = false;
}

void term_backspace(term_t *t)
{
    if (t->cx > 0) t->cx--;
    t->pending_wrap = false;
}

void term_index(term_t *t)
{
    if (t->cy == t->bot) {
        term_scroll_up(t, 1);
    } else if (t->cy < TERM_ROWS - 1) {
        t->cy++;
    }
    t->pending_wrap = false;
}

void term_reverse_index(term_t *t)
{
    if (t->cy == t->top) {
        term_scroll_down(t, 1);
    } else if (t->cy > 0) {
        t->cy--;
    }
    t->pending_wrap = false;
}

void term_tab_forward(term_t *t, int n)
{
    for (; n > 0; n--) {
        int c = t->cx + 1;
        while (c < TERM_COLS - 1 && !t->tabs[c]) c++;
        t->cx = c;
    }
    t->pending_wrap = false;
}

void term_tab_backward(term_t *t, int n)
{
    for (; n > 0; n--) {
        int c = t->cx - 1;
        while (c > 0 && !t->tabs[c]) c--;
        t->cx = c;
    }
    t->pending_wrap = false;
}

// --- Writing ----------------------------------------------------------------

static void write_cell(term_t *t, int col, uint32_t cp, uint16_t extra_attr)
{
    term_cell_t *c = &t->row[t->cy][col];
    c->ch   = (uint16_t)cp;
    c->attr = (uint16_t)(t->pen.attr | extra_attr);
    c->fg   = term_resolve_fg(t);
    c->bg   = term_resolve_bg(t);
}

// Shift the row right by `n` from the cursor, for insert mode / ICH.
static void shift_right(term_t *t, int from, int n)
{
    term_cell_t *row = t->row[t->cy];
    term_cell_t blank = term_blank_cell(t);
    for (int c = TERM_COLS - 1; c >= from + n; c--) row[c] = row[c - n];
    for (int c = from; c < from + n && c < TERM_COLS; c++) row[c] = blank;
}

void term_put_char(term_t *t, uint32_t cp)
{
    int width = utf8_char_width(cp);
    if (width == 0) return;              // combining mark: dropped, see utf8.c
    if (width < 0) width = 1;

    // Deferred wrap. Writing into the last column does NOT move the cursor; it
    // parks it there with pending_wrap set, and the wrap happens when the next
    // printable character arrives. Without this, every line that exactly fills
    // the width — htop's header, vim's status line — produces a spurious blank
    // line, and the symptom looks like broken scrolling rather than broken
    // wrapping.
    if (t->pending_wrap && t->autowrap) {
        t->cx = 0;
        term_index(t);
        t->pending_wrap = false;
    }

    if (width == 2 && t->cx == TERM_COLS - 1) {
        // A double-width character cannot straddle the edge: blank the last
        // cell and wrap first, so the pair stays together.
        if (t->autowrap) {
            t->row[t->cy][t->cx] = term_blank_cell(t);
            t->cx = 0;
            term_index(t);
        } else {
            return;
        }
    }

    if (t->insert_mode) shift_right(t, t->cx, width);

    t->last_graphic = (uint16_t)cp;

    if (width == 2) {
        write_cell(t, t->cx, cp, ATTR_WIDE_LEAD);
        write_cell(t, t->cx + 1, 0, ATTR_WIDE_TAIL);
    } else {
        write_cell(t, t->cx, cp, 0);
    }

    if (t->cx + width >= TERM_COLS) {
        t->cx = TERM_COLS - 1;
        t->pending_wrap = t->autowrap;
    } else {
        t->cx += width;
    }
}

void term_repeat_last(term_t *t, int n)
{
    if (!t->last_graphic) return;
    uint16_t cp = t->last_graphic;
    for (int i = 0; i < n && i < TERM_COLS * TERM_ROWS; i++) {
        term_put_char(t, cp);
        t->last_graphic = cp;            // term_put_char just overwrote it
    }
}

// --- Erasing / editing ------------------------------------------------------

void term_erase_display(term_t *t, int mode)
{
    term_cell_t blank = term_blank_cell(t);
    switch (mode) {
    case 0:                              // cursor to end
        for (int c = t->cx; c < TERM_COLS; c++) t->row[t->cy][c] = blank;
        for (int r = t->cy + 1; r < TERM_ROWS; r++)
            for (int c = 0; c < TERM_COLS; c++) t->row[r][c] = blank;
        break;
    case 1:                              // start to cursor
        for (int r = 0; r < t->cy; r++)
            for (int c = 0; c < TERM_COLS; c++) t->row[r][c] = blank;
        for (int c = 0; c <= t->cx && c < TERM_COLS; c++) t->row[t->cy][c] = blank;
        break;
    case 2:                              // whole screen
    case 3:                              // + scrollback (we keep none)
        for (int r = 0; r < TERM_ROWS; r++)
            for (int c = 0; c < TERM_COLS; c++) t->row[r][c] = blank;
        break;
    default:
        break;
    }
    t->pending_wrap = false;
}

void term_erase_line(term_t *t, int mode)
{
    term_cell_t blank = term_blank_cell(t);
    switch (mode) {
    case 0: for (int c = t->cx; c < TERM_COLS; c++) t->row[t->cy][c] = blank; break;
    case 1: for (int c = 0; c <= t->cx && c < TERM_COLS; c++) t->row[t->cy][c] = blank; break;
    case 2: for (int c = 0; c < TERM_COLS; c++) t->row[t->cy][c] = blank; break;
    default: break;
    }
    t->pending_wrap = false;
}

void term_erase_chars(term_t *t, int n)
{
    term_cell_t blank = term_blank_cell(t);
    for (int c = t->cx; c < t->cx + n && c < TERM_COLS; c++) t->row[t->cy][c] = blank;
    t->pending_wrap = false;
}

void term_insert_chars(term_t *t, int n)
{
    if (n > TERM_COLS - t->cx) n = TERM_COLS - t->cx;
    if (n > 0) shift_right(t, t->cx, n);
    t->pending_wrap = false;
}

void term_delete_chars(term_t *t, int n)
{
    if (n > TERM_COLS - t->cx) n = TERM_COLS - t->cx;
    term_cell_t *row = t->row[t->cy];
    term_cell_t blank = term_blank_cell(t);
    for (int c = t->cx; c < TERM_COLS; c++) {
        row[c] = (c + n < TERM_COLS) ? row[c + n] : blank;
    }
    t->pending_wrap = false;
}

// IL/DL operate on the region between the cursor row and the bottom margin,
// and are no-ops when the cursor sits outside the scroll region.
void term_insert_lines(term_t *t, int n)
{
    if (t->cy < t->top || t->cy > t->bot) return;
    int saved_top = t->top;
    t->top = t->cy;
    term_scroll_down(t, n);
    t->top = saved_top;
    t->cx = 0;
    t->pending_wrap = false;
}

void term_delete_lines(term_t *t, int n)
{
    if (t->cy < t->top || t->cy > t->bot) return;
    int saved_top = t->top;
    t->top = t->cy;
    term_scroll_up(t, n);
    t->top = saved_top;
    t->cx = 0;
    t->pending_wrap = false;
}

void term_set_scroll_region(term_t *t, int top, int bot)
{
    if (top < 0) top = 0;
    if (bot > TERM_ROWS - 1) bot = TERM_ROWS - 1;
    if (top >= bot) { top = 0; bot = TERM_ROWS - 1; }
    t->top = top;
    t->bot = bot;
    // DECSTBM homes the cursor (to the region origin under DECOM).
    term_move_to(t, 0, 0);
}

void term_clear_screen(term_t *t)
{
    term_cell_t blank = term_blank_cell(t);
    for (int r = 0; r < TERM_ROWS; r++)
        for (int c = 0; c < TERM_COLS; c++) t->row[r][c] = blank;
}

// --- Save / restore / alt screen --------------------------------------------

void term_save_cursor(term_t *t)
{
    t->saved.cx = t->cx;
    t->saved.cy = t->cy;
    t->saved.pen = t->pen;
    t->saved.charset[0] = t->charset[0];
    t->saved.charset[1] = t->charset[1];
    t->saved.gl = t->gl;
    t->saved.origin_mode = t->origin_mode;
    t->saved.pending_wrap = t->pending_wrap;
}

void term_restore_cursor(term_t *t)
{
    t->pen = t->saved.pen;
    t->charset[0] = t->saved.charset[0];
    t->charset[1] = t->saved.charset[1];
    t->gl = t->saved.gl;
    t->origin_mode = t->saved.origin_mode;
    t->cy = t->saved.cy < TERM_ROWS ? t->saved.cy : TERM_ROWS - 1;
    t->cx = t->saved.cx < TERM_COLS ? t->saved.cx : TERM_COLS - 1;
    t->pending_wrap = t->saved.pending_wrap;
}

void term_switch_screen(term_t *t, bool alt, bool clear)
{
    if (alt == t->alt_screen) {
        if (alt && clear) term_clear_screen(t);
        return;
    }
    t->alt_screen = alt;
    for (int r = 0; r < TERM_ROWS; r++) {
        t->row[r] = alt ? t->row_alt[r] : t->row_primary[r];
    }
    if (alt && clear) term_clear_screen(t);
}
