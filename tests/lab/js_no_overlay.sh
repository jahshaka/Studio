#!/bin/bash
# lab.js_no_overlay (LAB-SCRIPTS-1, the audit's F3) - A SCRIPTED FRAME NEVER CARRIES THE LAB's OVERLAY (the trap:
# sky-particles-overlay-view.md, "the overlay is geometry": its quads are submitted triangles, and a shot of the script
# would carry the lab's text): js_cube.js run twice in `jahlab --headless` on an empty world (`--js` of a one-check
# script), with the lab's overlay
# OFF, then ON. The lab hides its overlay while a script holds the view's frames and pushes it again after.
# THE LAW: the lab's shot right after each run (the script's last frame, its bytes) is THE SAME PICTURE, every byte.
# CONTROL: the lab's OWN next frame differs between the two (the overlay is on, and pushed again after the run).
# usage: js_no_overlay.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
HERE=$(cd "$(dirname "$0")" && pwd)
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; echo "lab.js_no_overlay: FAILED (1)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
printf '%s\n' 'host.check({ok: true, what: "the lab world is the empty one"});' > "$WD/empty.js"
printf 'js load %s\njs run\nshot run\nframes 1\nshot lab\nquit\n' "$HERE/js_cube.js" > "$WD/off.lab"
{ echo "overlay on"; cat "$WD/off.lab"; } > "$WD/on.lab"
rcs=
for w in off on; do
    "$LAB" --headless --no-stdin --size 320x180 --data-root "$WD/$w" --js "$WD/empty.js" --script "$WD/$w.lab" > "$WD/$w.log" 2>&1
    rcs="$rcs $?"
    grep -E '^LAB (EMPTY|OK|JS END|SHOT|SCRIPT|END)' "$WD/$w.log" | sed "s/^/$w: /"
done
python3 -I - "$WD" $rcs <<'PY'
import os, sys
from PIL import Image
wd, rcs = sys.argv[1], sys.argv[2:]
fails = []
def pic(w, name):
    p = os.path.join(wd, w, "shots", name + ".png")
    return Image.open(p).convert("RGB") if os.path.isfile(p) else None
def differing(a, b):
    if a.size != b.size:
        return -1
    x, y = a.tobytes(), b.tobytes()
    return sum(1 for i in range(0, len(x), 3) if x[i:i + 3] != y[i:i + 3])
if any(rc != "0" for rc in rcs):
    fails.append("a lab exited non-zero (%s)" % " ".join(rcs))
r0, r1, l0, l1 = pic("off", "run"), pic("on", "run"), pic("off", "lab"), pic("on", "lab")
if None in (r0, r1, l0, l1):
    fails.append("the shots were not all written")
else:
    n = differing(r0, r1)
    print("the script's last frame, overlay off vs on: %d pixels differ" % n)
    if n != 0:
        fails.append("THE LAW: the script's frame carries the lab's overlay (%d pixels differ)" % n)
    n = differing(l0, l1)
    print("the lab's own next frame, overlay off vs on: %d pixels differ" % n)
    if n <= 0:
        fails.append("CONTROL: the overlay is not drawn in the lab's own frame after the run")
for f in fails:
    print("FAIL: " + f)
print("lab.js_no_overlay: " + ("FAILED (%d)" % len(fails) if fails else "PASSED"))
sys.exit(1 if fails else 0)
PY
