"""rtreflect.walk_world's measures (PHOTON-7 stage 6; the lead's round-5 ruling, ledger §2849): per pair of a walk's
frames, per pixel, walk_m1.py's residual - this frame against the previous one reprojected through the gather's distance
and the two cameras, max channel - of walk A (seed 0) and walk B (seed 7919) against the fully frozen walk's:
rA = res(A) - res(frozen), rB = res(B) - res(frozen); the two walks differ only in the lobe draw's seed.
  GLOSSY is judged by the NOISE MAP (rA - rB) / sqrt 2 - the seed's part of the frame-to-frame change alone, at M1's
  scale: its p99 and its share over one code, each the mean over the pairs (M1's convention). Whole glossy M1 (rA) is
  printed as INFORMATION only: for a glossy surface it is the noise PLUS THE SIGNAL - the estimator correctly following
  the view-dependent change - so it rewards lag and counts a correct update as flicker (the lag is the lag law's row).
  THE MIRROR is judged by whole M1 (rA): its estimator is the centre ray, no noise of its own, every change a defect.
usage: walk_noise.py <dump root> <walk A> <walk B> <frozen>   (prints the measures; exit 1 on a missing dump)"""
import json
import math
import os
import sys

import numpy as np

root, A, B, F = sys.argv[1:5]
CLASSES = {'glossy': 3, 'mirror': 4}


def d(name):
    return os.path.join(root, 'dump-' + name)


cams, W, H, fov = {}, None, None, 45.0
cam_path = os.path.join(d(A), A + '-cams.txt')
if not os.path.exists(cam_path):
    print('FAIL: no camera list %s' % cam_path)
    sys.exit(1)
for line in open(cam_path):
    if line.startswith('#'):
        t = line.split('|')[1].split()
        W, H, fov = int(t[0]), int(t[1]), float(t[3])
        continue
    v = line.split()
    cams[int(v[0])] = (np.array(list(map(float, v[1:4]))), np.array(list(map(float, v[4:7]))))


def rgb(name, i):
    p = os.path.join(d(name), '%s-t%03d.rgb' % (name, i))
    return np.fromfile(p, dtype=np.uint8).reshape(H, W, 3).astype(np.float32) if os.path.exists(p) else None


def basis(e, t):
    f = t - e
    f = f / np.linalg.norm(f)
    r = np.cross(f, [0.0, 1.0, 0.0])
    r = r / np.linalg.norm(r)
    return f.astype(np.float32), r.astype(np.float32), np.cross(r, f).astype(np.float32)


tanY = math.tan(math.radians(fov) / 2.0)
tanX = tanY * W / H
ys, xs = np.mgrid[0:H, 0:W].astype(np.float32)
un = ((2.0 * (xs + 0.5) / W - 1.0) * tanX).astype(np.float32)
vn = ((1.0 - 2.0 * (ys + 0.5) / H) * tanY).astype(np.float32)
acc = {n: {'np99': [], 'nover': [], 'ap99': [], 'aover': []} for n in CLASSES}
pairs = 0
for i in sorted(cams):
    if i == 0 or (i - 1) not in cams:
        continue
    base = os.path.join(d(A), '%s-t%03d' % (A, i))
    gw, gh = map(int, open(base + '.gsize').read().split())
    dist = np.fromfile(base + '.dist', dtype=np.float32).reshape(gh, gw)
    flg = np.fromfile(base + '.flg', dtype=np.uint8).reshape(gh, gw)
    if (gw, gh) != (W, H):
        yy = np.arange(H) * gh // H
        xx = np.arange(W) * gw // W
        dist = dist[yy][:, xx]
        flg = flg[yy][:, xx]
    cls = np.fromfile(base + '.cls', dtype=np.uint8).reshape(H, W)
    want = (flg & 1).astype(bool) & (dist > 0) & np.isin(cls, list(CLASSES.values()))
    sel = np.nonzero(want)   # (only the judged classes' pixels are reprojected)
    e, t = cams[i]
    f, r, u = basis(e, t)
    e0, t0 = cams[i - 1]
    f0, r0, u0 = basis(e0, t0)
    dirs = f[None, :] + r[None, :] * un[sel][:, None] + u[None, :] * vn[sel][:, None]
    rel = dirs * dist[sel][:, None] + (e - e0).astype(np.float32)[None, :]
    z = rel @ f0
    with np.errstate(divide='ignore', invalid='ignore'):
        px = ((rel @ r0) / (z * tanX) + 1.0) * 0.5 * W - 0.5
        py = (1.0 - (rel @ u0) / (z * tanY)) * 0.5 * H - 0.5
    ok = (z > 1e-3) & (px >= 0) & (py >= 0) & (px <= W - 1.001) & (py <= H - 1.001)
    x0 = np.clip(np.floor(np.nan_to_num(px)), 0, W - 2).astype(np.int32)
    y0 = np.clip(np.floor(np.nan_to_num(py)), 0, H - 2).astype(np.int32)
    fx = np.clip(np.nan_to_num(px) - x0, 0, 1)[:, None].astype(np.float32)
    fy = np.clip(np.nan_to_num(py) - y0, 0, 1)[:, None].astype(np.float32)

    def res(name):
        a, b = rgb(name, i - 1), rgb(name, i)
        if a is None or b is None:
            print('FAIL: walk %s has no dump at frames %d/%d' % (name, i - 1, i))
            sys.exit(1)
        s = (a[y0, x0] * (1 - fx) * (1 - fy) + a[y0, x0 + 1] * fx * (1 - fy) + a[y0 + 1, x0] * (1 - fx) * fy +
             a[y0 + 1, x0 + 1] * fx * fy)
        return np.abs(b[sel] - s).max(axis=1)

    rf = res(F)
    rA = res(A) - rf
    rB = res(B) - rf
    pairs += 1
    for n, k in CLASSES.items():
        m = ok & (cls[sel] == k)
        if not m.any():
            continue
        nz = (rA - rB)[m] / math.sqrt(2.0)
        acc[n]['np99'].append(float(np.percentile(nz, 99)))
        acc[n]['nover'].append(float((nz > 1.0).mean()))
        acc[n]['ap99'].append(float(np.percentile(rA[m], 99)))
        acc[n]['aover'].append(float((rA[m] > 1.0).mean()))
out = {'pairs': pairs}
for n in CLASSES:
    if not acc[n]['np99']:
        continue
    out[n] = {'noise_p99': float(np.mean(acc[n]['np99'])), 'noise_over1': float(np.mean(acc[n]['nover'])),
              'm1_p99': float(np.mean(acc[n]['ap99'])), 'm1_over1': float(np.mean(acc[n]['aover']))}
print(json.dumps(out))
