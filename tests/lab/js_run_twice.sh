#!/bin/bash
# lab.js_run_twice (LAB-SCRIPTS-1, the lead's ask: THE TWICE-PRESSED DEMO) - RUN PRESSED TWICE STARTS CLEAN: one lab
# (`jahlab --headless --js demo_spiral.js`, an empty world) runs the demo, then `js run` again - a FRESH host per run
# (ruling 5), and the first run's scene, nodes and materials ended with ITS host (SCRIPT-IN-ENGINE-1 2b's ownership law:
# the demo's named scene "demo-spiral" is made again, not refused).
# THE LAWS: (1) both runs pass, `ok (2 checks, 241 frames)` each; (2) the lab's shot after each run (the demo's 240th
#   frame, its bytes: no frame drawn between) is THE SAME PICTURE, every byte - the lab's determinism for a watched script.
# CONTROL: the demo's own two checks (its cubes drawn above a black background) - two empty pictures would hold (2).
# usage: js_run_twice.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
HERE=$(cd "$(dirname "$0")" && pwd)
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; echo "lab.js_run_twice: FAILED (1)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
printf 'shot one\njs run\nshot two\nquit\n' > "$WD/twice.lab"
"$LAB" --headless --no-stdin --size 320x180 --data-root "$WD/data" --js "$HERE/demo_spiral.js" --script "$WD/twice.lab" \
    > "$WD/run.log" 2>&1
rc=$?
grep -E '^LAB (EMPTY|JS (RUN|END)|SHOT|SCRIPT|END)' "$WD/run.log"
python3 -I - "$WD" "$rc" <<'PY'
import os, re, sys
from PIL import Image
wd, rc = sys.argv[1], int(sys.argv[2])
log = open(os.path.join(wd, "run.log")).read()
fails = []
ends = re.findall(r"^LAB JS END demo_spiral\.js (.*)$", log, re.M)
if ends != ["ok (2 checks, 241 frames)"] * 2:
    fails.append("(1) / CONTROL: the two runs read %r, not 'ok (2 checks, 241 frames)' twice" % ends)
if rc != 0:
    fails.append("the lab exited %d" % rc)
pics = []
for n in ("one", "two"):
    p = os.path.join(wd, "data", "shots", n + ".png")
    pics.append(Image.open(p).convert("RGB").tobytes() if os.path.isfile(p) else None)
if None in pics:
    fails.append("(2) the shots were not both written")
else:
    a, b = pics
    n = sum(1 for i in range(0, len(a), 3) if a[i:i + 3] != b[i:i + 3]) if len(a) == len(b) else -1
    print("the demo's 240th frame, run 1 vs run 2: %d pixels differ" % n)
    if n != 0:
        fails.append("(2) the second press drew another picture (%d pixels differ)" % n)
for f in fails:
    print("FAIL: " + f)
print("lab.js_run_twice: " + ("FAILED (%d)" % len(fails) if fails else "PASSED"))
sys.exit(1 if fails else 0)
PY
