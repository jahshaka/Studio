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
#       0 codes, no GI row;
#   (1b) A HELD STOP KEEPS ITS MASK (the read's finding 1: "the stop frame's history count never came back"): a second
#       12-frame window whose stop is a held frame reads 0 codes, holds, and counts its mask (the held frame copies its
#       history count) - the first window's stop is the teleport, where no pixel has a history;
#   (2) THE HOLD WAITS FOR THE DECISION (R3's ruling: the readers' hold waits on the rest decision's work): the stop's one
#       rest decision is taken, and no held frame comes before the decision's last GI row;
#   (3) LIVENESS, the bound R3's design writes (since PHOTON-7 stage 10-3 computed ONCE, in the door -
#       jahshaka::engine::stillnessDeriveLiveness - and read here from the LAB STILLNESS line): the first hold comes within
#           16 + 9 (the readers' DEMAND SPAN L = N + the due: each reader's rest indices [1, L], PHOTON-7 stage 10's
#           per-reader snapshot) + 9 (the read-back's due after it: the period 4 + the ring 3 + 1 + 1)
#         + W (the decision's own work: its captures and card-to-card rounds, from the decision to its last GI row)
#         + 16 (the re-rest after that work) + 1 (the hold's own frame)
#       of the readers' FIRST REST frame; a view that never holds is red - and W'S PACE LAW (the lead's correction: W is
#       the cache's measured work, so its own bound is derived from the same session's counts):
#           W <= ceil(sum over the decision's rounds of pages_r / P) + 9 (one read-back due)
#       with pages_r the indirect texels the round's frames relit over a page (cards_indirect_relit, cards_page_size)
#       and P the tier's pages a frame (cards_indirect_texels over a page: 8 at High) - the work runs at its pace with
#       no idle frame. THE ROUNDS ARE PIPELINED, not sequential (measured: a frame's batch mixes rounds 9-17, the next
#       round's listings landing while the last one's pages relight), so one due covers the chain, and the per-round
#       form, sum ceil(pages_r / P) + rounds x 9, is printed beside it (it allows a due's stall at every round: 261
#       frames against the work's 72). How MANY rounds a stop takes is not this row's law (the card-to-card rest row's).
#   (4) EVERY REST RESTART HAS A CAUSE (PHOTON-7 stage 10, the lead's ruling: R3 (a)'s display quantum applies to every
#       reader): from each reader's first rest on (the gather's rest_frames, the reflection's refl_rest_frames), every
#       frame it restarts (its rest 0) has one at that frame or the one before - a VISIBLE relight read back (a card texel
#       moved a code since its last report: cards_visible), a capture or a still trace (cards_captures, a cards.capture /
#       cards.still row), a real change (a GI writer that is not the cards': a light's, the field's, the voxels'), or the
#       teleport itself. A relight whose every texel stayed under a code (cards_subquantum) is NOT a cause: a reader that
#       restarts on one is red - the count of such relight-frame restarts is printed for each reader.
#   (5) THE SERIES STOPS (the lead's ruling (B) on the card-to-card rest): after the window's last VISIBLE relight
#       reading, the relights end, and the window then holds at least 9 (one read-back due) + 16 (a reader's rest) frames
#       with no relight. PRINTED, NOT ASSERTED: the largest move read (dL0, codes), photon-room's albedo (rho_max 0.5,
#       every surface matte), the chain depth (cards_round's deepest - a chain's, not a root's) and the readings' tail.
#       (The bound ceil(ln(dL0/q)/ln(1/rho_max)) was withdrawn: the quantum is the READER's 1 %, the report cumulative,
#       cards_round a chain depth.)
#   (6) NO REPORT DROPPED (PHOTON-7 stage 10, the stranded listings): the cache's reports never read for want of a
#       read-back slot (cards_reports_dropped) stay 0 over the window - a dropped report is a defect if seen at all (its
#       fallback lists every card, but the ring holds one report a frame over the lag).
# On the rig's display, at 960x540.
# usage: stillness_control.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
cat > "$WD/control.lab" <<'S'
stillness teleport:0,2,3,0,3,-4 300 out=control
stillness teleport:0,2,3,0,3,-4/hold:1 12 out=held
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
    check(t_dec is not None, "(2) the stop's one rest decision is taken")
    if t_dec is not None:
        work_end = max((i for i in range(t_dec, len(gi)) if gi[i]), default=t_dec)
        check(first_hold > work_end and first_hold >= t_dec,
              "(2) THE HOLD WAITS FOR THE DECISION: the first hold (%d) after the decision (%d) and its last GI row (%d)"
              % (first_hold, t_dec, work_end))
        # (3) THE DERIVED BOUND IS THE DOOR's (jahshaka::engine::stillnessDeriveLiveness, ONE formula - the LAB STILLNESS
        # line's firstRest / decision / workEnd / holdBound / pace / pages; scripting.e2e.world_stillness reads the same):
        # the first hold within (N + due) + due + W + N + 1 of the readers' first rest, W the decision's work at its pace
        import re as _re
        door = {}
        for line in open(os.path.join(wd, "control.log"), errors="replace"):
            # (the control run's window line: the first walk window of len(ctl) - 1 frames; the held run's comes after)
            if not door and line.startswith("LAB STILLNESS walk") and (" frames=%d " % (len(ctl) - 1)) in line:
                for k in ("firstRest", "decision", "workEnd", "holdBound", "pace", "pages"):
                    m = _re.search(r"\b%s=(-?[0-9.]+)" % k, line)
                    if m: door[k] = float(m.group(1))
        check(len(door) == 6, "(3) the door's derived bound is printed (%s)" % door)
        if len(door) == 6 and first_rest is not None:
            W = int(door["workEnd"]) - int(door["decision"])
            check(int(door["decision"]) == t_dec and int(door["workEnd"]) == work_end and int(door["firstRest"]) == first_rest,
                  "(3) the door's frames are the record's (decision %d / %d, work end %d / %d, first rest %d / %d)"
                  % (int(door["decision"]), t_dec, int(door["workEnd"]), work_end, int(door["firstRest"]), first_rest))
            check(first_hold - first_rest <= int(door["holdBound"]), "(3) LIVENESS: the first hold %d frames after the "
                  "first rest, within the door's derived bound (N + due) + due + W %d + N + 1 = %d"
                  % (first_hold - first_rest, W, int(door["holdBound"])))
            check(int(door["pace"]) >= 0 and W <= int(door["pace"]), "(3) W'S PACE: the decision's work (%d frames) within "
                  "ceil(%.1f pages / the tier's pages a frame) + due = %d" % (W, door["pages"], int(door["pace"])))
