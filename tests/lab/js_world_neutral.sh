#!/bin/bash
# lab.js_world_neutral (LAB-SCRIPTS-1, the audit's F2) - A SCRIPT's PICTURE DOES NOT DEPEND ON THE LAB's WORLD: the demo
# (demo_spiral.js) run twice, `jahlab --headless --js` on an EMPTY lab world and on `--env photon-room` (an Epic world:
# its own post chain - exposure, meter, hdr - pushed into the view by the mirror). lab.view() lends the view with the
# DEFAULT post-fx (a script view's, as in jahengine), so the lab world's grade never reaches the script's frames.
# THE LAW: the lab's own shot right after each run (the demo's 240th frame, its bytes: no frame drawn between - the law
#   of lab.js_window_view) is THE SAME PICTURE, every byte, in both worlds.
# CONTROL: both runs pass the demo's own two checks (`ok (2 checks, 241 frames)`: the cubes are drawn, above a black
#   background) - two empty pictures would hold the law trivially.
# usage: js_world_neutral.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
HERE=$(cd "$(dirname "$0")" && pwd)
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; echo "lab.js_world_neutral: FAILED (1)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
printf 'shot demo\nquit\n' > "$WD/after.lab"
"$LAB" --headless --no-stdin --size 320x180 --data-root "$WD/empty" --js "$HERE/demo_spiral.js" --script "$WD/after.lab" \
    > "$WD/empty.log" 2>&1
rc1=$?
"$LAB" --headless --no-stdin --size 320x180 --data-root "$WD/room" --env photon-room --js "$HERE/demo_spiral.js" \
    --script "$WD/after.lab" > "$WD/room.log" 2>&1
rc2=$?
for w in empty room; do grep -E '^LAB (EMPTY|ENV|JS END|SHOT|END)' "$WD/$w.log" | sed "s/^/$w: /"; done
python3 -I - "$WD" "$rc1" "$rc2" <<'PY'
import os, re, sys
from PIL import Image
wd, rcs = sys.argv[1], (int(sys.argv[2]), int(sys.argv[3]))
fails = []
pics = {}
for w, rc in zip(("empty", "room"), rcs):
    log = open(os.path.join(wd, w + ".log")).read()
    end = re.search(r"^LAB JS END demo_spiral\.js (.*)$", log, re.M)
    if rc != 0 or not end or end.group(1) != "ok (2 checks, 241 frames)":
        fails.append("CONTROL (%s world): the demo's run reads %r, exit %d" % (w, end.group(1) if end else None, rc))
    p = os.path.join(wd, w, "shots", "demo.png")
    pics[w] = Image.open(p).convert("RGB") if os.path.isfile(p) else None
if pics["empty"] is None or pics["room"] is None:
    fails.append("the shots were not both written")
else:
    a, b = pics["empty"], pics["room"]
    x, y = a.tobytes(), b.tobytes()
    n = sum(1 for i in range(0, len(x), 3) if x[i:i + 3] != y[i:i + 3]) if a.size == b.size else -1
    print("the demo's picture, empty world vs photon-room: %d pixels differ (%dx%d)" % (n, a.size[0], a.size[1]))
    if n != 0:
        fails.append("THE LAW: the script's picture depends on the lab's world (%d pixels differ)" % n)
for f in fails:
    print("FAIL: " + f)
print("lab.js_world_neutral: " + ("FAILED (%d)" % len(fails) if fails else "PASSED"))
sys.exit(1 if fails else 0)
PY
