#!/bin/bash
# gi.walk_rough (PHOTON-7 stage 7, 7-3; re-pointed in 10-3 R12) — THE ROUGH SPECULAR'S WALK LAGS ITS CONVERGED PICTURE BY
# NO MORE THAN ITS MEMORY AND ITS OWN NOISE: photon-rough's four rough metal strips (0.45 - 0.75), the reflection's
# cutoff 0 (no traced reflection: the strips' environment term is the probes' prefiltered lobe whole, riding the gather's
# history), Epic, 960x540; a 2 m walk along them (0,1.6,2.4 -> 0,1.6,0.4 looking at 0,0,-3, 60 frames from a converged
# hold), and at walk frames k = 14, 28 and 59, per ROUGH pixel (the class map's 2)
#     |walk(k) - conv(k)| <= (N - 1) |conv(k+1) - conv(k)| + 3 SE_walk + 1
# THE LAG LAW (rtreflect.lag_law's, the lead's ruling): conv(k) the picture converged at the walk's pose k (600 frames
# there), N the gather's history count at the pixel (the dump's .ghc: the frames its mean holds), SE_walk = sigma / sqrt N,
# sigma the pixel's per-frame standard deviation of the RAW lobe at pose k (gather.temporal 0: twelve frames' estimates
# alone, in the same session - the lead's option B: a test's statistic, no engine channel), the +1 the two 8-bit
# pictures' rounding. THE BAR: the breaking share <= 1 % of the rough pixels at each k (at 3 sigma an honest estimate
# breaks ~0.3 % by chance). (It replaced the live residual against "the cone's recorded 48.32 %", the producer the
# lobe replaced - a picked bar.)
# DECLARED RED (2026-10-27, the lead's ruling: a stage-8 item, beside rough_reference's (a)/(b) - the gather's rough
# lobe): breaking 1.79 / 4.59 / 16.07 % at k 14 / 28 / 59, on the two smoother strips (0.45 / 0.55, the pure lobe) in
# blotches, the walk DARKER (-1.9 codes over the class at k 59, -3.7 on the breaking pixels). THE CAUSE, measured: NOT a
# bias of the moving rule (the moving rule held at pose 59 - gather.restOff - for 64 frames against the rest mean there:
# -0.015 codes, each strip within 0.3) and NOT the statistics (at that fixed pose the moving error's p99 2.5 codes sits
# inside 3 SE + 1): the excess exists only under MOTION - the lobe rides the diffuse's SURFACE reprojection, and a
# view-dependent lobe reprojected along the surface lags past what its window implies (the reflection takes a
# virtual-point history for the same reason).
# 8B (2026-10-29): B5's fast update takes k 59 to 0.27 % (the walk's scroll changes bricks; the history shortens there);
# k 14 / 28 stay RED at 1.86 / 4.60 %. B4's VIRTUAL-POINT history was built and measured here and REVERTED (the lead's
# ruling (b)): 3.51 / 3.99 / 0.11 bare, 3.55 / 3.53 / 0.23 with a same-plane tap, 3.16 / 3.05 / 0.24 with Lumen's 3 x 3
# pick - k 14 doubled, because one mean distance across a lobe spanning a depth break (a near strip edge before the far
# walls) names a point on neither. The filter keeps the lobes' mean distance layers (no reader); the leftover: the
# distance's variance as a third moment and the two histories blended by the parallax error it predicts.
# 960x540; two jahlab sessions: the walk (600 + 60 and its door's frames) and the converged poses (6 x 600 + 3 x 14).
# usage: rough_lag.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
SRC="--data-root $WD/data --env photon-rough --tier epic"
session() {   # name (the script is $WD/<name>.lab)
    "$LAB" --headless --no-stdin --size 960x540 $SRC --script "$WD/$1.lab" > "$WD/$1.log" 2>&1
    rc=$?
    [ $rc -eq 0 ] || { echo "FAIL: jahlab ($1) exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR|stillness:' "$WD/$1.log" | head -5; exit 1; }
}
S=0,1.6,2.4; T=0,0,-3; E=0,1.6,0.4
printf 'row reflectionRoughnessCutoff 0\ncamera %s,%s\nframes 600\nstillness teleport:%s,%s/hold:1/walk:%s,%s:60 8 trail=60 dump=rough-walk out=rough-walk\nquit\n' \
    "$S" "$T" "$S" "$T" "$E" "$T" > "$WD/walk.lab"
session walk
{
    printf 'row reflectionRoughnessCutoff 0\n'
    for k in 14 15 28 29 59 60; do
        python3 -I -c "
s=(0.0,1.6,2.4); e=(0.0,1.6,0.4); k=$k/60.0
print('camera %.6f,%.6f,%.6f,0,0,-3' % tuple(a+(b-a)*k for a,b in zip(s,e)))"
        printf 'frames 600\nshot conv%d\n' "$k"
        case $k in 14|28|59)
            printf 'arm gather.temporal 0\nframes 2\n'
            for j in $(seq 0 11); do printf 'frames 1\nshot raw%d_%d\n' "$k" "$j"; done
            printf 'arm gather.temporal 1\n' ;;
        esac
    done
    printf 'quit\n'
} > "$WD/conv.lab"
session conv
python3 -I - "$WD/data" <<'PY'
import os, sys
import numpy as np
from PIL import Image
root = sys.argv[1]
W, H = 960, 540
fails = []
def png(name):
    return np.asarray(Image.open(os.path.join(root, 'shots', name + '.png')).convert('RGB')).astype(np.float64)
