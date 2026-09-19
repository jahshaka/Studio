#!/usr/bin/env python3
"""Render a preset tile icon for a built-in primitive.

    scripts/primitives/genicons.py app/content/primitives app/modelpresets star wedge

Companion of genprimitives.py (SMALL-UI-A, owner review R6): a primitive needs a
760x490 tile for the editor's presets drawer, and asking a human to open a DCC
for one is how a new primitive comes to ship without a thumbnail. Deterministic
and dependency-free — the standard library plus zlib.


The shipped tiles (app/modelpresets/*.png) are 760x490 grey-shaded renders on a
TRANSPARENT background, from a three-quarter view above the shape.  The new
primitives get theirs the same way: a deterministic software rasteriser (z-buffer,
Lambert + a little ambient, 2x supersampled), so the icon is reproducible from the
mesh and nobody has to open a DCC to add a primitive.
"""
import math, os, struct, sys, zlib

W, H, SS = 760, 490, 3


def load_obj(path):
    verts, norms, faces = [], [], []
    for line in open(path):
        p = line.split()
        if not p:
            continue
        if p[0] == "v":
            verts.append((float(p[1]), float(p[2]), float(p[3])))
        elif p[0] == "vn":
            norms.append((float(p[1]), float(p[2]), float(p[3])))
        elif p[0] == "f":
            idx = []
            for tok in p[1:]:
                bits = tok.split("/")
                vi = int(bits[0])
                ni = int(bits[2]) if len(bits) > 2 and bits[2] else 0
                idx.append((vi - 1, ni - 1 if ni else None))
            for k in range(1, len(idx) - 1):
                faces.append((idx[0], idx[k], idx[k + 1]))
    return verts, norms, faces


