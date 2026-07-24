// ui_tab_sd.c — mount/unmount, card info, root listing, r/w speed test.
#include "ui.h"
#include "sd_card.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static lv_obj_t *s_status;
static lv_obj_t *s_list;
static lv_obj_t *s_mount_btn_lbl;
static lv_obj_t *s_speed_btn;

static void list_cb(const char *name, bool is_dir, uint32_t size, void *ctx)
{
    (void)ctx;
    char buf[300];
    if (is_dir) snprintf(buf, sizeof(buf), LV_SYMBOL_DIRECTORY " %s", name);
    else        snprintf(buf, sizeof(buf), LV_SYMBOL_FILE " %s  (%lu B)", name, (unsigned long)size);
    lv_list_add_button(s_list, NULL, buf);
}

static void refresh_view(void)
{
    lv_obj_clean(s_list);
    if (sd_is_mounted()) {
        sd_info_t info;
        if (sd_get_info(&info) == ESP_OK) {
            lv_label_set_text_fmt(s_status, "%s \"%s\"  %llu MB @ %lu kHz",
                info.type, info.name, info.capacity_mb, (unsigned long)info.speed_khz);
        }
        sd_list_root(list_cb, NULL);
        lv_label_set_text(s_mount_btn_lbl, "Unmount");
        lv_obj_clear_state(s_speed_btn, LV_STATE_DISABLED);
    } else {
        lv_label_set_text(s_status, "not mounted (insert card, then Mount)");
        lv_label_set_text(s_mount_btn_lbl, "Mount");
        lv_obj_add_state(s_speed_btn, LV_STATE_DISABLED);
    }
}

static void mount_cb(lv_event_t *e)
{
    (void)e;
    if (sd_is_mounted()) sd_unmount();
    else {
        if (sd_mount() != ESP_OK)
            lv_label_set_text(s_status, "mount failed - card inserted?");
    }
    refresh_view();
}

static void speed_task(void *arg)
{
    (void)arg;
    sd_speed_t r;
    sd_speed_test(4, &r);   // 4 MB
    if (lvgl_port_lock(0)) {
        if (r.err == ESP_OK)
            lv_label_set_text_fmt(s_status, "write %.2f MB/s   read %.2f MB/s",
                                  r.write_mbps, r.read_mbps);
        else
            lv_label_set_text(s_status, "speed test failed");
        lv_obj_clear_state(s_speed_btn, LV_STATE_DISABLED);
        lvgl_port_unlock();
    }
    vTaskDelete(NULL);
}

static void speed_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_state(s_speed_btn, LV_STATE_DISABLED);
    lv_label_set_text(s_status, "testing 4 MB...");
    xTaskCreate(speed_task, "sdspeed", 4096, NULL, 3, NULL);
}

void ui_tab_sd_create(lv_obj_t *parent)
{
    lv_obj_set_style_pad_all(parent, 6, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);

    s_status = lv_label_create(parent);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_status, LV_PCT(100));

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 4, 0);

    lv_obj_t *mb = lv_button_create(row);
    s_mount_btn_lbl = lv_label_create(mb);
    lv_label_set_text(s_mount_btn_lbl, "Mount");
    lv_obj_add_event_cb(mb, mount_cb, LV_EVENT_CLICKED, NULL);

    s_speed_btn = lv_button_create(row);
    lv_obj_t *sl = lv_label_create(s_speed_btn);
    lv_label_set_text(sl, "Speed test");
    lv_obj_add_event_cb(s_speed_btn, speed_cb, LV_EVENT_CLICKED, NULL);

    s_list = lv_list_create(parent);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);

    refresh_view();
}
