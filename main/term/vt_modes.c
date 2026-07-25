// vt_modes.c — SGR, SM/RM (and DEC private modes), host replies, and the
// DEC Special Graphics character set.
#include <stdio.h>
#include <string.h>

#include "term.h"
#include "vt_parser.h"

// --- SGR --------------------------------------------------------------------

static void set_default_pen(term_t *t)
{
    t->pen.fg.kind = COLOR_DEFAULT;
    t->pen.bg.kind = COLOR_DEFAULT;
    t->pen.attr = 0;
}

// Extended colour: 38/48 followed by 5;n (indexed) or 2;r;g;b (truecolour),
// with either ';' or ':' separators. The colon form has an extra "colour space
// id" slot — `38:2::R:G:B` — which we skip when present.
//
// Returns the number of parameters consumed beyond the 38/48 itself.
static int parse_ext_color(term_color_t *dst, const int16_t *p, int n, int i,
                           uint32_t submask, bool colon_form)
{
    if (i + 1 >= n) return 0;
    int kind = p[i + 1] < 0 ? 0 : p[i + 1];

    if (kind == 5) {
        if (i + 2 >= n) return 1;
        dst->kind = COLOR_INDEXED;
        dst->index = (uint8_t)(p[i + 2] < 0 ? 0 : p[i + 2] & 0xFF);
        return 2;
    }
    if (kind == 2) {
        int base = i + 2;
        // In the colon form the slot right after `2` may be an empty colour
        // space id. Skip it only when it is genuinely empty.
        if (colon_form && base < n && p[base] < 0) base++;
        if (base + 2 >= n) return (n - 1) - i;
        dst->kind = COLOR_RGB;
        dst->r = (uint8_t)(p[base] < 0 ? 0 : p[base] & 0xFF);
        dst->g = (uint8_t)(p[base + 1] < 0 ? 0 : p[base + 1] & 0xFF);
        dst->b = (uint8_t)(p[base + 2] < 0 ? 0 : p[base + 2] & 0xFF);
        (void)submask;
        return (base + 2) - i;
    }
    return 1;
}

void vt_sgr(term_t *t, const int16_t *p, int n, uint32_t submask)
{
    if (n == 0) {                      // an empty `CSI m` means `CSI 0 m`
        set_default_pen(t);
        return;
    }

    for (int i = 0; i < n; i++) {
        int v = p[i] < 0 ? 0 : p[i];
        switch (v) {
        case 0:  set_default_pen(t); break;
        case 1:  t->pen.attr |= ATTR_BOLD; break;
        case 2:  t->pen.attr |= ATTR_DIM; break;
        case 3:  t->pen.attr |= ATTR_ITALIC; break;
        case 4:  t->pen.attr |= ATTR_UNDERLINE; break;
        case 5:
        case 6:  t->pen.attr |= ATTR_BLINK; break;
        case 7:  t->pen.attr |= ATTR_REVERSE; break;
        case 8:  t->pen.attr |= ATTR_INVISIBLE; break;
        case 9:  t->pen.attr |= ATTR_STRIKE; break;
        case 21: t->pen.attr |= ATTR_UNDERLINE; break;   // double underline
        case 22: t->pen.attr &= ~(ATTR_BOLD | ATTR_DIM); break;
        case 23: t->pen.attr &= ~ATTR_ITALIC; break;
        case 24: t->pen.attr &= ~ATTR_UNDERLINE; break;
        case 25: t->pen.attr &= ~ATTR_BLINK; break;
        case 27: t->pen.attr &= ~ATTR_REVERSE; break;
        case 28: t->pen.attr &= ~ATTR_INVISIBLE; break;
        case 29: t->pen.attr &= ~ATTR_STRIKE; break;

        case 38:
        case 48: {
            term_color_t *dst = (v == 38) ? &t->pen.fg : &t->pen.bg;
            // A colon introduced the sub-parameters if the very next slot was
            // flagged by the parser.
            bool colon = (i + 1 < 32) && (submask & (1u << (i + 1))) != 0;
            i += parse_ext_color(dst, p, n, i, submask, colon);
            break;
        }
        case 39: t->pen.fg.kind = COLOR_DEFAULT; break;
        case 49: t->pen.bg.kind = COLOR_DEFAULT; break;

        default:
            if (v >= 30 && v <= 37) {
                t->pen.fg.kind = COLOR_INDEXED; t->pen.fg.index = (uint8_t)(v - 30);
            } else if (v >= 40 && v <= 47) {
                t->pen.bg.kind = COLOR_INDEXED; t->pen.bg.index = (uint8_t)(v - 40);
            } else if (v >= 90 && v <= 97) {
                t->pen.fg.kind = COLOR_INDEXED; t->pen.fg.index = (uint8_t)(v - 90 + 8);
            } else if (v >= 100 && v <= 107) {
                t->pen.bg.kind = COLOR_INDEXED; t->pen.bg.index = (uint8_t)(v - 100 + 8);
            }
            break;
        }
    }
}

// --- SM / RM ----------------------------------------------------------------

