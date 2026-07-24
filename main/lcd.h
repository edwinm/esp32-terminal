// lcd.h — display via LovyanGFX (ESP32-S3 I2S parallel bus), C API.
//
// Why LovyanGFX and not esp_lcd: on this board the esp_lcd i80 (LCD_CAM)
// peripheral does not drive the ILI9488 (verified: pins/init/geometry all
// correct, screen stays blank). The factory firmware and the reference project
// both use LovyanGFX's Bus_Parallel16, which uses the I2S peripheral instead —
// that is the combination proven to work on this hardware.
//
// Touch and the I2C scan also go through LovyanGFX (its legacy I2C driver);
// mixing in the new i2c_master driver aborts at boot, so we keep it all here.
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include "lvgl.h"

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

// LVGL flush callback (push a rendered area to the panel).
void lcd_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);

// Touch (LovyanGFX auto-detects FT6236 @ 0x38 / NS2009 @ 0x48).
#include <stdbool.h>
bool        lcd_get_touch(uint16_t *x, uint16_t *y);   // screen coords, rotated
const char *lcd_touch_name(void);                      // "FT6236"/"NS2009"/"none"
uint8_t     lcd_touch_addr(void);

// Probe the shared I2C bus (0x08..0x77); fills `out`, returns count.
int lcd_i2c_scan(uint8_t *out, int max);

// Raw helpers for the bring-up diagnostic and the perf tab (bypass LVGL).
void lcd_raw_fill(uint16_t color);                          // solid full screen
void lcd_raw_push(const uint16_t *buf, int x, int y, int w, int h);
void lcd_raw_start(void);
void lcd_raw_end(void);

#ifdef __cplusplus
}
#endif
