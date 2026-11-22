#!/bin/bash
# lab.js_view_given_back / lab.js_clear_restores (LAB-SCRIPTS-1) - THE LAB's WORLD COMES BACK TO ITS VIEW: `jahlab
# --headless --env photon-room --tier low` with `gi off` - a world with CONTENT (an empty world is its background alone,
# which a scene-less view clears to as well) that holds still frame to frame (measured: 0 pixels move from its third
# frame on) - the lab's own shot BEFORE a script takes the view (lab.view()), and AFTER the view is given back and the
# lab drew one frame of its own. <mode>:
#   scene-less (lab.js_view_given_back, the audit's F1): the script takes the view and THROWS before it binds a scene -
#       a view with no scene presents nothing (the window froze on the lab's last frame, the error's overlay row never
#       drawn): the lab gives the view back at the run's end;
#   clear (lab.js_clear_restores, ruling 8's sibling): js_cube.js draws in the view, then `js clear`.
# THE LAWS: (1) the shot AFTER equals the shot BEFORE, every byte (the lab's world, its background and its post-fx: the
#   script's view settings never stay); scene-less: (2) the run ended `LAB JS END bad.js FAILED (1)` with its error, and
#   (3) the overlay, turned on, is DRAWN over the lab's world (its shot differs from AFTER's).
# CONTROLS: the BEFORE picture is not black (a black world would equal a scene-less view's black); clear: the shot
#   DURING the loan (after the run, before `js clear`) differs from BEFORE - the script's picture was shown.
# usage: js_world_back.sh <jahlab> <work dir> scene-less|clear
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}; MODE=${3:?scene-less or clear}
HERE=$(cd "$(dirname "$0")" && pwd)
ROW=lab.js_clear_restores; [ "$MODE" = scene-less ] && ROW=lab.js_view_given_back
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; echo "$ROW: FAILED (1)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
if [ "$MODE" = scene-less ]; then
    printf '%s\n' '// bad.js: takes the lab view, checks once, then throws before any scene is bound' 'const view = lab.view();' \
        'host.check({ok: true, what: "lab.view() answered"});' 'throw new Error("the scene was never made");' > "$WD/bad.js"
    printf 'gi off\nframes 2\nshot before\njs load %s\ntry js run\nframes 1\nshot after\noverlay on\nframes 1\nshot overlay\nquit\n' \
        "$WD/bad.js" > "$WD/s.lab"
else
    printf 'gi off\nframes 2\nshot before\njs load %s\njs run\nshot during\njs clear\nframes 1\nshot after\nquit\n' \
        "$HERE/js_cube.js" > "$WD/s.lab"
fi
"$LAB" --headless --no-stdin --size 320x180 --data-root "$WD/data" --env photon-room --tier low \
    --script "$WD/s.lab" > "$WD/run.log" 2>&1
rc=$?
grep -E '^LAB (ENV|JS|SHOT|TRY|SCRIPT|END)' "$WD/run.log"
python3 -I - "$WD" "$rc" "$MODE" "$ROW" <<'PY'
import os, re, sys
from PIL import Image
wd, rc, mode, row = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
log = open(os.path.join(wd, "run.log")).read()
fails = []
def pic(name):
    p = os.path.join(wd, "data", "shots", name + ".png")
    return Image.open(p).convert("RGB") if os.path.isfile(p) else None
def differing(a, b):
    x, y = a.tobytes(), b.tobytes()
    return sum(1 for i in range(0, len(x), 3) if x[i:i + 3] != y[i:i + 3])
before, after = pic("before"), pic("after")
if before is None or after is None:
    fails.append("(1) the shots were not both written (before %s, after %s)" % (before is not None, after is not None))
else:
    n = differing(before, after) if before.size == after.size else -1
    print("before vs after: %d pixels differ (%dx%d)" % (n, before.size[0], before.size[1]))
    if n != 0:
        fails.append("(1) the lab's world did not come back: %d pixels differ from the shot before the run" % n)
    raw = before.tobytes()
    mean = [sum(raw[c::3]) / (len(raw) // 3) for c in range(3)]
    print("before's mean %s" % " ".join("%.3f" % v for v in mean))
    if not sum(mean) > 0:
        fails.append("CONTROL: the lab's world reads black (a scene-less view would equal it)")
if mode == "scene-less":
    if not re.search(r"^LAB JS END bad\.js FAILED \(1\)$", log, re.M) or not re.search(r"^LAB JS FAIL: .*the scene was never made", log, re.M):
        fails.append("(2) the run did not end FAILED (1) with its error")
    over = pic("overlay")
    if over is None or after is None:
        fails.append("(3) no overlay shot")
    else:
        n = differing(over, after)
        print("overlay vs after: %d pixels differ" % n)
        if n == 0:
            fails.append("(3) the overlay is not drawn over the lab's world")
else:
    during = pic("during")
    if during is None or before is None:
        fails.append("CONTROL: no shot during the loan")
    else:
        n = differing(during, before)
        print("during vs before: %d pixels differ" % n)
        if n == 0:
            fails.append("CONTROL: the script's picture was never shown (during == before)")
    if rc != 0:
        fails.append("the lab exited %d" % rc)
for f in fails:
    print("FAIL: " + f)
print(row + ": " + ("FAILED (%d)" % len(fails) if fails else "PASSED"))
sys.exit(1 if fails else 0)
PY
