// touch.c — turns lcd_get_touch() polling into press/move/repeat/release events.
//
// Two deliberate choices:
//
//  * The contact position is reported as it moves, so the keyboard can re-target
//    while your finger is still down and only commit on release. On keys this
//    small, being able to correct before lifting matters more than the few
//    milliseconds that emitting on press would save.
//  * Release needs two consecutive empty samples, press only one. The FT6236 is
//    clean, but the NS2009 resistive variant drops samples mid-contact and a
//    single-sample release turns one tap into two.
#include "esp_timer.h"

#include "lcd.h"
#include "touch.h"

#define REPEAT_DELAY_US   500000
#define REPEAT_PERIOD_US   60000

// Ignore sub-pixel jitter, so a still finger does not generate a move storm.
#define MOVE_THRESHOLD_PX 3

static bool     s_down;
static int      s_empty_samples;
static uint16_t s_x, s_y;
static bool     s_repeat_enabled;
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
            s_next_repeat_us = now + REPEAT_DELAY_US;
            ev.type = TOUCH_PRESS;
            ev.x = x;
            ev.y = y;
            return ev;
        }

        // Auto-repeat takes priority over movement: a held arrow key should keep
        // firing even if the finger drifts a little.
        if (s_repeat_enabled && now >= s_next_repeat_us) {
            s_next_repeat_us = now + REPEAT_PERIOD_US;
            ev.type = TOUCH_REPEAT;
            ev.x = s_x;
            ev.y = s_y;
            return ev;
        }

        int dx = (int)x - (int)s_x;
        int dy = (int)y - (int)s_y;
        if (dx * dx + dy * dy >= MOVE_THRESHOLD_PX * MOVE_THRESHOLD_PX) {
            s_x = x;
            s_y = y;
            ev.type = TOUCH_MOVE;
            ev.x = x;
            ev.y = y;
        }
        return ev;
    }

    if (s_down && ++s_empty_samples >= 2) {
        s_down = false;
        s_empty_samples = 0;
        s_repeat_enabled = false;
        ev.type = TOUCH_RELEASE;
        ev.x = s_x;                      // last good position, not the dropout
        ev.y = s_y;
    }
    return ev;
}

void touch_enable_repeat(bool enable)
{
    s_repeat_enabled = enable;
}

bool touch_is_down(void) { return s_down; }
