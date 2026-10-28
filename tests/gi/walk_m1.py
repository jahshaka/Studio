"""gi.walk_court's and gi.walk_glass's analysis (PHOTON-7 stage 0; walk.sh says the laws): THE WALKING FLICKER M1 per class and THE HITCH LAW.

M1 (PHOTON-2 round 2's method): per trail pair (i-1, i), on the pixels the gather's own test saw in both frames (frame i's
history accepted, .flg bit 0), this frame's picture against the previous frame's reprojected through the gather's view
distance and the two cameras (bilinear) - the RESIDUAL, max channel, in 8-bit codes. M1 = the LIVE walk's residual less
the GI-FROZEN walk's, per pixel (the frozen walk is the motion's own resampling of a converged picture; what is left is
the GI's stepping): per class the mean, the 99th percentile and the share over one code, each the mean over the pairs.
The geometry (distance, test, classes, cameras) is the live walk's: the two walks share the camera path and the scene.

usage: walk_m1.py <dump root> <scene> <live> <frozen> [<frozen2>] [--parts name:first:last,...] [--tsv <live tsv>]
                  [--classes] [--l1-only]
  <dump root>/dump-<name>/<name>-tNNN.{rgb,dist,flg,gsize,cls} and <name>-cams.txt (the stillness door's dump);
  --parts splits the pairs (by the index of the pair's newer frame) into named parts (Bistro: the walk, the fly);
  --tsv: the live walk's stillness TSV - the hitch law and the frame clock over its moving frames;
  --classes: the scene HAS a class map (an environment's; a model has none) - a pair without its .cls, or no counted
  pixel classified, then FAILS (L3 is never vacuous where it is the law: PHOTON-7 stage 0's read R5);
  --l1-only: L1 alone (frozen vs frozen2), and exit - walk.sh runs M1 with the live and frozen walks while the second
  frozen walk records, and L1 once it has (the row's time, PHOTON-7 stage 5).
Prints the scene's lines; exits 1 on a broken instrument law (walk.sh asserts):
  (L1) FROZEN = FROZEN: every frame of the second frozen walk byte-equal to the first's (a frozen walk is a function of
       the scene and the path - no sample, no clock, no cache state);
  (L2) THE ARMS START EQUAL: the trail's first frame (the converged hold before the walk) byte-equal live vs frozen - both
       booted on one warm shader cache (AB G10: a cold boot draws other variants on vertical silhouettes);
  (L3) THE CLASSES COVER THE COUNTED PIXELS: no pixel M1 counts is unclassified (--classes: the map exists on every pair).
"""
import json, math, os, sys
import numpy as np

args = sys.argv[1:]
def opt(name):
    if name in args:
        k = args.index(name); v = args[k + 1]; del args[k:k + 2]; return v
    return None
parts_arg, tsv_path = opt('--parts'), opt('--tsv')
want_classes = '--classes' in args
if want_classes: args.remove('--classes')
l1_only = '--l1-only' in args
if l1_only: args.remove('--l1-only')
root, scene, live, frozen = args[0], args[1], args[2], args[3]
frozen2 = args[4] if len(args) > 4 else None
NAMES = ['all', 'plaster', 'rough', 'glossy', 'mirror', 'foliage', 'mover']
fails = []

def d(name): return os.path.join(root, 'dump-' + name)
cams, W, H, fov = {}, None, None, 45.0
for line in open(os.path.join(d(live), live + '-cams.txt')):
    if line.startswith('#'):
        t = line.split('|')[1].split(); W, H, fov = int(t[0]), int(t[1]), float(t[3]); continue
    v = line.split(); cams[int(v[0])] = (np.array(list(map(float, v[1:4]))), np.array(list(map(float, v[4:7]))))
frames = sorted(cams)

def rgb(name, i):
    p = os.path.join(d(name), '%s-t%03d.rgb' % (name, i))
    return np.fromfile(p, dtype=np.uint8).reshape(H, W, 3) if os.path.exists(p) else None
def raw(name, i):
    p = os.path.join(d(name), '%s-t%03d.rgb' % (name, i))
    return open(p, 'rb').read() if os.path.exists(p) else None

