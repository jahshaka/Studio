#!/bin/bash
# rtreflect.walk_world / rtreflect.walk_world_both (PHOTON-7 stage 6 commit 7; the lead's rulings: the walk bar has two
# owners, two labelled arms after REFLECT-MOVING-1, the measures of round 5, ledger §2849) — THE REFLECTION'S WALKING
# BARS: gi.walk_glass's walk (photon-glass, -1.2,1.8,3.4 -> 1.2,1.8,1.0 looking at 0,0.5,-3, 60 frames from a converged
# hold) with THE WORLD HELD - the arm gi.freeze = 2 from the walk's first frame (the stillness door's freeze=world): no
# GI cache works and the gather keeps its histories, so no hit radiance can change, while the reflections run live -
# twice, with the lobe draw's seed 0 and 7919, against the fully frozen walk (walk_noise.py, walk_m1.py's residual):
#   GLOSSY: THE NOISE MAP (rA - rB) / sqrt 2, p99 <= 2 codes and share over one code <= 1 %. Whole glossy M1 is printed
#     as information only - for a glossy surface it is the noise PLUS THE SIGNAL, the estimator correctly following the
#     view-dependent change: it rewards lag and counts a correct update as flicker (the lag is rtreflect.lag_law's).
#   THE MIRROR: whole M1, p99 <= 2 codes and share over one code <= 1 % (its estimator is the centre ray: no noise of
#     its own, every change a defect).
# ONE ARM A ROW, its mode set and read back from the door (the mode label), its owner named:
#   raysflat (rtreflect.walk_world): RAYS WITH THE HITS FLATTENED (reflect.flatHits) - the reflection's own bar, the
#     estimator and its denoiser alone, the hit caches out. OWNER: PHOTON-7 stage 6.
#   both (rtreflect.walk_world_both): BOTH ON REAL HITS - REFLECT-MOVING-1's Both half (the screen source while moving,
#     the hit caches; its slide and its lag are owed before PHOTON-7's master check on the merged tree). OWNER:
#     REFLECT-MOVING-1.
# MEASURED at the stage's tip: raysflat glossy noise 0.71 / 0.39 %, mirror 0.00 / 0.00 % (whole glossy M1 2.90 / 2.83 %);
# both 0.92 / 0.98 %, 0.00 / 0.00 % (2.41 / 4.59 %).
# (The split by arm, not by measure: the noise map needs the frozen walk the mirror's M1 reads, so each arm's three
# walks serve both measures. The unfrozen walk - the world's half - is gi.walk_glass's.) The spatial filter's GPU time over the
# world-held walk printed. jahlab at Epic, 960x540; four sessions: a prime and the frozen and two world-held walks
# (6 + 3 x 570 frames).
# usage: walk_world.sh <jahlab> <work dir> <raysflat|both>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}; ARM=${3:?arm}
case "$ARM" in raysflat) MODE=rays; ARMS='reflections rays\narm reflect.flatHits 1\n' ;;
               both) MODE=both; ARMS='reflections both\n' ;;
               *) echo "FAIL: the arm '$ARM' (raysflat|both)"; exit 1 ;; esac
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
printf "$ARMS"'camera %s,%s\nframes 3\ncamera %s,%s\nframes 3\nquit\n' "$S" "$T" "$E" "$T" > "$WD/prime.lab"
session prime
# (the settle: 500 frames - the holds at S measured 357 rest frames in Rays with the hits flattened, 248 in Both)
for walk in frozen worldA worldB; do
    f=" freeze=world"; [ $walk = frozen ] && f=" freeze"
    seed=0; [ $walk = worldB ] && seed=7919
    printf "$ARMS"'arm reflect.seedOffset %d\ncamera %s,%s\nframes 500\nstillness teleport:%s,%s/hold:1/walk:%s,%s:60 8 trail=60 dump=glass-%s%s out=glass-%s\nquit\n' \
        "$seed" "$S" "$T" "$S" "$T" "$E" "$T" "$walk" "$f" "$walk" > "$WD/glass-$walk.lab"
    session glass-$walk
done
ROOT="$WD/data/stillness"
fails=0
python3 -I "$HERE/../support/reflect_sources.py" tsv "$ROOT/glass-frozen.tsv" "$ROOT/glass-worldA.tsv" "$ROOT/glass-worldB.tsv" -- $MODE || fails=1
python3 -I "$HERE/walk_noise.py" "$ROOT" glass-worldA glass-worldB glass-frozen > "$WD/measures.json" || { cat "$WD/measures.json"; fails=1; }
python3 -I - "$ROOT" "$WD/measures.json" "$ARM" <<'PY'
import csv, json, os, sys
root, path, arm = sys.argv[1:4]
label = {'raysflat': "THE REFLECTION'S OWN BAR (Rays, the hits flattened; stage 6's)",
         'both': "REFLECT-MOVING-1's BOTH HALF (Both, real hits)"}[arm]
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
rm -rf "$ROOT/dump-"*
echo "rtreflect.walk_world ($ARM): $([ $fails -eq 0 ] && echo PASS || echo FAIL)"
exit $fails
