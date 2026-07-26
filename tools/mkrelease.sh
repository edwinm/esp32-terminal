#!/bin/sh
# Build a single flashable image for people who do not want to compile.
#
#   ./tools/mkrelease.sh              -> release/esp32-terminal-<ver>.bin
#
# The output merges the bootloader, partition table and application into one
# file that is written at offset 0, so installing needs one command and no
# knowledge of the offsets. Upload it to a GitHub release; do not commit it.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
build="$root/.pio/build/makerfabs_s3_parallel_tft35"
out="$root/release"

PIO=${PIO:-$HOME/.platformio/penv/bin/pio}
PY=${PY:-$HOME/.platformio/penv/bin/python}

version=$(git -C "$root" describe --tags --always --dirty 2>/dev/null || echo unknown)

"$PIO" run

mkdir -p "$out"
image="$out/esp32-terminal-$version.bin"

# --flash_mode/--flash_freq keep: preserve exactly what the build produced.
# The bootloader header says DIO even though the app runs QIO — the ROM reads
# the bootloader in DIO and the bootloader then switches the flash over. Passing
# an explicit mode here would rewrite that header.
"$PY" -m esptool --chip esp32s3 merge_bin \
    -o "$image" \
    --flash_mode keep --flash_freq keep --flash_size 16MB \
    0x0     "$build/bootloader.bin" \
    0x8000  "$build/partitions.bin" \
    0x10000 "$build/firmware.bin"

# Verify the merge rather than trusting it: each source image must appear
# byte-for-byte at its offset, and the space between them must be erased flash.
"$PY" - "$image" "$build" <<'PY'
import sys
image, build = sys.argv[1], sys.argv[2]
merged = open(image, 'rb').read()
parts = [(0x0, 'bootloader.bin'), (0x8000, 'partitions.bin'), (0x10000, 'firmware.bin')]
prev_end = None
for off, name in parts:
    src = open(f'{build}/{name}', 'rb').read()
    if merged[off:off + len(src)] != src:
        sys.exit(f'MERGE BROKEN: {name} does not match at 0x{off:x}')
    if prev_end is not None and set(merged[prev_end:off]) - {0xFF}:
        sys.exit(f'MERGE BROKEN: junk between 0x{prev_end:x} and 0x{off:x}')
    prev_end = off + len(src)
print(f'  verified: 3 images, {len(merged)} bytes')
PY

# Manifest for ESP Web Tools, so the firmware can also be flashed from a
# browser with no toolchain at all. Serve it next to the .bin.
cat > "$out/manifest.json" <<EOF
{
  "name": "ESP32-S3 Serial Terminal",
  "version": "$version",
  "new_install_prompt_erase": true,
  "builds": [
    {
      "chipFamily": "ESP32-S3",
      "parts": [{ "path": "$(basename "$image")", "offset": 0 }]
    }
  ]
}
EOF

echo
echo "  $image"
echo "  $out/manifest.json"
echo
echo "Flash with:"
echo "  esptool.py --chip esp32s3 write_flash 0x0 $(basename "$image")"