def norm(v):
    l = math.sqrt(sum(c * c for c in v)) or 1.0
    return tuple(c / l for c in v)


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def render(objpath, outpath):
    verts, norms, faces = load_obj(objpath)
    lo = [min(v[i] for v in verts) for i in range(3)]
    hi = [max(v[i] for v in verts) for i in range(3)]
    ctr = [(lo[i] + hi[i]) / 2 for i in range(3)]
    radius = max(math.dist(v, ctr) for v in verts) or 1.0

    # the tile camera: three-quarter view from above-front-right, looking at the
    # shape's own centre, framed to a constant fraction of the tile.
    eye_dir = norm((0.45, 0.38, 1.0))
    eye = tuple(ctr[i] + eye_dir[i] * radius * 6.0 for i in range(3))
    fwd = norm(sub(ctr, eye))
    right = norm(cross(fwd, (0.0, 1.0, 0.0)))
    up = cross(right, fwd)
    light = norm((-0.45, 0.72, 0.52))

    w, h = W * SS, H * SS
    scale = h * 2.4 / radius                     # provisional; re-fitted below
    depth = [1e30] * (w * h)
    lum = [0.0] * (w * h)
    cov = [0] * (w * h)

    def project(p):
        d = sub(p, eye)
        cam = (dot(d, right), dot(d, up), dot(d, fwd))
        if cam[2] <= 1e-4:
            return None
        k = scale / cam[2]
        return (w / 2 + cam[0] * k, h / 2 - cam[1] * k, cam[2])

    # AUTO-FRAME: project once with the provisional scale, measure the
    # silhouette's box, and re-scale so the shape fills a constant fraction of
    # the tile whatever its proportions are (a flat star and a tall tube must
    # read at the same size in the drawer).
    xs, ys = [], []
    for p in verts:
        q = project(p)
        if q:
            xs.append(q[0])
            ys.append(q[1])
    if xs and ys:
        spanx = max(xs) - min(xs)
        spany = max(ys) - min(ys)
        fit = min((w * 0.80) / (spanx or 1), (h * 0.80) / (spany or 1))
        scale *= fit
        # re-centre on the silhouette, not on the model origin
        xs, ys = [], []
        for p in verts:
            q = project(p)
            if q:
                xs.append(q[0])
                ys.append(q[1])
        offx = w / 2 - (min(xs) + max(xs)) / 2
        offy = h / 2 - (min(ys) + max(ys)) / 2
    else:
        offx = offy = 0.0

    base_project = project

    def project(p, _b=base_project, _ox=lambda: offx, _oy=lambda: offy):
        q = _b(p)
        if not q:
            return None
        return (q[0] + _ox(), q[1] + _oy(), q[2])

    for tri in faces:
        pts = [verts[i] for i, _ in tri]
        ns = [norms[n] if n is not None and n < len(norms) else None for _, n in tri]
        if any(n is None for n in ns):
            fn = norm(cross(sub(pts[1], pts[0]), sub(pts[2], pts[0])))
            ns = [fn] * 3
        pr = [project(p) for p in pts]
        if any(q is None for q in pr):
            continue
        # back-face cull (screen-space winding)
        area = ((pr[1][0] - pr[0][0]) * (pr[2][1] - pr[0][1])
                - (pr[2][0] - pr[0][0]) * (pr[1][1] - pr[0][1]))
        if area >= 0:
            continue
        x0 = max(0, int(min(q[0] for q in pr)))
        x1 = min(w - 1, int(max(q[0] for q in pr)) + 1)
        y0 = max(0, int(min(q[1] for q in pr)))
        y1 = min(h - 1, int(max(q[1] for q in pr)) + 1)
        shade = []
        for n in ns:
            lam = max(0.0, dot(norm(n), light))
            shade.append(0.30 + 0.70 * lam)
        for y in range(y0, y1 + 1):
            py = y + 0.5
            for x in range(x0, x1 + 1):
                px = x + 0.5
                w0 = ((pr[1][0] - pr[0][0]) * (py - pr[0][1])
                      - (px - pr[0][0]) * (pr[1][1] - pr[0][1]))
                w1 = ((pr[2][0] - pr[1][0]) * (py - pr[1][1])
                      - (px - pr[1][0]) * (pr[2][1] - pr[1][1]))
                w2 = ((pr[0][0] - pr[2][0]) * (py - pr[2][1])
                      - (px - pr[2][0]) * (pr[0][1] - pr[2][1]))
                if not (w0 <= 0 and w1 <= 0 and w2 <= 0):
                    continue
                s = w0 + w1 + w2
                if s == 0:
                    continue
                b0, b1, b2 = w1 / s, w2 / s, w0 / s
                z = b0 * pr[0][2] + b1 * pr[1][2] + b2 * pr[2][2]
                o = y * w + x
                if z >= depth[o]:
                    continue
                depth[o] = z
                lum[o] = b0 * shade[0] + b1 * shade[1] + b2 * shade[2]
                cov[o] = 1

    # box-downsample to the tile size: grey + alpha, transparent where nothing was hit
    out = bytearray()
    for y in range(H):
        out.append(0)                                    # PNG filter byte: none
        for x in range(W):
            acc = a = 0.0
            for sy in range(SS):
                base = ((y * SS + sy) * w) + x * SS
                for sx in range(SS):
                    o = base + sx
                    if cov[o]:
                        acc += lum[o]
                        a += 1.0
            n = SS * SS
            alpha = a / n
            grey = (acc / a) if a else 1.0
            out.append(max(0, min(255, int(round(grey * 255)))))
            out.append(int(round(alpha * 255)))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 4, 0, 0, 0))   # 4 = grey+alpha
    png += chunk(b"IDAT", zlib.compress(bytes(out), 9))
    png += chunk(b"IEND", b"")
    open(outpath, "wb").write(png)
    print("%-12s -> %s (%d bytes)" % (os.path.basename(objpath), outpath,
                                      os.path.getsize(outpath)))


if __name__ == "__main__":
    objdir, icondir, names = sys.argv[1], sys.argv[2], sys.argv[3:]
    for n in names:
        render(os.path.join(objdir, n + ".obj"), os.path.join(icondir, n + ".png"))
