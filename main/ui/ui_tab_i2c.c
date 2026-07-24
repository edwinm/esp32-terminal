// ui_tab_i2c.c — scan the shared I2C bus (via LovyanGFX) and list devices.
#include "ui.h"
#include "lcd.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static lv_obj_t *s_list;
static lv_obj_t *s_scan_btn;

static const char *known_name(uint8_t addr)
{
    switch (addr) {
        case 0x38: return "FT6236 cap touch";
        case 0x48: return "NS2009 res touch";
        case 0x44: return "SHT31 temp/RH";
        case 0x58: return "SGP30 VOC";
        case 0x76: return "BME280";
        case 0x77: return "BMP280/BME280";
        case 0x3C: return "SSD1306 OLED";
        case 0x68: return "MPU6050/RTC";
        case 0x40: return "INA219/HTU21";
        default:   return "unknown";
    }
}

static void scan_task(void *arg)
{
    (void)arg;
    uint8_t found[32];
    int n = 0;

    // Hold the LVGL lock during the scan so it serializes with the touch reads
    // (both use the single LovyanGFX I2C bus).
    if (lvgl_port_lock(0)) {
        n = lcd_i2c_scan(found, 32);
        lv_obj_clean(s_list);
        if (n == 0) {
            lv_list_add_text(s_list, "no devices found");
        } else {
            for (int i = 0; i < n; i++) {
                char buf[64];
                snprintf(buf, sizeof(buf), "0x%02X  %s", found[i], known_name(found[i]));
                lv_list_add_button(s_list, LV_SYMBOL_GPS, buf);
            }
        }
        lv_obj_clear_state(s_scan_btn, LV_STATE_DISABLED);
        lvgl_port_unlock();
    }
    vTaskDelete(NULL);
}

static void scan_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_state(s_scan_btn, LV_STATE_DISABLED);
    lv_obj_clean(s_list);
    lv_list_add_text(s_list, "scanning 0x08..0x77 ...");
    xTaskCreate(scan_task, "i2cscan", 3072, NULL, 3, NULL);
}

void ui_tab_i2c_create(lv_obj_t *parent)
{
    lv_obj_set_style_pad_all(parent, 6, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *hdr = lv_label_create(parent);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_14, 0);
    lv_label_set_text(hdr, "Shared bus: SDA=38  SCL=39  (touch + Mabee)");

    s_scan_btn = lv_button_create(parent);
    lv_obj_t *l = lv_label_create(s_scan_btn);
    lv_label_set_text(l, LV_SYMBOL_REFRESH " Rescan");
    lv_obj_add_event_cb(s_scan_btn, scan_cb, LV_EVENT_CLICKED, NULL);

    s_list = lv_list_create(parent);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);

    scan_cb(NULL);   // scan once on open
}
