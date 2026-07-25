// main.c — ESP32-S3 serial terminal for a headless Linux host.
//
// One USB-C cable to the native (OTG) port makes this a CDC-ACM serial device;
// run a getty on /dev/ttyACM0 and the screen becomes that machine's console.
// The CP2104 port keeps doing flashing and IDF logs, so debug output never
// mixes into the terminal stream.
//
// Boot order matters: USB first, because host enumeration timeouts are the
// tightest constraint in the sequence and lcd_init() alone costs ~250ms in
// ILI9488 sleep-out/display-on delays.
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_pins.h"
#include "kbd.h"
#include "lcd.h"
#include "render.h"
#include "status.h"
#include "term.h"
#include "touch.h"
#include "usb_cdc.h"

static const char *TAG = "term";

#define RENDER_TICK_MS 16

static volatile bool s_splash_showing = true;

// --- Splash -----------------------------------------------------------------

static void splash_print(const char *s)
{
    // Written through the parser, so the splash is just terminal content and
    // needs no separate renderer.
    term_feed((const uint8_t *)s, strlen(s));
}

static void splash_draw(void)
{
    char line[128];

    splash_print("\033[2J\033[H");
    splash_print("\033[1;36mESP32-S3 Serial Terminal\033[0m\r\n");
    snprintf(line, sizeof(line), "%dx%d, 6x13 fixed font\r\n\r\n", TERM_COLS, TERM_ROWS);
    splash_print(line);

    snprintf(line, sizeof(line), "  touch controller : %s", lcd_touch_name());
    splash_print(line);
    if (lcd_touch_addr()) {
        snprintf(line, sizeof(line), " @ 0x%02X", lcd_touch_addr());
        splash_print(line);
    }
    splash_print("\r\n");

    snprintf(line, sizeof(line), "  free internal    : %u KB\r\n\r\n",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    splash_print(line);

    splash_print("On the Linux host, with this board's \033[1mnative\033[0m USB-C port plugged in:\r\n\r\n");
    splash_print("  \033[33msudo systemctl enable --now serial-getty@ttyACM0.service\033[0m\r\n\r\n");
    splash_print("Tap the screen for the keyboard. \033[2m1x/2x\033[0m toggles zoom.\r\n\r\n");
    splash_print("\033[2mWaiting for the host...\033[0m");
}

static void splash_clear(void)
{
    if (!s_splash_showing) return;
    s_splash_showing = false;
    term_lock();
    term_t *t = term_get();
    term_reset(t, true);
    t->generation++;
    term_unlock();
    status_set_message(NULL);
}

// --- Touch crosshair diagnostic ---------------------------------------------
//
// Held BOOT at reset enters this instead of the terminal. Tap the four corners
// and check the reported coordinates; if an axis is mirrored, tapping the top
// 40 pixels cycles Touch::offset_rotation so the fix can be found without four
// reflash cycles. The winning value goes into lcd_init().
static void touch_crosshair_mode(void)
{
    ESP_LOGW(TAG, "BOOT held: touch crosshair mode (tap the top edge to cycle "
                  "offset_rotation, reset to leave)");
    lcd_raw_fill(lcd_rgb(0, 0, 0));
    lcd_backlight_set(100);

    uint8_t offset = 0;
    int64_t last_log = 0;

    for (;;) {
        uint16_t x, y, rx, ry;
        bool down = lcd_get_touch(&x, &y);
        bool raw_ok = lcd_get_touch_raw(&rx, &ry);

        if (down) {
            if (y < 40) {
                offset = (uint8_t)((offset + 2) & 7);
                lcd_touch_set_offset_rotation(offset);
                lcd_raw_fill(lcd_rgb(0, 0, 0));
                ESP_LOGW(TAG, "offset_rotation = %u", offset);
                vTaskDelay(pdMS_TO_TICKS(300));
                continue;
            }
            lcd_raw_fill_rect(0, y, LCD_W, 1, lcd_rgb(0, 0xC0, 0));
            lcd_raw_fill_rect(x, 0, 1, LCD_H, lcd_rgb(0, 0xC0, 0));
            int64_t now = esp_timer_get_time();
            if (now - last_log > 200000) {
                last_log = now;
                ESP_LOGI(TAG, "screen (%3u,%3u)  raw (%4u,%4u)%s  offset=%u",
                         x, y, rx, ry, raw_ok ? "" : " [no raw]", offset);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}

static bool boot_button_held(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BTN_BOOT_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    return gpio_get_level(BTN_BOOT_PIN) == 0;
}

// --- Tasks ------------------------------------------------------------------

// Drains the CDC endpoint into the parser. Pinned to core 0 alongside the USB
// stack; the renderer has core 1 to itself.
static void term_task(void *arg)
{
    (void)arg;
    static uint8_t buf[1024];
    for (;;) {
        size_t n = usb_cdc_read(buf, sizeof(buf), 200);
        if (n == 0) continue;
        if (s_splash_showing) splash_clear();
        term_feed(buf, n);
    }
}

// Owns the panel: rendering, touch polling and keyboard drawing all happen
// here, which is what makes the "no LovyanGFX mutex" invariant in lcd.h hold.
static void disp_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        touch_event_t ev = touch_poll();
        kbd_handle_touch(&ev);

        // The key preview sits over the terminal area, so it has to go back on
        // top of anything the grid repaint just drew underneath it.
        bool painted = render_frame();
        kbd_refresh_overlay(painted);
        status_tick();

        vTaskDelayUntil(&last, pdMS_TO_TICKS(RENDER_TICK_MS));
    }
}

// --- Entry ------------------------------------------------------------------

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 serial terminal starting");

    bool diagnostic = boot_button_held();

    // USB first: start enumerating while the panel is still waking up.
    if (!diagnostic) ESP_ERROR_CHECK(usb_cdc_init());

    ESP_ERROR_CHECK(lcd_init());
    lcd_raw_fill(lcd_rgb(0, 0, 0));

    if (diagnostic) touch_crosshair_mode();     // never returns

    term_init();
    ESP_ERROR_CHECK(render_init());
    touch_init();
    kbd_init();
    status_init();
    status_set_message("starting up");

    splash_draw();
    render_frame();
    status_tick();

    // Ramp rather than snap: avoids the flash of whatever was in the panel's
    // RAM and reads as deliberate.
    for (int p = 0; p <= 100; p += 5) {
        lcd_backlight_set(p);
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    status_set_message(NULL);

    xTaskCreatePinnedToCore(term_task, "term", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(disp_task, "disp", 4096, NULL, 4, NULL, 1);

    ESP_LOGI(TAG, "running; free internal %u, free PSRAM %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
