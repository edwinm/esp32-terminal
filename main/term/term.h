// term.h — the terminal screen model.
//
// The model is what the host has told us the screen contains; the renderer
// (render.c) is what is physically on the glass. They are deliberately
// separate: the renderer diffs against its own shadow copy, so the model never
// has to track dirtiness.
//
// Threading: the `term` task mutates this through term_feed(); the `disp` task
// reads it through term_snapshot_row() / term_cursor(). Both take term_lock().
// Hold the lock briefly — one row at a time, never a whole frame.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board_pins.h"

// --- Cell ------------------------------------------------------------------

#define ATTR_BOLD      0x0001u
#define ATTR_DIM       0x0002u
#define ATTR_ITALIC    0x0004u   // parsed, not rendered (no room at 6px)
#define ATTR_UNDERLINE 0x0008u
#define ATTR_BLINK     0x0010u   // parsed, rendered as bold
#define ATTR_REVERSE   0x0020u
#define ATTR_INVISIBLE 0x0040u
#define ATTR_STRIKE    0x0080u
#define ATTR_WIDE_LEAD 0x0100u   // first cell of a double-width character
#define ATTR_WIDE_TAIL 0x0200u   // its placeholder second cell

typedef struct {
    uint16_t ch;    // Unicode codepoint (BMP; anything higher becomes U+FFFD)
    uint16_t attr;  // ATTR_* bits
    uint16_t fg;    // RGB565, ALREADY BYTE-SWAPPED (see lcd.h)
    uint16_t bg;    // RGB565, byte-swapped
} term_cell_t;      // 8 bytes

// --- Pen (logical SGR state) ------------------------------------------------
//
// Colours are kept logical rather than resolved so that "bold brightens the
// indexed colours 30-37 into 90-97" works no matter which order `1` and `31`
// arrive in. Resolution to RGB565 happens once, at cell-write time.

enum { COLOR_DEFAULT = 0, COLOR_INDEXED, COLOR_RGB };

typedef struct {
    uint8_t kind;              // COLOR_*
    uint8_t index;             // COLOR_INDEXED: 0..255
    uint8_t r, g, b;           // COLOR_RGB
} term_color_t;

typedef struct {
    term_color_t fg, bg;
    uint16_t     attr;
} term_pen_t;

typedef struct {
    int          cx, cy;
    term_pen_t   pen;
    uint8_t      charset[2];
    uint8_t      gl;
    bool         origin_mode;
    bool         pending_wrap;
} term_saved_t;

// --- Terminal ---------------------------------------------------------------

typedef struct {
    // Row indirection: scrolling rotates pointers instead of memmoving 15 KB.
    term_cell_t *row[TERM_ROWS];          // the screen currently displayed
    term_cell_t *row_primary[TERM_ROWS];
    term_cell_t *row_alt[TERM_ROWS];
    term_cell_t  cells_primary[TERM_ROWS][TERM_COLS];
    term_cell_t  cells_alt[TERM_ROWS][TERM_COLS];

    int  cx, cy;                  // cursor, 0-based, screen coordinates
    bool pending_wrap;            // deferred wrap: see term_put_char()
    term_pen_t pen;

    int  top, bot;                // scroll region, inclusive, 0-based

    // Modes
    bool origin_mode;             // DECOM   ?6
    bool autowrap;                // DECAWM  ?7
    bool insert_mode;             // IRM      4
    bool newline_mode;            // LNM     20
    bool cursor_visible;          // DECTCEM ?25
    bool cursor_blink;            // ATT610  ?12
    bool reverse_video;           // DECSCNM ?5
    bool app_cursor;              // DECCKM  ?1   <- the keyboard reads this
    bool app_keypad;              // DECKPAM
    bool alt_screen;              // ?47 / ?1047 / ?1049
    bool bracketed_paste;         // ?2004
    uint16_t mouse_mode;          // ?1000..?1006 — recorded, never reported

    uint8_t charset[2];           // G0, G1: 'B' = ASCII, '0' = DEC graphics
    uint8_t gl;                   // which of the two SO/SI has shifted in

    bool tabs[TERM_COLS];

    term_saved_t saved;           // DECSC / DECRC
    int sco_cx, sco_cy;           // CSI s / CSI u (a separate register)

    uint16_t last_graphic;        // for REP (CSI b)

    uint32_t generation;          // bumped on every mutation; renderer's gate
} term_t;

// The xterm 256-colour palette, byte-swapped RGB565, built by term_init().
extern uint16_t term_pal256[256];

// Default pen colours. Slightly brighter than palette index 7 — at 6px on a
// 165 DPI panel the standard (192,192,192) reads as grey mush.
#define TERM_DEFAULT_FG_RGB 0xD8D8D8u
#define TERM_DEFAULT_BG_RGB 0x000000u

void      term_init(void);
term_t   *term_get(void);
void      term_lock(void);
void      term_unlock(void);
uint32_t  term_generation(void);      // lock-free read; a hint, not a fence

// Feed bytes received from the host. Takes the lock internally.
void term_feed(const uint8_t *data, size_t len);

// Renderer accessors (each takes the lock briefly).
void term_snapshot_row(int r, term_cell_t *out);
void term_cursor_state(int *cx, int *cy, bool *visible, bool *blink, bool *reverse_video);

// True when the host has put us in application-cursor mode; the on-screen
// keyboard must then send ESC O A rather than ESC [ A.
bool term_app_cursor_mode(void);

// --- Internals shared between term_screen.c, vt_parser.c and vt_modes.c -----

uint16_t     term_resolve_fg(const term_t *t);
uint16_t     term_resolve_bg(const term_t *t);
term_cell_t  term_blank_cell(const term_t *t);

void term_reset(term_t *t, bool hard);
void term_soft_reset(term_t *t);
void term_clear_screen(term_t *t);

void term_move_to(term_t *t, int row, int col);   // honours origin mode
void term_move_rel(term_t *t, int drow, int dcol);

void term_put_char(term_t *t, uint32_t cp);
void term_repeat_last(term_t *t, int n);

void term_index(term_t *t);           // LF within the scroll region
void term_reverse_index(term_t *t);   // RI
void term_carriage_return(term_t *t);
void term_backspace(term_t *t);
void term_tab_forward(term_t *t, int n);
void term_tab_backward(term_t *t, int n);

void term_scroll_up(term_t *t, int n);
void term_scroll_down(term_t *t, int n);

void term_erase_display(term_t *t, int mode);   // ED
void term_erase_line(term_t *t, int mode);      // EL
void term_erase_chars(term_t *t, int n);        // ECH
void term_insert_chars(term_t *t, int n);       // ICH
void term_delete_chars(term_t *t, int n);       // DCH
void term_insert_lines(term_t *t, int n);       // IL
void term_delete_lines(term_t *t, int n);       // DL

void term_set_scroll_region(term_t *t, int top, int bot);
void term_save_cursor(term_t *t);
void term_restore_cursor(term_t *t);
void term_switch_screen(term_t *t, bool alt, bool clear);

// Reply channel back to the host (DA, CPR, ...). Implemented in usb_cdc.c.
void term_reply(const char *s, int len);
