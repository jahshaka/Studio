#!/usr/bin/env python3
"""OPEN-PATH-1: a model file of N distinct small meshes (tests/openasync, open.streams_to_the_end).

Each mesh is its OWN object (`o mesh_<i>`) with its own geometry — T separate triangles scattered over its own
1 m cell — so the bake keeps N meshes and the mirror makes N attaches (no two share an engine mesh). Separate
triangles share no vertex (3T vertices a mesh, nothing to weld) and T stays under the bake's LOD/cluster floor
(256 triangles), so the import is the parse and a distance field per mesh, nothing else.
Usage: make_many_meshes.py <out.obj> <meshes> <triangles per mesh>"""
import math, sys

out, count, tris = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
side = int(math.ceil(math.sqrt(count)))
lines = ["# OPEN-PATH-1 generated: %d meshes of %d vertices" % (count, 3 * tris)]
v = 1
for m in range(count):
    ox, oz = (m % side) * 1.5, (m // side) * 1.5
    lines.append("o mesh_%d" % m)
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
