# Makerfabs ESP32-S3 Parallel TFT 3.5" — Hardware Demo

An ESP-IDF + LVGL 9 demo firmware for the **Makerfabs ESP32-S3 Parallel TFT with
Touch 3.5" (ILI9488)** board, exercising all of the onboard hardware from a
touch-driven tabbed UI. Built with PlatformIO.

## Hardware

- **MCU:** ESP32-S3-WROOM-1-N16R2 (16 MB quad flash, 2 MB quad PSRAM), 240 MHz
- **Display:** ILI9488 320×480, 16-bit Intel-8080 parallel bus (run landscape, 480×320)
- **Touch:** capacitive FT6236 @ 0x38 or resistive NS2009 @ 0x48 (auto-detected)
- **Storage:** microSD over SPI
- **Expansion:** two Mabee (Grove-compatible) connectors — one I²C (SDA=38/SCL=39,
  shared with touch), one "IO" on GPIO43/44 (the UART0 pins)
- Dual USB-C: CP2104 (UART/flash) + native USB-OTG

## Features (tabs)

- **Info** — chip/PSRAM/heap/flash, die temperature, uptime
- **TFT** — raw parallel-bus throughput benchmark
- **SD** — mount, card info, directory listing, read/write speed test
- **I2C** — scan the shared bus, annotate known devices
- **Periph** — backlight brightness (PWM), BOOT-button counter, battery status
- **WiFi** — station-mode AP scan
- **USB** — native-USB HID keyboard (types into a host)
- **Con** — serial terminal on the 2nd Mabee connector (GPIO43/44): shows incoming
  data and sends lines via an on-screen keyboard (shares UART0 with the debug log)

## Build & flash

```bash
pio run                                   # build
pio run -t upload                         # flash (CP2104 UART port)
pio device monitor                        # serial console @ 115200
```

## Notes on the display driver

This board is driven with **LovyanGFX**, not ESP-IDF's `esp_lcd`. The `esp_lcd`
i80 (LCD_CAM) peripheral would not drive this panel; LovyanGFX's I2S-based
parallel bus does (as does the factory firmware). LVGL renders through a manual
`lv_display` + LovyanGFX flush; touch and the I²C scan also go through LovyanGFX
(mixing the newer `i2c_master` driver aborts at boot). LovyanGFX is vendored
under `components/` (the bundled CJK fonts are stripped to stubs, as they are
unused here).

**Board revision:** the display control pins on this unit are **WR=35, RS=36,
CS=37** (see `main/lcd.cpp`), which differs from the v2.0 schematic/factory
firmware set (18/17/46). If your panel stays blank, that pin set is the first
thing to check.