# (4) EVERY REST RESTART HAS A CAUSE
num = lambda r, k: int(float(r.get(k) or 0))
vis = [num(r, "cards_visible") for r in ctl]
sub = [num(r, "cards_subquantum") for r in ctl]
cap = [num(r, "cards_captures") for r in ctl]
rel = [num(r, "cards_relights") for r in ctl]
def moved(a, i): return i > 0 and a[i] != a[i - 1]
def writer(i):
    parts = [p for p in ctl[i]["gi_rows"].split(",") if ":" in p] if ctl[i]["gi_rows"] != "-" else []
    return any(not p.startswith("cards.") for p in parts), any(p.startswith(("cards.capture:", "cards.still:")) for p in parts)
def cause(i):
    for j in (i, i - 1):
        if j <= 1: return "the teleport"
        w, c = writer(j)
        if moved(vis, j): return "a visible relight"
        if moved(cap, j) or c: return "a capture"
        if w: return "a GI writer"
    return None
print("the readings: %d relight frames VISIBLE (%d unmeasured), %d SUB-QUANTUM; the largest move %s codes" % (
    vis[-1] - vis[0], num(ctl[-1], "cards_unmeasured") - num(ctl[0], "cards_unmeasured"), sub[-1] - sub[0],
    max((float(r.get("cards_move_codes") or 0) for r in ctl), default=0.0)))
for label, key in (("the gather", "rest_frames"), ("the reflection", "refl_rest_frames")):
    rs = [num(r, key) for r in ctl]
    start = next((i for i, n in enumerate(rs) if n >= 1), None)
    if start is None:
        check(False, "(4) %s rests in the window" % label)
        continue
    restarts = [i for i in range(start, len(rs)) if rs[i] == 0]
    blind = [i for i in restarts if cause(i) is None]
    on_relight = [i for i in blind if moved(rel, i) or moved(rel, i - 1)]
    print("  %s: %d restart frames after its first rest (%d), %d with no cause, %d of them on a relight frame%s" % (
        label, len(restarts), start, len(blind), len(on_relight), (" - frames %s" % blind[:12]) if blind else ""))
    check(not blind, "(4) EVERY REST RESTART HAS A CAUSE: %s - %d restart(s) with none (%d on a sub-quantum relight)" % (
        label, len(blind), len(on_relight)))
# (5) THE SERIES STOPS
t_vis = max((i for i in range(1, len(ctl)) if moved(vis, i)), default=None)
t_rel = max((i for i in range(1, len(ctl)) if moved(rel, i)), default=None)
quiet = len(ctl) - 1 - t_rel if t_rel is not None else len(ctl) - 1
codes = [float(r.get("cards_move_codes") or 0) for r in ctl]
tail = [round(codes[i], 3) for i in range(1, len(ctl)) if moved(vis, i) or moved(sub, i)][-8:]
vtail = [round(codes[i], 3) for i in range(1, len(ctl)) if moved(vis, i)][-6:]
depth = max((int(r["cards_round"]) for r in ctl if (r.get("cards_round") or "").isdigit()), default=0)
print("the series: the last visible reading at %s, the last relight at %s, then %d quiet frames; dL0 %.1f codes, rho_max 0.5 "
      "(photon-room, matte), the chain depth %d, the readings' tail %s (the visible ones' %s)" % (t_vis, t_rel, quiet, max(codes), depth, tail, vtail))
check(t_vis is not None and t_rel is not None and quiet >= 9 + 16,
      "(5) THE SERIES STOPS: after the last visible reading (%s) the relights end (%s) and %d frames follow with none "
      "(at least 9 + 16)" % (t_vis, t_rel, quiet))
# (6) NO REPORT DROPPED
dropped = num(ctl[-1], "cards_reports_dropped")
check(dropped == 0, "(6) NO REPORT DROPPED: %d of the cache's reports found no read-back slot" % dropped)
held2 = list(csv.DictReader(open(os.path.join(wd, "data", "stillness", "held.tsv")), delimiter="\t"))
check(len(held2) == 13 and all(r["held"] == "1" and int(r["all_max"]) == 0 and r["gi_rows"] == "-" for r in held2),
      "(1b) the held stop's window: 13 frames held, 0 codes, no GI row")
check(len(held2) > 1 and int(held2[1]["all_px"]) > 0, "(1b) A HELD STOP KEEPS ITS MASK: the gather's count (pixels counted)")
clog = open(os.path.join(wd, "control.log")).read()
check(" visible=gather " in clog, "the control's mask is the gather's")
if fails:
    print("lab.stillness_control: FAIL"); sys.exit(1)
print("lab.stillness_control: PASS")
PY
