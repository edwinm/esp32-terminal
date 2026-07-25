// render.c — shadow-diffing rasteriser for the terminal grid.
//
// Per frame, per visible row:
//   snapshot the model row -> diff against the shadow -> find changed column
//   runs -> rasterise each run into a DMA strip -> push -> update the shadow.
//
// Colours everywhere are byte-swapped RGB565; see the byte-order note in lcd.h.
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board_pins.h"
#include "lcd.h"
#include "render.h"
#include "term.h"
#include "glyph.h"

static const char *TAG = "render";

#define MAX_SCALE      2
#define STRIP_PIXELS   (GRID_W * CELL_H * MAX_SCALE)
#define STRIP_BYTES    (STRIP_PIXELS * sizeof(uint16_t))

// Rows physically on the glass. Sentinel-stamped by render_invalidate_*.
static term_cell_t s_shadow[TERM_ROWS][TERM_COLS];
static term_cell_t s_scratch[TERM_COLS];

// Two DMA strips: one is in flight while the other is being filled.
static uint16_t *s_strip[2];
static int       s_strip_idx;

static int  s_scale = 1;
static bool s_kbd_visible;
static int  s_view_top, s_view_left;
static int  s_last_view_top = -1, s_last_view_left = -1, s_last_scale = -1;
static bool s_last_kbd = false;

static bool s_need_letterbox = true;   // repaint the sliver below the last row
// Set by the render_invalidate_* calls. Without it the fast-path gate in
// render_frame() — which only looks at the model generation and the cursor —
// would return before ever consulting the shadow, and any repaint requested by
// something other than the terminal itself (a dismissed overlay, say) would be
// silently dropped.
static bool s_forced_dirty = true;
static int  s_prev_cx = -1, s_prev_cy = -1;
static bool s_prev_cursor_on;
static bool s_last_reverse_video;

static uint32_t s_last_generation;
static int64_t  s_blink_epoch_us;
static uint32_t s_seen_generation;

#define BLINK_PERIOD_US 530000

// --- Colour helpers ---------------------------------------------------------

// Halve each channel of a byte-swapped RGB565. Cached because DIM runs are
// almost always long stretches of one colour.
static uint16_t dim_color(uint16_t swapped)
{
    static uint16_t last_in, last_out;
    if (swapped == last_in && last_out) return last_out;
    uint16_t v = (uint16_t)((swapped >> 8) | (swapped << 8));
    uint16_t r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
    v = (uint16_t)(((r >> 1) << 11) | ((g >> 1) << 5) | (b >> 1));
    last_in = swapped;
    last_out = (uint16_t)((v >> 8) | (v << 8));
    return last_out;
}

// --- Rasteriser -------------------------------------------------------------

// Draw cells [c0..c1] of `row` into `dst`, whose stride is `stride` pixels.
static void raster_run(uint16_t *dst, int stride, const term_cell_t *row,
                       int c0, int c1, int scale, bool reverse_video,
                       int cursor_col)
{
    const int cw = CELL_W * scale;

    for (int col = c0; col <= c1; col++) {
        const term_cell_t *cell = &row[col];
        uint16_t fg = cell->fg;
        uint16_t bg = cell->bg;
        uint16_t attr = cell->attr;

        if (attr & ATTR_DIM)     fg = dim_color(fg);
        if (attr & ATTR_INVISIBLE) fg = bg;
        bool rev = (attr & ATTR_REVERSE) != 0;
        if (reverse_video) rev = !rev;
        if (col == cursor_col)  rev = !rev;
        if (rev) { uint16_t tmp = fg; fg = bg; bg = tmp; }

        const uint8_t *glyph;
        if (attr & ATTR_WIDE_TAIL) {
            glyph = NULL;                        // second half of a wide char
        } else {
            bool bold = (attr & (ATTR_BOLD | ATTR_BLINK)) != 0;
            glyph = font_glyph(cell->ch, bold);
        }

        uint16_t *base = dst + (col - c0) * cw;

        for (int gy = 0; gy < CELL_H; gy++) {
            uint8_t bits = glyph ? glyph[gy] : 0;
            if (attr & ATTR_UNDERLINE && gy == 11) bits = 0xFC;
            if (attr & ATTR_STRIKE    && gy == 6)  bits = 0xFC;

            if (scale == 1) {
                uint16_t *p = base + gy * stride;
                p[0] = (bits & 0x80) ? fg : bg;
                p[1] = (bits & 0x40) ? fg : bg;
                p[2] = (bits & 0x20) ? fg : bg;
                p[3] = (bits & 0x10) ? fg : bg;
                p[4] = (bits & 0x08) ? fg : bg;
                p[5] = (bits & 0x04) ? fg : bg;
            } else {
                uint16_t px[CELL_W];
                px[0] = (bits & 0x80) ? fg : bg;
                px[1] = (bits & 0x40) ? fg : bg;
                px[2] = (bits & 0x20) ? fg : bg;
                px[3] = (bits & 0x10) ? fg : bg;
                px[4] = (bits & 0x08) ? fg : bg;
                px[5] = (bits & 0x04) ? fg : bg;
                for (int sy = 0; sy < scale; sy++) {
                    uint16_t *p = base + (gy * scale + sy) * stride;
                    for (int gx = 0; gx < CELL_W; gx++) {
                        for (int sx = 0; sx < scale; sx++) *p++ = px[gx];
                    }
                }
            }
        }
    }
}

