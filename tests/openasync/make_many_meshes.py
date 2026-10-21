#!/usr/bin/env python3
"""OPEN-PATH-1: a model file of N distinct small meshes (tests/openasync, open.streams_to_the_end).

Each mesh is its OWN object (`o mesh_<i>`) with its own geometry — T separate triangles scattered over its own
1 m cell — so the bake keeps N meshes and the mirror makes N attaches (no two share an engine mesh). Separate
triangles share no vertex (3T vertices a mesh, nothing to weld) and T stays under the bake's LOD/cluster floor
(256 triangles), so the import is the parse and a distance field per mesh, nothing else.
With a fourth argument M, the file also carries M materials (`<out>.mtl`), each with its own 4x4 texture
(`<out>-tex-<k>.png`), mesh i wearing material i % M — a session set of M materials and M textures beside the
model (open.first_frame_work: the session registration's queries per asset type).
Usage: make_many_meshes.py <out.obj> <meshes> <triangles per mesh> [materials]"""
import math, os, struct, sys, zlib

out, count, tris = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
materials = int(sys.argv[4]) if len(sys.argv) > 4 else 0
side = int(math.ceil(math.sqrt(count)))
lines = ["# OPEN-PATH-1 generated: %d meshes of %d vertices" % (count, 3 * tris)]


def png(path, rgb):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
    raw = b"".join(b"\x00" + bytes(rgb) * 4 for _ in range(4))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


if materials:
    base = os.path.splitext(out)[0]
    mtl = base + ".mtl"
    lines.append("mtllib %s" % os.path.basename(mtl))
    with open(mtl, "w") as f:
        for k in range(materials):
            tex = "%s-tex-%d.png" % (os.path.basename(base), k)
            png(os.path.join(os.path.dirname(out) or ".", tex), ((k * 37) % 256, (k * 91) % 256, (k * 53) % 256))
            f.write("newmtl mat_%d\nKd 1 1 1\nmap_Kd %s\n\n" % (k, tex))
v = 1
for m in range(count):
    ox, oz = (m % side) * 1.5, (m // side) * 1.5
    lines.append("o mesh_%d" % m)
    if materials:
        lines.append("usemtl mat_%d" % (m % materials))
    faces = []
    for t in range(tris):
        h = (m * 2654435761 + t * 40503) & 0xffffffff
        x = ox + (h % 1000) / 1000.0
        z = oz + ((h >> 10) % 1000) / 1000.0
        y = ((h >> 20) % 100) / 1000.0
        s = 0.04
        # counter-clockwise seen from +Y: (x,z) -> (x,z+s) -> (x+s,z)
        lines.append("v %.4f %.4f %.4f" % (x, y, z))
        lines.append("v %.4f %.4f %.4f" % (x, y, z + s))
        lines.append("v %.4f %.4f %.4f" % (x + s, y, z))
        faces.append("f %d %d %d" % (v, v + 1, v + 2))
        v += 3
    lines.extend(faces)
with open(out, "w") as f:
    f.write("\n".join(lines) + "\n")