void vt_set_mode(term_t *t, int mode, bool priv, bool set)
{
    if (!priv) {
        switch (mode) {
        case 4:  t->insert_mode = set; break;      // IRM
        case 20: t->newline_mode = set; break;     // LNM
        default: break;
        }
        return;
    }

    switch (mode) {
    case 1:  t->app_cursor = set; break;           // DECCKM — the keyboard reads this
    case 3:                                        // DECCOLM
        // We cannot change column count, but applications depend on the side
        // effect: the screen clears and the cursor homes.
        term_clear_screen(t);
        term_set_scroll_region(t, 0, TERM_ROWS - 1);
        term_move_to(t, 0, 0);
        break;
    case 5:  t->reverse_video = set; break;        // DECSCNM
    case 6:                                        // DECOM
        t->origin_mode = set;
        term_move_to(t, 0, 0);
        break;
    case 7:  t->autowrap = set; break;             // DECAWM
    case 12: t->cursor_blink = set; break;         // ATT610 cursor blink
    case 25: t->cursor_visible = set; break;       // DECTCEM

    case 47:
    case 1047:
        term_switch_screen(t, set, set);
        break;
    case 1048:
        if (set) term_save_cursor(t); else term_restore_cursor(t);
        break;
    case 1049:
        if (set) {
            term_save_cursor(t);
            term_switch_screen(t, true, true);
        } else {
            term_switch_screen(t, false, false);
            term_restore_cursor(t);
        }
        break;

    // Mouse and focus reporting: recorded so DECRQM-style probes see something
    // consistent, but we never generate events — there is no mouse.
    case 1000: case 1002: case 1003: case 1005: case 1006: case 1015:
        if (set) t->mouse_mode |= (uint16_t)mode; else t->mouse_mode = 0;
        break;
    case 1004: break;                              // focus events
    case 2004: t->bracketed_paste = set; break;

    default: break;
    }
}

// --- Replies ----------------------------------------------------------------

void vt_reply_da(term_t *t)
{
    (void)t;
    // "VT220 with 132 columns, selective erase, national replacement charsets,
    // technical characters, horizontal scrolling, ANSI colour" — the usual
    // xterm answer, which is what TERM=xterm-256color implies.
    static const char da[] = "\033[?62;1;6;9;15;22c";
    term_reply(da, sizeof(da) - 1);
}

void vt_reply_da2(term_t *t)
{
    (void)t;
    static const char da2[] = "\033[>0;10;1c";
    term_reply(da2, sizeof(da2) - 1);
}

void vt_reply_dsr(term_t *t, int what)
{
    char buf[32];
    switch (what) {
    case 5:
        term_reply("\033[0n", 4);                  // "terminal OK"
        break;
    case 6: {
        // CPR. This is how `resize` learns the terminal size: it homes the
        // cursor to 999;999, asks here, and reads back the clamped position.
        int row = t->cy + 1, col = t->cx + 1;
        if (t->origin_mode) row = t->cy - t->top + 1;
        int n = snprintf(buf, sizeof(buf), "\033[%d;%dR", row, col);
        term_reply(buf, n);
        break;
    }
    default:
        break;
    }
}

void vt_reply_winop(term_t *t, int what)
{
    (void)t;
    char buf[32];
    if (what == 18) {                              // report text area in chars
        int n = snprintf(buf, sizeof(buf), "\033[8;%d;%dt", TERM_ROWS, TERM_COLS);
        term_reply(buf, n);
    }
    // Everything else (resize/move/iconify requests) is silently ignored.
}

// --- DEC Special Graphics ---------------------------------------------------

// Needed by dialog, mc and ncurses outside a UTF-8 locale. htop in a UTF-8
// locale emits the box characters directly and never comes through here.
static const uint16_t k_dec_graphics[] = {
    /* 0x5F _ */ 0x0020,  /* 0x60 ` */ 0x25C6,  /* 0x61 a */ 0x2592,
    /* 0x62 b */ 0x2409,  /* 0x63 c */ 0x240C,  /* 0x64 d */ 0x240D,
    /* 0x65 e */ 0x240A,  /* 0x66 f */ 0x00B0,  /* 0x67 g */ 0x00B1,
    /* 0x68 h */ 0x2424,  /* 0x69 i */ 0x240B,  /* 0x6A j */ 0x2518,
    /* 0x6B k */ 0x2510,  /* 0x6C l */ 0x250C,  /* 0x6D m */ 0x2514,
    /* 0x6E n */ 0x253C,  /* 0x6F o */ 0x23BA,  /* 0x70 p */ 0x23BB,
    /* 0x71 q */ 0x2500,  /* 0x72 r */ 0x23BC,  /* 0x73 s */ 0x23BD,
    /* 0x74 t */ 0x251C,  /* 0x75 u */ 0x2524,  /* 0x76 v */ 0x2534,
    /* 0x77 w */ 0x252C,  /* 0x78 x */ 0x2502,  /* 0x79 y */ 0x2264,
    /* 0x7A z */ 0x2265,  /* 0x7B { */ 0x03C0,  /* 0x7C | */ 0x2260,
    /* 0x7D } */ 0x00A3,  /* 0x7E ~ */ 0x00B7,
};

uint32_t vt_dec_graphics(uint8_t c)
{
    if (c < 0x5F || c > 0x7E) return 0;
    return k_dec_graphics[c - 0x5F];
}
