// lcd.h — display via LovyanGFX (ESP32-S3 LCD_CAM parallel bus), C API.
//
// Why LovyanGFX and not esp_lcd: on this board the esp_lcd i80 driver does not
// drive the ILI9488 (verified: pins/init/geometry all correct, screen stays
// blank). LovyanGFX's Bus_Parallel16 talks to the same LCD_CAM + GDMA silicon
// but configures it differently (LCD_CMD_2_CYCLE_EN | LCD_2BYTE_EN |
// LCD_ALWAYS_OUT_EN, and it drives CS itself) — that is the combination proven
// to work on this hardware. LovyanGFX therefore owns LCD_CAM and one GDMA TX
// channel; nothing else may touch them.
//
// Touch also goes through LovyanGFX (its legacy I2C driver); mixing in the new
// i2c_master driver aborts at boot, so keep all I2C access here.
//
// THREADING INVARIANT: only the `disp` task may call into this module (and
// therefore into LovyanGFX). LGFX_Device is not thread-safe — its transactions
// are stateful and getTouch() reads the transaction counter. Rendering, touch
// polling and keyboard drawing all live in `disp`, which is what makes an LGFX
// mutex unnecessary. Do not call these from the parser or USB tasks.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Landscape geometry (LovyanGFX rotation 1).
#define LCD_W 480
#define LCD_H 320

esp_err_t lcd_init(void);          // init LovyanGFX + backlight (starts at 0%)
int  lcd_width(void);
int  lcd_height(void);

void lcd_backlight_set(int percent);
int  lcd_backlight_get(void);

// Touch (LovyanGFX auto-detects FT6236 @ 0x38 / NS2009 @ 0x48).
bool        lcd_get_touch(uint16_t *x, uint16_t *y);   // screen coords, rotated
bool        lcd_get_touch_raw(uint16_t *x, uint16_t *y);
const char *lcd_touch_name(void);                      // "FT6236"/"NS2009"/"none"
uint8_t     lcd_touch_addr(void);
// Axis fix-up for panels whose touch layer is mirrored/rotated relative to the
// display. Useful values with rotation 1: 0 (default), 2, 4, 6. See the
// crosshair diagnostic in main.c.
void        lcd_touch_set_offset_rotation(uint8_t offset);
uint8_t     lcd_touch_get_offset_rotation(void);

// --- Pixel pushing ---------------------------------------------------------
//
// IMPORTANT: pixel buffers are BYTE-SWAPPED RGB565 (big-endian in memory).
// These call writePixels(..., swap=false), which makes LovyanGFX read the
// source as lgfx::swap565_t and hand the bytes to the bus verbatim — the
// fastest path, but it means every colour you store must be pre-swapped.
// Use lcd_rgb565() / lcd_rgb() below rather than writing raw 0xF800 literals.
void lcd_raw_fill(uint16_t color);                          // solid full screen

// Optional: bracket a batch of pushes to keep one bus transaction open across
// all of them instead of one each. Purely an optimisation — the pushes below
// are correct with or without it — but it is what lets consecutive DMA pushes
// overlap, since only the outermost endWrite() waits for the transfer.
void lcd_raw_start(void);
void lcd_raw_end(void);

// Non-DMA push: copies through LovyanGFX's 256-byte bus cache. Fine for small
// rectangles; use the DMA variant for anything strip-sized.
void lcd_raw_push(const uint16_t *buf, int x, int y, int w, int h);
// DMA push: transfers straight out of `buf`, which MUST live in DMA-capable
// internal RAM and MUST stay untouched until the next push or lcd_raw_end().
void lcd_raw_push_dma(const uint16_t *buf, int x, int y, int w, int h);
// Solid rectangle (no caller buffer needed).
void lcd_raw_fill_rect(int x, int y, int w, int h, uint16_t color);

// Byte-swapped RGB565 constructors.
static inline uint16_t lcd_rgb565(uint16_t native565)
{
    return (uint16_t)((native565 >> 8) | (native565 << 8));
}
static inline uint16_t lcd_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t v = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return lcd_rgb565(v);
}

#ifdef __cplusplus
}
#endif
