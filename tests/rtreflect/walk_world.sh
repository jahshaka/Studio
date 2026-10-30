#!/bin/bash
# rtreflect.walk_world / rtreflect.walk_world_rays (PHOTON-7 stage 6 commit 7; the lead's rulings: the walk bar has two
# arms, the measures of round 5, ledger §2849; PHOTON-7-9A: the Both arm rewritten as Rays on real hits) — THE REFLECTION'S WALKING
# BARS: gi.walk_glass's walk (photon-glass, -1.2,1.8,3.4 -> 1.2,1.8,1.0 looking at 0,0.5,-3, 60 frames from a converged
# hold) with THE WORLD HELD - the arm gi.freeze = 2 from the walk's first frame (the stillness door's freeze=world): no
# GI cache works and the gather keeps its histories, so no hit radiance can change, while the reflections run live -
# twice, with the lobe draw's seed 0 and 7919, against the fully frozen walk (walk_noise.py, walk_m1.py's residual):
#   GLOSSY: THE NOISE MAP (rA - rB) / sqrt 2, p99 <= 2 codes and share over one code <= 1 %. Whole glossy M1 is printed
#     as information only - for a glossy surface it is the noise PLUS THE SIGNAL, the estimator correctly following the
#     view-dependent change: it rewards lag and counts a correct update as flicker (the lag is rtreflect.lag_law's).
#   THE MIRROR: whole M1, p99 <= 2 codes and share over one code <= 1 % (its estimator is the centre ray: no noise of
#     its own, every change a defect).
# ONE ARM A ROW (the reflection has no mode since PHOTON-7-9A: both arms are the rays):
#   raysflat (rtreflect.walk_world): THE HITS FLATTENED (reflect.flatHits) - the reflection's own bar, the estimator and
#     its denoiser alone, the hit caches out. OWNER: PHOTON-7 stage 6.
#   rays (rtreflect.walk_world_rays): REAL HITS - the product's own reflection walking: the lobe's samples read the
#     cards, the voxels and the decode at their hits, so the noise map holds the hit radiance's spread over the lobe
#     too. (It was the BOTH arm, REFLECT-MOVING-1's, until PHOTON-7-9A deleted Both - its declared red, 1.16 % from a
#     held start at photon-7 188268ffc, was the Both estimator's and closed with it.)
# MEASURED at stage 6's tip: raysflat glossy noise 0.71 / 0.39 %, mirror 0.00 / 0.00 % (whole glossy M1 2.90 / 2.83 %);
# from a held start at photon-7 188268ffc 0.84 %, mirror 0.00.
# (The split by arm, not by measure: the noise map needs the frozen walk the mirror's M1 reads, so each arm's three
# walks serve both measures. The unfrozen walk - the world's half - is gi.walk_glass's.) The spatial filter's GPU time over the
# world-held walk printed. jahlab at Epic, 960x540; four sessions: a prime and the frozen and two world-held walks
# (6 + 3 x 570 frames).
# THE SETTLE (HARNESS-3 (f)): each walk starts from `rest 1200 held` - the GI at rest AND every reader HELD (the door's
# freeze precondition), the engine's own predicate; it was 500 frames (the holds at S measured 357 rest frames with
# the hits flattened under the old K_max): since 11-2 (8) the glass holds ~1,000 rest frames in and the 500
# read "not converged at the walk's start". THE SPLIT BY PROOF where the three walks no longer fit one row: the optional
# PART runs one walk on the arm's work dir - frozen (the prime first; FIXTURES_SETUP), worldA, then worldB with the
# measures; no PART = the three in one row.
# usage: walk_world.sh <jahlab> <work dir> <raysflat|rays> [frozen|worldA|worldB]
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}; ARM=${3:?arm}; PART=${4:-all}
case "$ARM" in raysflat) ARMS='arm reflect.flatHits 1\n' ;;
               rays) ARMS='' ;;
               *) echo "FAIL: the arm '$ARM' (raysflat|rays)"; exit 1 ;; esac
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
HERE=$(cd "$(dirname "$0")" && pwd)
case "$PART" in all|frozen) rm -rf "$WD"; mkdir -p "$WD" ;; worldA|worldB) mkdir -p "$WD" ;;
               *) echo "FAIL: the part '$PART' (frozen|worldA|worldB)"; exit 1 ;; esac
