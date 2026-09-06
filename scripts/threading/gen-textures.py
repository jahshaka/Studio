#!/usr/bin/env python3
"""Generates the texture-heavy scene's art (SPECS/THREADING_ADOPTION_SPEC.md G2-c).

N distinct 2048x2048 RGBA PNGs into a directory. Deliberately a GENERATOR and not
committed art: the measurement needs ~48 large images, which is ~100 MB of binary
nobody would ever look at, and a repository is the wrong place for a fixture whose
only property is "it takes real work to decode".

WHAT MAKES THE IMAGES HONEST:
  * 2048x2048 RGBA8 — upstream's own benchmark size for the multiload pool
    (OgreTextureGpuManager.h:1128-1135).
  * every file DIFFERENT, so nothing anywhere can serve two of them from one
    decode, and so their compressed sizes vary the way real art does.
  * a gradient + per-file noise-ish pattern rather than flat colour: a flat PNG
    compresses to nothing and decodes in microseconds, which would measure the
    filesystem instead of the decoder.

Written with the standard library only (zlib + struct). No Pillow, no numpy: this
has to run on a build box that has neither.

    python3 gen-textures.py <outdir> [count] [size]
"""

import os
import struct
import sys
import zlib


def png_chunk(tag: bytes, data: bytes) -> bytes:
    return (struct.pack(">I", len(data)) + tag + data +
            struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def write_png(path: str, width: int, height: int, seed: int) -> None:
    # One row template per 8 rows, rotated per row, so the file has real entropy
    # without costing minutes to build. Every seed gives a different palette and a
    # different stripe period, so no two files share a byte pattern.
    period = 7 + (seed % 23)
    r0 = (seed * 37) & 0xFF
    g0 = (seed * 91) & 0xFF
    b0 = (seed * 53) & 0xFF
    rows = []
    for band in range(8):
        px = bytearray()
        for x in range(width):
            t = (x + band * 31) % period
            px += bytes(((r0 + x) & 0xFF,
                         (g0 + t * 17) & 0xFF,
                         (b0 + (x >> 3) + band) & 0xFF,
                         0xFF))
        rows.append(bytes(px))

    raw = bytearray()
    for y in range(height):
        raw.append(0)                      # filter type 0 (None)
        raw += rows[(y + (y >> 5)) & 7]

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)   # 8-bit RGBA
    blob = (b"\x89PNG\r\n\x1a\n" +
            png_chunk(b"IHDR", ihdr) +
            png_chunk(b"IDAT", zlib.compress(bytes(raw), 1)) +
            png_chunk(b"IEND", b""))
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(blob)
    os.replace(tmp, path)


def main() -> int:
    if len(sys.argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    outdir = sys.argv[1]
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 48
    size = int(sys.argv[3]) if len(sys.argv) > 3 else 2048
    os.makedirs(outdir, exist_ok=True)
    total = 0
    for i in range(count):
        path = os.path.join(outdir, "abtex_%02d.png" % i)
        if not os.path.isfile(path):
            write_png(path, size, size, i + 1)
        total += os.path.getsize(path)
    print("%d textures, %dx%d, %.1f MB total in %s" %
          (count, size, size, total / (1024.0 * 1024.0), outdir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
