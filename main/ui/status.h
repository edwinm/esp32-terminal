// status.h — the 8-pixel strip below the terminal grid (y 312..319).
//
// 480x8 is exactly 80 columns of the 5x8 status font. It carries the link
// state, the grid size, and the sticky-modifier state — the last of which has
// nowhere else to live once the keyboard is dismissed.
#pragma once

#include <stdbool.h>

void status_init(void);

// Recompute and repaint only if anything changed. Call once per render tick.
void status_tick(void);

// Override the left-hand text (used by the splash screen). NULL restores the
// normal link-state display.
void status_set_message(const char *msg);
