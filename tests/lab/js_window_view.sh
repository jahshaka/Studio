#!/bin/bash
# lab.js_window_view (LAB-SCRIPTS-1 stage 1) - AN ENGINE SCRIPT DRAWS IN THE LAB's OWN VIEW: `jahlab --headless --js
# demo_spiral.js` on an EMPTY lab world (no model, no --env) - the demo makes its own scene, takes the lab's view by
# lab.view() (handed over DETACHED: the view refuses a second bind), binds its scene to it, draws 240 frames and reads
# the view (demo_spiral.js says its two laws: the background is the cleared colour, 0 codes; the cubes are drawn above it).
# THE LAWS HERE:
#   (1) the run passes: `LAB JS END demo_spiral.js ok (2 checks, 241 frames)` - 240 frames and the read's one - and the
#       process exits 0 (the host's verdict, headless: ruling 8);
#   (2) THE LAB's OWN SHOT AFTER THE RUN SHOWS THE SCRIPT's PICTURE: its centre rectangle's mean equals, exactly, the
#       mean the script read there (`LAB JS LOG: centre r g b a`): the view still shows the script's scene, and the shot reads
#       the same frame's bytes (no frame drawn between);
#   CONTROL: that centre is above zero (a black picture would hold (2) trivially).
#   (3) THE FRAME LOOP NEVER READS THE SCRIPTS FOLDER (the audit's F4): the lab's `status` counts the folder's reads
#       (scriptListings); 30 of the lab's own frames with the overlay on (it is pushed every 10) add NONE.
#   CONTROL: the count rose by one at the --js load (1 at the first status: the press read the folder once).
#   (4) THE VIEW IS HANDED IN (SCRIPT-IN-ENGINE-1 2b's ownership law, its view half): a second script takes lab.view()
#       and binds its scene (engine.view.setScene) and places it (engine.view.lookAt) - ALLOWED - and its
#       engine.view.destroy is REFUSED by name ("handed in ... not the script's to destroy"): `LAB JS END view_law.js
#       ok (3 checks, 0 frames)`; then a shot (the lab's view still there: its destroy did not happen).
# usage: js_window_view.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
HERE=$(cd "$(dirname "$0")" && pwd)
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; echo "lab.js_window_view: FAILED (1)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
cat > "$WD/view_law.js" <<'JS'
// the handed-in view: driven (a scene bound, placed), never destroyed
const view = lab.view();
const scene = engine.scene.create("view-law");
let bound = false, placed = false, destroyed = true, why = "";
try { engine.view.setScene(view, {scene}); bound = true; } catch (e) { why = String(e); }
try { engine.view.lookAt(view, {eye: [0, 1, 5], target: [0, 0, 0]}); placed = true; } catch (e) { why = String(e); }
try { engine.view.destroy(view); } catch (e) { destroyed = false; why = String(e); }
host.check({ok: bound, what: "the handed-in view takes the script's scene (engine.view.setScene)"});
host.check({ok: placed, what: "the handed-in view is placed (engine.view.lookAt)"});
host.check({ok: !destroyed && /handed in/.test(why), what: "the handed-in view is not the script's to destroy (" + why + ")"});
JS
printf 'shot after\nstatus\noverlay on\nframes 30\nstatus\njs load %s\njs run\nframes 1\nshot law\nquit\n' "$WD/view_law.js" > "$WD/after.lab"
"$LAB" --headless --no-stdin --size 320x180 --data-root "$WD/data" --js "$HERE/demo_spiral.js" --script "$WD/after.lab" \
    > "$WD/run.log" 2>&1
rc=$?
grep -E '^LAB (EMPTY|FIRST|JS|SHOT|END)' "$WD/run.log"
python3 -I - "$WD" "$rc" <<'PY'
import os, re, sys
from PIL import Image
wd, rc = sys.argv[1], int(sys.argv[2])
log = open(os.path.join(wd, "run.log")).read()
fails = []
if rc != 0:
    fails.append("the lab exited %d (the script's verdict must be 0)" % rc)
end = re.search(r"^LAB JS END demo_spiral\.js (.*)$", log, re.M)
if not end or end.group(1) != "ok (2 checks, 241 frames)":
    fails.append("(1) the END line reads %r, not 'ok (2 checks, 241 frames)'" % (end.group(1) if end else None))
m = re.search(r"^LAB JS LOG: centre ([-0-9.e]+) ([-0-9.e]+) ([-0-9.e]+) ([-0-9.e]+)$", log, re.M)
shot = os.path.join(wd, "data", "shots", "after.png")
if not m:
    fails.append("(2) the script printed no centre mean")
elif not os.path.isfile(shot):
    fails.append("(2) the lab's shot after the run was not written")
else:
    want = [float(m.group(i)) for i in (1, 2, 3)]
    img = Image.open(shot).convert("RGB")
    W, H = img.size
    cw, ch = max(1, W >> 4), max(1, H >> 4)
    x0, y0, x1, y1 = (W >> 1) - cw, (H >> 1) - ch, (W >> 1) + cw, (H >> 1) + ch
    px = img.load()
    sums = [0, 0, 0]
    for y in range(y0, y1):
        for x in range(x0, x1):
            p = px[x, y]
            for c in range(3):
                sums[c] += p[c]
    n = (x1 - x0) * (y1 - y0)
    got = [s / n for s in sums]
    print("the script's centre %s, the shot's %s (%dx%d, rect %d,%d-%d,%d)" % (
        " ".join("%.4f" % v for v in want), " ".join("%.4f" % v for v in got), W, H, x0, y0, x1, y1))
    if any(abs(a - b) > 1e-9 for a, b in zip(want, got)):
        fails.append("(2) the shot's centre %s is not the script's %s" % (got, want))
    if not sum(got) > 0:
        fails.append("CONTROL: the centre reads 0 in every channel (no picture)")
import json
counts = [json.loads(s)["lab"].get("scriptListings") for s in re.findall(r"^LAB STATUS (\{.*\})$", log, re.M)]
print("the scripts folder's reads at the two status lines: %s" % counts)
if len(counts) != 2 or counts[0] != 1:
    fails.append("CONTROL (3): the --js load did not read the folder once (the counts %s)" % counts)
elif counts[1] != counts[0]:
    fails.append("(3) the lab's 30 frames read the scripts folder %s time(s)" % (counts[1] - counts[0]))
law = re.search(r"^LAB JS END view_law\.js (.*)$", log, re.M)
if not law or law.group(1) != "ok (3 checks, 0 frames)":
    fails.append("(4) the handed-in view's law reads %r, not 'ok (3 checks, 0 frames)'" % (law.group(1) if law else None))
if not os.path.isfile(os.path.join(wd, "data", "shots", "law.png")):
    fails.append("(4) no shot of the lab's view after the refused destroy")
for f in fails:
    print("FAIL: " + f)
print("lab.js_window_view: " + ("FAILED (%d)" % len(fails) if fails else "PASSED"))
sys.exit(1 if fails else 0)
PY
