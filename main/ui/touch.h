// touch.h — press/release state machine over the polled single-point read.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TOUCH_NONE = 0,
    TOUCH_PRESS,     // a new contact
    TOUCH_MOVE,      // the contact moved; lets the keyboard re-target before commit
    TOUCH_REPEAT,    // auto-repeat while held (only if enabled for this press)
    TOUCH_RELEASE,   // commit point — the keyboard emits here, not on press
} touch_event_type_t;

typedef struct {
    touch_event_type_t type;
    uint16_t x, y;               // current contact position
} touch_event_t;

void touch_init(void);

// Poll once per render tick.
touch_event_t touch_poll(void);

// Enable auto-repeat for the press currently in progress. Called by the
// keyboard right after TOUCH_PRESS, since only some keys should repeat.
void touch_enable_repeat(bool enable);

bool touch_is_down(void);