for k in (14, 28, 59):
    base = os.path.join(root, 'stillness', 'dump-rough-walk', 'rough-walk-t%03d' % k)
    need = [base + x for x in ('.rgb', '.cls', '.ghc', '.gsize')]
    missing = [p for p in need if not os.path.exists(p)]
    if missing:
        fails.append('walk frame %d: missing %s' % (k, ', '.join(os.path.basename(p) for p in missing))); continue
    walk = np.fromfile(base + '.rgb', dtype=np.uint8).reshape(H, W, 3).astype(np.float64)
    cls = np.fromfile(base + '.cls', dtype=np.uint8).reshape(H, W)
    gw, gh = map(int, open(base + '.gsize').read().split())
    n = np.fromfile(base + '.ghc', dtype=np.uint8).reshape(gh, gw).astype(np.float64)
    if (gw, gh) != (W, H):   # (the history's grid onto the picture's)
        n = n[(np.arange(H) * gh // H)][:, (np.arange(W) * gw // W)]
    c0, c1 = png('conv%d' % k), png('conv%d' % (k + 1))
    raw = np.stack([png('raw%d_%d' % (k, j)) for j in range(12)])
    sigma = raw.std(axis=0, ddof=1).max(axis=2)
    N = np.maximum(n, 1.0)
    se = sigma / np.sqrt(N)
    lhs = np.abs(walk - c0).max(axis=2)
    step = np.abs(c1 - c0).max(axis=2)
    rough = cls == 2
    if rough.sum() < 50000:
        fails.append('walk frame %d: %d rough pixels (< 50,000): the fixture moved' % (k, rough.sum())); continue
    breaks = rough & (lhs > (N - 1.0) * step + 3.0 * se + 1.0)
    share = breaks.sum() / rough.sum()
    print('walk frame %d: rough %d px, N median %.0f, sigma median %.2f codes, SE_walk median %.2f (p90 %.2f); '
          '|walk - conv| mean %.2f p99 %.1f; breaking %.2f %% (bar 1 %%)' % (k, rough.sum(), np.median(N[rough]),
          np.median(sigma[rough]), np.median(se[rough]), np.percentile(se[rough], 90), lhs[rough].mean(),
          np.percentile(lhs[rough], 99), 100 * share))
    if share > 0.01:
        fails.append('walk frame %d: %.2f %% of the rough pixels lag their converged picture past their memory and noise'
                     % (k, 100 * share))
for f in fails: print('FAIL: ' + f)
sys.exit(1 if fails else 0)
PY
rc=$?
rm -rf "$WD/data/stillness/dump-"* "$WD/data/shots"
echo "gi.walk_rough: $([ $rc -eq 0 ] && echo PASS || echo FAIL)"
exit $rc
