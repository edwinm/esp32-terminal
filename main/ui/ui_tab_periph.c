// ui_tab_periph.c — backlight control, BOOT button, and battery status.
#include "ui.h"
#include "board_pins.h"
#include "lcd.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <stdio.h>

static lv_obj_t *s_btn_state;
static volatile uint32_t s_click_count = 0;
static volatile int64_t s_last_us = 0;

static void IRAM_ATTR boot_isr(void *arg)
{
    (void)arg;
    int64_t now = esp_timer_get_time();
    if (now - s_last_us > 30000) {   // 30 ms debounce
        s_click_count++;
        s_last_us = now;
    }
}

static void boot_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BTN_BOOT_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,   // pressed = low
    };
    gpio_config(&cfg);
    static bool isr_svc = false;
    if (!isr_svc) { gpio_install_isr_service(0); isr_svc = true; }
    gpio_isr_handler_add(BTN_BOOT_PIN, boot_isr, NULL);
}

static void bl_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    lcd_backlight_set(lv_obj_has_state(sw, LV_STATE_CHECKED) ? 100 : 0);
}

static void bl_slider_cb(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    lcd_backlight_set((int)lv_slider_get_value(sl));
}

static void poll_cb(lv_timer_t *t)
{
    (void)t;
    bool pressed = gpio_get_level(BTN_BOOT_PIN) == 0;
    lv_label_set_text_fmt(s_btn_state, "BOOT (GPIO0): %s   clicks: %lu",
                          pressed ? "PRESSED" : "released",
                          (unsigned long)s_click_count);
}

void ui_tab_periph_create(lv_obj_t *parent)
{
    lv_obj_set_style_pad_all(parent, 8, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 10, 0);

    // --- Backlight ---
    lv_obj_t *bl_hdr = lv_label_create(parent);
    lv_obj_set_style_text_font(bl_hdr, &lv_font_montserrat_16, 0);
    lv_label_set_text(bl_hdr, "Backlight (LEDC on GPIO45)");

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 4, 0);

    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, bl_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *slider = lv_slider_create(row);
    lv_obj_set_flex_grow(slider, 1);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, lcd_backlight_get(), LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, bl_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // --- BOOT button ---
    lv_obj_t *btn_hdr = lv_label_create(parent);
    lv_obj_set_style_text_font(btn_hdr, &lv_font_montserrat_16, 0);
    lv_label_set_text(btn_hdr, "User button");
    s_btn_state = lv_label_create(parent);
    lv_obj_set_style_text_font(s_btn_state, &lv_font_montserrat_14, 0);

    // --- Battery ---
    lv_obj_t *bat_hdr = lv_label_create(parent);
    lv_obj_set_style_text_font(bat_hdr, &lv_font_montserrat_16, 0);
    lv_label_set_text(bat_hdr, "Battery");
    lv_obj_t *bat = lv_label_create(parent);
    lv_obj_set_style_text_font(bat, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bat, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_label_set_text(bat, "No battery / fuel-gauge hardware on this board\n"
                           "(powered from USB-C 5V only).");

    boot_init();
    poll_cb(NULL);
    lv_timer_create(poll_cb, 100, NULL);
}
