// ui_tab_speed.c — raw i80 flush throughput + optional LVGL benchmark demo.
#include "ui.h"
#include "perf_test.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if __has_include("demos/lv_demos.h")
#include "demos/lv_demos.h"
#define HAS_LV_DEMOS 1
#endif

static lv_obj_t *s_result;
static lv_obj_t *s_run_btn;

// Worker: run the benchmark with LVGL paused so it owns the panel, then restore.
static void speed_task(void *arg)
{
    (void)arg;
    lvgl_port_stop();                 // pause the LVGL task (frees the panel)
    perf_result_t r;
    perf_run(30, &r);
    lvgl_port_resume();

    if (lvgl_port_lock(0)) {
        lv_label_set_text_fmt(s_result,
            "Fill:     %.1f MB/s\n"
            "          %.1f fps  (%lu px/s)\n"
            "Gradient: %.1f MB/s\n"
            "Frames:   %lu",
            r.fill_mbps, r.fill_fps, (unsigned long)r.pixels_per_s,
            r.grad_mbps, (unsigned long)r.frames);
        lv_obj_clear_state(s_run_btn, LV_STATE_DISABLED);
        lv_obj_invalidate(lv_screen_active());
        lvgl_port_unlock();
    }
    vTaskDelete(NULL);
}

static void run_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_state(s_run_btn, LV_STATE_DISABLED);
    lv_label_set_text(s_result, "running...");
    xTaskCreate(speed_task, "speedtest", 4096, NULL, 4, NULL);
}

#ifdef HAS_LV_DEMOS
static void bench_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_clean(lv_screen_active());   // benchmark takes over the screen
    lv_demo_benchmark();
}
#endif

void ui_tab_speed_create(lv_obj_t *parent)
{
    lv_obj_set_style_pad_all(parent, 8, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *title = lv_label_create(parent);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_label_set_text(title, "Raw panel throughput (i80 DMA)");

    s_result = lv_label_create(parent);
    lv_obj_set_style_text_font(s_result, &lv_font_montserrat_16, 0);
    lv_label_set_text(s_result, "press Run");

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_style_border_width(row, 0, 0);

    s_run_btn = lv_button_create(row);
    lv_obj_t *l = lv_label_create(s_run_btn);
    lv_label_set_text(l, "Run speed test");
    lv_obj_add_event_cb(s_run_btn, run_cb, LV_EVENT_CLICKED, NULL);

#ifdef HAS_LV_DEMOS
    lv_obj_t *bench = lv_button_create(row);
    lv_obj_t *bl = lv_label_create(bench);
    lv_label_set_text(bl, "LVGL benchmark");
    lv_obj_add_event_cb(bench, bench_cb, LV_EVENT_CLICKED, NULL);
#endif
}
