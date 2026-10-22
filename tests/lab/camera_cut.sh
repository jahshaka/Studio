#!/bin/bash
# lab.camera_cut (PHOTON-2 round 2, N3 - spec rev 11's brief-4 audit) — A CAMERA CUT RESTARTS THE HISTORIES. Lumen's rule
# as is (IsLargeCameraMovement): a frame on which a view axis turns more than 75 degrees or the eye moves more than 100 m
# is a cut, and the gather's and the reflection's histories restart (ScreenProbeGather.h isCameraCut). jahlab's
# stillness door on photon-glass, three moves from the stop pose after a 20-frame hold, each ending in ONE teleport
# frame (the window's frame 0): 150 m sideways (a cut), 50 m sideways (none: a walk the reprojection follows), a 90
# degree snap turn in place (a cut). The door's columns `cuts` (GatherStatus::cameraCuts) and `refl_cuts`
# (RayQueryStatus::reflectCameraCuts) step by exactly that on the teleport frame against the move's last frame.
# (`since_restart` cannot tell: a 50 m teleport re-places the GI, which restarts on its own key.)
# usage: camera_cut.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
cat > "$WD/cut.lab" <<S
stillness teleport:0,1.8,3.4,0,0.5,-3/hold:20/teleport:150,1.8,3.4,150,0.5,-3 4 trail=2 out=cut150
stillness teleport:0,1.8,3.4,0,0.5,-3/hold:20/teleport:50,1.8,3.4,50,0.5,-3 4 trail=2 out=cut50
stillness teleport:0,1.8,3.4,0,0.5,-3/hold:20/teleport:0,1.8,3.4,4,1.8,3.4 4 trail=2 out=snap90
quit
S
"$LAB" --headless --no-stdin --size 960x540 --data-root "$WD/data" --env photon-glass --script "$WD/cut.lab" > "$WD/cut.log" 2>&1
rc=$?
[ $rc -eq 0 ] || { echo "FAIL: jahlab exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR' "$WD/cut.log" | head; tail -5 "$WD/cut.log"; exit 1; }
python3 -I - "$WD" <<'PY'
import csv, os, sys
wd = sys.argv[1]
fails = []
for name, want in (("cut150", 1), ("cut50", 0), ("snap90", 1)):
    p = os.path.join(wd, "data", "stillness", name + ".tsv")
    if not os.path.exists(p):
        fails.append("%s: no TSV" % name); continue
    rows = {(r["window"], int(r["frame"])): r for r in csv.DictReader(open(p), delimiter="\t")}
    before, at = rows.get(("walk-move", -1)), rows.get(("walk", 0))
    if not before or not at:
        fails.append("%s: the move's last frame or the teleport frame is missing" % name); continue
    for col in ("cuts", "refl_cuts"):
        step = int(at[col]) - int(before[col])
        print("%s: %s %s -> %s (a step of %d, %d expected)" % (name, col, before[col], at[col], step, want))
        if step != want: fails.append("%s: %s stepped %d, %d expected" % (name, col, step, want))
for f in fails: print("FAIL:", f)
print("lab.camera_cut: %s" % ("FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
PY
