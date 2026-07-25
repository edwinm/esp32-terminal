#!/bin/sh
# Push the render test's sample screen at a real board, so what appears on the
# panel is byte-for-byte what render.sh renders to PNG.
#
#   ./tools/hosttest/sendtest.sh /dev/cu.usbmodemXXXX     (macOS)
#   ./tools/hosttest/sendtest.sh /dev/ttyACM0             (Linux)
#
# With no argument it picks the only CDC device it can find.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)

port=$1
if [ -z "$port" ]; then
    port=$(ls /dev/cu.usbmodem* /dev/ttyACM* 2>/dev/null | head -1)
fi
if [ -z "$port" ]; then
    echo "no CDC device found; pass the port explicitly" >&2
    exit 1
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 \
    -I"$here/stubs" -I"$root/main" -I"$root/main/term" -I"$root/main/ui" -I"$root/main/io" \
    -o "$work/test_render" \
    "$here/test_render.c" "$root"/main/term/*.c "$root"/main/ui/*.c

"$work/test_render" -s > "$work/stream.bin"

# The terminal owns the line settings; just push the bytes.
cat "$work/stream.bin" > "$port"
echo "sent $(wc -c < "$work/stream.bin" | tr -d ' ') bytes to $port"
