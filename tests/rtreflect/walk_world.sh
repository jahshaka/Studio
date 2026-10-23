#!/bin/bash
# rtreflect.walk_world (PHOTON-7 stage 6 commit 7; the lead's ruling: the walk bar has two owners; two labelled arms
# after REFLECT-MOVING-1) — THE REFLECTION'S WALKING BAR: gi.walk's glass walk (photon-glass, -1.2,1.8,3.4 ->
# 1.2,1.8,1.0 looking at 0,0.5,-3, 60 frames from a converged hold) with THE WORLD HELD - the arm gi.freeze = 2 from
# the walk's first frame (the stillness door's freeze=world): no GI cache works and the gather keeps its histories, so
# no hit radiance can change, while the reflections run live. M1 (gi.walk's: walk_m1.py) of that walk against the fully
# frozen walk is the reflection's own estimator and denoiser: GLOSSY AND MIRROR M1 p99 <= 2 codes, share over one code
# <= 1 %. TWO ARMS, each its mode set, read back from the door and printed (the mode label), each its owner:
#   (i)  RAYS WITH THE HITS FLATTENED (reflect.flatHits) - THE REFLECTION'S OWN BAR: the estimator and its denoiser
#        alone, the hit caches out. OWNER: PHOTON-7 stage 6. MEASURED at the stage's tip (irisgl 6fd0f847): glossy
#        2.90 / 2.83 % - RED (before the moving variance: 3.24 / 4.66 %); the lead rules its owner.
#   (ii) BOTH ON REAL HITS - REFLECT-MOVING-1's BOTH HALF (the screen source while moving): red until that work, owed
#        before PHOTON-7's master check on the merged tree. OWNER: REFLECT-MOVING-1. The base before the moving history
#        kept its variance (irisgl 52b6fe90): 2.77 / 5.11 %; with it: 2.41 / 4.59 %.
# (The unfrozen walk - the world's half - is gi.walk's.) The spatial filter's GPU time over each world-held walk printed.
# jahlab at Epic, 960x540; five sessions: one prime for both modes and per arm the frozen and the world-held walk
# (12 + 4 x 570 frames).
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
printf 'reflections rays\ncamera %s,%s\nframes 3\ncamera %s,%s\nframes 3\nreflections both\ncamera %s,%s\nframes 3\ncamera %s,%s\nframes 3\nquit\n' \
    "$S" "$T" "$E" "$T" "$S" "$T" "$E" "$T" > "$WD/prime.lab"
session prime
# (the settle: 500 frames - the holds at S measured 357 rest frames in Rays with the hits flattened, 248 in Both)
for arm in raysflat:rays both:both; do
    name=${arm%%:*}; mode=${arm#*:}
    arms="reflections $mode\n"; [ $name = raysflat ] && arms="${arms}arm reflect.flatHits 1\n"
    for walk in frozen world; do
        f=" freeze"; [ $walk = world ] && f=" freeze=world"
        printf "$arms"'camera %s,%s\nframes 500\nstillness teleport:%s,%s/hold:1/walk:%s,%s:60 8 trail=60 dump=%s-%s%s out=%s-%s\nquit\n' \
            "$S" "$T" "$S" "$T" "$E" "$T" "$name" "$walk" "$f" "$name" "$walk" > "$WD/$name-$walk.lab"
        session $name-$walk
    done
done
ROOT="$WD/data/stillness"
fails=0
for arm in raysflat:rays both:both; do
    name=${arm%%:*}; mode=${arm#*:}
    python3 -I "$HERE/../support/reflect_sources.py" tsv "$ROOT/$name-frozen.tsv" "$ROOT/$name-world.tsv" -- $mode || fails=1
    python3 -I "$M1" "$ROOT" ${name}w $name-world $name-frozen > "$WD/$name-m1.out" 2>&1 || true
    grep -E 'M1 (all|glossy|mirror)|\(L2\)|\(L3\)' "$WD/$name-m1.out" | sed "s/^/  $name (world held): /"
done
python3 -I - "$ROOT" <<'PY'
import csv, json, os, sys
root = sys.argv[1]
fails = 0
for name, label in (('raysflat', "(i) THE REFLECTION'S OWN BAR (Rays, the hits flattened; stage 6's)"),
                    ('both', "(ii) REFLECT-MOVING-1's BOTH HALF (Both, real hits; red until that work)")):
    path = os.path.join(root, '%sw-walk.json' % name)
    if not os.path.exists(path):
        print('FAIL: %s: no M1 (%s)' % (label, path)); fails += 1; continue
    m1 = list(json.load(open(path))['parts'].values())[0]['m1']
    for cls in ('glossy', 'mirror'):
        v = m1.get(cls)
        if v is None:
            print('FAIL: %s: the world-held walk counted no %s pixel' % (label, cls)); fails += 1; continue
        ok = v['p99'] <= 2.0 and v['over1'] <= 0.01
        print('%s: %s %s M1 p99 %.2f (bar 2), share over one code %.2f %% (bar 1)' % (
              'ok' if ok else 'FAIL', label, cls, v['p99'], 100.0 * v['over1']))
        fails += 0 if ok else 1
    # the reflection's GPU passes over the world-held walk's moving frames (the door's gpu_rows: name:ms,...)
    ms = {}
    for x in csv.DictReader(open(os.path.join(root, '%s-world.tsv' % name)), delimiter='\t'):
        if x.get('window') != 'walk-move' or x.get('gpu_rows', '-') == '-': continue
        for tok in x['gpu_rows'].split(','):
            if ':' not in tok: continue
            n, val = tok.rsplit(':', 1)
            if n.startswith('rq.reflect.'): ms.setdefault(n, []).append(float(val))
    print('  %s: the reflection\'s GPU passes over the world-held walk (mean ms): ' % name +
          ', '.join('%s %.3f (%d frames)' % (n, sum(v) / len(v), len(v)) for n, v in sorted(ms.items())))
sys.exit(1 if fails else 0)
PY
[ $? -eq 0 ] || fails=1
rm -rf "$ROOT/dump-"*
echo "rtreflect.walk_world: $([ $fails -eq 0 ] && echo PASS || echo FAIL)"
exit $fails
