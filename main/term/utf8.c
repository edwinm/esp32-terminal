// utf8.c — incremental UTF-8 decoder + minimal wcwidth.
//
// The decoder sits in FRONT of the VT parser's GROUND state, never inside it:
// bytes 0x80..0x9F arriving from a UTF-8 host are continuation bytes, not 8-bit
// C1 controls. We deliberately do not implement 8-bit C1 at all.
#include "utf8.h"

#define REPLACEMENT 0xFFFDu

void utf8_reset(utf8_state_t *s)
{
    s->cp = 0;
    s->need = 0;
    s->seen = 0;
    s->min = 0;
}

bool utf8_decode(utf8_state_t *s, uint8_t b, uint32_t *out)
{
    if (s->need == 0) {
        if (b < 0x80) {                        // ASCII
            *out = b;
            return true;
        }
        if (b >= 0xC2 && b <= 0xDF) {          // 2-byte (0xC0/0xC1 are overlong)
            s->cp = b & 0x1Fu; s->need = 1; s->min = 0x80;
        } else if (b >= 0xE0 && b <= 0xEF) {   // 3-byte
            s->cp = b & 0x0Fu; s->need = 2; s->min = 0x800;
        } else if (b >= 0xF0 && b <= 0xF4) {   // 4-byte
            s->cp = b & 0x07u; s->need = 3; s->min = 0x10000;
        } else {                               // continuation or invalid lead
            *out = REPLACEMENT;
            return true;
        }
        s->seen = 0;
        return false;
    }

    if ((b & 0xC0) != 0x80) {                  // truncated sequence
        utf8_reset(s);
        // Re-dispatch this byte as a fresh lead so we resynchronise here rather
        // than swallowing a valid character.
        if (b < 0x80) { *out = b; return true; }
        *out = REPLACEMENT;
        return true;
    }

    s->cp = (s->cp << 6) | (b & 0x3Fu);
    if (++s->seen < s->need) return false;

    uint32_t cp = s->cp;
    uint32_t min = s->min;
    utf8_reset(s);

    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        *out = REPLACEMENT;                    // overlong, surrogate, or too big
    } else {
        *out = cp;
    }
    return true;
}

int utf8_char_width(uint32_t cp)
{
    if (cp == 0) return 0;
    if (cp < 0x0300) return 1;                 // fast path: Latin and below

    // Zero-width: combining marks and format characters.
    if ((cp >= 0x0300 && cp <= 0x036F) ||
        (cp >= 0x0483 && cp <= 0x0489) ||
        (cp >= 0x0591 && cp <= 0x05BD) ||
        (cp >= 0x0610 && cp <= 0x061A) ||
        (cp >= 0x064B && cp <= 0x065F) ||
        (cp >= 0x1AB0 && cp <= 0x1AFF) ||
        (cp >= 0x1DC0 && cp <= 0x1DFF) ||
        (cp >= 0x200B && cp <= 0x200F) ||
        (cp >= 0x20D0 && cp <= 0x20F0) ||
        (cp >= 0xFE00 && cp <= 0xFE0F) ||
        (cp >= 0xFE20 && cp <= 0xFE2F) ||
        cp == 0x00AD || cp == 0xFEFF) {
        return 0;
    }

    // Double-width: the East Asian Wide / Fullwidth blocks. We have no glyphs
    // for these — the renderer draws the unmapped-codepoint box — but the
    // column accounting has to be right regardless.
    if ((cp >= 0x1100 && cp <= 0x115F) ||
        (cp >= 0x2E80 && cp <= 0x303E) ||
        (cp >= 0x3041 && cp <= 0x33FF) ||
        (cp >= 0x3400 && cp <= 0x4DBF) ||
        (cp >= 0x4E00 && cp <= 0x9FFF) ||
        (cp >= 0xA000 && cp <= 0xA4CF) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) ||
        (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE30 && cp <= 0xFE6F) ||
        (cp >= 0xFF00 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6)) {
        return 2;
    }

    return 1;
}
