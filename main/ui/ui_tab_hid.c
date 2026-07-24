// ui_tab_hid.c — type over the native USB-C port as a HID keyboard.
#include "ui.h"
#include "usb_hid.h"
#include "class/hid/hid.h"   // HID_KEY_* codes

static lv_obj_t *s_status;

static void demo_cb(lv_event_t *e)
{
    (void)e;
    usb_hid_type_string("Hello from Makerfabs ESP32-S3 TFT!\n");
}

static void arrows_cb(lv_event_t *e)
{
    usb_hid_send_key((uint8_t)(uintptr_t)lv_event_get_user_data(e));
}

static void status_cb(lv_timer_t *t)
{
    (void)t;
    lv_label_set_text_fmt(s_status, "USB host: %s",
                          usb_hid_mounted() ? "connected" : "not connected");
}

void ui_tab_hid_create(lv_obj_t *parent)
{
    lv_obj_set_style_pad_all(parent, 8, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 10, 0);

    lv_obj_t *hint = lv_label_create(parent);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_label_set_text(hint,
        "Connect the NATIVE USB-C port to a computer, focus a text field, "
        "then press a button. (The other USB-C is the serial/flash port.)");

    s_status = lv_label_create(parent);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_16, 0);
    lv_label_set_text(s_status, "USB host: ...");

    lv_obj_t *demo = lv_button_create(parent);
    lv_obj_t *dl = lv_label_create(demo);
    lv_label_set_text(dl, "Type demo string");
    lv_obj_add_event_cb(demo, demo_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 4, 0);

    struct { const char *txt; uint8_t key; } arrows[] = {
        { LV_SYMBOL_LEFT,  HID_KEY_ARROW_LEFT },
        { LV_SYMBOL_UP,    HID_KEY_ARROW_UP },
        { LV_SYMBOL_DOWN,  HID_KEY_ARROW_DOWN },
        { LV_SYMBOL_RIGHT, HID_KEY_ARROW_RIGHT },
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = lv_button_create(row);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, arrows[i].txt);
        lv_obj_add_event_cb(b, arrows_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)arrows[i].key);
    }

    status_cb(NULL);
    lv_timer_create(status_cb, 500, NULL);
}
