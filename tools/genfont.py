#!/usr/bin/env python3
"""Convert the vendored misc-fixed BDFs into the C headers the renderer uses.

Run manually; the firmware build never touches the network or this script.

    python3 tools/genfont.py

Outputs:
    main/term/font6x13.h   terminal font, regular + bold, range-indexed
    main/term/font5x8.h    status-strip font (5x8 cell, ASCII only)

Glyph storage: one byte per pixel row, pixels MSB-aligned (bit 7 = leftmost
column). A 6-wide glyph therefore uses bits 7..2 and leaves bits 1..0 zero,
which is exactly what the rasteriser's unrolled inner loop expects.

Fonts are X11 misc-fixed, public domain ("Public domain font. Share and
enjoy."), vendored under tools/fonts/.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FONTS = os.path.join(HERE, "fonts")
OUT = os.path.join(ROOT, "main", "term")

# Codepoint ranges baked into the terminal font, in ascending order. The
# renderer resolves a codepoint by walking this same table, so the order and
# contents here and in font6x13.h's glyph_range_t array must agree — which they
# do, because this script emits both.
RANGES = [
    (0x0020, 0x007E, "ASCII printable"),
    (0x00A0, 0x00FF, "Latin-1 supplement"),
    (0x0370, 0x03FF, "Greek (pi, for DEC special graphics)"),
    (0x0400, 0x04FF, "Cyrillic"),
    (0x2000, 0x206F, "general punctuation"),
    (0x20A0, 0x20BF, "currency symbols"),
    (0x2100, 0x214F, "letterlike symbols"),
    (0x2190, 0x21FF, "arrows"),
    (0x2200, 0x22FF, "mathematical operators"),
    (0x2300, 0x23FF, "misc technical (DEC scan lines)"),
    (0x2400, 0x243F, "control pictures"),
    (0x2500, 0x257F, "box drawing"),
    (0x2580, 0x259F, "block elements"),
    (0x25A0, 0x25FF, "geometric shapes"),
    (0x2600, 0x26FF, "misc symbols"),
]

# Glyph index 0 is the "no glyph for this codepoint" box, drawn rather than
# taken from the font so it is visibly distinct from a real '?' or space.
NOTDEF_6x13 = [
    0b000000, 0b000000,
    0b011110, 0b010010, 0b010010, 0b010010, 0b010010,
    0b010010, 0b010010, 0b010010, 0b011110,
    0b000000, 0b000000,
]


def parse_bdf(path):
    """Return (cell_w, cell_h, ox, oy, {codepoint: [rowbytes...]}).

    Row lists are indexed from the top of the font bounding box and are
    left-aligned in the MSBs of each byte.
    """
    glyphs = {}
    fbb = None
    with open(path, "r", encoding="latin-1") as fh:
        enc = None
        bbx = None
        rows = None
        for line in fh:
            line = line.rstrip("\n")
            if line.startswith("FONTBOUNDINGBOX"):
                fbb = [int(v) for v in line.split()[1:5]]
            elif line.startswith("ENCODING"):
                enc = int(line.split()[1])
            elif line.startswith("BBX"):
                bbx = [int(v) for v in line.split()[1:5]]
            elif line == "BITMAP":
                rows = []
            elif line == "ENDCHAR":
                if enc is not None and enc >= 0 and bbx and rows is not None:
                    glyphs[enc] = (bbx, rows)
                enc, bbx, rows = None, None, None
            elif rows is not None:
                rows.append(line.strip())

    if fbb is None:
        raise SystemExit("no FONTBOUNDINGBOX in %s" % path)
    cw, ch, ox, oy = fbb
    ytop = oy + ch - 1          # font-space y of cell row 0

    out = {}
    for cp, (bbx, rows) in glyphs.items():
        bw, bh, bx, by = bbx
        cell = [0] * ch
        for i, hexrow in enumerate(rows):
            if not hexrow:
                continue
            y = by + bh - 1 - i
            r = ytop - y
            if not (0 <= r < ch):
                continue        # glyph overshoots the cell; clip
            value = int(hexrow, 16)
            nbits = len(hexrow) * 4
            acc = 0
            for j in range(bw):
                if value & (1 << (nbits - 1 - j)):
                    c = bx + j - ox
                    if 0 <= c < cw:
                        acc |= 1 << (7 - c)
            cell[r] |= acc
        out[cp] = cell
    return cw, ch, ox, oy, out


def build_table(regular, bold, cell_h, notdef):
    """Return (glyph_rows_regular, glyph_rows_bold, ranges_with_bases)."""
    reg = [list(notdef)]
    bld = [list(notdef)]
    ranges = []
    missing = []
    for lo, hi, label in RANGES:
        base = len(reg)
        present = 0
        for cp in range(lo, hi + 1):
            g = regular.get(cp)
            if g is None:
                # Absent from the BDF: draw the notdef box rather than nothing,
                # so a font gap never masquerades as a space.
                reg.append(list(notdef))
                bld.append(list(notdef))
                missing.append(cp)
            else:
                present += 1
                reg.append(g)
                # Bold is a smaller font; fall back to regular where it has no
                # glyph, which is what you want for box drawing anyway (bolding
                # a line-drawing character would break tiling).
                bld.append(bold.get(cp, g))
        ranges.append((lo, hi, base, label, present))
    return reg, bld, ranges, missing


def emit_rows(fh, name, rows, cell_h):
    fh.write("const uint8_t %s[] = {\n" % name)
    for g in rows:
        fh.write("    " + ",".join("0x%02X" % b for b in g) + ",\n")
    fh.write("};\n\n")
    fh.write("_Static_assert(sizeof(%s) == %d * %d, \"%s size\");\n\n"
             % (name, len(rows), cell_h, name))


def gen_terminal_font():
    cw, ch, ox, oy, reg_glyphs = parse_bdf(os.path.join(FONTS, "6x13.bdf"))
    if (cw, ch) != (6, 13):
        raise SystemExit("6x13.bdf is %dx%d, expected 6x13" % (cw, ch))
    _, _, _, _, bold_glyphs = parse_bdf(os.path.join(FONTS, "6x13B.bdf"))

    reg, bld, ranges, missing = build_table(reg_glyphs, bold_glyphs, ch, NOTDEF_6x13)

    banner = """\
