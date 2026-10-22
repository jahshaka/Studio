#!/bin/bash
# gi.hold_after_stop (PHOTON-2 stage 2 and its round, SPECS/PHOTON_2_SPEC.md rev 8's H4) — THE GATHER SETTLES, THEN HOLDS.
# jahlab's stillness door at 960x540, three sessions; the gather's four states (H1): MOVING, SETTLING (the camera still,
# an input written or owed: the EMA, no hold), RESTING (the rest mean), HELD; the field's rest budget (H2):
#   ROOM, a 1.2 m walk inside one lattice cell (the no-regression check): no GI row after the stop, the gather held
#     by frame 17, the plaster's per-frame change <= 1 code from frame 1;
#   ROOM, a lamp step 8 -> 12 at rest (f): the field's change done within 16 frames of the step, then the hold, and
#     0 codes after it;
#   COURT, the 15 m crossing (cascade commits inside the walk), after the stop: (a) the per-frame change
#     non-increasing over 10-frame windows until the hold; (b) 0 codes after the hold - no burst; (c) the hold <= the
#     last input write + 17 frames and the field's last write <= 40 frames after the stop; (d) the follow frames'
#     extra GPU (three of the four entering samples: 3/4 of the ifd.follow row) <= 3 ms; the frame's busy GPU while
#     the field's rest budget runs reported (the round's (e) is Bistro's, measured in the lab).
# usage: hold_after_stop.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
ROOM=1.5,2,3,1.5,1.5,-4
printf 'stillness teleport:0.3,2,3,0.3,1.5,-4/hold:300/walk:%s:30 60 out=room-sub\nquit\n' "$ROOM" > "$WD/room-sub.lab"
printf 'camera %s\nframes 300\nlamp lamp intensity 12\nstillness teleport:%s/hold:1 120 out=room-lamp\nquit\n' "$ROOM" "$ROOM" > "$WD/room-lamp.lab"
printf 'stillness teleport:0,3,18,0,3,-6/hold:300/walk:0,3,3,0,3,-21:60 120 out=court-x\nquit\n' > "$WD/court-x.lab"
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
# ROOM, the lamp step (f)
w = window("room-lamp")
conv = [int(r["frame"]) for r in w if "ifd.converge" in rows_of(r)]
done = max(conv) + 1 if conv else 0
h = held_from(w)
after = [int(r["frame"]) for r in w if h is not None and int(r["frame"]) > h and int(r["all_max"]) > 0]
print("room lamp step: the field's change on frames %s, done %d frames after the step (bar <= 16); held from %s; frames changing after the hold: %d"
      % ("%d-%d" % (min(conv), max(conv)) if conv else "none", done, h, len(after)))
if not conv: fails.append("room lamp step: the field never integrated the change")
elif done > 16: fails.append("(f) room lamp step: the field's change within 16 frames: %d" % done)
if h is None: fails.append("(f) room lamp step: the picture never held")
elif after: fails.append("(f) room lamp step: no change after the hold: %d frames from %d" % (len(after), after[0]))
# COURT, the crossing
w = window("court-x")
h = held_from(w)
lastw = max([int(r["frame"]) for r in w if rows_of(r)] or [-1])
lastf = max([int(r["frame"]) for r in w if any(n.startswith("ifd.") for n in rows_of(r))] or [-1])
chg = [float(r["all_mean"]) for r in w]
end = h if h is not None else len(w)
wins = [sum(chg[a:min(a + 10, end)]) / max(1, min(10, end - a)) for a in range(1, end, 10)]
rises = [(i, round(wins[i], 3), round(wins[i + 1], 3)) for i in range(len(wins) - 1) if wins[i + 1] > wins[i]]
after = [int(r["frame"]) for r in w if h is not None and int(r["frame"]) > h and int(r["all_max"]) > 0]
print("court crossing: held from %s; the last input write %d (hold - write = %s); the field's last write %d; 10-frame windows %s; frames changing after the hold %d"
      % (h, lastw, None if h is None else h - lastw, lastf, " ".join("%.3f" % x for x in wins), len(after)))
if rises: fails.append("(a) court: the per-frame change non-increasing over 10-frame windows: rises %s" % rises)
if h is None: fails.append("(b) court: the picture never held")
elif after: fails.append("(b) court: no change after the hold: %d frames from %d" % (len(after), after[0]))
if h is not None and h > lastw + 17: fails.append("(c) court: the hold <= the last input write + 17: %d > %d" % (h, lastw + 17))
if lastf > 40: fails.append("(c) court: the field's last write <= 40 frames after the stop: %d" % lastf)
log = open(os.path.join(wd, "court-x.log")).read()
m = re.search(r"ifd\.follow=([\d.]+)\((\d+)\)", log)
if m:
    extra = 0.75 * float(m.group(1))
    print("court crossing: the follow frames' ifd.follow %.2f ms GPU (%s frames): extra %.2f ms (bar <= 3)" % (float(m.group(1)), m.group(2), extra))
    if extra > 3.0: fails.append("(d) court: a follow frame's extra GPU <= 3 ms: %.2f" % extra)
else:
    print("court crossing: no follow frame measured (no GPU timing)")
busy = [float(r["frame_gpu_ms"]) for r in w[1:] if float(r["frame_gpu_ms"]) >= 0 and any(n.startswith("ifd.") for n in rows_of(r))]
if busy: print("court crossing: the frame's busy GPU while the field's rest budget ran: mean %.2f, max %.2f ms" % (sum(busy) / len(busy), max(busy)))
for f in fails: print("FAIL:", f)
print("gi.hold_after_stop: %s" % ("FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
PY
