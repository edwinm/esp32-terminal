// board_pins.h — Makerfabs ESP32-S3 Parallel TFT with Touch 3.5" ILI9488 (v2.0)
// Single source of truth for every GPIO and board-level constant.
// Pins verified against the v2.0 schematic PDF and on this physical unit.
#pragma once

#include "driver/gpio.h"

// ---------------------------------------------------------------------------
// Display: ILI9488 320x480, 16-bit Intel-8080 parallel bus
// ---------------------------------------------------------------------------
#define LCD_H_RES_NATIVE   320   // portrait native width
#define LCD_V_RES_NATIVE   480   // portrait native height

// Control pins. NOTE: this physical board uses WR=35, RS=36, CS=37 (verified on
// hardware — the display is blank with the v2.0 factory-firmware set 18/17/46).
// The bus config that actually takes effect lives in lcd.cpp; these are for
// reference, so changing them here alone does nothing.
#define LCD_PIN_WR         35    // write strobe
#define LCD_PIN_DC         36    // data/command (RS)
#define LCD_PIN_CS         37    // chip select (LovyanGFX-managed)
#define LCD_PIN_RD         48    // read strobe
#define LCD_PIN_BL         45    // backlight (transistor, active high, PWM via LEDC)
#define LCD_PIN_RST        -1    // tied to board reset

// 16-bit data bus D0..D15
#define LCD_DATA_PINS { 47, 21, 14, 13, 12, 11, 10, 9, 3, 8, 16, 15, 7, 6, 5, 4 }

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
// NS2009 12-bit ADC calibration (raw -> screen), from the LGFX reference config
#define NS2009_RAW_X_MIN   368
#define NS2009_RAW_X_MAX   3800
#define NS2009_RAW_Y_MIN   212
#define NS2009_RAW_Y_MAX   3800

// ---------------------------------------------------------------------------
// Buttons / misc
// ---------------------------------------------------------------------------
#define BTN_BOOT_PIN       0     // BOOT button; held at reset -> touch crosshair test

// Native USB-OTG (TinyUSB CDC-ACM — the terminal link) is on GPIO19/20, hardwired
// to the PHY. The IDF console and flashing stay on UART0 via the CP2104 bridge.

// ---------------------------------------------------------------------------
// Terminal geometry (landscape 480x320, LovyanGFX rotation 1)
// ---------------------------------------------------------------------------
#define TERM_COLS          80
#define TERM_ROWS          24
#define CELL_W             6
#define CELL_H             13

#define GRID_X             0
#define GRID_Y             0
#define GRID_W             (TERM_COLS * CELL_W)   // 480
#define GRID_H             (TERM_ROWS * CELL_H)   // 312

#define STATUS_Y           GRID_H                 // 312
#define STATUS_H           (320 - GRID_H)         // 8

// On-screen keyboard overlay: 4 rows of 36px, sitting above the status strip.
#define KBD_ROWS           4
#define KBD_KEY_H          36
#define KBD_H              (KBD_ROWS * KBD_KEY_H) // 144
#define KBD_Y              (GRID_H - KBD_H)       // 168
#define KBD_UNITS          12                     // layout columns
#define KBD_UNIT_W         (GRID_W / KBD_UNITS)   // 40

// Terminal rows still visible while the keyboard is up.
#define KBD_VISIBLE_ROWS   (KBD_Y / CELL_H)       // 12