# (L1) frozen = frozen, (L2) the arms start equal
if frozen2:
    differ = []
    for i in frames:
        a = raw(frozen, i)
        if a is None or a != raw(frozen2, i): differ.append(i)
    print('%s (L1) frozen vs frozen: %d of %d frames differ%s' % (scene, len(differ), len(frames),
          (' (first t%03d)' % differ[0]) if differ else ''))
    if differ: fails.append('(L1) %s: the two frozen walks differ on %d frames (first t%03d)' % (scene, len(differ), differ[0]))
if l1_only:
    for f in fails: print('FAIL: %s' % f)
    sys.exit(1 if fails or not frozen2 else 0)
a0, b0 = rgb(live, frames[0]), rgb(frozen, frames[0])
if a0 is None or b0 is None:
    fails.append('(L2) %s: the trail\'s first frame is missing' % scene)
else:
    n0 = int((np.abs(a0.astype(np.int16) - b0.astype(np.int16)).max(axis=2) > 0).sum())
    print('%s (L2) the arms\' first frame (the converged hold): %d px differ live vs frozen' % (scene, n0))
    if n0: fails.append('(L2) %s: the arms start %d px apart (a cold boot or a nondeterminism)' % (scene, n0))

def basis(e, t):
    f = t - e; f = f / np.linalg.norm(f)
    r = np.cross(f, [0.0, 1.0, 0.0]); r = r / np.linalg.norm(r)
    return f.astype(np.float32), r.astype(np.float32), np.cross(r, f).astype(np.float32)
tanY = math.tan(math.radians(fov) / 2.0); tanX = tanY * W / H
ys, xs = np.mgrid[0:H, 0:W].astype(np.float32)
un = ((2.0 * (xs + 0.5) / W - 1.0) * tanX).astype(np.float32); vn = ((1.0 - 2.0 * (ys + 0.5) / H) * tanY).astype(np.float32)

def geo(i):
    base = os.path.join(d(live), '%s-t%03d' % (live, i))
    if not (os.path.exists(base + '.dist') and os.path.exists(base + '.flg')): return None
    gw, gh = map(int, open(base + '.gsize').read().split())
    dist = np.fromfile(base + '.dist', dtype=np.float32).reshape(gh, gw)
    flg = np.fromfile(base + '.flg', dtype=np.uint8).reshape(gh, gw)
    if (gw, gh) != (W, H):
        yy = np.arange(H) * gh // H; xx = np.arange(W) * gw // W
        dist = dist[yy][:, xx]; flg = flg[yy][:, xx]
    cls = np.fromfile(base + '.cls', dtype=np.uint8).reshape(H, W) if os.path.exists(base + '.cls') else None
    return dist, flg, cls

def sample(im, x0, y0, fx, fy):   # im: float32 H x W x 3
    return (im[y0, x0] * (1 - fx) * (1 - fy) + im[y0, x0 + 1] * fx * (1 - fy) + im[y0 + 1, x0] * (1 - fx) * fy +
            im[y0 + 1, x0 + 1] * fx * fy)
pics = {}
def fpic(name, i):   # each frame decoded to float32 once (a frame is the newer of one pair and the older of the next)
    k = (name, i)
    if k not in pics:
        for old in [q for q in pics if q[0] == name and q[1] < i - 1]: del pics[old]
        a = rgb(name, i)
        pics[k] = None if a is None else a.astype(np.float32)
    return pics[k]

parts = [('walk', None, None)]
if parts_arg:
    parts = [(p.split(':')[0], int(p.split(':')[1]), int(p.split(':')[2])) for p in parts_arg.split(',')]
