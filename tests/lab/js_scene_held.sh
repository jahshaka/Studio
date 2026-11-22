#!/bin/bash
# lab.js_scene_held (LAB-SCRIPTS-1, the audit's D5 / unverified item 5) - BETWEEN RUNS THE LAB HOLDS THE SCRIPT's LAST
# FRAME STILL: after a run the lent view keeps showing the script's scene, and the lab's own frames neither sync the lab's
# world into it (labsession.cpp frame(): no mirror sync while the view is lent) nor step the script scene's clock (only
# engine.frames does: SCRIPT-IN-ENGINE-1 2b, b3). `jahlab --headless --js js_cube.js` (an empty world), then the lab's
# own frames. MEASURED first (spikes/lab-scripts-1/item5/held-probe.txt): js_cube.js and the demo both 0 px moved.
# THE LAW: the lab's shots at +1 and at +120 of its own frames equal the script's last frame (the shot right after the
#   run: no frame drawn between), every byte.
# CONTROL: that picture is not black (the cube is drawn: a black view would hold the law trivially).
# usage: js_scene_held.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
HERE=$(cd "$(dirname "$0")" && pwd)
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; echo "lab.js_scene_held: FAILED (1)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
printf 'shot run\nframes 1\nshot f1\nframes 119\nshot f120\nquit\n' > "$WD/held.lab"
"$LAB" --headless --no-stdin --size 320x180 --data-root "$WD/data" --js "$HERE/js_cube.js" --script "$WD/held.lab" \
    > "$WD/run.log" 2>&1
rc=$?
grep -E '^LAB (EMPTY|JS END|SHOT|SCRIPT|END)' "$WD/run.log"
python3 -I - "$WD" "$rc" <<'PY'
import os, sys
from PIL import Image
wd, rc = sys.argv[1], int(sys.argv[2])
fails = []
def pic(n):
    p = os.path.join(wd, "data", "shots", n + ".png")
    return Image.open(p).convert("RGB").tobytes() if os.path.isfile(p) else None
def differing(a, b):
    return sum(1 for i in range(0, len(a), 3) if a[i:i + 3] != b[i:i + 3]) if len(a) == len(b) else -1
run, f1, f120 = pic("run"), pic("f1"), pic("f120")
if None in (run, f1, f120):
    fails.append("the shots were not all written")
else:
    for name, p in (("+1", f1), ("+120", f120)):
        n = differing(run, p)
        print("the script's last frame vs the lab's frame %s: %d pixels differ" % (name, n))
        if n != 0:
            fails.append("THE LAW: the lab's frame %s moved the script's picture (%d pixels)" % (name, n))
    mean = [sum(run[c::3]) / (len(run) // 3) for c in range(3)]
    print("the script's picture's mean %s" % " ".join("%.3f" % v for v in mean))
    if not sum(mean) > 0:
        fails.append("CONTROL: the script's picture is black")
if rc != 0:
    fails.append("the lab exited %d" % rc)
for f in fails:
    print("FAIL: " + f)
print("lab.js_scene_held: " + ("FAILED (%d)" % len(fails) if fails else "PASSED"))
sys.exit(1 if fails else 0)
PY
