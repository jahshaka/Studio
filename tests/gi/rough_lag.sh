#!/bin/bash
# gi.walk_rough (PHOTON-7 stage 7, 7-3; re-pointed in 10-3 R12) — THE ROUGH SPECULAR'S WALK LAGS ITS CONVERGED PICTURE BY
# NO MORE THAN ITS MEMORY AND ITS OWN NOISE: photon-rough's four rough metal strips (0.45 - 0.75), the reflection's
# cutoff 0 (no traced reflection: the strips' environment term is the probes' prefiltered lobe whole, riding the gather's
# history), Epic, 960x540; a 2 m walk along them (0,1.6,2.4 -> 0,1.6,0.4 looking at 0,0,-3, 60 frames from a converged
# hold), and at walk frames k = 14, 28 and 59, per ROUGH pixel (the class map's 2)
#     |walk(k) - conv(k)| <= (N - 1) |conv(k+1) - conv(k)| + 3 SE_walk + 1
# THE LAG LAW (rtreflect.lag_law's, the lead's ruling): conv(k) the picture converged at the walk's pose k (the GI at
# rest there: CONVERGED, below), N the gather's history count at the pixel (the dump's .ghc: the frames its mean holds), SE_walk = sigma / sqrt N,
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
# CONVERGED (HARNESS-3 (a), RE-DERIVED; it was a picked 600 frames): a converged picture is the one the engine's own settle
# predicate says has stopped moving - GiStatus::giAtRest, the one --engine-selftest waits on: the chain owes nothing, the
# cards' rest decision is taken, and the gather's history is settled (an EMA at h = 10 takes ceil(ln(1/D) / ln(1 - 1/h))
# frames to bring a D-code step under one code: 16 at D = 5, 53 at the full 255) and HELD (the rest mean of N = 16 rest
# frames). No count derived from N alone bounds it: measured on this fixture (one session, 2026-10-29) the predicate was
# false for 200 frames at every pose, the gather's rest started 172-245 frames after a pose change (the cards' queue
# drained at 98-158, then the rest), and the picture at 53 frames read > 1 code from the held one on 3.2-5.8 % of pixels
# (12-25 codes at worst) - the live EMA's noise around the rest mean. So every converged pose here is the lab's
# `rest 600`: frames until giAtRest, REFUSED past 600 (the old count kept as the refusal, never a wait).
# THE SPLIT BY PROOF (HARNESS-3 (a); the row whole ran 55.78 / 57.69 s against the 45-s quiet law): ONE ROW PER WALK FRAME k,
# its law alone, on the walk's dump - a fixture chain:
#   gi.walk_rough.walk   `rough_lag.sh <jahlab> <walk dir> walk`: the walk session (a converged hold at the start, the
#                        teleport, hold:1, the 60-frame walk, the door's 8 frames and its 60-frame trail, the dump) - its
#                        dump pruned to frames 14 / 28 / 59 (FIXTURES_SETUP gi_walk_rough_dump);
#   gi.walk_rough.k<k>   `rough_lag.sh <jahlab> <walk dir> <k> <own dir>`: the converged poses k and k + 1 (the law's step
#                        term) and the twelve raw frames at k, then the law at k on the walk's frame k.
# 960x540; one jahlab session a row (.walk 350 frames, its rest 279; .k14 / .k28 / .k59 506 / 519 / 550, their rests
# 208-291 - the whole row was 4,314). Each row prints its law's numbers as LAW lines (tests/support/lawline.h).
# THE SAME LAW, SPLIT (one run each at 1e1c4d97e, no B5, 2026-10-29): the split 2.01 / 5.14 / 17.82 %, the whole row
# 2.15 / 4.93 / 17.22 % - |walk - conv| mean 1.46 / 1.91 / 2.93 and p99 13 / 13 / 11 codes identical, the rest the twelve
# raw frames' sigma; the declared 1.79 / 4.59 / 16.07 above are cafcc1f64's (the tree moved since, not the split).
# usage: rough_lag.sh <jahlab> <walk dir> walk | rough_lag.sh <jahlab> <walk dir> 14|28|59 <work dir>
set -u
LAB=${1:?jahlab}; WALK=${2:?the walk dir}; PART=${3:?walk, 14, 28 or 59}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
case $PART in
walk) WD=$WALK ;;
14|28|59) WD=${4:?the work dir} ;;
*) echo "FAIL: the part is walk, 14, 28 or 59, not '$PART'"; exit 2 ;;
esac
rm -rf "$WD"; mkdir -p "$WD"
SRC="--data-root $WD/data --env photon-rough --tier epic"
session() {   # name (the script is $WD/<name>.lab)
    "$LAB" --headless --no-stdin --size 960x540 $SRC --script "$WD/$1.lab" > "$WD/$1.log" 2>&1
    rc=$?
    grep -E '^LAB REST' "$WD/$1.log"
    [ $rc -eq 0 ] || { echo "FAIL: jahlab ($1) exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR|stillness:' "$WD/$1.log" | head -5; exit 1; }
}
DUMP=$WALK/data/stillness/dump-rough-walk
if [ "$PART" = walk ]; then
    S=0,1.6,2.4; T=0,0,-3; E=0,1.6,0.4
    printf 'row reflectionRoughnessCutoff 0\ncamera %s,%s\nrest 600\nstillness teleport:%s,%s/hold:1/walk:%s,%s:60 8 trail=60 dump=rough-walk out=rough-walk\nquit\n' \
        "$S" "$T" "$S" "$T" "$E" "$T" > "$WD/walk.lab"
    session walk
    # the dump pruned to the three frames the k rows read (each frame ~2.6 MB; the trail writes ~130)
    find "$DUMP" -type f ! -name 'rough-walk-t014.*' ! -name 'rough-walk-t028.*' ! -name 'rough-walk-t059.*' -delete
    n=$(ls "$DUMP" 2>/dev/null | wc -l)
    echo "gi.walk_rough.walk: the dump keeps $n files (frames 14 / 28 / 59)"
    [ "$n" -ge 12 ] || { echo "FAIL: the walk's dump lacks frames 14 / 28 / 59 ($n files)"; exit 1; }
    rm -rf "$WD/data/shots"
    echo "gi.walk_rough.walk: PASS"
    exit 0
fi
K=$PART
{
    printf 'row reflectionRoughnessCutoff 0\n'
    for k in $K $((K + 1)); do
        python3 -I -c "
s=(0.0,1.6,2.4); e=(0.0,1.6,0.4); k=$k/60.0
print('camera %.6f,%.6f,%.6f,0,0,-3' % tuple(a+(b-a)*k for a,b in zip(s,e)))"
        printf 'rest 600\nshot conv%d\n' "$k"
        if [ "$k" = "$K" ]; then
            printf 'arm gather.temporal 0\nframes 2\n'
            for j in $(seq 0 11); do printf 'frames 1\nshot raw%d_%d\n' "$k" "$j"; done
            printf 'arm gather.temporal 1\n'
        fi
    done
    printf 'quit\n'
} > "$WD/conv.lab"
session conv
python3 -I - "$WD/data" "$DUMP" "$K" <<'PY'
import os, sys
import numpy as np
from PIL import Image
root, dump, k = sys.argv[1], sys.argv[2], int(sys.argv[3])
W, H = 960, 540
fails = []
def png(name):
    return np.asarray(Image.open(os.path.join(root, 'shots', name + '.png')).convert('RGB')).astype(np.float64)
base = os.path.join(dump, 'rough-walk-t%03d' % k)
need = [base + x for x in ('.rgb', '.cls', '.ghc', '.gsize')]
missing = [p for p in need if not os.path.exists(p)]
if missing:
    print('FAIL: walk frame %d: missing %s (gi.walk_rough.walk writes them)' % (k, ', '.join(os.path.basename(p) for p in missing)))
    sys.exit(1)
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
    print('FAIL: walk frame %d: %d rough pixels (< 50,000): the fixture moved' % (k, rough.sum())); sys.exit(1)
breaks = rough & (lhs > (N - 1.0) * step + 3.0 * se + 1.0)
share = breaks.sum() / rough.sum()
print('walk frame %d: rough %d px, N median %.0f, sigma median %.2f codes, SE_walk median %.2f (p90 %.2f); '
      '|walk - conv| mean %.2f p99 %.1f; breaking %.2f %% (bar 1 %%)' % (k, rough.sum(), np.median(N[rough]),
      np.median(sigma[rough]), np.median(se[rough]), np.percentile(se[rough], 90), lhs[rough].mean(),
      np.percentile(lhs[rough], 99), 100 * share))
print('LAW k%d.breaking %.4f %% <= 1' % (k, 100 * share))
print('LAW k%d.lag_mean %.4f codes' % (k, lhs[rough].mean()))
print('LAW k%d.lag_p99 %.4f codes' % (k, np.percentile(lhs[rough], 99)))
if share > 0.01:
    fails.append('walk frame %d: %.2f %% of the rough pixels lag their converged picture past their memory and noise'
                 % (k, 100 * share))
for f in fails: print('FAIL: ' + f)
sys.exit(1 if fails else 0)
PY
rc=$?
rm -rf "$WD/data/shots"
rm -f "$DUMP/rough-walk-t$(printf '%03d' "$K")."*   # (this k's frame read: the walk row writes it afresh)
echo "gi.walk_rough.k$K: $([ $rc -eq 0 ] && echo PASS || echo FAIL)"
exit $rc
