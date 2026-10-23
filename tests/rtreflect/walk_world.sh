#!/bin/bash
# rtreflect.walk_world (PHOTON-7 stage 6 commit 7; the lead's ruling: the walk bar has two owners) — THE REFLECTION'S
# OWN WALKING BAR: gi.walk's glass walk (photon-glass, -1.2,1.8,3.4 -> 1.2,1.8,1.0 looking at 0,0.5,-3, 60 frames from a
# converged hold) with THE WORLD HELD - the arm gi.freeze = 2 from the walk's first frame (the stillness door's
# freeze=world): no GI cache works and the gather keeps its histories, so no hit radiance can change, while the
# reflections run live. M1 (gi.walk's: walk_m1.py) of that walk against the fully frozen walk is the reflection's own
# estimator and denoiser alone, which must fit the whole budget by itself: GLOSSY AND MIRROR M1 p99 <= 2 codes, share
# over one code <= 1 %. The unfrozen walk's M1 is printed beside it (the merge judges the world's half: stages 1-5 on
# photon-7), and the spatial filter's GPU time over the world-held walk (the stillness door's per-frame rows).
# jahlab at Epic, 960x540; four sessions: a prime, the live, the frozen and the world-held walk (7 + 3 x 710 frames).
# usage: walk_world.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
HERE=$(cd "$(dirname "$0")" && pwd)
M1="$HERE/../gi/walk_m1.py"
rm -rf "$WD"; mkdir -p "$WD"
SRC="--data-root $WD/data --env photon-glass"
session() {   # name (the script is $WD/<name>.lab)
    "$LAB" --headless --no-stdin --size 960x540 $SRC --script "$WD/$1.lab" > "$WD/$1.log" 2>&1
    rc=$?
    [ $rc -eq 0 ] || { echo "FAIL: jahlab ($1) exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR|stillness:' "$WD/$1.log" | head -5; exit 1; }
}
S=-1.2,1.8,3.4; T=0,0.5,-3; E=1.2,1.8,1.0
printf 'camera %s,%s\nframes 3\ncamera %s,%s\nframes 3\nquit\n' "$S" "$T" "$E" "$T" > "$WD/prime.lab"
session prime
for arm in live frozen world; do
    f=""; [ $arm = frozen ] && f=" freeze"; [ $arm = world ] && f=" freeze=world"
    printf 'camera %s,%s\nframes 640\nstillness teleport:%s,%s/hold:1/walk:%s,%s:60 8 trail=60 dump=glass-%s%s out=glass-%s\nquit\n' \
        "$S" "$T" "$S" "$T" "$E" "$T" "$arm" "$f" "$arm" > "$WD/glass-$arm.lab"
    session glass-$arm
done
ROOT="$WD/data/stillness"
python3 -I "$M1" "$ROOT" glass glass-live glass-frozen > "$WD/live-m1.out" 2>&1; rcl=$?
python3 -I "$M1" "$ROOT" glassworld glass-world glass-frozen > "$WD/world-m1.out" 2>&1; rcw=$?
grep -E 'M1 (all|glossy|mirror)|\(L2\)' "$WD/live-m1.out" | sed 's/^/  unfrozen (the merge judges it): /'
grep -E 'M1 (all|glossy|mirror)|\(L2\)|\(L3\)' "$WD/world-m1.out" | sed 's/^/  world held: /'
grep '^FAIL' "$WD/live-m1.out" "$WD/world-m1.out"
python3 -I - "$ROOT" <<'PY'
import csv, json, os, sys
root = sys.argv[1]
r = json.load(open(os.path.join(root, 'glassworld-walk.json')))['parts']
m1 = list(r.values())[0]['m1']
fails = 0
for cls in ('glossy', 'mirror'):
    v = m1.get(cls)
    if v is None:
        print('FAIL: the world-held walk counted no %s pixel' % cls); fails += 1; continue
    ok = v['p99'] <= 2.0 and v['over1'] <= 0.01
    print('%s: THE REFLECTION\'S OWN BAR (world held) %s M1 p99 %.2f (bar 2), share over one code %.2f %% (bar 1)' % (
          'ok' if ok else 'FAIL', cls, v['p99'], 100.0 * v['over1']))
    fails += 0 if ok else 1
# the spatial filter's GPU time over the world-held walk's moving frames (the door's gpu_rows: name:ms,...)
ms = {}
for x in csv.DictReader(open(os.path.join(root, 'glass-world.tsv')), delimiter='\t'):
    if x.get('window') != 'walk-move' or x.get('gpu_rows', '-') == '-': continue
    for tok in x['gpu_rows'].split(','):
        if ':' not in tok: continue
        n, v = tok.rsplit(':', 1)
        if n.startswith('rq.reflect.'): ms.setdefault(n, []).append(float(v))
print('  the reflection\'s GPU passes over the world-held walk (mean ms): ' +
      ', '.join('%s %.3f (%d frames)' % (n, sum(v) / len(v), len(v)) for n, v in sorted(ms.items())))
sys.exit(1 if fails else 0)
PY
rcg=$?
rm -rf "$ROOT/dump-"*
fails=0; [ $rcl -eq 0 ] && [ $rcw -eq 0 ] && [ $rcg -eq 0 ] || fails=1
echo "rtreflect.walk_world: $([ $fails -eq 0 ] && echo PASS || echo FAIL)"
exit $fails
