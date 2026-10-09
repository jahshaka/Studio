#!/usr/bin/env python3
"""DEMO-SCENES-1: makes the Bistro's FBX files readable by our model door on Linux.

Usage: python3 -I demo_fbx_prepare.py <Bistro_v5_2 dir> <converted png dir> <out dir>

Two facts of the source, measured:
  1. every Texture/Video RelativeFilename is Windows-style (`Textures\\Foo_BaseColor.dds`): on Linux
     one file name with a backslash in it, so the import resolves no map at all;
  2. the maps are BC1/BC3/BC5 .dds (and a few .tga), which no image reader of ours decodes —
     demo_bistro_textures.py turns them into full-resolution PNGs in <converted png dir>.
This tool writes a copy of each .fbx whose texture-path STRING PROPERTIES (and only those, found by
walking the binary FBX node tree) say `Textures/Foo_BaseColor.png`: the same byte length (`\\`->`/`,
`.dds`/`.tga`->`.png`), so no record offset moves. The PNGs are HARD-LINKED into <out dir>/Textures
(a symlink would resolve outside the model's directory, which the import's containment refuses).
The FBX is read and rewritten as DATA; nothing in it is executed.
"""
import os, struct, sys

def patch(data):
    buf = bytearray(data)
    assert buf[:20] == b'Kaydara FBX Binary  ', 'not a binary FBX'
    ver = struct.unpack('<I', buf[23:27])[0]
    big = ver >= 7500
    hits = 0
    def walk(p, inside):
        nonlocal hits
        if big: end, nprops, _ = struct.unpack_from('<QQQ', buf, p); p += 24
        else: end, nprops, _ = struct.unpack_from('<III', buf, p); p += 12
        nl = buf[p]; p += 1
        if end == 0: return None
        name = bytes(buf[p:p + nl]); p += nl
        here = inside or name in (b'Texture', b'Video')
        for _ in range(nprops):
            t = chr(buf[p]); p += 1
            if t in 'YCIFDL': p += {'Y': 2, 'C': 1, 'I': 4, 'F': 4, 'D': 8, 'L': 8}[t]
            elif t in 'SR':
                n = struct.unpack_from('<I', buf, p)[0]; p += 4
                if t == 'S' and here:
                    s = bytes(buf[p:p + n]); low = s.lower()
                    if low.endswith((b'.dds', b'.tga')):
                        s2 = s.replace(b'\\', b'/')[:-4] + b'.png'
                        assert len(s2) == len(s); buf[p:p + n] = s2; hits += 1
                p += n
            elif t in 'fdlibc':
                _, _, clen = struct.unpack_from('<III', buf, p); p += 12 + clen
            else: raise ValueError('property type %r' % t)
        while p < end:
            q = walk(p, here)
            if q is None: break
            p = q
        return end
    p = 27
    while p < len(buf) - 160:
        q = walk(p, False)
        if q is None: break
        p = q
    return bytes(buf), hits

src, png, out = (os.path.abspath(a) for a in sys.argv[1:4])
os.makedirs(os.path.join(out, 'Textures'), exist_ok=True)
for f in sorted(os.listdir(src)):
    if f.lower().endswith('.fbx'):
        data, hits = patch(open(os.path.join(src, f), 'rb').read())
        open(os.path.join(out, f), 'wb').write(data)
        print(f'{f}: {hits} texture paths rewritten')
n = 0
for f in os.listdir(png):
    if f.lower().endswith('.png'):
        o = os.path.join(out, 'Textures', f)
        if os.path.lexists(o): os.remove(o)
        os.link(os.path.join(png, f), o); n += 1
print(f'{n} PNGs linked into {out}/Textures')
