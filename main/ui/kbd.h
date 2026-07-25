// kbd.h — the on-screen keyboard overlay.
//
// Tapping anywhere on the terminal brings it up; the dismiss key or a tap above
// the keyboard puts it away. While it is up the renderer clips the grid to the
// rows above it and pans so the cursor stays visible — you never type blind.
#pragma once

#include <stdbool.h>

#include "touch.h"

void kbd_init(void);

bool kbd_visible(void);
void kbd_show(void);
void kbd_hide(void);

// Feed every touch event here. Returns true when the event was consumed (which
// includes show/hide), so the caller can ignore it.
bool kbd_handle_touch(const touch_event_t *ev);

// Full repaint; called on show and whenever the layer changes.
void kbd_draw(void);

// Sticky-modifier state, mirrored in the status strip so it is still visible
// once the keyboard is dismissed.
bool kbd_shift_active(void);
bool kbd_ctrl_active(void);
bool kbd_shift_locked(void);
bool kbd_ctrl_locked(void);
