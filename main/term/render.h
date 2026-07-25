// render.h — draws the terminal grid onto the panel.
//
// The renderer keeps its own shadow copy of what is physically on the glass and
// diffs the model against it every frame. That is ~20us for the whole 80x24
// grid and removes the entire class of missed-invalidation bugs that per-cell
// dirty flags invite.
//
// Only the `disp` task may call anything here (see the invariant in lcd.h).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t render_init(void);

// Draw one frame if anything changed. Cheap to call when nothing has.
// Returns true if any pixels were pushed, so overlays drawn on top of the grid
// know whether they need redrawing.
bool render_frame(void);

// Force a repaint of model rows [r0, r1] on the next frame. Needed for changes
// the model does not know about: the keyboard being dismissed, the viewport
// moving, the splash screen being cleared.
void render_invalidate_rows(int r0, int r1);
void render_invalidate_all(void);

// Force a repaint of whatever occupies screen pixel rows [y0, y1]. Use this
// rather than render_invalidate_rows() when you know where something was drawn
// but not which terminal row was under it: with the keyboard up the viewport is
// panned, so screen row and model row are not the same number, and only the
// renderer knows the offset.
void render_invalidate_pixel_band(int y0, int y1);

// Zoom. scale 1 = 80x24 at 6x13; scale 2 = 12x26 cells, a 40x12 window that
// follows the cursor. Anything else is clamped.
void render_set_scale(int scale);
int  render_get_scale(void);

// While the keyboard overlay is up the grid is clipped to the rows above it,
// and the viewport follows the cursor so you are never typing blind.
void render_set_keyboard_visible(bool visible);

// Wipe the whole grid area to the default background (used when leaving the
// splash screen).
void render_clear_grid(void);
