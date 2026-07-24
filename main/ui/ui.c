// ui.c — LVGL bring-up (custom LovyanGFX display) and the tabview shell.
//
// esp_lvgl_port runs the LVGL task, tick and lock, but its display helper only
// binds esp_lcd panels. Since the display is driven by LovyanGFX, we create the
// lv_display ourselves with a LovyanGFX flush; esp_lvgl_port's task services it
// (it runs lv_timer_handler whenever a default display exists). Touch is a
// manual lv_indev reading LovyanGFX's getTouch().
#include "ui.h"
#include "lcd.h"
#include "esp_lvgl_port.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "ui";

#define DRAW_BUF_LINES 40
#define DRAW_BUF_PX    (LCD_W * DRAW_BUF_LINES)

// LVGL input device read: pull the latest point from LovyanGFX.
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint16_t x, y;
    if (lcd_get_touch(&x, &y)) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void build_ui(void)
{
    lv_obj_t *scr = lv_screen_active();

    // Dark theme so the UI reads clearly.
    lv_theme_t *th = lv_theme_default_init(lv_display_get_default(),
        lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_CYAN),
        true /* dark */, LV_FONT_DEFAULT);
    lv_display_set_theme(lv_display_get_default(), th);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *tv = lv_tabview_create(scr);
    lv_tabview_set_tab_bar_size(tv, 34);
    lv_obj_set_style_text_font(lv_tabview_get_tab_bar(tv), &lv_font_montserrat_14, 0);

    ui_tab_info_create(lv_tabview_add_tab(tv, "Info"));
    ui_tab_speed_create(lv_tabview_add_tab(tv, "TFT"));
    ui_tab_sd_create(lv_tabview_add_tab(tv, "SD"));
    ui_tab_i2c_create(lv_tabview_add_tab(tv, "I2C"));
    ui_tab_periph_create(lv_tabview_add_tab(tv, "Periph"));
    ui_tab_wifi_create(lv_tabview_add_tab(tv, "WiFi"));
    ui_tab_hid_create(lv_tabview_add_tab(tv, "USB"));
    ui_tab_console_create(lv_tabview_add_tab(tv, "Con"));
}

void ui_start(void)
{
    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));   // lv_init + task + tick + lock

    if (!lvgl_port_lock(0)) { ESP_LOGE(TAG, "lock failed"); return; }

    // Custom display driven by LovyanGFX (two DMA-capable partial buffers).
    lv_display_t *disp = lv_display_create(LCD_W, LCD_H);
    static lv_color_t *buf1, *buf2;
    buf1 = heap_caps_malloc(DRAW_BUF_PX * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    buf2 = heap_caps_malloc(DRAW_BUF_PX * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    lv_display_set_buffers(disp, buf1, buf2, DRAW_BUF_PX * sizeof(uint16_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, lcd_flush);

    // Touch input device (LovyanGFX getTouch).
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, disp);

    build_ui();
    lvgl_port_unlock();

    lcd_backlight_set(100);   // reveal the UI now that the first frame is queued
    ESP_LOGI(TAG, "UI started");
}
