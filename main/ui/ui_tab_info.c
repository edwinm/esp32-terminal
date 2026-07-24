// ui_tab_info.c — chip / memory / temperature / uptime, refreshed every second.
#include "ui.h"
#include "sys_info.h"
#include <stdio.h>

static lv_obj_t *s_label;

static void refresh_cb(lv_timer_t *t)
{
    (void)t;
    sys_info_t si;
    sys_info_get(&si);
    float temp = sys_temp_read();

    char tempbuf[24];
    if (temp > -999.0f) snprintf(tempbuf, sizeof(tempbuf), "%.1f C", temp);
    else                snprintf(tempbuf, sizeof(tempbuf), "n/a");

    uint64_t up = si.uptime_s;
    lv_label_set_text_fmt(s_label,
        "Chip:  %s rev %d, %d cores @ %lu MHz\n"
        "IDF:   %s\n"
        "Flash: %lu MB\n"
        "PSRAM: %lu / %lu KB free\n"
        "DRAM:  %lu / %lu KB free (min %lu)\n"
        "Die temp: %s\n"
        "Uptime:   %02llu:%02llu:%02llu",
        si.chip_model, si.chip_revision, si.cpu_cores, (unsigned long)si.cpu_mhz,
        si.idf_version,
        (unsigned long)si.flash_size_mb,
        (unsigned long)si.psram_free_kb, (unsigned long)si.psram_total_kb,
        (unsigned long)si.heap_free_kb, (unsigned long)si.heap_total_kb,
        (unsigned long)si.heap_min_free_kb,
        tempbuf,
        up / 3600, (up % 3600) / 60, up % 60);
}

void ui_tab_info_create(lv_obj_t *parent)
{
    lv_obj_set_style_pad_all(parent, 8, 0);
    s_label = lv_label_create(parent);
    lv_obj_set_style_text_font(s_label, &lv_font_montserrat_16, 0);
    lv_label_set_text(s_label, "reading...");
    refresh_cb(NULL);
    lv_timer_create(refresh_cb, 1000, NULL);
}