rows = []
unclassified = 0
counted = 0
for i in frames:
    if i == 0 or (i - 1) not in cams: continue
    g = geo(i)
    la, lb, fa, fb = fpic(live, i - 1), fpic(live, i), fpic(frozen, i - 1), fpic(frozen, i)
    if g is None or la is None or lb is None or fa is None or fb is None:
        fails.append('%s: pair t%03d is missing a file' % (scene, i)); continue
    dist, flg, cls = g
    if want_classes and cls is None:
        fails.append('(L3) %s: pair t%03d has no class map (--classes)' % (scene, i)); continue
    e, t = cams[i]; f, r, u = basis(e, t)
    e0, t0 = cams[i - 1]; f0, r0, u0 = basis(e0, t0)
    # the pixel's world point relative to the previous eye: (e - e0) + dir * dist, dir = f + r un + u vn
    dirs = f[None, None, :] + r[None, None, :] * un[..., None] + u[None, None, :] * vn[..., None]
    rel = dirs * dist[..., None] + (e - e0).astype(np.float32)[None, None, :]
    z = rel @ f0
    with np.errstate(divide='ignore', invalid='ignore'):
        px = ((rel @ r0) / (z * tanX) + 1.0) * 0.5 * W - 0.5
        py = (1.0 - (rel @ u0) / (z * tanY)) * 0.5 * H - 0.5
    ok = (flg & 1).astype(bool) & (dist > 0) & (z > 1e-3) & (px >= 0) & (py >= 0) & (px <= W - 1.001) & (py <= H - 1.001)
    x0 = np.clip(np.floor(px), 0, W - 2).astype(np.int32); y0 = np.clip(np.floor(py), 0, H - 2).astype(np.int32)
    fx = np.clip(px - x0, 0, 1)[..., None].astype(np.float32); fy = np.clip(py - y0, 0, 1)[..., None].astype(np.float32)
    live_res = np.abs(lb - sample(la, x0, y0, fx, fy)).max(axis=2)
    res = live_res - np.abs(fb - sample(fa, x0, y0, fx, fy)).max(axis=2)
    if cls is not None:
        unclassified += int((ok & (cls == 0)).sum())
        counted += int(ok.sum())
    row = {'i': i}
    for k, n in enumerate(NAMES):
        m = ok if k == 0 else (ok & (cls == k) if cls is not None else None)
        if m is None or not m.any(): continue
        v = res[m]
        row[n] = (float(v.mean()), float(np.percentile(v, 99)), float((v > 1.0).mean()), int(m.sum()))
        lv = live_res[m]
        row['live ' + n] = (float(lv.mean()), float(np.percentile(lv, 99)), float((lv > 1.0).mean()), int(m.sum()))
    rows.append(row)

# (L3) the classes cover the counted pixels
if counted:
    print('%s (L3) unclassified pixels among the %d counted pixel-frames: %d' % (scene, counted, unclassified))
    if unclassified: fails.append('(L3) %s: %d counted pixels have no class' % (scene, unclassified))
elif want_classes:
    fails.append('(L3) %s: no counted pixel was classified (--classes: the class map is the law here)' % scene)

result = {'scene': scene, 'parts': {}}
for pname, lo, hi in parts:
    sel = [r for r in rows if (lo is None or r['i'] >= lo) and (hi is None or r['i'] <= hi)]
    out = {}
    for n in NAMES:
        vals = [r[n] for r in sel if n in r]
        if not vals: continue
        out[n] = {'pairs': len(vals), 'mean': float(np.mean([v[0] for v in vals])), 'p99': float(np.mean([v[1] for v in vals])),
                  'over1': float(np.mean([v[2] for v in vals])), 'px': int(np.mean([v[3] for v in vals]))}
        print('%s %s M1 %-8s pairs %3d  mean %+.3f  p99 %.2f  >1 code %.2f %%  px %d' % (
              scene, pname, n, out[n]['pairs'], out[n]['mean'], out[n]['p99'], 100.0 * out[n]['over1'], out[n]['px']))
        # ...AND THE LIVE ARM'S OWN RESIDUAL (PHOTON-7 stage 7, 7-3): M1 subtracts the frozen arm's, which carries every
        # stochastic input both arms draw alike (the gather's probes are traced in both: a reader of the probes with no
        # history of its own flickers the same in both arms and M1 cancels it) - the absolute walking change, the
        # motion's resampling in it, compared between two estimators on one path
        lvals = [r['live ' + n] for r in sel if ('live ' + n) in r]
        if lvals:
            print('%s %s live residual %-8s mean %.3f  p99 %.2f  >1 code %.2f %%' % (
                  scene, pname, n, float(np.mean([v[0] for v in lvals])), float(np.mean([v[1] for v in lvals])),
                  100.0 * float(np.mean([v[2] for v in lvals]))))
    result['parts'][pname] = {'m1': out}

