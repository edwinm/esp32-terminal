// lcd.cpp — LovyanGFX ILI9488 (16-bit I2S parallel) display + touch + backlight.
//
// Everything display/touch/I2C goes through LovyanGFX (legacy I2C driver). We do
// NOT use the esp_lcd_touch / i2c_master path: mixing the legacy and new I2C
// drivers in one binary aborts at boot (check_i2c_driver_conflict).
//
// Pin map verified on this board: WR=35, RD=48, RS/DC=36, CS=37, BL=45,
// data D0..D15 (see below), touch I2C SDA=38 SCL=39. (The v2.0 factory firmware
// uses WR=18/RS=17/CS=46 instead, which leaves THIS board's panel blank.)
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32/common.hpp>   // lgfx::i2c

#include "lcd.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "lcd";

static const char *s_touch_name = "none";
static uint8_t      s_touch_addr = 0;

// --- LovyanGFX device: display + touch + PWM backlight ----------------------
class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_Parallel16 _bus;
    lgfx::Panel_ILI9488  _panel;
    lgfx::Light_PWM      _light;
    lgfx::ITouch        *_touch = nullptr;

    // Detect FT6236 (0x38) or NS2009 (0x48) at init, like the reference config.
    bool init_impl(bool use_reset, bool use_clear) override {
        if (_touch == nullptr) {
            lgfx::i2c::init(I2C_PORT_NUM, I2C_PIN_SDA, I2C_PIN_SCL);
            lgfx::ITouch::config_t cfg;
            if (lgfx::i2c::beginTransaction(I2C_PORT_NUM, TOUCH_ADDR_FT6236, I2C_FREQ_HZ, false).has_value()
             && lgfx::i2c::endTransaction(I2C_PORT_NUM).has_value()) {
                _touch = new lgfx::Touch_FT5x06();
                cfg = _touch->config();
                cfg.i2c_addr = TOUCH_ADDR_FT6236;
                cfg.x_max = LCD_H_RES_NATIVE; cfg.y_max = LCD_V_RES_NATIVE;
                s_touch_name = "FT6236"; s_touch_addr = TOUCH_ADDR_FT6236;
            } else if (lgfx::i2c::beginTransaction(I2C_PORT_NUM, TOUCH_ADDR_NS2009, I2C_FREQ_HZ, false).has_value()
                    && lgfx::i2c::endTransaction(I2C_PORT_NUM).has_value()) {
                _touch = new lgfx::Touch_NS2009();
                cfg = _touch->config();
                cfg.i2c_addr = TOUCH_ADDR_NS2009;
                cfg.x_min = NS2009_RAW_X_MIN; cfg.y_min = NS2009_RAW_Y_MIN;
                cfg.x_max = NS2009_RAW_X_MAX; cfg.y_max = NS2009_RAW_Y_MAX;
                s_touch_name = "NS2009"; s_touch_addr = TOUCH_ADDR_NS2009;
            }
            if (_touch) {
                cfg.i2c_port = I2C_PORT_NUM;
                cfg.pin_sda = I2C_PIN_SDA; cfg.pin_scl = I2C_PIN_SCL;
                cfg.pin_int = I2C_PIN_TOUCH_INT;
                cfg.freq = I2C_FREQ_HZ; cfg.bus_shared = false;
                _touch->config(cfg);
                _panel.touch(_touch);
            }
        }
        return lgfx::LGFX_Device::init_impl(use_reset, use_clear);
    }

