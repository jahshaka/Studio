#!/bin/bash
# gi.hold_after_stop (PHOTON-2 stage 2, SPECS/PHOTON_2_SPEC.md stage 2 + rev 4's bars) — THE GATHER HOLDS AFTER A STOP.
# jahlab's stillness door at 960x540, three sessions:
#   ROOM, a 1.2 m walk inside one lattice cell (the no-regression check): no GI row after the stop, the gather held
#     by frame 17, the plaster's per-frame change <= 1 code from frame 1;
#   ROOM, a lamp step 8 -> 12 at rest: the irradiance field's change (every probe four samples deep - never a
#     one-sample probe, by construction: PhotonIrradianceField::setChangeSamples) done within 2 passes (16 frames
#     at the budget's 8 frames a pass) of the step;
#   COURT, the 15 m crossing (cascade commits inside the walk): the gather held by frame 17 after the stop (base 69),
#     the field's rows after the stop within one pass (8 frames; base 45), the canopy (the foliage class) <= 1 code a
#     frame after the stop, and the plaster's frames over a code REPORTED with the GI rows that landed on them (the
#     cards' re-march steps are stage 4's).
# usage: hold_after_stop.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
ROOM=1.5,2,3,1.5,1.5,-4
printf 'stillness teleport:0.3,2,3,0.3,1.5,-4/hold:300/walk:%s:30 60 out=room-sub\nquit\n' "$ROOM" > "$WD/room-sub.lab"
printf 'camera %s\nframes 300\nlamp lamp intensity 12\nstillness teleport:%s/hold:1 60 out=room-lamp\nquit\n' "$ROOM" "$ROOM" > "$WD/room-lamp.lab"
printf 'stillness teleport:0,3,18,0,3,-6/hold:300/walk:0,3,3,0,3,-21:60 90 out=court-x\nquit\n' > "$WD/court-x.lab"
for run in room-sub:photon-room room-lamp:photon-room court-x:photon-court; do
    name=${run%%:*}; env=${run#*:}
    "$LAB" --headless --no-stdin --size 960x540 --data-root "$WD/data" --env "$env" --script "$WD/$name.lab" \
        > "$WD/$name.log" 2>&1
    rc=$?
    [ $rc -eq 0 ] || { echo "FAIL: jahlab ($name) exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR' "$WD/$name.log" | head; tail -5 "$WD/$name.log"; exit 1; }
    grep '^LAB STILLNESS walk' "$WD/$name.log" | cut -c1-300
done
python3 -I - "$WD" <<'PY'
import csv, os, re, sys
wd = sys.argv[1]
def window(name):
    return [r for r in csv.DictReader(open(os.path.join(wd, "data", "stillness", name + ".tsv")), delimiter="\t") if r["window"] == "walk"]
def rows_of(r):
    return [] if r["gi_rows"] == "-" else [it.split(":")[0] for it in r["gi_rows"].split(",")]
def held_from(w):
    return next((int(r["frame"]) for r in w if r["held"] == "1"), None)
fails = []
# ROOM, the sub-cell walk
w = window("room-sub")
rows = sorted({n for r in w[1:] for n in rows_of(r)})
h = held_from(w)
over = [(int(r["frame"]), int(r["rough_max"])) for r in w[1:] if int(r["rough_max"]) > 1]
print("room sub-cell: GI rows after the stop %s; held from frame %s; plaster frames over a code: %s" % (rows or "none", h, over[:6]))
if rows: fails.append("room sub-cell: no GI row after the stop: %s" % rows)
if h is None or h > 17: fails.append("room sub-cell: the gather held by frame 17: %s" % h)
if over: fails.append("room sub-cell: the plaster <= 1 code a frame from frame 1: %s" % over[:6])
# ROOM, the lamp step
w = window("room-lamp")
conv = [int(r["frame"]) for r in w if "ifd.converge" in rows_of(r)]
done = max(conv) + 1 if conv else 0
print("room lamp step: the field's change (ifd.converge) on frames %s; done %d frames after the step (bar <= 16)"
      % ("%d-%d" % (min(conv), max(conv)) if conv else "none", done))
if not conv: fails.append("room lamp step: the field never integrated the change")
elif done > 16: fails.append("room lamp step: the field's change within 2 passes (16 frames): %d" % done)
# COURT, the crossing
w = window("court-x")
h = held_from(w)
ifd = [int(r["frame"]) for r in w if any(n.startswith("ifd.") for n in rows_of(r))]
leaf = max(int(r["foliage_max"]) for r in w[1:]) if int(w[1]["foliage_px"]) else -1
print("court crossing: held from frame %s (bar <= 17, base 69); the field's rows after the stop to frame %s (bar < 8, base 45);"
      " the canopy's largest change a frame %d" % (h, max(ifd) if ifd else None, leaf))
if h is None or h > 17: fails.append("court crossing: the gather held by frame 17: %s" % h)
if ifd and max(ifd) >= 8: fails.append("court crossing: the field's rows after the stop within one pass: to frame %d" % max(ifd))
if leaf < 0: fails.append("court crossing: the canopy is not counted")
elif leaf > 1: fails.append("court crossing: the canopy <= 1 code a frame after the stop: %d" % leaf)
plaster = [(int(r["frame"]), int(r["rough_max"]), ",".join(n for n in rows_of(r) if n.startswith("cards."))) for r in w[1:] if int(r["rough_max"]) > 1]
print("court crossing: the plaster's frames over a code (frame, codes, the card rows that landed): %d, %s"
      % (len(plaster), plaster[:8]))
for f in fails: print("FAIL:", f)
print("gi.hold_after_stop: %s" % ("FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
PY
