#!/usr/bin/env python3
"""Convert a binary PPM (P6) to PNG using only the standard library."""
import struct
import sys
import zlib


def main(src, dst):
    with open(src, "rb") as fh:
        data = fh.read()

    # Header: P6 <w> <h> <maxval>, whitespace-separated.
    fields, pos = [], 2
    while len(fields) < 3:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    pos += 1
    w, h, _maxval = fields
    pixels = data[pos:pos + w * h * 3]

    raw = b"".join(b"\x00" + pixels[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))

    with open(dst, "wb") as fh:
        fh.write(png)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