public:
    LGFX(void) {
        {   // 16-bit parallel bus over the I2S peripheral (port 0).
            // This board revision uses WR=35, RS=36, CS=37 (per the reference
            // LGFX-IDF project), NOT the factory-firmware 18/17/46.
            auto cfg = _bus.config();
            cfg.port = 0;
            cfg.freq_write = 40000000;
            cfg.pin_wr = LCD_PIN_WR;   // 35
            cfg.pin_rd = LCD_PIN_RD;   // 48
            cfg.pin_rs = LCD_PIN_DC;   // 36
            cfg.pin_d0 = 47; cfg.pin_d1 = 21; cfg.pin_d2 = 14; cfg.pin_d3 = 13;
            cfg.pin_d4 = 12; cfg.pin_d5 = 11; cfg.pin_d6 = 10; cfg.pin_d7 = 9;
            cfg.pin_d8 = 3;  cfg.pin_d9 = 8;  cfg.pin_d10 = 16; cfg.pin_d11 = 15;
            cfg.pin_d12 = 7; cfg.pin_d13 = 6; cfg.pin_d14 = 5;  cfg.pin_d15 = 4;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs   = LCD_PIN_CS;   // 37, managed by LovyanGFX (this revision)
            cfg.pin_rst  = -1;
            cfg.pin_busy = -1;
            cfg.memory_width  = LCD_H_RES_NATIVE;
            cfg.memory_height = LCD_V_RES_NATIVE;
            cfg.panel_width   = LCD_H_RES_NATIVE;
            cfg.panel_height  = LCD_V_RES_NATIVE;
            cfg.readable   = true;
            cfg.invert     = false;
            cfg.rgb_order  = false;
            cfg.dlen_16bit = true;
            cfg.bus_shared = true;
            _panel.config(cfg);
        }
        {   // PWM backlight on GPIO45.
            auto cfg = _light.config();
            cfg.pin_bl = LCD_PIN_BL;
            cfg.invert = false;
            cfg.freq = 44100;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        setPanel(&_panel);
    }
};

static LGFX s_lcd;
static int  s_bl_percent = 0;

extern "C" {

esp_err_t lcd_init(void)
{
    if (!s_lcd.init()) {
        ESP_LOGE(TAG, "LovyanGFX init failed");
        return ESP_FAIL;
    }
    s_lcd.setRotation(1);                 // landscape 480x320
    s_lcd.setSwapBytes(true);
    s_lcd.fillScreen(0x0000);
    s_lcd.setBrightness(0);
    s_bl_percent = 0;
    ESP_LOGI(TAG, "LovyanGFX ILI9488 %dx%d, touch: %s @ 0x%02X",
             s_lcd.width(), s_lcd.height(), s_touch_name, s_touch_addr);
    return ESP_OK;
}

int lcd_width(void)  { return s_lcd.width(); }
int lcd_height(void) { return s_lcd.height(); }

void lcd_backlight_set(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_bl_percent = percent;
    s_lcd.setBrightness((uint8_t)(percent * 255 / 100));
}

int lcd_backlight_get(void) { return s_bl_percent; }

void lcd_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    s_lcd.startWrite();
    s_lcd.setAddrWindow(area->x1, area->y1, w, h);
    s_lcd.writePixels((uint16_t *)px_map, w * h, true);
    s_lcd.endWrite();
    lv_display_flush_ready(disp);
}

bool lcd_get_touch(uint16_t *x, uint16_t *y) { return s_lcd.getTouch(x, y); }
const char *lcd_touch_name(void) { return s_touch_name; }
uint8_t     lcd_touch_addr(void) { return s_touch_addr; }

int lcd_i2c_scan(uint8_t *out, int max)
{
    int n = 0;
    for (uint8_t a = 0x08; a <= 0x77 && n < max; a++) {
        if (lgfx::i2c::beginTransaction(I2C_PORT_NUM, a, I2C_FREQ_HZ, false).has_value()
         && lgfx::i2c::endTransaction(I2C_PORT_NUM).has_value()) {
            out[n++] = a;
        } else {
            lgfx::i2c::endTransaction(I2C_PORT_NUM);
        }
    }
    return n;
}

void lcd_raw_fill(uint16_t color)   { s_lcd.fillScreen(color); }
void lcd_raw_start(void)            { s_lcd.startWrite(); }
void lcd_raw_end(void)             { s_lcd.endWrite(); }

void lcd_raw_push(const uint16_t *buf, int x, int y, int w, int h)
{
    s_lcd.setAddrWindow(x, y, w, h);
    s_lcd.writePixels((uint16_t *)buf, w * h, false);
}

} // extern "C"
