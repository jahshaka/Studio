#!/bin/bash
# lab.stillness_control (PHOTON-2 stage 0's fix round, the read's finding 1): THE CONTROL OF THE STILLNESS DOOR — NOTHING
# MOVES. photon-room, converged and held at the stop (its GI rows end ~170 frames after the build; the gather holds 17
# frames later): the door must read 0 codes on every window frame, no GI row and the hold from the stop on — and a stop
# the view already HOLDS must still have its mask (the held frame copies its history count). Base: the verb failed here
# ("the stop frame's history count never came back"). Its own row beside lab.stillness_runs: the two together are over
# the fixture's 10 s. On the rig's display, at 960x540.
# usage: stillness_control.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
cat > "$WD/control.lab" <<'S'
stillness teleport:0,2,3,0,3,-4/hold:200 12 out=control
quit
S
"$LAB" --headless --no-stdin --size 960x540 --data-root "$WD/data" --env photon-room --script "$WD/control.lab" > "$WD/control.log" 2>&1
rc=$?
[ $rc -eq 0 ] || { echo "FAIL: the control run exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR' "$WD/control.log" | head; tail -5 "$WD/control.log"; exit 1; }
grep '^LAB STILLNESS' "$WD/control.log"
python3 -I - "$WD" <<'PY'
import csv, os, sys
wd = sys.argv[1]
ctl = list(csv.DictReader(open(os.path.join(wd, "data", "stillness", "control.tsv")), delimiter="\t"))
assert [int(r["frame"]) for r in ctl] == list(range(13)), "the control window's frames 0..12"
assert all(int(r["all_max"]) == 0 for r in ctl), "nothing moved, nothing changed: max %s" % [r["all_max"] for r in ctl]
assert all(r["held"] == "1" for r in ctl), "the view holds from the stop on: %s" % [r["held"] for r in ctl]
assert all(r["gi_rows"] == "-" for r in ctl), "no GI row fired: %s" % [r["gi_rows"] for r in ctl if r["gi_rows"] != "-"]
assert int(ctl[1]["all_px"]) > 0, "the held stop's mask is the gather's count (pixels counted)"
clog = open(os.path.join(wd, "control.log")).read()
assert " visible=gather " in clog and " hold=0 " in clog, "the control's mask is the gather's and it holds at the stop"
print("lab.stillness_control: PASS")
PY
