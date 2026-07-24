// ui.h — LVGL bring-up and the tabview shell.
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start LVGL (via esp_lvgl_port) with a LovyanGFX-backed display + touch, and
// build the tabview UI.
void ui_start(void);

// Each tab's builder. `parent` is the tab page object.
void ui_tab_info_create(lv_obj_t *parent);
void ui_tab_speed_create(lv_obj_t *parent);
void ui_tab_sd_create(lv_obj_t *parent);
void ui_tab_i2c_create(lv_obj_t *parent);
void ui_tab_periph_create(lv_obj_t *parent);
void ui_tab_wifi_create(lv_obj_t *parent);
void ui_tab_hid_create(lv_obj_t *parent);
void ui_tab_console_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
