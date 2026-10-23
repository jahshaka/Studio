#!/bin/bash
# gi.hold_after_stop (PHOTON-2 stage 2 and its round, SPECS/PHOTON_2_SPEC.md rev 8's H4 as rev 9 restated it: J1-J5) —
# THE GATHER SETTLES, THEN HOLDS.
# jahlab's stillness door at 960x540, five sessions; the gather's four states (H1): MOVING, SETTLING (the camera still,
# an input written or owed: the EMA, no hold), RESTING (the rest mean), HELD; the field's rest budget (H2):
#   ROOM, a 1.2 m walk inside one lattice cell (the no-regression check): no GI row after the stop, the gather held
#     by frame 17, the plaster's per-frame change <= 1 code from frame 1;
#   ROOM, a lamp step 8 -> 12 at rest (f, J2): THE FIELD'S LAW (PHOTON-7 stage 5, 5-4: the 36 frames were the deleted
#     rest budget's arithmetic) - its last write <= max(the voxel store's rest frame, the frame the integrations owed
#     then run out at that frame's grant) + the restart's depth (4) + the marks' readback lag (4) + 1, and every frame
#     while restarts are owed spends its whole grant (field_law, below) - the per-frame change non-increasing over
#     10-frame windows to the hold -
#     THE RISE LAW, tests/support/rise_law.py (PHOTON-7 stage 0's read R2: one robust noise figure for the whole
#     series, never a window's own, and every window judged against it, its mean and each of its frames - a burst of
#     any length is a rise; the tolerance THIS row's own, rule R3; J5's 0.01 is the court's (a) only), then the hold,
#     and 0 codes after it;
#   COURT, the 15 m crossing (cascade commits inside the walk), after the stop: (a) the per-frame change
#     non-increasing over 10-frame windows until the hold (0.01-code tolerance, J5); (b) 0 codes after the hold - no
#     burst; (c) the hold <= the last frame the hold waited on + 17 (the door's hold_key: the gather's rest key moved -
#     every write, a ray-level refit or a light write with no GI row included - or its inputs owed work), the field's
#     law (the room's, above; the 40 frames deleted with the rest budget); the rest frames' GPU against the moving
#     median and the walk's marks per outer commit printed;
#     (d, J1) a follow frame's extra GPU (the ifd.follow row: the entered planes invalidated, faded in one sample a
#     pass at the budget) <= 3 ms, and on the move's last 60 frames the frames the planes fill (ifd.follow / ifd.enter)
#     step no more pixels beyond their 3x3 neighbourhood's motion change of the frame before (the door's step_px)
#     than the same frames of the same crossing with the planes integrated whole in the follow frame (the arm
#     gi.fieldEnterInline: the field already there, nothing to fade) - by at most 2 % of that run's or 50 pixels;
#     the rest budget's share of a settling frame's busy GPU printed (J4's bar, <= 0.5 ms, is Bistro's, measured in
#     the lab);
#   COURT, THE FOLIAGE CLASS (K5: foliage = the alpha-tested canopy, its leaf mask): the gather wrote its class on the
#     canopy (>= 1000 foliage pixels in its own history at the stop - 0 when the class has no writer: the base), and
#     the same crossing again under the class-off arm (gather.classOff: every pixel the 1 % rule, no class split) -
#     the pixels whose history only the foliage rule accepted (a depth delta between the 1 % and the 3 % bounds: the
#     door's foliage_rule) > 0 with the class and 0 under the arm (red on an inverted or missing class test, L8); the
#     canopy's share of pixels restarting their history during the move's last 60 frames and its per-frame change
#     after the stop, class vs no class, printed.
# usage: hold_after_stop.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
HERE=$(cd "$(dirname "$0")" && pwd)
rm -rf "$WD"; mkdir -p "$WD"
ROOM=1.5,2,3,1.5,1.5,-4
printf 'stillness teleport:0.3,2,3,0.3,1.5,-4/hold:300/walk:%s:30 60 out=room-sub\nquit\n' "$ROOM" > "$WD/room-sub.lab"
printf 'camera %s\nframes 300\nlamp lamp intensity 12\nstillness teleport:%s/hold:1 120 out=room-lamp\nquit\n' "$ROOM" "$ROOM" > "$WD/room-lamp.lab"
printf 'stillness teleport:0,3,18,0,3,-6/hold:300/walk:0,3,3,0,3,-21:60 120 trail=60 restarts out=court-x\nquit\n' > "$WD/court-x.lab"
printf 'arm gather.classOff 1\nstillness teleport:0,3,18,0,3,-6/hold:300/walk:0,3,3,0,3,-21:60 120 trail=60 restarts out=court-off\nquit\n' > "$WD/court-off.lab"
printf 'arm gi.fieldEnterInline 1\nstillness teleport:0,3,18,0,3,-6/hold:300/walk:0,3,3,0,3,-21:60 120 trail=60 out=court-inline\nquit\n' > "$WD/court-inline.lab"
for run in room-sub:photon-room room-lamp:photon-room court-x:photon-court court-off:photon-court court-inline:photon-court; do
    name=${run%%:*}; env=${run#*:}
    "$LAB" --headless --no-stdin --size 960x540 --data-root "$WD/data" --env "$env" --script "$WD/$name.lab" \
        > "$WD/$name.log" 2>&1
    rc=$?
    [ $rc -eq 0 ] || { echo "FAIL: jahlab ($name) exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR' "$WD/$name.log" | head; tail -5 "$WD/$name.log"; exit 1; }
    grep '^LAB STILLNESS walk' "$WD/$name.log" | cut -c1-300
done
python3 -I - "$WD" "$HERE/../support" <<'PY'
import csv, os, re, sys
wd = sys.argv[1]
sys.path.insert(0, sys.argv[2])
import rise_law
def window(name, part="walk"):
    return [r for r in csv.DictReader(open(os.path.join(wd, "data", "stillness", name + ".tsv")), delimiter="\t") if r["window"] == part]
def rows_of(r):
    return [] if r["gi_rows"] == "-" else [it.split(":")[0] for it in r["gi_rows"].split(",")]
def held_from(w):
    return next((int(r["frame"]) for r in w if r["held"] == "1"), None)
def windows_to(w, h):
    chg = [float(r["all_mean"]) for r in w]
    end = h if h is not None else len(w)
    return [sum(chg[a:min(a + 10, end)]) / max(1, min(10, end - a)) for a in range(1, end, 10)]
def field_law(w):
    # PHOTON-7 stage 5 (5-4), THE LEAD'S LAW for the field's last write (36 / 40 were B8's arithmetic, deleted with it):
    # <= max(the voxel store's rest frame R - the first frame from which the chain owes nothing - and the frame the owed
    # integrations run out at R's grant) + the restart's depth (4) + the marks' readback lag (4) + 1; and every frame
    # while restarts are owed spends its whole grant (within one restart's depth).
    fr = [int(r["frame"]) for r in w]
    owed = [int(r["store_owed"]) for r in w]
    R = next((fr[i] for i in range(len(w)) if not any(owed[i:])), None)
    last = max([f for r, f in zip(w, fr) if any(n.startswith("ifd.") for n in rows_of(r))] or [-1])
    short = [(fr[k], int(w[k]["ifd_spent"]), int(w[k]["ifd_grant"])) for k in range(1, len(w))
             if int(w[k - 1]["ifd_owed_restarts"]) > 0 and int(w[k]["ifd_spent"]) + 4 <= int(w[k]["ifd_grant"])]
    spend = " ".join("%d:%s/%s" % (fr[k], w[k]["ifd_spent"], w[k]["ifd_grant"]) for k in range(len(w)) if int(w[k]["ifd_grant"]))
    if R is None:
        return None, None, None, None, last, short, spend
    i = fr.index(R)
    W = int(w[i]["ifd_owed_integrations"])
    grants = [int(r["ifd_grant"]) for r in w[i:] if int(r["ifd_grant"])]
    G = grants[0] if grants else 0
    exhaust = R + (-(-W // G) if W and G else 0)
    return R, W, G, max(R, exhaust) + 4 + 4 + 1, last, short, spend
def rises_of(wins):   # J5: a rise beyond 0.01 code (the court's (a) only - the lead's ruling for that law)
    return [(i, round(wins[i], 3), round(wins[i + 1], 3)) for i in range(len(wins) - 1) if wins[i + 1] > wins[i] + 0.01]
fails = list(rise_law.selfcheck())   # the law's instrument first: the read's synthetic bursts are rises, a flat tail none
# ROOM, the sub-cell walk
w = window("room-sub")
rows = sorted({n for r in w[1:] for n in rows_of(r)})
h = held_from(w)
over = [(int(r["frame"]), int(r["plaster_max"])) for r in w[1:] if int(r["plaster_max"]) > 1]
print("room sub-cell: GI rows after the stop %s; held from frame %s; plaster frames over a code: %s" % (rows or "none", h, over[:6]))
if rows: fails.append("room sub-cell: no GI row after the stop: %s" % rows)
if h is None or h > 17: fails.append("room sub-cell: the gather held by frame 17: %s" % h)
if over: fails.append("room sub-cell: the plaster <= 1 code a frame from frame 1: %s" % over[:6])
# ROOM, the lamp step (f)
w = window("room-lamp")
conv = [int(r["frame"]) for r in w if "ifd.converge" in rows_of(r)]
done = max(conv) + 1 if conv else 0
lR, lW, lG, lbound, llast, lshort, lspend = field_law(w)
h = held_from(w)
after = [int(r["frame"]) for r in w if h is not None and int(r["frame"]) > h and int(r["all_max"]) > 0]
# (the record prints a mean to 4 decimals: the quantum is the larger of that and one pixel-code over the pixels)
spans, lwins, lsigma, lrises = rise_law.judge([float(r["all_mean"]) for r in w], 1, h if h is not None else len(w),
                                              max(1e-4, 1.0 / max(1, int(w[1]["all_px"]))))
print("room lamp step: the field's change on frames %s, done %d frames after the step; THE LAW: the store at rest from frame %s, %s integrations owed then at a grant of %s, so the last write <= %s - the field's last write %d; frames spending under the grant while restarts were owed: %s; 10-frame windows (sqrt of the mean change; its noise %.5f) %s; held from %s; frames changing after the hold: %d"
      % ("%d-%d" % (min(conv), max(conv)) if conv else "none", done, lR, lW, lG, lbound, llast, lshort[:6] or "none", lsigma,
         " ".join("%.4f" % x for x in lwins), h, len(after)))
print("room lamp step: the grant spent per frame (frame:spent/grant): %s" % lspend)
if not conv: fails.append("room lamp step: the field never integrated the change")
elif lbound is None or llast > lbound: fails.append("(f) room lamp step: the field's last write within the law: %d > %s" % (llast, lbound))
if lshort: fails.append("(f) room lamp step: every frame while restarts are owed spends its grant: %s" % lshort[:6])
if lrises: fails.append("(f) room lamp step: the change non-increasing to the hold (the rise law): rises (kind, frame, from, to, bound) %s"
                        % [(k, f, round(a, 4), round(b, 4), round(c, 4)) for k, f, a, b, c in lrises[:8]])
if h is None: fails.append("(f) room lamp step: the picture never held")
elif after: fails.append("(f) room lamp step: no change after the hold: %d frames from %d" % (len(after), after[0]))
# COURT, the crossing
w = window("court-x")
h = held_from(w)
lastw = max([int(r["frame"]) for r in w[1:] if r["hold_key"] == "1"] or [0])
lastrow = max([int(r["frame"]) for r in w if rows_of(r)] or [-1])
lastf = max([int(r["frame"]) for r in w if any(n.startswith("ifd.") for n in rows_of(r))] or [-1])
cR, cW, cG, cbound, clast, cshort, cspend = field_law(w)
mv = sorted(float(r["frame_gpu_ms"]) for r in window("court-x", "walk-move") if float(r["frame_gpu_ms"]) >= 0)
mmed = mv[len(mv) // 2] if mv else -1.0
restg = [(int(r["frame"]), float(r["frame_gpu_ms"])) for r in w[1:] if float(r["frame_gpu_ms"]) >= 0]
# THE WALK'S MARKS PER OUTER COMMIT (PHOTON-7 stage 5, 5-4's baseline for the cone-shell commit's premise): the probes the
# accumulation marked over the move's trail and the window, against the outer cascades' commits (vct.scrollN /
# vct.cascadeN, N >= 1) in the same frames
both = window("court-x", "walk-move") + w
commits = sum(sum(1 for n in rows_of(r) if n[:10] in ("vct.scroll", "vct.cascad") and n[-1:].isdigit() and n[-1:] != "0") for r in both)
marked = int(both[-1]["ifd_marked"]) - int(both[0]["ifd_marked"]) if both else 0
wins = windows_to(w, h)
rises = rises_of(wins)
after = [int(r["frame"]) for r in w if h is not None and int(r["frame"]) > h and int(r["all_max"]) > 0]
print("court crossing: held from %s; the last frame the hold waited on %d (hold - that = %s; the last GI row %d); the field's last write %d; 10-frame windows %s; frames changing after the hold %d"
      % (h, lastw, None if h is None else h - lastw, lastrow, lastf, " ".join("%.3f" % x for x in wins), len(after)))
if rises: fails.append("(a) court: the per-frame change non-increasing over 10-frame windows (0.01 tolerance): rises %s" % rises)
if h is None: fails.append("(b) court: the picture never held")
elif after: fails.append("(b) court: no change after the hold: %d frames from %d" % (len(after), after[0]))
if h is not None and h > lastw + 17: fails.append("(c) court: the hold <= the last frame it waited on + 17: %d > %d" % (h, lastw + 17))
if h is not None and h <= lastw: fails.append("(c) court: the hold after the last frame it waited on: %d <= %d" % (h, lastw))
print("court crossing: THE LAW: the store at rest from frame %s, %s integrations owed then at a grant of %s, so the field's last write <= %s - its last write %d; frames spending under the grant while restarts were owed: %s; the rest frames' GPU (information: stage 0's whole-frame law is the hitch guard) largest %.2f ms against the moving frames' median %.2f (twice it %.2f)"
      % (cR, cW, cG, cbound, clast, cshort[:6] or "none", max([g for _, g in restg] or [-1.0]), mmed, 2.0 * mmed))
print("court crossing: the walk's marks (the cone-shell commit's baseline): %d probes marked by the moves over %d outer commits (%.0f a commit)"
      % (marked, commits, marked / max(1, commits)))
print("court crossing: the grant spent per frame (frame:spent/grant): %s" % cspend)
if cbound is None or clast > cbound: fails.append("(c) court: the field's last write within the law: %d > %s" % (clast, cbound))
if cshort: fails.append("(c) court: every frame while restarts are owed spends its grant: %s" % cshort[:6])
log = open(os.path.join(wd, "court-x.log")).read()
m = re.search(r"ifd\.follow=([\d.]+)\((\d+)\)", log)
me = re.search(r"ifd\.enter=([\d.]+)\((\d+)\)", log)
if m:
    extra = float(m.group(1))
    print("court crossing: the follow frames' ifd.follow %.3f ms GPU (%s frames; bar <= 3); the entering passes' ifd.enter %s"
          % (extra, m.group(2), "%s ms (%s frames)" % (me.group(1), me.group(2)) if me else "none"))
    if extra > 3.0: fails.append("(d) court: a follow frame's extra GPU <= 3 ms: %.2f" % extra)
else:
    fails.append("(d) court: no follow frame measured (no GPU timing)")
inl = {r["frame"]: int(r["step_px"]) for r in window("court-inline", "walk-move")}
fill = [(r["frame"], int(r["step_px"]), inl.get(r["frame"], -1)) for r in window("court-x", "walk-move")
        if int(r["step_px"]) >= 0 and any(n in ("ifd.follow", "ifd.enter") for n in rows_of(r))]
over = [f for f in fill if f[2] < 0 or f[1] > f[2] + max(50, f[2] // 50)]
print("court crossing: steps beyond the neighbourhood's motion (step_px) on the %d fill frames: %d more than the inline arm's at most (max %s vs %s)"
      % (len(fill), max([f[1] - f[2] for f in fill] or [0]), max([f[1] for f in fill] or [None]), max([f[2] for f in fill] or [None])))
if not fill: fails.append("(d) court: the move's trail carries no fill frame")
elif over: fails.append("(d) court: a fill frame steps more pixels than the same frame with the planes inline: %s" % over[:4])
sett = [r for r in w[1:] if r["held"] != "1" and float(r["frame_gpu_ms"]) >= 0]
withf = [float(r["frame_gpu_ms"]) for r in sett if any(n.startswith("ifd.") for n in rows_of(r))]
without = [float(r["frame_gpu_ms"]) for r in sett if not any(n.startswith("ifd.") for n in rows_of(r))]
if withf and without:
    print("court crossing: a settling frame's busy GPU with the field's rest budget %.2f ms (%d), without %.2f (%d): it adds %.2f ms"
          % (sum(withf) / len(withf), len(withf), sum(without) / len(without), len(without), sum(withf) / len(withf) - sum(without) / len(without)))
# COURT, THE FOLIAGE CLASS (K5)
def restarts(name):
    tr = [r for r in window(name, "walk-move") if int(r["class_px"]) >= 0]
    px = sum(int(r["class_px"]) for r in tr)
    return len(tr), (sum(int(r["class_restart"]) for r in tr) / px if px else -1.0)
def canopy_after(w):
    return max(int(r["foliage_max"]) for r in w[1:]), round(sum(float(r["foliage_mean"]) for r in w[1:]) / max(1, len(w) - 1), 4)
stop_px = int(w[0]["class_px"])
n_on, share_on = restarts("court-x")
woff = window("court-off")
n_off, share_off = restarts("court-off")
print("court foliage class: %d foliage pixels in the gather's history at the stop (bar >= 1000); the canopy restarting during the move's last 60 frames: class %.4f (%d frames read), no class %.4f (%d); after the stop, max/mean codes: class %s, no class %s; held from %s / %s"
      % (stop_px, share_on, n_on, share_off, n_off, canopy_after(w), canopy_after(woff), held_from(w), held_from(woff)))
if stop_px < 1000: fails.append("K5 court: the gather's foliage class on the canopy (>= 1000 pixels at the stop): %d" % stop_px)
rule_on = sum(int(r["foliage_rule"]) for r in window("court-x", "walk-move") + window("court-x"))
rule_off = sum(int(r["foliage_rule"]) for r in window("court-off", "walk-move") + window("court-off"))
print("court foliage rule: the histories only the 3 %% bound accepted, over the move's trail and the window: class %d, class off %d (bars > 0 / 0)" % (rule_on, rule_off))
if rule_on <= 0: fails.append("L8 court: the foliage rule accepts some history with the class: %d" % rule_on)
if rule_off != 0: fails.append("L8 court: the foliage rule accepts none under gather.classOff: %d" % rule_off)
for f in fails: print("FAIL:", f)
print("gi.hold_after_stop: %s" % ("FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
PY
