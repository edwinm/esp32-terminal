// status.c — bottom status strip.
#include <stdio.h>
#include <string.h>

#include "board_pins.h"
#include "glyph.h"
#include "kbd.h"
#include "lcd.h"
#include "render.h"
#include "status.h"
#include "usb_cdc.h"

#define STATUS_COLS (GRID_W / (STATUS_FONT_W + 1))    // 80

#define C_BG     lcd_rgb(0x14, 0x16, 0x1a)
#define C_FG     lcd_rgb(0x9a, 0xa4, 0xb0)
#define C_ACCENT lcd_rgb(0xe0, 0xe0, 0xe0)
#define C_OPEN   lcd_rgb(0x40, 0xc0, 0x50)
#define C_IDLE   lcd_rgb(0xd0, 0x9a, 0x20)
#define C_GONE   lcd_rgb(0xd0, 0x40, 0x40)

static uint16_t s_buf[GRID_W * STATUS_H];
static char     s_last[STATUS_COLS + 1];
static uint16_t s_last_dot;
static const char *s_message;

static void put_text(int col, const char *text, uint16_t fg)
{
    for (int i = 0; text[i] && col + i < STATUS_COLS; i++) {
        const uint8_t *g = font_status_glyph(text[i]);
        int x = (col + i) * (STATUS_FONT_W + 1);
        for (int gy = 0; gy < STATUS_FONT_H && gy < STATUS_H; gy++) {
            uint8_t bits = g[gy];
            for (int gx = 0; gx < STATUS_FONT_W; gx++) {
                if (bits & (0x80 >> gx)) s_buf[gy * GRID_W + x + gx] = fg;
            }
        }
    }
}

void status_init(void)
{
    memset(s_last, 0, sizeof(s_last));
    s_last_dot = 0;
    s_message = NULL;
}

void status_set_message(const char *msg)
{
    s_message = msg;
    s_last[0] = '\0';          // force a repaint
}

void status_tick(void)
{
    char line[STATUS_COLS + 1];
    memset(line, ' ', STATUS_COLS);
    line[STATUS_COLS] = '\0';

    uint16_t dot;
    char left[48];
    if (s_message) {
        snprintf(left, sizeof(left), "%s", s_message);
        dot = C_IDLE;
    } else {
        switch (usb_cdc_link_state()) {
        case USB_LINK_OPEN:
            snprintf(left, sizeof(left), "USB open   %dx%d  xterm-256color",
                     TERM_COLS, TERM_ROWS);
            dot = C_OPEN;
            break;
        case USB_LINK_MOUNTED:
            snprintf(left, sizeof(left), "USB idle   %dx%d  no getty attached",
                     TERM_COLS, TERM_ROWS);
            dot = C_IDLE;
            break;
        default:
            snprintf(left, sizeof(left), "USB detached          waiting for host");
            dot = C_GONE;
            break;
        }
    }

    // Column 0 is the link indicator; text starts at column 2.
    int n = (int)strlen(left);
    if (n > STATUS_COLS - 2) n = STATUS_COLS - 2;
    memcpy(line + 2, left, n);

    char right[24];
    snprintf(right, sizeof(right), "%s%s%s%dx",
             kbd_ctrl_active()  ? (kbd_ctrl_locked()  ? "CTRL* " : "CTRL ") : "",
             kbd_shift_active() ? (kbd_shift_locked() ? "SHIFT* " : "SHIFT ") : "",
             "  ", render_get_scale());
    int rn = (int)strlen(right);
    if (rn < STATUS_COLS) memcpy(line + STATUS_COLS - rn, right, rn);

    if (dot == s_last_dot && memcmp(line, s_last, STATUS_COLS) == 0) return;
    memcpy(s_last, line, STATUS_COLS + 1);
    s_last_dot = dot;

    for (int i = 0; i < GRID_W * STATUS_H; i++) s_buf[i] = C_BG;

    // The indicator: a 4x4 block, vertically centred in the 8-pixel strip.
    for (int y = 2; y < 6; y++) {
        for (int x = 1; x < 5; x++) s_buf[y * GRID_W + x] = dot;
    }

    put_text(0, line, C_FG);
    // Redraw just the modifier tail in a brighter colour so it is noticeable.
    if (kbd_ctrl_active() || kbd_shift_active()) {
        put_text(STATUS_COLS - (int)strlen(right), right, C_ACCENT);
    }

    lcd_raw_push(s_buf, GRID_X, STATUS_Y, GRID_W, STATUS_H);
}
