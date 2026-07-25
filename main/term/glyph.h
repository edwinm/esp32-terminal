// glyph.h — codepoint -> bitmap lookup, shared by the renderer and the
// on-screen keyboard (which draws its labels in the same font).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "font5x8.h"
#include "font6x13.h"

// Always returns a valid 13-byte bitmap; unmapped codepoints get glyph 0, the
// hollow box. Never NULL.
const uint8_t *font_glyph(uint16_t cp, bool bold);

// 8-byte bitmap for the status strip; non-ASCII returns a blank.
const uint8_t *font_status_glyph(char c);
