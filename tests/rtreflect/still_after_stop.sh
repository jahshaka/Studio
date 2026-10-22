#!/bin/bash
# rtreflect.still_after_stop (PHOTON-2 stage 1, its fix round and its round 2: SPECS/PHOTON_2_SPEC.md rev 6's bars, R1;
# split in round 4: (e) is rtreflect.lamp_step_held's, lamp_step_held.sh) — THE GLASS IS STILL AFTER A STOP. jahlab on photon-glass (the glossy pane pair at roughness 0.1 / 0.3, the pillars, the
# mirror, the declared mover - parked), a 1.2 m walk INSIDE one c0 lattice cell (no GI row may fire after the stop: the
# reflection's own law, alone), the stop, then the path's teleport back to the stop at EQUAL GI placements (the door's
# placement pin). Two 600-frame windows: P1's stop on the standard error runs most texels to K_max, so the view holds
# on the 513th rest frame and a 530-frame window ended before it (the round-3 red at 2fb7f67b).
# THE LAW (physics: under still lights and geometry a reflection is a function of the world and the eye): once the eye
# stops, each texel hands its moving history over to its own rest mean (eased out over 2N = 102 samples,
# jah_reflect_rest.glsl), converges PER TEXEL until its answer has changed under a code for 8 frames, then STOPS
# tracing; the view HOLDS - bit-still - when no texel is left or at 512 rest frames (K_max):
#   (a) from frame 17 the glossy per-frame mean change <= 0.05 codes and DECREASING over 10-frame windows to the hold -
#       THE RISE LAW, tests/support/rise_law.py (PHOTON-7 stage 0's read R2: one robust noise figure for the whole
#       series, never a window's own, and every window judged against it, its mean and each of its frames - a burst of
#       any length is a rise; the tolerance THIS row's own, rule R3);
#   (b) from the HOLD frame on the glossy panes and the mirror change 0 codes, the hold at <= 513 rest frames (K_max
#       512, the view holding on the frame after it);
#   (c) before the hold the mirror's per-frame mean change <= 1 code with the mover parked (its fixed estimator);
#   (d) walk vs teleport at EQUAL placements: glossy mean <= 0.5 codes and p99 <= 2 (the MIRROR's half is stage 5's,
#       R2: printed, not judged);
#   (e) the held small lamp step is rtreflect.lamp_step_held's (lamp_step_held.sh).
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
cat > "$WD/run.lab" <<S
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
    late = [i for i in range(1, len(rows)) if moving(rows[i])]
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
    print("(b) the hold: frame %d (0 codes from it on: %s; K_max 512)" % (hold, "yes" if held else "NO"))
    rest = int(rows[hold]["refl_rest_frames"]) if held else -1
    print("(b) the reflection's rest frames at the hold: %d (<= 513: the view holds on the frame after K_max 512)" % rest)
    if not held: fails.append("(b) the glass never held inside the %d-frame window" % (len(rows) - 1))
    elif rest > 513: fails.append("(b) the hold at <= 513 rest frames: %d" % rest)
    end = hold if held else len(rows)
    glossy = [float(r["glossy_mean"]) for r in rows]
    spans = rise_law.windows(17, end)
    win10 = [sum(glossy[a:b]) / (b - a) for a, b in spans]
    print("(a) glossy mean change, 10-frame windows from 17 to the hold: %s" % " ".join("%.4f" % w for w in win10))
    over = [(a, w) for (a, b), w in zip(spans, win10) if w > 0.05]
    if over: fails.append("(a) <= 0.05 codes from frame 17: %s" % " ".join("%d:%.4f" % x for x in over))
    # (the record prints a mean to 4 decimals: the quantum is the larger of that and one pixel-code over the pixels)
    _, _, sigma, rising = rise_law.judge(glossy, 17, end, max(1e-4, 1.0 / max(1, int(rows[1]["glossy_px"]))))
    print("(a) the rise law's noise (sqrt of the mean change, the tail's successive differences): %.5f; rises %d" % (sigma, len(rising)))
    if rising: fails.append("(a) decreasing to the hold (the rise law): rises (kind, frame, from, to, bound) %s"
                            % " ".join("%s:%d:%.4f->%.4f(%.4f)" % r for r in rising[:10]))
    mirror = max(float(r["mirror_mean"]) for r in rows[1:max(hold, 2)])
    print("(c) the mirror's mean change before the hold: max %.3f" % mirror)
    if mirror > 1.0: fails.append("(c) the mirror's mean change before the hold <= 1 code: %.3f" % mirror)
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