SRC="--data-root $WD/data --env photon-glass"
session() {   # name (the script is $WD/<name>.lab)
    "$LAB" --headless --no-stdin --size 960x540 $SRC --script "$WD/$1.lab" > "$WD/$1.log" 2>&1
    rc=$?
    grep -E '^LAB REST' "$WD/$1.log" | sed "s/^/  $1: /"
    [ $rc -eq 0 ] || { echo "FAIL: jahlab ($1) exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR|stillness:' "$WD/$1.log" | head -5; exit 1; }
}
S=-1.2,1.8,3.4; T=0,0.5,-3; E=1.2,1.8,1.0
printf "$ARMS"'camera %s,%s\nframes 3\ncamera %s,%s\nframes 3\nquit\n' "$S" "$T" "$E" "$T" > "$WD/prime.lab"
case "$PART" in all) WALKS="frozen worldA worldB" ;; *) WALKS=$PART ;; esac
case "$PART" in all|frozen) session prime ;; esac
# (the parts are rows with their own homes: they boot on ONE cache's bytes - AB G10, a grown cache draws other variants;
# measured on gi.walk_glass's chain: 2,712 px apart at the hold without it, 0 with it - the frozen part keeps the primed
# cache, the world parts restore it)
if [ -n "${JAHSHAKA_SHADER_CACHE_DIR:-}" ]; then
    case "$PART" in
    frozen) rm -rf "$WD/cache.snap"; cp -a "$JAHSHAKA_SHADER_CACHE_DIR" "$WD/cache.snap" ;;
    worldA|worldB) [ -d "$WD/cache.snap" ] || { echo "FAIL: the frozen part's cache snapshot is missing"; exit 1; }
                   rm -rf "$JAHSHAKA_SHADER_CACHE_DIR"; cp -a "$WD/cache.snap" "$JAHSHAKA_SHADER_CACHE_DIR" ;;
    esac
fi
# EACH ARM WALKS FROM ITS OWN HOLD (`rest 1200 held`): measured once with the world arms replaying the frozen walk's count
# instead (rest + 4, as gi.walk_glass's chain does - its L2 needs byte-equal starts, these measures do not): the glossy
# noise map read the same 1.16 % and the mirror's M1 went from p99 0.01 to 10.55 codes (18.5 % over one) - a world arm
# frozen where its own readers had not chosen to stop. The holds differ by seed (in Both, deleted: seed 0 828, seed 7919 798 frames).
for walk in $WALKS; do
    f=" freeze=world"; [ $walk = frozen ] && f=" freeze"
    seed=0; [ $walk = worldB ] && seed=7919
    printf "$ARMS"'arm reflect.seedOffset %d\ncamera %s,%s\nrest 1200 held\nstillness teleport:%s,%s/hold:1/walk:%s,%s:60 8 trail=60 dump=glass-%s%s out=glass-%s\nquit\n' \
        "$seed" "$S" "$T" "$S" "$T" "$E" "$T" "$walk" "$f" "$walk" > "$WD/glass-$walk.lab"
    session glass-$walk
done
ROOT="$WD/data/stillness"
case "$PART" in frozen|worldA) echo "rtreflect.walk_world ($ARM) $PART: PASS"; exit 0 ;; esac
for w in frozen worldA; do [ -f "$ROOT/glass-$w.tsv" ] || { echo "FAIL: no $w walk in $WD (its part writes it)"; exit 1; }; done
fails=0
python3 -I "$HERE/walk_noise.py" "$ROOT" glass-worldA glass-worldB glass-frozen > "$WD/measures.json" || { cat "$WD/measures.json"; fails=1; }
python3 -I - "$ROOT" "$WD/measures.json" "$ARM" <<'PY'
import csv, json, os, sys
root, path, arm = sys.argv[1:4]
label = {'raysflat': "THE REFLECTION'S OWN BAR (the hits flattened; stage 6's)",
         'rays': "THE PRODUCT'S WALKING REFLECTION (real hits; PHOTON-7-9A)"}[arm]
fails = 0
try:
    m = json.load(open(path))
except ValueError:
    print('FAIL: %s: no measures' % label); sys.exit(1)
g, mi = m.get('glossy'), m.get('mirror')
if g is None or mi is None:
    print('FAIL: %s: the walks counted no %s pixel' % (label, 'glossy' if g is None else 'mirror')); sys.exit(1)
for what, p99, over in (('glossy NOISE MAP (rA - rB) / sqrt 2', g['noise_p99'], g['noise_over1']),
                        ('mirror whole M1', mi['m1_p99'], mi['m1_over1'])):
    ok = p99 <= 2.0 and over <= 0.01
    print('%s: %s %s p99 %.2f (bar 2), share over one code %.2f %% (bar 1) - %d pairs' % (
          'ok' if ok else 'FAIL', label, what, p99, 100.0 * over, m['pairs']))
    fails += 0 if ok else 1
print('  (information, no bar: whole glossy M1 p99 %.2f, share over one code %.2f %% - the noise and the signal)' % (
      g['m1_p99'], 100.0 * g['m1_over1']))
ms = {}
for x in csv.DictReader(open(os.path.join(root, 'glass-worldA.tsv')), delimiter='\t'):
    if x.get('window') != 'walk-move' or x.get('gpu_rows', '-') == '-': continue
    for tok in x['gpu_rows'].split(','):
        if ':' not in tok: continue
        n, val = tok.rsplit(':', 1)
        if n.startswith('rq.reflect.'): ms.setdefault(n, []).append(float(val))
print('  the reflection\'s GPU passes over the world-held walk (mean ms): ' +
      ', '.join('%s %.3f (%d frames)' % (n, sum(v) / len(v), len(v)) for n, v in sorted(ms.items())))
sys.exit(1 if fails else 0)
PY
[ $? -eq 0 ] || fails=1
rm -rf "$ROOT/dump-"* "$WD/cache.snap"
echo "rtreflect.walk_world ($ARM): $([ $fails -eq 0 ] && echo PASS || echo FAIL)"
exit $fails
