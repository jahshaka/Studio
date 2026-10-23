#!/bin/bash
# rtreflect.lag_law (PHOTON-7 stage 6, REFLECT-MOVING-1's row; the lead's law and its noise term) — THE MOVING
# REFLECTION LAGS ITS CONVERGED PICTURE BY NO MORE THAN ITS MEMORY AND ITS OWN NOISE: gi.walk's glass walk (photon-glass,
# -1.2,1.8,3.4 -> 1.2,1.8,1.0 looking at 0,0.5,-3, 60 frames from a converged hold) in RAYS with the hits flattened (the
# arm reflect.flatHits: the reflection's own estimator, the hit caches out) and the world held; at walk frames k = 14,
# 28 and 59, per glossy pixel
#     |walk(k) - conv(k)| <= (N - 1) |conv(k+1) - conv(k)| + 3 SE_walk + 1
# conv(k) the picture converged at the walk's pose k (the world held), N the texel's moving count (the door's .rhc),
# SE_walk its moving mean's standard error in display codes (the door's .rse: RayQueryStatus::reflectHistorySe, the
# temporal pass' variance over its count - the walk's ESTIMATE against the converged picture, its noise the estimate's
# own), the +1 the two 8-bit pictures' rounding (|round(a) - round(b)| <= |a - b| + 1). THE BAR: the breaking share
# <= 1 % of the glossy pixels at each k (at 3 sigma an honest estimate breaks ~0.3 % by chance).
# THE BASE (irisgl c98e7718, the moving history's raw second moment: SE_walk read 0): 4.16 / 4.82 / 3.37 % breaking;
# with the variance kept centred (05f61e46): 0.05 / 0.13 / 0.16 %.
# 960x540; three jahlab sessions: a prime (6 frames), the walk (640 + its door's 69) and the converged poses
# (640 + 6 x 600) - 4,955 frames.
# usage: lag_law.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
HERE=$(cd "$(dirname "$0")" && pwd)
rm -rf "$WD"; mkdir -p "$WD"
SRC="--data-root $WD/data --env photon-glass"
session() {   # name (the script is $WD/<name>.lab)
    "$LAB" --headless --no-stdin --size 960x540 $SRC --script "$WD/$1.lab" > "$WD/$1.log" 2>&1
    rc=$?
    [ $rc -eq 0 ] || { echo "FAIL: jahlab ($1) exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR|stillness:' "$WD/$1.log" | head -5; exit 1; }
}
S=-1.2,1.8,3.4; T=0,0.5,-3; E=1.2,1.8,1.0
ARMS='reflections rays\narm reflect.flatHits 1\n'
printf "$ARMS"'camera %s,%s\nframes 3\ncamera %s,%s\nframes 3\nquit\n' "$S" "$T" "$E" "$T" > "$WD/prime.lab"
session prime
printf "$ARMS"'camera %s,%s\nframes 640\nstillness teleport:%s,%s/hold:1/walk:%s,%s:60 8 trail=60 dump=glass-world freeze=world out=glass-world\nquit\n' \
    "$S" "$T" "$S" "$T" "$E" "$T" > "$WD/walk.lab"
session walk
{
    printf "$ARMS"'camera %s,%s\nframes 640\narm gi.freeze 2\n' "$S" "$T"
    for k in 14 15 28 29 59 60; do
        python3 -I -c "
s=(-1.2,1.8,3.4); e=(1.2,1.8,1.0); k=$k/60.0
print('camera %.6f,%.6f,%.6f,0,0.5,-3' % tuple(a+(b-a)*k for a,b in zip(s,e)))"
        printf 'frames 600\nshot lag%d\n' "$k"
    done
    printf 'quit\n'
} > "$WD/conv.lab"
session conv
# THE MODE LABEL: the walk's and the converged shots' sources as the engine ran them
python3 -I "$HERE/../support/reflect_sources.py" tsv "$WD/data/stillness/glass-world.tsv" -- rays &&
python3 -I "$HERE/../support/reflect_sources.py" shots "$WD/conv.log" -- lag=rays
srcrc=$?
python3 -I - "$WD/data" <<'PY'
import os, sys
import numpy as np
from PIL import Image
root = sys.argv[1]
W, H = 960, 540
fails = []
def dil(m, k):
    o = m.copy()
    for dy in range(-k, k + 1):
        for dx in range(-k, k + 1):
            o |= np.roll(np.roll(m, dy, 0), dx, 1)
    return o
for k in (14, 28, 59):
    base = os.path.join(root, 'stillness', 'dump-glass-world', 'glass-world-t%03d' % k)
    need = [base + x for x in ('.rgb', '.cls', '.rhc', '.rse', '.rsize')] + \
           [os.path.join(root, 'shots', 'lag%d.png' % j) for j in (k, k + 1)]
    missing = [p for p in need if not os.path.exists(p)]
    if missing:
        fails.append('walk frame %d: missing %s' % (k, ', '.join(os.path.basename(p) for p in missing))); continue
    if open(base + '.rsize').read().split() != [str(W), str(H)]:
        fails.append('walk frame %d: the trace grid is %s, the row reads %dx%d' % (k, open(base + '.rsize').read().strip(), W, H)); continue
    walk = np.fromfile(base + '.rgb', dtype=np.uint8).reshape(H, W, 3).astype(np.int32)
    cls = np.fromfile(base + '.cls', dtype=np.uint8).reshape(H, W)
    n = np.fromfile(base + '.rhc', dtype=np.uint8).reshape(H, W).astype(np.int32)
    se = np.fromfile(base + '.rse', dtype=np.float32).reshape(H, W)
    c0 = np.asarray(Image.open(os.path.join(root, 'shots', 'lag%d.png' % k)).convert('RGB')).astype(np.int32)
    c1 = np.asarray(Image.open(os.path.join(root, 'shots', 'lag%d.png' % (k + 1))).convert('RGB')).astype(np.int32)
    lhs = np.abs(walk - c0).max(axis=2)
    step = np.abs(c1 - c0).max(axis=2)
    glossy = cls == 3
    if glossy.sum() < 100000:
        fails.append('walk frame %d: %d glossy pixels (< 100,000): the fixture moved' % (k, glossy.sum())); continue
    breaks = glossy & (lhs > np.maximum(n - 1, 0) * step + 3.0 * se + 1.0)
    share = breaks.sum() / glossy.sum()
    edge = dil(cls != np.roll(cls, 1, 1), 3) | dil(cls != np.roll(cls, 1, 0), 3)
    L = c0.mean(axis=2)
    sil = dil(np.maximum(np.abs(np.roll(L, -2, 1) - L), np.abs(np.roll(L, -2, 0) - L)) > 8, 1) & ~edge
    where = ', '.join('%s %d' % (nm, (breaks & m).sum()) for nm, m in (('near an edge', edge), ('on a silhouette', sil),
                                                                       ('interior', ~edge & ~sil)))
    print('walk frame %d: glossy %d px, SE_walk median %.2f codes (p90 %.2f); breaking %.2f %% (bar 1 %%) - %s' % (
        k, glossy.sum(), np.median(se[glossy]), np.percentile(se[glossy], 90), 100 * share, where))
    if share > 0.01:
        fails.append('walk frame %d: %.2f %% of the glossy pixels lag their converged picture past their memory and noise' % (k, 100 * share))
for f in fails: print('FAIL: ' + f)
sys.exit(1 if fails else 0)
PY
rc=$?
rm -rf "$WD/data/stillness/dump-"*
[ $srcrc -eq 0 ] || rc=1
echo "rtreflect.lag_law: $([ $rc -eq 0 ] && echo PASS || echo FAIL)"
exit $rc
