"""The rough specular's two laws' analysis (PHOTON-7 stage 7, 7-3; rough_spec.sh says the laws).
usage: rough_spec.py <work dir> quadrature|reference
"""
import glob, math, os, sys
import numpy as np
from PIL import Image

wd, mode = sys.argv[1], sys.argv[2]
data = os.path.join(wd, 'data')
fails = 0
def check(ok, msg):
    global fails
    print(('  ok   ' if ok else '  FAIL ') + msg)
    if not ok: fails += 1

# THE STRIPS' MASKS: the geometry dump at the pose - each pixel's world point (the eye, the image ray, the view
# distance) and its class (2 = rough, walk_m1.py's NAMES); a strip is its rectangle on the floor, a hair inside its edges
dd = os.path.join(data, 'stillness', 'dump-geo')
base = sorted(glob.glob(os.path.join(dd, 'geo-t*.dist')))[-1][:-5]
gw, gh = map(int, open(base + '.gsize').read().split())
dist = np.fromfile(base + '.dist', dtype=np.float32).reshape(gh, gw)
cams, W, H, fov = {}, None, None, 45.0
for line in open(os.path.join(dd, 'geo-cams.txt')):
    if line.startswith('#'):
        t = line.split('|')[1].split(); W, H, fov = int(t[0]), int(t[1]), float(t[3]); continue
    v = line.split(); cams[int(v[0])] = (np.array(list(map(float, v[1:4]))), np.array(list(map(float, v[4:7]))))
cls = np.fromfile(base + '.cls', dtype=np.uint8).reshape(H, W)
if (gw, gh) != (W, H):
    yy = np.arange(H) * gh // H; xx = np.arange(W) * gw // W
    dist = dist[yy][:, xx]
e, t = cams[sorted(cams)[-1]]
f = (t - e) / np.linalg.norm(t - e)
r = np.cross(f, [0.0, 1.0, 0.0]); r /= np.linalg.norm(r)
u = np.cross(r, f)
tanY = math.tan(math.radians(fov) / 2.0); tanX = tanY * W / H
ys, xs = np.mgrid[0:H, 0:W].astype(np.float64)
un = (2.0 * (xs + 0.5) / W - 1.0) * tanX; vn = (1.0 - 2.0 * (ys + 0.5) / H) * tanY
world = e[None, None, :] + (f[None, None, :] + r[None, None, :] * un[..., None] + u[None, None, :] * vn[..., None]) * dist[..., None]
rough = cls == 2
strips = []
for i in range(4):
    x0 = -1.2 + 0.6 * i
    strips.append(('strip %.2f' % (0.45 + 0.1 * i),
                   rough & (world[..., 0] > x0 + 0.02) & (world[..., 0] < x0 + 0.57) & (world[..., 1] < 0.05) &
                   (world[..., 2] > -3.5) & (world[..., 2] < 1.5)))

def img(name):
    return np.asarray(Image.open(os.path.join(data, 'shots', name)).convert('RGB')).astype(np.int32)
def err(X, R, m):
    d = (X - R)[m]
    a = np.abs(d).max(axis=1)
    s = d[np.arange(len(d)), np.abs(d).argmax(axis=1)]
    return float(a.mean()), float(np.percentile(a, 99)), float(s.mean()), int(m.sum()), float(d.mean())

if mode == 'quadrature':
    A, B = img('shipped.png'), img('brute.png')
    label = 'the shipped read against the brute-force quadrature of the same probe map'
    rows = [('rough (all)', rough)] + strips
    print('   %s (8-bit codes: the per-pixel error |shipped - brute| of the larger channel, its p99, the signed mean of '
          'the larger channel, and THE ENERGY - the signed mean over the channels):' % label)
    res = {}
    for n, m in rows:
        res[n] = err(A, B, m)
        print('   %-12s %6d px  per-pixel mean %.2f  p99 %.1f  larger-channel bias %+.2f  energy %+.2f' %
              (n, res[n][3], res[n][0], res[n][1], res[n][2], res[n][4]))
    print('   (the per-pixel error is the split sum\'s own: the n = v = r lobe against the view-stretched one, the levels\' '
          'lerp, the last level\'s clamp - printed, never the law)')
    a = res['rough (all)']
    check(a[3] > 10000, 'not vacuous: the rough class covers the strips (%d px)' % a[3])
    check(np.abs(A - B)[rough].max() > 0, 'the door is engaged: the brute force reads otherwise than the shipped read')
    check(abs(a[4]) <= 1.0,
          'THE SPLIT SUM KEEPS THE ENERGY: the rough class\'s mean signed error %+.2f codes within one of the brute force' %
          a[4])
elif mode == 'reference':
    L, R = img('lobe.png'), img('ref.png')
    print('   the held picture against the traced reflection\'s reference (4,096 raw GGX samples a texel; 8-bit codes, max '
          'channel |lobe - ref|, the signed mean of the larger channel):')
    res = {}
    for n, m in [('rough (all)', rough)] + strips:
        res[n] = err(L, R, m)
        print('   %-12s %6d px  mean %.2f  p99 %.1f  bias %+.2f' % (n, res[n][3], res[n][0], res[n][1], res[n][2]))
    a = res['rough (all)']
    check(a[3] > 10000, 'not vacuous: the rough class covers the strips (%d px)' % a[3])
    check(a[0] < 26.60 and a[1] < 62.0,
          'THE LOBE BEATS THE CONE\'S RECORDED ERROR: mean %.2f < 26.60, p99 %.1f < 62 (spikes/photon-7/stage7/7-3/M1.txt)' %
          (a[0], a[1]))
sys.exit(1 if fails else 0)
