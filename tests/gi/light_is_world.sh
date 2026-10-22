#!/bin/bash
# gi.light_is_world (PHOTON-7 stage 1, L1: LIGHT IS A WORLD FUNCTION; SPECS/PHOTON_7_SPEC.md stage 1's acceptance) — THE
# VALUE OF A CACHE AT A WORLD POINT IS INDEPENDENT OF WHERE ITS CASCADE'S BOX HAPPENS TO BE. jahlab on two of its
# environments (src/lab/envs/photon_shells.cpp says their geometry and its derivation), the lab's GI doors: `pin` (the
# placement pin, then a forced whole rebuild - or the scroll path) and `voxels` (one cascade's level-0 DIRECT volume by
# WORLD cell, read once the chain owes no voxel work). light_is_world.py judges, per arm:
#   (opaque)     photon-shells, cascade 0 at (0,1.6,0) and at (0,1.6,-1.5) (its box's +Z face then 3.44 m: cuts occluder A's
#                crossing for the floor at z 2.6-2.8), every other cascade pinned at (0,1.6,0): the direct term of every
#                world voxel band-free at both placements equal within one RGBA16F step;
#   (outer)      the same for cascade 1 at (0,1.6,0) and (0,1.6,-14.2) (its +Z face then -4.06 m: cuts occluder B's crossing),
#                cascade 2 and 3 pinned;
#   (partial)    the voxels under the cut-out plane's shadow - part of (opaque)'s set, printed apart;
#   (multiplier) cascade 0 at (0,1.6,0) and at (-2,1.6,2), whose box holds the range-1.5 m lamp of 8: the stored direct of the
#                voxels the lamp does not reach equal within one step (the per-box multiplier k = pi / D_max made it 8x);
#   (pinned walk) the eye walks 2.8 m off a pinned chain (past cascade 0's 2.66 m step: a scroll of an unpinned one):
#                no abort, cascade 0's store where the pin says, bit-equal;
#   (pinned scroll) the pin moves cascade 0 by 2.8 m through the SCROLL path: placed where the pin says, and its store
#                equal to a whole rebuild there within one step;
#   (roof)       photon-roof at Epic, High, Medium and Low: the direct term of every floor voxel of every cascade under the
#                plate (z < -3.2: the reference D_sun = 0) - 0 within one step.
# BAND-FREE (the shells' own law, SPECS/PHOTON_7_SPEC.md stage 1): a voxel's march over its own cascade's shell - D_k
# (8 cells of the cascade: Epic 0.625 / 1.875 / 5.625 m), the hand-over to the next coarser cell plane (two of its cells)
# and the face's offset in the voxel (one cell) - stays inside its box toward the sun at BOTH placements; inside the band
# the hand-over starts mid-cell and the voxel is the placement's (the stage's named residual), not compared.
# THE BASE (PHOTON-7 stage 1's door commit, before the shells): red on (opaque), (multiplier) and the roof at every tier;
# the session ends at (outer)'s pin in a Debug ABORT - the latent pin defect: cascade 1 pinned 14.2 m from the eye is
# "moved" by the eye's test, planScroll aims at the eye and the commit's recentre at the pin (PREFLIGHT_1_2 W2) - so
# (outer) and both pinned arms are red there; with the pin fixed, (outer) is the box cut's red until the shells.
# 960x540, Epic; two jahlab sessions. usage: light_is_world.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
HERE=$(cd "$(dirname "$0")" && pwd)
rm -rf "$WD"; mkdir -p "$WD/data"
cat > "$WD/shells.lab" <<'S'
camera 0,1.6,0,0,1.6,-5
frames 30
pin all 0,1.6,0
voxels 0 direct s-c0-p0.vox
voxels 1 direct s-c1-p0.vox
pin 0 0,1.6,-1.5
voxels 0 direct s-c0-p1.vox
pin 0 -2,1.6,2
voxels 0 direct s-c0-pm.vox
pin 0 0,1.6,0
pin 1 0,1.6,-14.2
voxels 1 direct s-c1-p1.vox
pin all 0,1.6,0
voxels 0 direct s-c0-p0b.vox
move 0,1.6,2.8,0,1.6,-2.2 40
voxels 0 direct s-c0-walked.vox
pin 0 0,1.6,2.8 scroll
voxels 0 direct s-c0-scroll.vox
pin 0 0,1.6,2.8
voxels 0 direct s-c0-whole.vox
quit
S
{
    echo "camera 0,1.6,-4,0,1.6,-9"
    echo "frames 30"
    for t in epic high medium low; do
        echo "tier $t"
        echo "frames 4"
        for c in 0 1 2 3; do
            [ "$t" = low ] && [ $c -gt 1 ] && continue
            echo "voxels $c direct r-$t-c$c.vox -9.5,-1.0,-9.5,9.5,1.0,-3.2"
        done
    done
    echo "quit"
} > "$WD/roof.lab"
fails=0
for run in shells:photon-shells roof:photon-roof; do
    name=${run%%:*}; env=${run#*:}
    "$LAB" --headless --no-stdin --size 960x540 --data-root "$WD/data" --env "$env" --script "$WD/$name.lab" \
        > "$WD/$name.log" 2>&1
    rc=$?
    grep -E '^LAB (VOXELS|PIN)' "$WD/$name.log" | cut -c1-220 | sed "s/^/  $name: /"
    if [ $rc -ne 0 ]; then
        echo "  $name: jahlab exited $rc"; grep -E 'LAB (REFUSED|FAILED)|Assertion|abort' "$WD/$name.log" | head -4
        fails=1
    fi
done
python3 -I "$HERE/light_is_world.py" "$WD/data" || fails=1
rm -f "$WD/data/"*.vox   # the dumps (~32 MB a whole cascade) once their numbers are printed
echo "gi.light_is_world: $([ $fails -eq 0 ] && echo PASS || echo FAIL)"
exit $fails
