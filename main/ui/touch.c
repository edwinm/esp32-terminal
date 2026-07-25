// touch.c — turns lcd_get_touch() polling into press/repeat/release events.
//
// Two deliberate choices:
//
//  * Events fire on PRESS, not release. At the ~60 Hz poll rate, waiting for
//    release makes every keystroke feel late.
//  * Release needs two consecutive empty samples, press only one. The FT6236 is
//    clean, but the NS2009 resistive variant drops samples mid-contact and a
//    single-sample release turns one tap into two.
#include "esp_timer.h"

#include "lcd.h"
#include "touch.h"

#define REPEAT_DELAY_US   500000
#define REPEAT_PERIOD_US   60000

static bool     s_down;
static int      s_empty_samples;
static uint16_t s_x, s_y;
static bool     s_repeat_enabled;
static int64_t  s_press_us;
static int64_t  s_next_repeat_us;

void touch_init(void)
{
    s_down = false;
    s_empty_samples = 0;
    s_repeat_enabled = false;
}

touch_event_t touch_poll(void)
{
    touch_event_t ev = { TOUCH_NONE, s_x, s_y };
    uint16_t x, y;
    bool contact = lcd_get_touch(&x, &y);
    int64_t now = esp_timer_get_time();

    if (contact) {
        s_empty_samples = 0;
        if (!s_down) {
            s_down = true;
            s_x = x;
            s_y = y;
            s_repeat_enabled = false;
            s_press_us = now;
            s_next_repeat_us = now + REPEAT_DELAY_US;
            ev.type = TOUCH_PRESS;
            ev.x = x;
            ev.y = y;
            return ev;
        }
        // Held. Sliding within the press does not re-trigger and does not move
        // the reported position: one key per press cycle.
        if (s_repeat_enabled && now >= s_next_repeat_us) {
            s_next_repeat_us = now + REPEAT_PERIOD_US;
            ev.type = TOUCH_REPEAT;
        }
        return ev;
    }

    if (s_down && ++s_empty_samples >= 2) {
        s_down = false;
        s_empty_samples = 0;
        s_repeat_enabled = false;
        ev.type = TOUCH_RELEASE;
    }
    (void)s_press_us;
    return ev;
}

void touch_enable_repeat(bool enable) { s_repeat_enabled = enable; }

bool touch_is_down(void) { return s_down; }
