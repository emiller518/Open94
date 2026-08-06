#!/usr/bin/env python3
"""Minimal binary-PPM (P6) -> PNG converter (stdlib only)."""
import sys, zlib, struct


def chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def convert(src, dst):
    with open(src, "rb") as f:
        blob = f.read()
    # header: P6 <ws> W <ws> H <ws> MAX <single ws> pixels
    fields, pos = [], 2
    while len(fields) < 3:
        while blob[pos:pos + 1].isspace():
            pos += 1
        if blob[pos:pos + 1] == b"#":
            while blob[pos:pos + 1] not in (b"\n", b""):
                pos += 1
            continue
        start = pos
        while not blob[pos:pos + 1].isspace():
            pos += 1
        fields.append(int(blob[start:pos]))
    pos += 1
    w, h, _ = fields
    px = blob[pos:pos + w * h * 3]
    raw = b"".join(b"\x00" + px[y * w * 3:(y + 1) * w * 3] for y in range(h))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 6))
           + chunk(b"IEND", b""))
    with open(dst, "wb") as f:
        f.write(png)


if __name__ == "__main__":
    for p in sys.argv[1:]:
        convert(p, p.rsplit(".", 1)[0] + ".png")
