// ui_tab_wifi.c — scan nearby access points (STA scan, no association).
#include "ui.h"
#include "wifi_scan.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static lv_obj_t *s_list;
static lv_obj_t *s_scan_btn;

static void scan_task(void *arg)
{
    (void)arg;
    static wifi_ap_t aps[24];
    uint16_t n = 0;
    esp_err_t err = wifi_scan_run(aps, 24, &n);

    if (lvgl_port_lock(0)) {
        lv_obj_clean(s_list);
        if (err != ESP_OK) {
            lv_list_add_text(s_list, "scan failed");
        } else if (n == 0) {
            lv_list_add_text(s_list, "no networks found");
        } else {
            for (uint16_t i = 0; i < n; i++) {
                char buf[80];
                snprintf(buf, sizeof(buf), "%.32s  %ddBm ch%u %.11s",
                         aps[i].ssid, aps[i].rssi, aps[i].channel, aps[i].auth);
                lv_list_add_button(s_list, LV_SYMBOL_WIFI, buf);
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
    lv_list_add_text(s_list, "scanning...");
    // WiFi init + scan can take a second; give the worker a roomy stack.
    xTaskCreate(scan_task, "wifiscan", 5120, NULL, 4, NULL);
}

void ui_tab_wifi_create(lv_obj_t *parent)
{
    lv_obj_set_style_pad_all(parent, 6, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);

    s_scan_btn = lv_button_create(parent);
    lv_obj_t *l = lv_label_create(s_scan_btn);
    lv_label_set_text(l, LV_SYMBOL_WIFI " Scan networks");
    lv_obj_add_event_cb(s_scan_btn, scan_cb, LV_EVENT_CLICKED, NULL);

    s_list = lv_list_create(parent);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_list_add_text(s_list, "press Scan");
}
