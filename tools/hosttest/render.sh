#!/bin/sh
# Render a sample screen with the firmware's own rasteriser and write a PNG.
# Useful for checking font indexing, colours and box-drawing alignment without
# flashing the board.
#
#   ./tools/hosttest/render.sh [output.png]
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
png=${1:-$root/screen.png}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

${CC:-cc} -std=c11 -Wall -Wextra -g -O1 \
    -I"$here/stubs" -I"$root/main" -I"$root/main/term" \
    -o "$work/test_render" \
    "$here/test_render.c" \
    "$root/main/term/term_screen.c" \
    "$root/main/term/vt_parser.c" \
    "$root/main/term/vt_modes.c" \
    "$root/main/term/utf8.c" \
    "$root/main/term/render.c" \
    "$root/main/term/glyph.c" \
    "$root/main/term/font6x13.c" \
    "$root/main/term/font5x8.c"

"$work/test_render" "$work/screen.ppm" >/dev/null
python3 "$here/ppm2png.py" "$work/screen.ppm" "$png"
echo "wrote $png"
