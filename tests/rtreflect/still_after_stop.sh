#!/bin/bash
# rtreflect.still_after_stop (PHOTON-2 stage 1, its fix round and its round 2: SPECS/PHOTON_2_SPEC.md rev 6's bars, R1;
# split in round 4: (e) is rtreflect.lamp_step_held's, lamp_step_held.sh) — THE GLASS IS STILL AFTER A STOP. jahlab on photon-glass (the glossy pane pair at roughness 0.1 / 0.3, the pillars, the
# mirror, the declared mover - parked), a 1.2 m walk INSIDE one c0 lattice cell - EVERY GI ROW AFTER THE STOP OWED BY
# THE MOTION, within its bound (the room's law, gi.hold_after_stop's, PHOTON-7 stage 5, 5-5: the demand's read-back
# lags, so a card arrival and a field plane's refinement come after the stop; tests/support/owed_rows.py judges both
# rows), so the reflection's law is judged on a still world - the stop, then the path's teleport back to the stop at EQUAL GI placements (the door's
# placement pin). One 600-frame window (the rest re-founded, PHOTON-7 stage 6 commit 8: four rest rays a texel a frame,
# no frame cap - PREFLIGHT_R section 3's estimate, (2N + the measured samples) / 4 + 8, about 100 frames).
# THE LAW (physics: under still lights and geometry a reflection is a function of the world and the eye): once the eye
# stops, each texel hands its moving history over to its own rest mean (eased out over 2N = 38 samples,
# jah_reflect_rest.glsl), converges PER TEXEL until its rest mean's standard error has stayed under a code for 8
# frames, then STOPS tracing; the view HOLDS - bit-still - when no texel is left (no frame count holds it):
#   (a) NO RISE over the settle: from 16 frames past the rest's own start (its hand-over's transient: 2N / 4 frames; here
#       the stop's first frame, so frame 17) to the hold, the
#       glossy per-frame mean change decays and never rises - THE RISE LAW, tests/support/rise_law.py (photon-7 stage
#       0's read R2: one robust noise figure for the whole series, every window judged against it, its mean and each of
#       its frames - a burst of any length is a rise). (Its "<= 0.05 codes from frame 17" is DELETED, the lead's ruling:
#       a moving estimate holds <= 12 samples, so the rest's refinement must move the mean by up to its SE, ~sigma /
#       sqrt 12 - no honest estimator reaches 0.05 at frame 17; it read 0.30 on the base.)
#   (a2) THE LAG LAW AT A STOP (PHOTON-7 10-3 R12, the lead's ruling: the rise law says the change shrinks, this one
#       that the picture is the world's within its noise): the eye still, the converged picture does not move, so per
#       glossy pixel at frames 17 / 24 / 32 / 48 / 64 / 96 after the stop |frame(k) - hold| <= 3 sqrt( SE^2 + 1 ) + 1
#       (SE the moving mean's standard error at the stop - the trail's last frame's .rse; the rest mean's at k is no
#       larger - the hold's own error under a code, the +1 two 8-bit pictures' rounding), the breaking share <= 1 %
#       (the dump's window frames, dumpwin=; measured 2026-10-27: 0.001-0.08 %, |frame - hold| p99 1-3 codes);
#   (b') the HOLD - no texel left converging (the door's refl_held: the rest's active count 0, never the picture's
#       stationarity) - reached inside the window, from it on the glossy panes and the mirror change 0 codes, and at the
#       hold NO CAPPED TEXEL (the door's refl_rest_capped 0: every texel stopped by its own standard error within its
#       share of the one-code display budget, sqrt( n_eff ) codes at its pixel - so the displayed SE is under one code
#       where the neighbours' n_eff match, the stop's own guarantee; a capped one stopped at its count's ceiling);
#   (c) before the hold the mirror's per-frame mean change <= 1 code with the mover parked (its fixed estimator);
#   (d) walk vs teleport at EQUAL placements: glossy mean <= 0.5 codes and p99 <= 2 (the MIRROR's half is stage 5's,
#       R2: printed, not judged);
#   (e) the held small lamp step is rtreflect.lamp_step_held's (lamp_step_held.sh).
#   (g) THE REST's COST (re-derived, HARNESS-3 (c)): a rest frame never outweighs a tile frame - read as THE TRACE's
#       WORK through its GPU time, so judged where that time is the work's: in every 10-frame window of the compact
#       path's tail (rise_law's windows) the MEDIAN trace ms <= the MEDIAN over the tile frames (each a full k-4 frame
#       over every batching texel). Why a median: a frame's GPU time is its work over the throughput it had, and the
#       throughput drops on the frames where another GI pass runs beside the trace (the cards' relight, the field's
#       refinement) - an event of SOME frames, never the trace's own; a window's median holds while fewer than half its
#       frames carry one, and the tiles' median is the typical full frame on the same footing. THE FRAGILE FORM IT
#       REPLACED (one frame's GPU ms against the tiles' max) read red 4.63, red 4.85 and green 2.30 ms in three runs of
#       one tree on 2026-10-29 (the trace's own median 0.49 -> 1.32 ms over the rest against the tiles' 3.31 - 4.05): the
#       over-bar frames sat where another GI pass ran beside the trace. That max is still printed, never judged.
#   (f) the held picture against a 4,096-sample reference is a MEASUREMENT (the arm reflect.restReference: 4,300
#       frames, over a row's minute), never this row's: spikes/photon-2/round2/r1.
# Every judgement is a FAIL line, never a traceback (a window that never holds included).
# THE BASE (main 8423ec631, 2026-10-22, 1080p, spikes/photon-2/stage0): glossy mean 0.22 at frame 17 and 0.16 for ever
# (69k pixels every frame); the mirror 1.24 codes every frame; never 0; walk vs teleport 130 codes on the glass.
# usage: still_after_stop.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
HERE=$(cd "$(dirname "$0")" && pwd)
rm -rf "$WD"; mkdir -p "$WD"
POSE=1.5,1.8,3.4,1.5,0.5,-3
WIN=600
LAGK=17,24,32,48,64,96   # (a2)'s frames after the stop: from the rest's hand-over (2N / 4 frames) + its 16
# (the sources this row's bars are Both's: set, never assumed - the mode label below asserts what ran)
cat > "$WD/run.lab" <<S
reflections both
stillness teleport:0.3,1.8,3.4,0.3,0.5,-3/hold:200/walk:$POSE:30 $WIN path trail=1 dump=still dumpwin=$LAGK,$WIN out=still
quit
S
for run in run; do
    "$LAB" --headless --no-stdin --size 960x540 --data-root "$WD/data" --env photon-glass --script "$WD/$run.lab" \
        > "$WD/$run.log" 2>&1
    rc=$?
    [ $rc -eq 0 ] || { echo "FAIL: jahlab ($run) exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR' "$WD/$run.log" | head; tail -5 "$WD/$run.log"; exit 1; }
done
grep '^LAB STILLNESS' "$WD/run.log" | cut -c1-400
HERE=$(cd "$(dirname "$0")" && pwd)
python3 -I "$HERE/../support/reflect_sources.py" tsv "$WD/data/stillness/still.tsv" -- both || {
    echo "rtreflect.still_after_stop: FAIL (the mode label)"; exit 1; }
LAGK=$LAGK python3 -I - "$WD" "$WIN" "$HERE/../support" <<'PY'
import csv, math, os, re, sys
wd, win = sys.argv[1], int(sys.argv[2])
sys.path.insert(0, sys.argv[3])
import owed_rows, rise_law
fails = list(rise_law.selfcheck())   # the law's instrument first: the read's synthetic bursts are rises, a flat tail none
def window(name, which="walk"):
    p = os.path.join(wd, "data", "stillness", name + ".tsv")
    if not os.path.exists(p): return None
    return [r for r in csv.DictReader(open(p), delimiter="\t") if r["window"] == which]
moving = lambda r: int(r["glossy_max"]) > 0 or int(r["mirror_max"]) > 0
def holdOf(rows):
    # THE HOLD IS THE REST'S, NOT THE PICTURE'S (the lead's ruling, PHOTON-7 stage 6): the first frame from which the
    # view holds to the window's end - no texel left converging (the host's read-back active count 0: the door's
    # refl_held 1; the trace no longer dispatched). A picture that stops moving a code while texels still trace is
    # not held.
    late = [i for i in range(1, len(rows)) if rows[i]["refl_held"] != "1"]
    return (late[-1] + 1) if late else 1
rows = window("still")
log = open(os.path.join(wd, "run.log")).read()
if rows is None or len(rows) != win + 1:
    fails.append("the walk window's %d frames: %s" % (win + 1, "no TSV" if rows is None else len(rows)))
    rows = None
if rows:
    owed, stray, lastOwed = owed_rows.judge(rows)
    print("the post-stop GI rows owed by the motion: %s; stray: %s" % (owed or "none", stray or "none"))
    if stray: fails.append("every post-stop GI row owed by the motion within its bound (the room's law): %s" % stray[:4])
    giAt = lambda a, b: [(int(r["frame"]), r["gi_rows"]) for r in rows[a:b] if r["gi_rows"] != "-"]
if rows and not (int(rows[1]["mirror_px"]) > 0 and int(rows[1]["glossy_px"]) > 0):
    fails.append("the panes and the mirror are counted")
if rows:
    hold = holdOf(rows)
    held = hold < len(rows)
    still = held and not any(moving(rows[i]) for i in range(hold, len(rows)))
    traced = [i for i in range(hold, len(rows)) if "rq.reflect.trace:" in rows[i].get("gpu_rows", "")] if held else []
    print("(b') the hold (no texel active): frame %d; from it on the glass changes 0 codes: %s; frames from it on with "
          "the trace's GPU row: %d%s" % (hold, "yes" if still else "NO", len(traced),
                                         (" (first %d)" % traced[0]) if traced else ""))
    rest = int(rows[hold]["refl_rest_frames"]) if held else -1
    capped = int(rows[hold]["refl_rest_capped"]) if held else -1
    print("(b') at the hold: %d rest frames; capped texels %d (the door's refl_rest_capped)" % (rest, capped))
    if not held: fails.append("(b') the glass never held (no texel active) inside the %d-frame window" % (len(rows) - 1))
    elif capped != 0: fails.append("(b') capped texels at the hold: %d (the rest's defect reading must be 0)" % capped)
    elif not still: fails.append("(b') the glass moved after the hold")
    end = hold if held else len(rows)
    glossy = [float(r["glossy_mean"]) for r in rows]
    # FROM THE REST'S OWN SETTLE: 16 frames past the later of its last start and the last change of its inputs (a visible
    # card relight, a close handed at rest - rise_law.settle_start, PHOTON-7 8B B2's re-derivation)
    restStart = max([i for i in range(1, end) if int(rows[i]["refl_rest_frames"]) <= 1] or [1])
    start = rise_law.settle_start(rows, end)
    spans = rise_law.windows(start, end)
    win10 = [sum(glossy[a:b]) / (b - a) for a, b in spans]
    print("(a) glossy mean change, 10-frame windows from %d to the hold: %s" % (start, " ".join("%.4f" % w for w in win10)))
    # (the record prints a mean to 4 decimals: the quantum is the larger of that and one pixel-code over the pixels)
    _, _, sigma, rising = rise_law.judge(glossy, start, end, max(1e-4, 1.0 / max(1, int(rows[1]["glossy_px"]))))
    print("(a) the rise law's noise (sqrt of the mean change, the tail's successive differences): %.5f; rises %d" % (sigma, len(rising)))
    if rising: fails.append("(a) no rise to the hold (the rise law): rises (kind, frame, from, to, bound) %s"
                            % " ".join("%s:%d:%.4f->%.4f(%.4f)" % r for r in rising[:10]))
    if rising: print("(a) the GI rows at the rises' frames (+-10): %s" % (sorted(set(sum([giAt(max(0, r[1] - 10), r[1] + 11) for r in rising], []))) or "none"))
    active = {i: rows[i]["refl_rest_active"] for i in (80, 300, 600) if i < len(rows)}
    print("(b') the rest's active texels at frames 80 / 300 / 600: %s" % " / ".join(str(active.get(i, "-")) for i in (80, 300, 600)))
    mirror = max(float(r["mirror_mean"]) for r in rows[1:max(hold, 2)])
    print("(c) the mirror's mean change before the hold: max %.3f" % mirror)
    if mirror > 1.0: fails.append("(c) the mirror's mean change before the hold <= 1 code: %.3f" % mirror)
    # (g) THE REST'S COST (the lead's bar, PHOTON-7 stage 6): no rest frame's trace costs more than one full TILE frame -
    # THE MAX OVER THE TILE FRAMES (k 4 over every batching texel, the hand-over included; frame 5 alone was a proxy
    # 2.5 % low: the residual's store begins at v = 2N, frames 14-15) - the compact path's tail included (its frames:
    # from the first whose read-back active count is within the list's 16,384).
    def traceMs(r):
        for tok in r.get("gpu_rows", "").split(","):
            if tok.startswith("rq.reflect.trace:"): return float(tok.split(":")[1])
        return -1.0
    rest = [(int(r["refl_rest_frames"]), traceMs(r), int(r["refl_rest_active"]) if r["refl_rest_active"] not in ("-", "") else -1)
            for r in rows[restStart:end] if r["refl_rest_frames"] not in ("-", "")]
    rest = [x for x in rest if x[0] >= 2 and x[1] > 0.0]
    take = next((i for i, x in enumerate(rest) if 0 <= x[2] <= 16384), len(rest))
    tile, tail = rest[:take], rest[take:]
    if tile:
        med = lambda xs: sorted(xs)[len(xs) // 2] if len(xs) % 2 else 0.5 * sum(sorted(xs)[len(xs) // 2 - 1:len(xs) // 2 + 1])
        peak = max(x[1] for x in tile)
        tileMed = med([x[1] for x in tile])
        worst = max(tail, key=lambda x: x[1]) if tail else None
        print("(g) the trace's GPU ms: the tile frames (%d) median %.3f, max %.3f; the compact path from rest frame %s, its "
              "single-frame max %s (the fragile form's reading, printed only)"
              % (len(tile), tileMed, peak, tail[0][0] if tail else "-",
                 ("%.3f at rest frame %d" % (worst[1], worst[0])) if worst else "-"))
        wins = [(tail[a][0], tail[b - 1][0], med([x[1] for x in tail[a:b]])) for a, b in rise_law.windows(0, len(tail))]
        print("(g) the compact path's 10-frame window medians (rest frames: ms): %s"
              % (" ".join("%d-%d:%.3f" % w for w in wins) or "none"))
        top = max(wins, key=lambda w: w[2]) if wins else None
        print("LAW g.tile_median %.4f ms" % tileMed)
        print("LAW g.tile_max %.4f ms" % peak)
        if worst: print("LAW g.tail_single_frame_max %.4f ms" % worst[1])
        if top: print("LAW g.tail_window_median_max %.4f ms <= %.4f" % (top[2], tileMed))
        if top and top[2] > tileMed:
            fails.append("(g) the compact path's window median above the tile frames' median: %.3f ms over rest frames "
                         "%d-%d > %.3f" % (top[2], top[0], top[1], tileMed))
    else:
        fails.append("(g) no tile frame timed (the trace's GPU row)")
m = re.search(r"path walk-vs-teleport \(at equal placements\) \| picture mean/p99/max codes, pixels >= 1: (.*?) \|", log)
if not m:
    fails.append("(d) the path line is missing")
else:
    cls = {k: (float(a), int(b)) for k, a, b in re.findall(r"(\w+)=([\d.]+)/(\d+)/\d+,\d+", m.group(1))}
    print("(d) walk vs teleport at equal placements: glossy %s mirror %s" % (cls["glossy"], cls["mirror"]))
    if cls["glossy"][0] > 0.5 or cls["glossy"][1] > 2: fails.append("(d) glossy mean <= 0.5 and p99 <= 2: %s" % (cls["glossy"],))
    print("(d) the mirror's half is stage 5's (R2), not judged here: %s" % (cls["mirror"],))
# (a2) THE LAG LAW AT A STOP (PHOTON-7 10-3 R12, the lead's ruling: the picture is the world's within its noise): the
# eye still, the converged picture does not move (conv(k+1) - conv(k) = 0), so per glossy pixel at each listed frame k
# after the hand-over |frame(k) - hold| <= 3 sqrt( SE^2 + 1 ) + 1 - SE the moving mean's standard error at the stop (the
# stop frame's .rse: the rest mean's at k, which has taken 4 rays a frame since, is no larger), the hold's own error under
# a code (the stop law's), the +1 the two 8-bit pictures' rounding; the breaking share <= 1 % of the glossy pixels.
try:
    import numpy as np
    dd = os.path.join(wd, "data", "stillness", "dump-still")
    W, H = 960, 540
    base = os.path.join(dd, "still-t000")
    rsz = open(base + ".rsize").read().split()
    rw, rh = int(rsz[0]), int(rsz[1])
    se = np.fromfile(base + ".rse", dtype=np.float32).reshape(rh, rw)
    if (rw, rh) != (W, H):
        se = se[(np.arange(H) * rh // H)][:, (np.arange(W) * rw // W)]
    last = win   # the window's last frame (0 the stop, 1 .. win after it): the hold
    hold = np.fromfile(os.path.join(dd, "still-w0-f%03d.rgb" % last), dtype=np.uint8).reshape(H, W, 3).astype(np.int32)
    cls = np.fromfile(os.path.join(dd, "still-w0-f%03d.cls" % last), dtype=np.uint8).reshape(H, W)
    glossy = cls == 3
    bound = 3.0 * np.sqrt(se.astype(np.float64) ** 2 + 1.0) + 1.0
    for k in [int(x) for x in os.environ.get("LAGK", "").split(",") if x]:
        f = np.fromfile(os.path.join(dd, "still-w0-f%03d.rgb" % k), dtype=np.uint8).reshape(H, W, 3).astype(np.int32)
        lhs = np.abs(f - hold).max(axis=2)
        breaks = glossy & (lhs > bound)
        share = breaks.sum() / max(1, glossy.sum())
        print("(a2) frame %d after the stop: glossy %d px, SE at the stop median %.2f (p90 %.2f) codes, |frame - hold| "
              "mean %.3f p99 %.1f; breaking %.3f %% (bar 1 %%)" % (k, glossy.sum(), np.median(se[glossy]),
              np.percentile(se[glossy], 90), lhs[glossy].mean(), np.percentile(lhs[glossy], 99), 100.0 * share))
        if glossy.sum() < 10000: fails.append("(a2) frame %d: %d glossy pixels - the fixture moved" % (k, glossy.sum()))
        elif share > 0.01: fails.append("(a2) frame %d: %.2f %% of the glossy pixels off their held picture past their noise" % (k, 100.0 * share))
except Exception as ex:
    fails.append("(a2) the lag law's dump: %s" % ex)
print("(f) the held picture against the 4,096-sample reference: a measurement (the arm reflect.restReference), not this row")
for f in fails: print("FAIL:", f)
print("rtreflect.still_after_stop: %s" % ("FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
PY
rc=$?
rm -rf "$WD/data/stillness/dump-"*   # (the dumped pictures, once their numbers are printed)
exit $rc