// %s — GENERATED by tools/genfont.py, do not edit.
//
// Source: X11 misc-fixed 6x13 / 6x13B (public domain), vendored in
// tools/fonts/. Regenerate with:  python3 tools/genfont.py
//
// One byte per pixel row, 13 rows per glyph, pixels MSB-aligned (bit 7 is the
// leftmost of the 6 columns; bits 1..0 are always zero). Glyph 0 is the
// "unmapped codepoint" box — deliberately not a '?', so a font gap is visibly
// different from an encoding bug.
"""

    with open(os.path.join(OUT, "font6x13.h"), "w") as fh:
        fh.write(banner % "font6x13.h")
        fh.write("""#pragma once

#include <stdint.h>

#define FONT_CELL_W %d
#define FONT_CELL_H %d
#define FONT6X13_GLYPH_COUNT %d

// The data lives in font6x13.c so that both the renderer and the on-screen
// keyboard can use it without duplicating %d KB of flash.
extern const uint8_t font6x13_regular[FONT6X13_GLYPH_COUNT * FONT_CELL_H];
extern const uint8_t font6x13_bold[FONT6X13_GLYPH_COUNT * FONT_CELL_H];

typedef struct {
    uint16_t lo, hi, base;
} glyph_range_t;

// Ascending and non-overlapping, which is what lets the lookup in glyph.c stop
// early and cache the last hit.
extern const glyph_range_t font6x13_ranges[];
#define FONT6X13_RANGE_COUNT %d
""" % (cw, ch, len(reg), (len(reg) * ch * 2) // 1024, len(ranges)))

    with open(os.path.join(OUT, "font6x13.c"), "w") as fh:
        fh.write(banner % "font6x13.c")
        fh.write('#include "font6x13.h"\n\n')
        emit_rows(fh, "font6x13_regular", reg, ch)
        emit_rows(fh, "font6x13_bold", bld, ch)
        fh.write("const glyph_range_t font6x13_ranges[FONT6X13_RANGE_COUNT] = {\n")
        for lo, hi, base, label, present in ranges:
            fh.write("    { 0x%04X, 0x%04X, %4d },   // %3d glyphs  %s\n"
                     % (lo, hi, base, hi - lo + 1, label))
        fh.write("};\n")

    total = len(reg) * ch * 2
    print("font6x13.c: %d glyphs x 2 weights = %d bytes flash" % (len(reg), total))
    if missing:
        print("  %d codepoints absent from the BDF -> notdef box, e.g. %s"
              % (len(missing), " ".join("U+%04X" % c for c in missing[:8])))
    for lo, hi, base, label, present in ranges:
        print("  U+%04X..U+%04X  %3d/%3d present  %s"
              % (lo, hi, present, hi - lo + 1, label))


def gen_status_font():
    cw, ch, ox, oy, glyphs = parse_bdf(os.path.join(FONTS, "5x8.bdf"))
    if (cw, ch) != (5, 8):
        raise SystemExit("5x8.bdf is %dx%d, expected 5x8" % (cw, ch))

    rows = [glyphs.get(cp, [0] * ch) for cp in range(0x20, 0x7F)]

    banner = """\
// %s — GENERATED by tools/genfont.py, do not edit.
//
// Source: X11 misc-fixed 5x8 (public domain). ASCII 0x20..0x7E only; this
// drives the 8-pixel status strip below the terminal grid.
"""

    with open(os.path.join(OUT, "font5x8.h"), "w") as fh:
        fh.write(banner % "font5x8.h")
        fh.write("""#pragma once

#include <stdint.h>

#define STATUS_FONT_W %d
#define STATUS_FONT_H %d
#define STATUS_FONT_FIRST 0x20
#define STATUS_FONT_LAST  0x7E
#define STATUS_FONT_COUNT %d

extern const uint8_t font5x8_ascii[STATUS_FONT_COUNT * STATUS_FONT_H];
""" % (cw, ch, len(rows)))

    with open(os.path.join(OUT, "font5x8.c"), "w") as fh:
        fh.write(banner % "font5x8.c")
        fh.write('#include "font5x8.h"\n\n')
        emit_rows(fh, "font5x8_ascii", rows, ch)

    print("font5x8.c: %d glyphs = %d bytes flash" % (len(rows), len(rows) * ch))


if __name__ == "__main__":
    if not os.path.isdir(OUT):
        raise SystemExit("expected %s to exist" % OUT)
    gen_terminal_font()
    gen_status_font()
