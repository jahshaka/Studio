#!/usr/bin/env python3
"""THE KIT-BASHED BOX SET as an OBJ (LAB-1's rows): tests/meshbake/test_mesh_bake.cpp kitBash() (:2342) — 200 closed
boxes of 10 cm (12 triangles, 24 vertices each: a face's own normal), on a 20 x 10 grid 30 cm apart in the XY plane —
written with OUTWARD counter-clockwise winding (the bake's fixture ignores winding; a rendered one must not).
usage: kitbash_obj.py <out.obj>"""
import sys

def main(out):
    h = 0.05
    faces = [(0, 0, 1), (0, 0, -1), (1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0)]
    v, vn, f = [], [], []
    for gy in range(10):
        for gx in range(20):
            c = (gx * 0.3, gy * 0.3, 0.0)
            for n in faces:
                ax = [i for i in range(3) if n[i] == 0]
                u, w = ax[0], ax[1]
                # u x w must point along n for CCW-outward: swap when it does not
                cr = [0, 0, 0]
                eu = [0, 0, 0]; eu[u] = 1
                ew = [0, 0, 0]; ew[w] = 1
                cr = [eu[1] * ew[2] - eu[2] * ew[1], eu[2] * ew[0] - eu[0] * ew[2], eu[0] * ew[1] - eu[1] * ew[0]]
                if sum(cr[i] * n[i] for i in range(3)) < 0:
                    u, w = w, u
                base = len(v) + 1
                for k in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                    p = [c[i] + h * n[i] for i in range(3)]
                    p[u] += h * k[0]
                    p[w] += h * k[1]
                    v.append(p)
                vn.append(n)
                ni = len(vn)
                f.append((base, base + 1, base + 2, ni))
                f.append((base, base + 2, base + 3, ni))
    with open(out, "w") as o:
        o.write("# jahlab kit-bash: 200 closed 10 cm boxes (test_mesh_bake.cpp kitBash)\no KitBash\n")
        for p in v:
            o.write("v %.6f %.6f %.6f\n" % tuple(p))
        for n in vn:
            o.write("vn %d %d %d\n" % n)
        for a, b, c, n in f:
            o.write("f %d//%d %d//%d %d//%d\n" % (a, n, b, n, c, n))

if __name__ == "__main__":
    main(sys.argv[1])