# THE HITCH LAW AND THE FRAME CLOCK (the live walk's moving frames, its trail but the converged hold before it)
if tsv_path:
    import csv
    tr = [x for x in csv.DictReader(open(tsv_path), delimiter='\t') if x['window'] == 'walk-move']
    tr = sorted(tr, key=lambda x: int(x['frame']))[1:]   # the trail's first frame is the hold
    def col(rs, c): return [float(x[c]) for x in rs if c in x and float(x[c]) >= 0.0]
    def stat(v):
        if not v: return None
        s = sorted(v); med = s[len(s) // 2]
        return {'p50': med, 'p95': s[min(len(s) - 1, int(len(s) * 0.95))], 'max': s[-1], 'over2x': sum(1 for x in v if x > 2.0 * med)}
    for pname, lo, hi in parts:
        n = len(tr)
        # a part's frames: the trail rows whose newer-frame index is in [lo, hi] (trail index = n + frame, frame <= -1)
        rs = [x for k, x in enumerate(tr, start=1) if (lo is None or k >= lo) and (hi is None or k <= hi)]
        line = []
        for c, label in (('frame_ms', 'wall (renderOneFrame)'), ('frame_gpu_ms', 'GPU busy (span - idle)'),
                         ('period_ms', 'the host\'s period'), ('read_ms', 'the door\'s read'), ('gpu_span_ms', 'GPU span'),
                         ('gpu_idle_ms', 'GPU idle'), ('gpu_unattr_ms', 'GPU unattributed'),
                         ('gpu_passes_ms', 'GPU top-level passes'), ('gpu_rows_ms', 'GPU cache rows (nested)')):
            s = stat(col(rs, c))
            if s is None: continue
            result['parts'][pname].setdefault('clock', {})[c] = s
            line.append('%s p50 %.2f / p95 %.2f / max %.2f ms (%d over 2x the median)' % (label, s['p50'], s['p95'], s['max'], s['over2x']))
        print('%s %s HITCH over %d moving frames: %s' % (scene, pname, len(rs), '; '.join(line)))
        # THE GPU CLOCK'S MODES (PHOTON-7 stage 4, the lead's R3): a p50 on the boundary of two modes flips between them
        # run to run, so each mode reports its own p50 - the LIGHT mode the frames within 2x the lowest quartile, the
        # HEAVY frames those over 2x the light mode's p50 - with the heavy frames' count and what they ARE: the door's
        # GPU rows (gpu_rows, name:ms) by how much more a heavy frame spends in each than a light one.
        def rowsOf(x):
            out = {}
            for kv in (x.get('gpu_rows') or '').split(','):
                if ':' not in kv: continue
                k, v = kv.rsplit(':', 1)
                try: out[k] = out.get(k, 0.0) + float(v)
                except ValueError: pass
            return out
        busy = [(float(x['frame_gpu_ms']), x) for x in rs if 'frame_gpu_ms' in x and float(x['frame_gpu_ms']) >= 0.0]
        if busy:
            sv = sorted(v for v, _ in busy)
            q1 = sv[len(sv) // 4]
            light = sorted(v for v in sv if v <= 2.0 * q1)
            lp50 = light[len(light) // 2]
            heavyRows = [x for v, x in busy if v > 2.0 * lp50]
            lightRows = [x for v, x in busy if v <= 2.0 * lp50]
            hv = sorted(float(x['frame_gpu_ms']) for x in heavyRows)
            mode = 'GPU busy MODES over %d frames: light p50 %.2f ms (%d frames)' % (len(busy), lp50, len(lightRows))
            if hv:
                mode += '; HEAVY (over 2x the light p50) %d frames, p50 %.2f ms' % (len(hv), hv[len(hv) // 2])
                def meanRows(xs):
                    acc = {}
                    for x in xs:
                        for k, v in rowsOf(x).items(): acc[k] = acc.get(k, 0.0) + v
                    return {k: v / max(1, len(xs)) for k, v in acc.items()}
                mh, ml = meanRows(heavyRows), meanRows(lightRows)
                extra = sorted(((mh.get(k, 0.0) - ml.get(k, 0.0), k) for k in set(mh) | set(ml)), reverse=True)
                named = ['%s +%.2f' % (k, d) for d, k in extra[:4] if d > 0.05]
                mode += '; the heavy frames ARE (their rows over a light frame\'s, ms): ' + (', '.join(named) if named else 'no row')
            else:
                mode += '; no heavy frame'
            result['parts'][pname].setdefault('clock', {})['gpu_modes'] = {'light_p50': lp50, 'light': len(lightRows),
                                                                            'heavy': len(hv), 'heavy_p50': hv[len(hv) // 2] if hv else 0.0}
            print('%s %s %s' % (scene, pname, mode))
json.dump(result, open(os.path.join(root, scene + '-walk.json'), 'w'), indent=1)
for f in fails: print('FAIL:', f)
sys.exit(1 if fails else 0)
