// perf_test.c — raw panel throughput benchmark (no LVGL in the path).
#include "perf_test.h"
#include "lcd.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "perf";

#define BLOCK_LINES 40
#define BLOCK_PX    (LCD_W * BLOCK_LINES)

// Push one full screen from a reusable block buffer; returns bytes pushed.
static size_t push_full_screen(uint16_t *block)
{
    size_t bytes = 0;
    lcd_raw_start();
    for (int y = 0; y < LCD_H; y += BLOCK_LINES) {
        int h = (y + BLOCK_LINES > LCD_H) ? (LCD_H - y) : BLOCK_LINES;
        lcd_raw_push(block, 0, y, LCD_W, h);
        bytes += (size_t)LCD_W * h * sizeof(uint16_t);
    }
    lcd_raw_end();
    return bytes;
}

void perf_run(int frames, perf_result_t *out)
{
    memset(out, 0, sizeof(*out));
    uint16_t *block = heap_caps_malloc(BLOCK_PX * sizeof(uint16_t),
                                       MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!block) { ESP_LOGE(TAG, "no DMA buffer"); return; }

    static const uint16_t palette[] = {0xF800, 0x07E0, 0x001F, 0xFFFF};

    // --- Solid fills ---
    int64_t t0 = esp_timer_get_time();
    size_t fill_bytes = 0;
    for (int f = 0; f < frames; f++) {
        uint16_t c = palette[f % 4];
        for (int i = 0; i < BLOCK_PX; i++) block[i] = c;
        fill_bytes += push_full_screen(block);
    }
    int64_t t1 = esp_timer_get_time();

    // --- Gradient blits (per-line color ramp) ---
    size_t grad_bytes = 0;
    for (int f = 0; f < frames; f++) {
        for (int ln = 0; ln < BLOCK_LINES; ln++) {
            uint16_t c = (uint16_t)((ln * 0x21) ^ (f * 0x9));
            for (int x = 0; x < LCD_W; x++) block[ln * LCD_W + x] = c + x;
        }
        grad_bytes += push_full_screen(block);
    }
    int64_t t2 = esp_timer_get_time();

    free(block);

    double fill_s = (t1 - t0) / 1e6;
    double grad_s = (t2 - t1) / 1e6;
    out->fill_mbps = fill_s > 0 ? (fill_bytes / 1e6) / fill_s : 0;
    out->grad_mbps = grad_s > 0 ? (grad_bytes / 1e6) / grad_s : 0;
    out->fill_fps  = fill_s > 0 ? frames / fill_s : 0;
    out->pixels_per_s = fill_s > 0 ?
        (uint32_t)(((double)frames * LCD_W * LCD_H) / fill_s) : 0;
    out->frames = frames;
    ESP_LOGI(TAG, "fill %.1f MB/s (%.1f fps), gradient %.1f MB/s",
             out->fill_mbps, out->fill_fps, out->grad_mbps);
}
