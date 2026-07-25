// utf8.h — incremental UTF-8 decoding and character width.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t cp;      // codepoint being assembled
    uint8_t  need;    // continuation bytes still expected
    uint8_t  seen;    // continuation bytes consumed so far
    uint32_t min;     // smallest legal value for this length (overlong check)
} utf8_state_t;

void utf8_reset(utf8_state_t *s);

// Feed one byte. Returns true and stores the codepoint in *out when a complete
// character (or a U+FFFD for malformed input) is ready.
bool utf8_decode(utf8_state_t *s, uint8_t byte, uint32_t *out);

// Columns this codepoint occupies: 0 (combining), 1, or 2 (wide).
// Required for column accounting, not just for drawing — if the host advances
// two columns for a CJK character and we advance one, every later cell on that
// line is permanently off by one.
int utf8_char_width(uint32_t cp);
