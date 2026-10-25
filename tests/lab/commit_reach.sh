#!/bin/bash
# lab.commit_reach (PHOTON-7's walk hitch, 2) — A SCROLL COMMIT COMPUTES ITS SLAB AND THE VOXELS WHOSE MARCH TOWARD A LIGHT
# MEETS A MOVED FACE, NOTHING ELSE. An inner cascade's commit used to compute BOTH moved faces' bands (each the visibility
# march's reach: 11 / 15 / 17 cells for c0 / c1 / c2) across the whole cascade, light-blind: 3-29x its slab on the Bistro
# walks, 99.86-100 % of it unchanged. Now every cascade's own commit sweeps its moved faces against each light
# (innerRemarchRegion): toward ONE directional light a kept voxel can march into only one of an axis's two faces (the slab
# if the sun stands beyond it, else the trailing face), and no farther than the reach.
# jahlab on photon-court (its sun and sky, no lamp) at 960x540 (Epic), a straight walk along -z (one moved axis), under
# `monitor` with each commit's counts: LAWS -
#   (1) the walk committed inner cascades (c0 at least);
#   (2) ONE FACE: every inner commit's cells computed past its slab are at most one face's band - the reach, the sweep's
#       2-cell pad and the injection's 4-cell groups at both ends (3 cells each): (reach_c + 8) planes of 128 x 128 -
#       where the light-blind rule computed two (c0 26 planes, c1 32-34 before; a fact of the geometry, never a timing);
#   (3) NEVER THE WHOLE CASCADE: on photon-glass (its lamp, sun and sky) walked DIAGONALLY (three moved axes: six faces x
#       three lights = 18 regions + 3 slabs, past the injection's 16 boxes) no inner commit computes 90 % of its cascade -
#       the regions FOLD pair by pair (foldInjectBoxes), never into their union (which covered the whole box: Bistro
#       Interior's c0 and c1 commits computed 100 % of 128^3 for a 1.4 % slab).
# usage: commit_reach.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
cat > "$WD/w.lab" <<S
lights
camera 0,3,18,0,3,-6
frames 30
monitor 150 $WD/walk.txt 0,3,2,0,3,-22
quit
S
cat > "$WD/d.lab" <<S
lights
camera 0,1.8,3.4,0,0.5,-3
frames 30
monitor 120 $WD/diag.txt 3,3.4,-3.0,3,2.1,-9.4
quit
S
"$LAB" --headless --no-stdin --size 960x540 --data-root "$WD/data" --env photon-court --script "$WD/w.lab" > "$WD/w.log" 2>&1
rc=$?
[ $rc -eq 0 ] || { echo "FAIL: jahlab exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR' "$WD/w.log" | head; tail -5 "$WD/w.log"; exit 1; }
"$LAB" --headless --no-stdin --size 960x540 --data-root "$WD/gdata" --env photon-glass --script "$WD/d.lab" > "$WD/d.log" 2>&1
rc=$?
[ $rc -eq 0 ] || { echo "FAIL: jahlab (diagonal) exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR' "$WD/d.log" | head; tail -5 "$WD/d.log"; exit 1; }
grep -E '^LAB LIGHT' "$WD/w.log" "$WD/d.log"
python3 -I - "$WD" <<'PY'
import os, re, sys
wd = sys.argv[1]
REACH = {0: 11, 1: 15, 2: 17}   # the march's reach in each inner cascade's cells (shell end + a cell of it and the next)
PLANE = 128 * 128
fails = []
def commits_of(name):
    out = []
    p = os.path.join(wd, name)
    if not os.path.exists(p):
        fails.append('no monitor file %s' % name); return out
    for l in open(p):
        if not l.startswith('F '): continue
        head, _, rest = l.partition('|')
        kv = dict(re.findall(r'(\w+) (-?[\d.]+|nan)', head))
        t = dict((k, float(v)) for k, v in re.findall(r' (\S+)=(-?[\d.eE+-]+)', rest))
        for c in REACH:
            slab, inj = t.get('count:vct.commit_slab%d' % c, 0.0), t.get('count:vct.commit_inject%d' % c, 0.0)
            if slab or inj:
                out.append((kv['F'], c, int(slab), int(inj)))
    return out
commits = commits_of('walk.txt')
for f, c, slab, inj in commits:
    band = inj - slab
    bound = (REACH[c] + 8) * PLANE
    print('F%s c%d: slab %d (%.1f planes), computed %d, past the slab %d (%.1f planes; one face <= %d planes)' % (
        f, c, slab, slab / PLANE, inj, band, band / PLANE, REACH[c] + 8))
    if band > bound:
        fails.append('(2) F%s c%d computed %.1f planes past its slab: more than one face\'s band (%d)' % (f, c, band / PLANE, REACH[c] + 8))
if not any(c == 0 for _, c, _, _ in commits): fails.append('(1) the walk committed no cascade 0')
diag = commits_of('diag.txt')
for f, c, slab, inj in diag:
    share = inj / float(128 ** 3)
    print('diagonal F%s c%d: slab %d, computed %d (%.1f %% of the cascade)' % (f, c, slab, inj, 100.0 * share))
    if share >= 0.9:
        fails.append('(3) diagonal F%s c%d computed %.1f %% of its cascade for a %.1f %% slab' % (
            f, c, 100.0 * share, 100.0 * slab / 128 ** 3))
if not diag: fails.append('(3) the diagonal walk committed no inner cascade')
for x in fails: print('FAIL:', x)
print('lab.commit_reach:', 'FAIL' if fails else 'PASS')
sys.exit(1 if fails else 0)
PY
