#!/bin/sh
# Compile and run the screen-model / VT-parser tests on the build machine.
# The firmware itself is untouched by this; only stub headers are added.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -O1 \
    -I"$here/stubs" -I"$root/main" -I"$root/main/term" \
    -o "$out/test_term" \
    "$here/test_term.c" \
    "$root/main/term/term_screen.c" \
    "$root/main/term/vt_parser.c" \
    "$root/main/term/vt_modes.c" \
    "$root/main/term/utf8.c"

"$out/test_term" "$@"
