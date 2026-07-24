// board_pins.h — Makerfabs ESP32-S3 Parallel TFT with Touch 3.5" ILI9488 (v2.0)
// Single source of truth for every GPIO and board-level constant.
// Pins verified against the v2.0 schematic PDF and factory firmware SD16_3.5.ino.
#pragma once

#include "driver/gpio.h"

// ---------------------------------------------------------------------------
// Display: ILI9488 320x480, 16-bit Intel-8080 parallel bus
// ---------------------------------------------------------------------------
#define LCD_H_RES_NATIVE   320   // portrait native width
#define LCD_V_RES_NATIVE   480   // portrait native height
// The demo runs landscape (swap_xy): 480 x 320
#define LCD_H_RES          480
#define LCD_V_RES          320

// Control pins. NOTE: this physical board uses WR=35, RS=36, CS=37 (verified on
// hardware — the display is blank with the v2.0 factory-firmware set 18/17/46).
// The actual bus config lives in lcd.cpp; these are kept for reference.
#define LCD_PIN_WR         35    // write strobe
#define LCD_PIN_DC         36    // data/command (RS)
#define LCD_PIN_CS         37    // chip select (LovyanGFX-managed)
#define LCD_PIN_RD         48    // read strobe
#define LCD_PIN_BL         45    // backlight (transistor, active high, PWM via LEDC)
#define LCD_PIN_RST        -1    // tied to board reset

// 16-bit data bus D0..D15
#define LCD_DATA_PINS { 47, 21, 14, 13, 12, 11, 10, 9, 3, 8, 16, 15, 7, 6, 5, 4 }

// Pixel clock. Factory firmware runs 20 MHz (ILI9488 datasheet max ~20 MHz on
// parallel). Bump to experiment; drop back if you see tearing/artifacts.
#define LCD_PCLK_HZ        (20 * 1000 * 1000)

// LVGL partial draw buffer height (lines). Two buffers of LCD_H_RES x this.
#define LCD_DRAW_BUF_LINES 60

// ---------------------------------------------------------------------------
// I2C — shared bus for touch controller and Mabee connector
// ---------------------------------------------------------------------------
#define I2C_PORT_NUM       0
#define I2C_PIN_SDA        38
#define I2C_PIN_SCL        39
#define I2C_PIN_TOUCH_INT  40
#define I2C_FREQ_HZ        400000

// Touch controller variants (auto-detected at runtime)
#define TOUCH_ADDR_FT6236  0x38  // capacitive (FT5x06-compatible)
#define TOUCH_ADDR_NS2009  0x48  // resistive (TI TSC2007-like)
// NS2009 12-bit ADC calibration (raw -> screen), from LGFX reference config
#define NS2009_RAW_X_MIN   368
#define NS2009_RAW_X_MAX   3800
#define NS2009_RAW_Y_MIN   212
#define NS2009_RAW_Y_MAX   3800
#define NS2009_Z_THRESHOLD 30

// ---------------------------------------------------------------------------
// microSD — SPI (SPI2_HOST / HSPI)
// ---------------------------------------------------------------------------
#define SD_SPI_HOST        2     // SPI2_HOST
#define SD_PIN_CS          1
#define SD_PIN_MOSI        2
#define SD_PIN_MISO        41
#define SD_PIN_SCK         42
#define SD_MAX_FREQ_KHZ    20000
#define SD_MOUNT_POINT     "/sdcard"

// ---------------------------------------------------------------------------
// Buttons / misc
// ---------------------------------------------------------------------------
#define BTN_BOOT_PIN       0     // BOOT button, usable as a user button at runtime

// Native USB-OTG (TinyUSB HID) is on GPIO19/20, hardwired to the PHY (no config).
// Console + flashing is on UART0 via the CP2104 bridge.
