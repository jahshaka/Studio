#!/bin/bash
# rtreflect.lag_law (PHOTON-7 stage 6, REFLECT-MOVING-1's row; the lead's law and its noise term) — THE MOVING
# REFLECTION LAGS ITS CONVERGED PICTURE BY NO MORE THAN ITS MEMORY AND ITS OWN NOISE: gi.walk_glass's walk (photon-glass,
# -1.2,1.8,3.4 -> 1.2,1.8,1.0 looking at 0,0.5,-3, 60 frames from a converged hold) with the hits flattened (the arm
# reflect.flatHits: the reflection's own RAY estimator and denoiser, the caches out - under the arm the screen gives no
# answer, its read of the previous picture being a cache: PHOTON-7-9A, the lead's ruling (A) - which is the STEREO
# chain's route; the desktop hybrid's lag is rtreflect.history_detectors', its motion rtreflect.walk_world_hybrid's) and
# the world held; at walk frames k = 14,
# 28 and 59, per glossy pixel
#     |walk(k) - conv(k)| <= (N - 1) |conv(k+1) - conv(k)| + 3 SE_walk + 1
# conv(k) the picture converged at the walk's pose k (the world held), N the texel's moving count (the door's .rhc),
# SE_walk its moving mean's standard error in display codes (the door's .rse: RayQueryStatus::reflectHistorySe, the
# temporal pass' variance over its count - the walk's ESTIMATE against the converged picture, its noise the estimate's
# own), the +1 the two 8-bit pictures' rounding (|round(a) - round(b)| <= |a - b| + 1). THE BAR: the breaking share
# <= 1 % of the glossy pixels at each k (at 3 sigma an honest estimate breaks ~0.3 % by chance).
# THE BASE (irisgl c98e7718, the moving history's raw second moment: SE_walk read 0): 4.16 / 4.82 / 3.37 % breaking;
# with the variance kept centred (05f61e46): 0.05 / 0.13 / 0.16 %.
# 960x540. SPLIT BY PROOF (URGENT #131, 2026-11-02: the one row of 4,955 frames priced 28.31 s solo ran 56.27 s and timed
# out at 60.06 s under two builds' load - a row at the cap with no margin, #96's class): ONE ROW PER WALK FRAME's LAW, a
# fixture chain on one work dir -
#   walk (rtreflect.lag_law.walk, FIXTURES_SETUP): the prime (6 frames) and the world-held walk (640 + its door's 69) -
#     715 frames; its proof: the three frames' dumps; it keeps the dumps of
#     k = 14 / 28 / 59 only and SNAPSHOTS the shader cache, so every converged part boots on the bytes the converged
#     session booted on in the one row (a grown cache draws other variants: walk_world.sh's measure, 2,712 px at a hold);
#   14 | 28 | 59 (rtreflect.lag_law.k14 / .k28 / .k59): the world settled at the start pose and held (640 frames, the
#     arm gi.freeze 2), the two converged poses k and k + 1 (2 x 600) - 1,840 frames - and the law at its k; each part
#     reads the walk's files and removes none, so the three run in any order (URGENT #133);
#   clean (rtreflect.lag_law.clean, FIXTURES_CLEANUP; no card, no jahlab: `-`): the dumps and the snapshot removed,
#     after every part that ran. The law, its bar and its door are the one row's, unchanged.
# usage: lag_law.sh <jahlab> <work dir> <walk|14|28|59|clean>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}; PART=${3:?part (walk|14|28|59|clean)}
DUMP=$WD/data/stillness/dump-glass-world
if [ "$PART" = clean ]; then
    rm -rf "$DUMP" "$WD/cache.snap"
    echo "rtreflect.lag_law clean: the walk's dumps and its cache snapshot removed - PASS"
    exit 0
fi
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
HERE=$(cd "$(dirname "$0")" && pwd)
case "$PART" in
    walk) rm -rf "$WD"; mkdir -p "$WD" ;;
    14|28|59) [ -f "$DUMP/glass-world-t0$PART.rgb" ] || { echo "FAIL: no walk frame $PART in $WD (rtreflect.lag_law.walk writes it)"; exit 1; } ;;
    *) echo "FAIL: the part '$PART' (walk|14|28|59|clean)"; exit 1 ;;
esac
SRC="--data-root $WD/data --env photon-glass"
session() {   # name (the script is $WD/<name>.lab)
    "$LAB" --headless --no-stdin --size 960x540 $SRC --script "$WD/$1.lab" > "$WD/$1.log" 2>&1
    rc=$?
    [ $rc -eq 0 ] || { echo "FAIL: jahlab ($1) exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR|stillness:' "$WD/$1.log" | head -5; exit 1; }
}
S=-1.2,1.8,3.4; T=0,0.5,-3; E=1.2,1.8,1.0
ARMS='arm reflect.flatHits 1\n'
if [ "$PART" = walk ]; then
    printf "$ARMS"'camera %s,%s\nframes 3\ncamera %s,%s\nframes 3\nquit\n' "$S" "$T" "$E" "$T" > "$WD/prime.lab"
    session prime
    printf "$ARMS"'camera %s,%s\nframes 640\nstillness teleport:%s,%s/hold:1/walk:%s,%s:60 8 trail=60 dump=glass-world freeze=world out=glass-world\nquit\n' \
        "$S" "$T" "$S" "$T" "$E" "$T" > "$WD/walk.lab"
    session walk
    rc=0
    for k in 014 028 059; do
        for x in rgb cls rhc rse rsize; do
            [ -f "$DUMP/glass-world-t$k.$x" ] || { echo "FAIL: the walk wrote no glass-world-t$k.$x"; rc=1; }
        done
    done
    for f in "$DUMP"/*; do case "$f" in *-t014.*|*-t028.*|*-t059.*) ;; *) rm -f "$f" ;; esac; done
    if [ -n "${JAHSHAKA_SHADER_CACHE_DIR:-}" ]; then rm -rf "$WD/cache.snap"; cp -a "$JAHSHAKA_SHADER_CACHE_DIR" "$WD/cache.snap"; fi
    echo "rtreflect.lag_law walk: $([ $rc -eq 0 ] && echo PASS || echo FAIL)"
    exit $rc
fi
if [ -n "${JAHSHAKA_SHADER_CACHE_DIR:-}" ]; then
    [ -d "$WD/cache.snap" ] || { echo "FAIL: the walk part's cache snapshot is missing"; exit 1; }
    rm -rf "$JAHSHAKA_SHADER_CACHE_DIR"; cp -a "$WD/cache.snap" "$JAHSHAKA_SHADER_CACHE_DIR"
fi
{
    printf "$ARMS"'camera %s,%s\nframes 640\narm gi.freeze 2\n' "$S" "$T"
    for k in $PART $((PART + 1)); do
        python3 -I -c "
s=(-1.2,1.8,3.4); e=(1.2,1.8,1.0); k=$k/60.0
print('camera %.6f,%.6f,%.6f,0,0.5,-3' % tuple(a+(b-a)*k for a,b in zip(s,e)))"
        printf 'frames 600\nshot lag%d\n' "$k"
    done
    printf 'quit\n'
} > "$WD/conv$PART.lab"
session conv$PART
python3 -I - "$WD/data" "$PART" <<'PY'
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
for k in (int(sys.argv[2]),):
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
echo "rtreflect.lag_law k$PART: $([ $rc -eq 0 ] && echo PASS || echo FAIL)"
exit $rc
