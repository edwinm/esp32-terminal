// glyph.c — range-table lookup over the generated font data.
#include "glyph.h"

// Terminal rows are overwhelmingly one script at a time, so remembering the
// last range that matched turns the scan into a single comparison in practice.
static uint16_t s_cache;

const uint8_t *font_glyph(uint16_t cp, bool bold)
{
    const uint8_t *table = bold ? font6x13_bold : font6x13_regular;

    const glyph_range_t *r = &font6x13_ranges[s_cache];
    if (cp >= r->lo && cp <= r->hi) {
        return &table[(r->base + cp - r->lo) * FONT_CELL_H];
    }

    for (unsigned i = 0; i < FONT6X13_RANGE_COUNT; i++) {
        r = &font6x13_ranges[i];
        if (cp < r->lo) break;                  // ranges are ascending
        if (cp <= r->hi) {
            s_cache = (uint16_t)i;
            return &table[(r->base + cp - r->lo) * FONT_CELL_H];
        }
    }
    return &table[0];                           // the unmapped-codepoint box
}

const uint8_t *font_status_glyph(char c)
{
    if (c < STATUS_FONT_FIRST || c > STATUS_FONT_LAST) c = ' ';
    return &font5x8_ascii[(c - STATUS_FONT_FIRST) * STATUS_FONT_H];
}
