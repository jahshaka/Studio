#!/bin/bash
# rtreflect.still_after_stop (PHOTON-2 stage 1, its fix round and its round 2: SPECS/PHOTON_2_SPEC.md rev 6's bars, R1;
# split in round 4: (e) is rtreflect.lamp_step_held's, lamp_step_held.sh) — THE GLASS IS STILL AFTER A STOP. jahlab on photon-glass (the glossy pane pair at roughness 0.1 / 0.3, the pillars, the
# mirror, the declared mover - parked), a 1.2 m walk INSIDE one c0 lattice cell (no GI row may fire after the stop: the
# reflection's own law, alone), the stop, then the path's teleport back to the stop at EQUAL GI placements (the door's
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
#   (b') the HOLD - no texel left converging (the door's refl_held: the rest's active count 0, never the picture's
#       stationarity) - reached inside the window, from it on the glossy panes and the mirror change 0 codes, and at the
#       hold NO CAPPED TEXEL (the door's refl_rest_capped 0: every texel stopped by its own standard error within its
#       share of the one-code display budget, sqrt( n_eff ) codes at its pixel - so the displayed SE is under one code
#       where the neighbours' n_eff match, the stop's own guarantee; a capped one stopped at its count's ceiling);
#   (c) before the hold the mirror's per-frame mean change <= 1 code with the mover parked (its fixed estimator);
#   (d) walk vs teleport at EQUAL placements: glossy mean <= 0.5 codes and p99 <= 2 (the MIRROR's half is stage 5's,
#       R2: printed, not judged);
#   (e) the held small lamp step is rtreflect.lamp_step_held's (lamp_step_held.sh).
#   (g) the rest's cost: no frame's trace above THE MAX OVER THE TILE FRAMES (one full k-4 frame over every batching
#       texel), the compact path's tail included.
#   (f) the held picture against a 4,096-sample reference is a MEASUREMENT (the arm reflect.restReference: 4,300
#       frames, over a row's minute), never this row's: spikes/photon-2/round2/r1.
# Every judgement is a FAIL line, never a traceback (a window that never holds included).
# THE BASE (main 8423ec631, 2026-10-22, 1080p, spikes/photon-2/stage0): glossy mean 0.22 at frame 17 and 0.16 for ever
# (69k pixels every frame); the mirror 1.24 codes every frame; never 0; walk vs teleport 130 codes on the glass.
# usage: still_after_stop.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
POSE=1.5,1.8,3.4,1.5,0.5,-3
WIN=600
# (the sources this row's bars are Both's: set, never assumed - the mode label below asserts what ran)
cat > "$WD/run.lab" <<S
reflections both
stillness teleport:0.3,1.8,3.4,0.3,0.5,-3/hold:200/walk:$POSE:30 $WIN path out=still
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
python3 -I - "$WD" "$WIN" "$HERE/../support" <<'PY'
import csv, math, os, re, sys
wd, win = sys.argv[1], int(sys.argv[2])
sys.path.insert(0, sys.argv[3])
import rise_law
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
if not re.search(r"LAB STILLNESS walk stop=\S+ frames=%d visible=gather lastGiRow=-1 " % win, log):
    fails.append("the walk stayed in one lattice cell: no GI row after the stop")
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
    # FROM THE REST'S OWN START (its last, before the hold) + 16 frames - past its hand-over's transient (2N / 4 frames);
    # here the rest begins on the stop's first frame, so frame 17
    restStart = max([i for i in range(1, end) if int(rows[i]["refl_rest_frames"]) <= 1] or [1])
    start = restStart + 16
    spans = rise_law.windows(start, end)
    win10 = [sum(glossy[a:b]) / (b - a) for a, b in spans]
    print("(a) glossy mean change, 10-frame windows from %d to the hold: %s" % (start, " ".join("%.4f" % w for w in win10)))
    # (the record prints a mean to 4 decimals: the quantum is the larger of that and one pixel-code over the pixels)
    _, _, sigma, rising = rise_law.judge(glossy, start, end, max(1e-4, 1.0 / max(1, int(rows[1]["glossy_px"]))))
    print("(a) the rise law's noise (sqrt of the mean change, the tail's successive differences): %.5f; rises %d" % (sigma, len(rising)))
    if rising: fails.append("(a) no rise to the hold (the rise law): rises (kind, frame, from, to, bound) %s"
                            % " ".join("%s:%d:%.4f->%.4f(%.4f)" % r for r in rising[:10]))
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
        peak = max(x[1] for x in tile)
        worst = max(tail, key=lambda x: x[1]) if tail else None
        print("(g) the trace's GPU ms: the max over the %d tile frames %.3f; the compact path from rest frame %s, its max %s"
              % (len(tile), peak, tail[0][0] if tail else "-", ("%.3f at rest frame %d" % (worst[1], worst[0])) if worst else "-"))
        if worst and worst[1] > peak:
            fails.append("(g) a tail frame's trace above the max over the tile frames: %.3f at rest frame %d > %.3f" % (worst[1], worst[0], peak))
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
print("(f) the held picture against the 4,096-sample reference: a measurement (the arm reflect.restReference), not this row")
for f in fails: print("FAIL:", f)
print("rtreflect.still_after_stop: %s" % ("FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
PY
