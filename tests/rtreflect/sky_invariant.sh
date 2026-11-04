#!/bin/bash
# rtreflect.sky_invariant (PHOTON-7-9A, the lead's ruling R6 + (A)) — A SEALED ROOM IS THE SAME UNDER ANY SKY.
# jahlab on photon-sealed (photon-glass's fixture - the glossy panes, the pillars, the mirror, the still mover - in the
# CLOSED shell: roomShell without its opening, the lamp inside; src/lab/envs/photon_room.cpp), 960x540, the walk's view.
# THE LAW (physics, CODING_RULES §1 L9: what blocks blocks): no light path joins the sky - nor the sun, which the walls
# block as well, and which the second sky's rayleigh dial recolours - to any inside surface but through 0.3 m of matte
# wall, so every inside pixel is a function of the room and the lamp alone: TWO SKIES GIVE EXACT EQUAL CODES (a law in
# codes compares exact codes, CODING_RULES §6). The second sky: `sky brightness 4` (the sky's light x 4: its pixels, the
# Sky Light, reflections, the in-scatter) and `sky rayleigh 3` (its colour, and the sun's): any leak scales with either.
# It is the proof of the bound cube's open-share sites (spikes/photon-7-9a/cube-fallback/CUBE_LERP_SITES.txt (1) the
# roughness cutoff, (7) the blended passes): the cube may light only what the world's rays let through.
# TWO LAWS, TWO PARTS (one row per proof):
#   first  frames k1..k8 of a NEW scene's first frames (the voxels building from nothing): exact codes at each k - a red
#          here and a green hold is the engine's finding "an unbuilt cell is unknown, not open" (the lead declares it);
#          WITH ITS CONTROL: the same two skies in photon-glass (its 1 m opening lets the sky in) must DIFFER at k8 - the
#          lever reaches the picture, or the law proves nothing.
#   hold   at the hold (`rest 1200 held`, the engine's own predicate: every reader held): exact codes.
# Each session declares its route (`routes gather reflection store`: the lab's view is StillPicture, GI on).
# usage: sky_invariant.sh <jahlab> <work dir> first|hold
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}; PART=${3:?first or hold}
case "$PART" in first|hold) ;; *) echo "FAIL: the part is first or hold, not '$PART'"; exit 2 ;; esac
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
V=-1.2,1.8,3.4,0,0.5,-3
SKY2='sky brightness 4
sky rayleigh 3'
session() {   # <name> <env> <sky lines or ''> <body>
    { [ -n "$3" ] && printf '%s\n' "$3"; printf 'routes gather reflection store\ncamera %s\n%s\nquit\n' "$V" "$4"; } > "$WD/$1.lab"
    "$LAB" --headless --no-stdin --size 960x540 --data-root "$WD/$1" --env "$2" --script "$WD/$1.lab" > "$WD/$1.log" 2>&1
    local rc=$?
    grep -E '^LAB (ROUTES|REST|END|FAILED|REFUSED)' "$WD/$1.log" | sed "s/^/  $1: /"
    [ $rc -eq 0 ] || { echo "FAIL: jahlab ($1) exited $rc"; tail -5 "$WD/$1.log"; exit 1; }
}
if [ "$PART" = first ]; then
    K=""; for k in 1 2 3 4 5 6 7 8; do K="$K"$'frames 1\n'"shot k$k"$'\n'; done
    session sealed1 photon-sealed "" "$K"
    session sealed2 photon-sealed "$SKY2" "$K"
    session glass1 photon-glass "" "$K"
    session glass2 photon-glass "$SKY2" "$K"
else
    H=$'rest 1200 held\nframes 2\nshot hold'
    session sealed1 photon-sealed "" "$H"
    session sealed2 photon-sealed "$SKY2" "$H"
fi
python3 -I - "$WD" "$PART" <<'PY'
import os, sys
wd, part = sys.argv[1], sys.argv[2]
fails = []
try:
    from PIL import Image
except ImportError:
    print("FAIL: PIL is missing"); sys.exit(1)
def img(s, n):
    p = os.path.join(wd, s, "shots", n + ".png")
    return Image.open(p).convert("RGB").tobytes() if os.path.exists(p) else None
def diff(a, b):
    d = [max(abs(a[i] - b[i]), abs(a[i + 1] - b[i + 1]), abs(a[i + 2] - b[i + 2])) for i in range(0, len(a), 3)]
    return sum(1 for v in d if v), max(d), len(d)
shots = ["k%d" % k for k in range(1, 9)] if part == "first" else ["hold"]
for n in shots:
    a, b = img("sealed1", n), img("sealed2", n)
    if a is None or b is None or len(a) != len(b):
        fails.append("the sealed room's %s shots are missing or differ in size" % n); continue
    px, mx, tot = diff(a, b)
    print("(%s) the sealed room, two skies, %s: %d of %d px differ, max %d codes (the law: 0)" % (part, n, px, tot, mx))
    if px: fails.append("(%s) a sealed room is the same under any sky: %s differs on %d px, max %d" % (part, n, px, mx))
if part == "first":
    a, b = img("glass1", "k8"), img("glass2", "k8")
    if a is None or b is None or len(a) != len(b):
        fails.append("the control's k8 shots are missing")
    else:
        px, mx, tot = diff(a, b)
        print("(control) photon-glass (its 1 m opening), two skies, k8: %d of %d px differ, max %d codes (must differ)" % (px, tot, mx))
        if not px: fails.append("(control) the sky change does not reach photon-glass's picture: the lever proves nothing")
for f in fails: print("FAIL:", f)
print("rtreflect.sky_invariant.%s: %s" % (part, "FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
PY
