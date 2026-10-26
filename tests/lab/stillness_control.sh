#!/bin/bash
# lab.stillness_control (PHOTON-2 stage 0's fix round, the read's finding 1; RESTATED in PHOTON-7 stage 10, the lead's
# ruling on R3) - THE CONTROL OF THE STILLNESS DOOR: NOTHING MOVES, so AFTER THE VIEW'S FIRST HOLD NOTHING CHANGES.
# photon-room from a teleport to its pose, the stillness door's window opened at the teleport itself and run past the
# first hold: every frame from the view's FIRST HOLD on holds, reads 0 codes against the frame before and fires no GI row,
# and the held frames keep their mask (the held frame copies its history count). The restatement replaces a fixed stop
# 200 frames after the teleport (the fixture's premise "its GI rows end ~170 frames after the build", which R3's one rest
# decision moved: the decision lands after the readers' rest and its rounds run on) - a fixed stop can pass by luck past
# a broken hold; the first hold cannot.
# THE LAWS:
#   (1) THE FIRST HOLD IS THE LAST: from the first held frame to the window's end (at least 12 frames) every frame held,
#       0 codes, no GI row; the held frames' mask counted (the gather's history count);
#   (2) THE HOLD WAITS FOR THE DECISION (R3's ruling: the readers' hold waits on the rest decision's work): the stop's one
#       rest decision is taken, and no held frame comes before the decision's last GI row;
#   (3) LIVENESS, the bound R3's design writes: the first hold comes within
#           16 (the readers' rest demand, N) + 9 (the read-back's due: the period 4 + the ring 3 + 1 + 1)
#         + W (the decision's own work: its captures and card-to-card rounds, from the decision to its last GI row)
#         + 16 (the re-rest after that work) + 1 (the hold's own frame)
#       of the readers' FIRST REST frame - W is the cache's measured work (its pace is the L7 count, the optimisation
#       list's item: photon-room's 72 frames of rounds at 8 pages a frame), never a margin; a view that never holds is red.
# On the rig's display, at 960x540.
# usage: stillness_control.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
cat > "$WD/control.lab" <<'S'
stillness teleport:0,2,3,0,3,-4 300 out=control
quit
S
"$LAB" --headless --no-stdin --size 960x540 --data-root "$WD/data" --env photon-room --script "$WD/control.lab" > "$WD/control.log" 2>&1
rc=$?
[ $rc -eq 0 ] || { echo "FAIL: the control run exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR' "$WD/control.log" | head; tail -5 "$WD/control.log"; exit 1; }
grep '^LAB STILLNESS' "$WD/control.log" | cut -c1-400
python3 -I - "$WD" <<'PY'
import csv, os, sys
wd = sys.argv[1]
ctl = list(csv.DictReader(open(os.path.join(wd, "data", "stillness", "control.tsv")), delimiter="\t"))
fails = []
def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok: fails.append(what)
fr = [int(r["frame"]) for r in ctl]
check(fr == list(range(len(ctl))) and len(ctl) >= 200, "the window's frames from the teleport (%d)" % len(ctl))
held = [r["held"] == "1" for r in ctl]
rest = [int(r["rest_frames"] or 0) for r in ctl]
gi = [r["gi_rows"] != "-" for r in ctl]
dec = [int(r.get("cards_decisions") or 0) for r in ctl]
first_hold = next((i for i, h in enumerate(held) if h), None)
first_rest = next((i for i, n in enumerate(rest) if n >= 1), None)
t_dec = next((i for i in range(1, len(dec)) if dec[i] > dec[i - 1]), None)
last_gi = max((i for i, g in enumerate(gi) if g), default=None)
print("frames from the teleport: the readers' first rest %s, the decision %s, its last GI row %s, the FIRST HOLD %s"
      % (first_rest, t_dec, last_gi, first_hold))
check(first_hold is not None, "(3) the view holds within the window")
if first_hold is not None:
    after = ctl[first_hold:]
    check(len(after) >= 12, "(1) at least 12 frames after the first hold (%d)" % len(after))
    bad = [int(r["frame"]) for r in after if not (r["held"] == "1" and int(r["all_max"]) == 0 and r["gi_rows"] == "-")]
    check(not bad, "(1) THE FIRST HOLD IS THE LAST: held, 0 codes and no GI row on every frame after it%s"
          % ("" if not bad else " - broken at frames %s (max %s, GI %s)" % (bad[:6], [ctl[i]["all_max"] for i in bad[:6]],
                                                                         [ctl[i]["gi_rows"][:40] for i in bad[:3]])))
    check(all(int(r["all_px"]) > 0 for r in after), "(1) the held frames' mask is the gather's count (pixels counted)")
    check(t_dec is not None, "(2) the stop's one rest decision is taken")
    if t_dec is not None:
        work_end = max((i for i in range(t_dec, len(gi)) if gi[i]), default=t_dec)
        check(first_hold > work_end and first_hold >= t_dec,
              "(2) THE HOLD WAITS FOR THE DECISION: the first hold (%d) after the decision (%d) and its last GI row (%d)"
              % (first_hold, t_dec, work_end))
        if first_rest is not None:
            W = work_end - t_dec
            bound = 16 + 9 + W + 16 + 1
            check(first_hold - first_rest <= bound, "(3) LIVENESS: the first hold %d frames after the first rest, within "
                  "16 + 9 + W %d + 16 + 1 = %d" % (first_hold - first_rest, W, bound))
clog = open(os.path.join(wd, "control.log")).read()
check(" visible=gather " in clog, "the control's mask is the gather's")
if fails:
    print("lab.stillness_control: FAIL"); sys.exit(1)
print("lab.stillness_control: PASS")
PY