// --- Viewport ---------------------------------------------------------------

static int visible_rows(void)
{
    int pixels = s_kbd_visible ? KBD_Y : GRID_H;
    int rows = pixels / (CELL_H * s_scale);
    return rows > TERM_ROWS ? TERM_ROWS : rows;
}

static int visible_cols(void)
{
    int cols = GRID_W / (CELL_W * s_scale);
    return cols > TERM_COLS ? TERM_COLS : cols;
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// Keep the cursor on screen. With the keyboard up this is what stops you typing
// blind: the prompt is always parked directly above the keys.
static void update_viewport(int cx, int cy)
{
    int rows = visible_rows();
    int cols = visible_cols();

    if (rows >= TERM_ROWS) {
        s_view_top = 0;
    } else {
        s_view_top = clampi(cy - (rows - 1), 0, TERM_ROWS - rows);
    }
    if (cols >= TERM_COLS) {
        s_view_left = 0;
    } else if (cx < s_view_left) {
        s_view_left = cx;
    } else if (cx >= s_view_left + cols) {
        s_view_left = cx - cols + 1;
    }
    s_view_left = clampi(s_view_left, 0, TERM_COLS - cols);
}

// --- Public -----------------------------------------------------------------

esp_err_t render_init(void)
{
    for (int i = 0; i < 2; i++) {
        s_strip[i] = heap_caps_malloc(STRIP_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!s_strip[i]) {
            ESP_LOGE(TAG, "no DMA-capable internal RAM for strip %d (%d bytes)",
                     i, (int)STRIP_BYTES);
            return ESP_ERR_NO_MEM;
        }
    }
    render_invalidate_all();
    s_blink_epoch_us = esp_timer_get_time();
    ESP_LOGI(TAG, "%dx%d grid, %d-byte strips, free internal %u",
             TERM_COLS, TERM_ROWS, (int)STRIP_BYTES,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return ESP_OK;
}

void render_invalidate_rows(int r0, int r1)
{
    r0 = clampi(r0, 0, TERM_ROWS - 1);
    r1 = clampi(r1, 0, TERM_ROWS - 1);
    for (int r = r0; r <= r1; r++) {
        for (int c = 0; c < TERM_COLS; c++) {
            s_shadow[r][c].ch = 0xFFFF;
            s_shadow[r][c].attr = 0xFFFF;
        }
    }
    s_forced_dirty = true;
}

void render_invalidate_all(void)
{
    render_invalidate_rows(0, TERM_ROWS - 1);
    s_need_letterbox = true;
}

void render_invalidate_pixel_band(int y0, int y1)
{
    const int ch = CELL_H * s_scale;
    if (y1 < GRID_Y) return;
    if (y0 < GRID_Y) y0 = GRID_Y;

    int first = (y0 - GRID_Y) / ch;
    int last  = (y1 - GRID_Y) / ch;

    for (int i = first; i <= last; i++) {
        int r = s_view_top + i;          // screen row -> model row
        if (r < 0) continue;
        if (r >= TERM_ROWS) break;
        render_invalidate_rows(r, r);
    }
}

void render_set_scale(int scale)
{
    scale = clampi(scale, 1, MAX_SCALE);
    if (scale == s_scale) return;
    s_scale = scale;
    render_invalidate_all();
}

int render_get_scale(void) { return s_scale; }

void render_set_keyboard_visible(bool visible)
{
    if (visible == s_kbd_visible) return;
    s_kbd_visible = visible;
    render_invalidate_all();
}

void render_clear_grid(void)
{
    lcd_raw_fill_rect(GRID_X, GRID_Y, GRID_W, GRID_H, lcd_rgb(0, 0, 0));
    render_invalidate_all();
}

bool render_frame(void)
{
    int cx, cy;
    bool cursor_visible, cursor_blink, reverse_video;
    term_cursor_state(&cx, &cy, &cursor_visible, &cursor_blink, &reverse_video);

    // A solid cursor while output is streaming reads as "alive"; a blinking one
    // is just noise. Restart the blink phase whenever the model changed.
    uint32_t gen = term_generation();
    int64_t now = esp_timer_get_time();
    if (gen != s_seen_generation) {
        s_seen_generation = gen;
        s_blink_epoch_us = now;
    }
    bool blink_on = true;
    if (cursor_blink && (now - s_blink_epoch_us) > 200000) {
        blink_on = (((now - s_blink_epoch_us) / BLINK_PERIOD_US) & 1) == 0;
    }
    bool cursor_on = cursor_visible && blink_on;

    update_viewport(cx, cy);

    if (s_view_top != s_last_view_top || s_view_left != s_last_view_left ||
        s_scale != s_last_scale || s_kbd_visible != s_last_kbd ||
        reverse_video != s_last_reverse_video) {
        render_invalidate_all();
        s_need_letterbox = true;
        s_last_view_top = s_view_top;
        s_last_view_left = s_view_left;
        s_last_scale = s_scale;
        s_last_kbd = s_kbd_visible;
        s_last_reverse_video = reverse_video;
    } else if (!s_forced_dirty && gen == s_last_generation &&
               cx == s_prev_cx && cy == s_prev_cy && cursor_on == s_prev_cursor_on) {
        return false;                            // nothing at all to do
    }
    s_last_generation = gen;

    // The cursor is a synthetic reverse-video overlay, so the shadow diff
    // cannot see it move. Stamp the cells it left and the cells it entered.
    if (cx != s_prev_cx || cy != s_prev_cy || cursor_on != s_prev_cursor_on) {
        if (s_prev_cy >= 0 && s_prev_cy < TERM_ROWS && s_prev_cx < TERM_COLS) {
            s_shadow[s_prev_cy][s_prev_cx].ch = 0xFFFF;
        }
        if (cy < TERM_ROWS && cx < TERM_COLS) {
            s_shadow[cy][cx].ch = 0xFFFF;
        }
        s_prev_cx = cx;
        s_prev_cy = cy;
        s_prev_cursor_on = cursor_on;
    }

    const int rows = visible_rows();
    const int cols = visible_cols();
    const int cw = CELL_W * s_scale;
    const int chh = CELL_H * s_scale;

    bool painted = false;
    s_forced_dirty = false;      // the shadow diff below now owns the repaint
    lcd_raw_start();

    for (int i = 0; i < rows; i++) {
        int r = s_view_top + i;
        if (r >= TERM_ROWS) break;

        term_snapshot_row(r, s_scratch);

        // Find the changed column runs within the visible window.
        int first = -1, last = -1, runs = 0, changed = 0;
        bool in_run = false;
        for (int j = 0; j < cols; j++) {
            int c = s_view_left + j;
            const uint32_t *a = (const uint32_t *)&s_scratch[c];
            const uint32_t *b = (const uint32_t *)&s_shadow[r][c];
            bool diff = (a[0] != b[0]) || (a[1] != b[1]);
            if (diff) {
                changed++;
                if (first < 0) first = c;
                last = c;
                if (!in_run) { runs++; in_run = true; }
            } else {
                in_run = false;
            }
        }
        if (changed == 0) continue;

        // Every setAddrWindow costs ~10-20us, so more than a couple of runs is
        // more expensive than just repainting the row.
        int c0 = first, c1 = last;
        if (runs > 2 || changed * 2 > cols) {
            c0 = s_view_left;
            c1 = s_view_left + cols - 1;
        }

        int span = c1 - c0 + 1;
        int stride = span * cw;
        uint16_t *strip = s_strip[s_strip_idx];
        s_strip_idx ^= 1;

        int cursor_col = (cursor_on && r == cy) ? cx : -1;
        raster_run(strip, stride, s_scratch, c0, c1, s_scale, reverse_video, cursor_col);

        lcd_raw_push_dma(strip,
                         GRID_X + (c0 - s_view_left) * cw,
                         GRID_Y + i * chh,
                         stride, chh);

        memcpy(&s_shadow[r][c0], &s_scratch[c0], span * sizeof(term_cell_t));
        painted = true;
    }

    // The sliver below the last visible row, when the available height is not
    // an exact multiple of the scaled cell height. Only on layout changes.
    if (s_need_letterbox) {
        int painted = rows * chh;
        int limit = s_kbd_visible ? KBD_Y : GRID_H;
        if (painted < limit) {
            lcd_raw_fill_rect(GRID_X, GRID_Y + painted, GRID_W, limit - painted,
                              lcd_rgb(0, 0, 0));
        }
        s_need_letterbox = false;
        painted = true;
    }

    lcd_raw_end();
    return painted;
}
