#!/bin/bash
# rtreflect.still_after_stop (PHOTON-2 stage 1 + its fix round, SPECS/PHOTON_2_SPEC.md rev 6's bars, F6/R1-R2) — THE GLASS IS
# STILL AFTER A STOP. jahlab on photon-glass (the glossy pane pair at roughness 0.1 / 0.3, the pillars, the mirror, the
# declared mover), a 1.2 m walk INSIDE one c0 lattice cell (no GI row may fire after the stop: the reflection's own law,
# alone), the stop, then the path's teleport back to the stop at EQUAL GI placements (the door's placement pin); and a
# lamp step smaller than the one-ray sigma at rest against a session converged at the new light. THE LAW (physics:
# under still lights and geometry a reflection is a function of the world and the eye; once the eye stops it rests on
# the gather's rule — a rest mean handed over from the moving history over N = settleFramesOf(32) = 51 frames — and
# HOLDS, bit-still):
#   (a) PENDING R1 (stage 1's round 2, rev 6): from frame 17 the glossy per-frame mean change <= 0.05 codes and
#       DECREASING over 10-frame windows to the hold — printed, not judged, until the per-texel rest lands;
#   (b) from the HOLD frame on the glossy panes and the mirror change 0 codes (the hold's frame printed; rev 6 bounds
#       it by K_max = 512 frames);
#   (c) before the hold the mirror's per-frame mean change <= 1 code with the mover parked (its fixed estimator);
#   (d) walk vs teleport at EQUAL placements: glossy mean <= 0.5 codes and p99 <= 2 (the MIRROR's half moved to stage
#       5's close, R2: printed, not judged);
#   (f) PENDING R1: the held picture against a 4,096-sample reference — not measured by this row yet;
#   (e) a lamp step SMALLER than the one-ray sigma (8 -> 8.16) held at rest equals a session converged at the new
#       light: mean |change| <= 0.05 codes and at most 0.01 % of the pixels more than a code apart (the frozen residual
#       the history detectors' 95 % arrival cannot see). The step's own arrival (<= 16 frames) is
#       rtreflect.history_detectors'.
# THE BASE (main 8423ec631, 2026-10-22, 1080p, spikes/photon-2/stage0): glossy mean 0.22 at frame 17 and 0.16 for ever
# (69k pixels every frame); the mirror 1.24 codes every frame; never 0; walk vs teleport 130 codes on the glass.
# usage: still_after_stop.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
POSE=1.5,1.8,3.4,1.5,0.5,-3
cat > "$WD/run.lab" <<S
stillness teleport:0.3,1.8,3.4,0.3,0.5,-3/hold:200/walk:$POSE:30 230 path out=still
quit
S
printf 'camera %s\nframes 300\nlamp lamp intensity 8.16\nframes 300\nshot e-step\nquit\n' "$POSE" > "$WD/step.lab"
printf 'camera %s\nlamp lamp intensity 8.16\nframes 600\nshot e-ref\nquit\n' "$POSE" > "$WD/ref.lab"
for run in run step ref; do
    "$LAB" --headless --no-stdin --size 960x540 --data-root "$WD/data" --env photon-glass --script "$WD/$run.lab" \
        > "$WD/$run.log" 2>&1
    rc=$?
    [ $rc -eq 0 ] || { echo "FAIL: jahlab ($run) exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR' "$WD/$run.log" | head; tail -5 "$WD/$run.log"; exit 1; }
done
grep '^LAB STILLNESS' "$WD/run.log" | cut -c1-400
python3 -I - "$WD" <<'PY'
import csv, os, re, sys
wd = sys.argv[1]
rows = [r for r in csv.DictReader(open(os.path.join(wd, "data", "stillness", "still.tsv")), delimiter="\t") if r["window"] == "walk"]
assert len(rows) == 231, "the window's 231 frames: %d" % len(rows)
log = open(os.path.join(wd, "run.log")).read()
assert re.search(r"LAB STILLNESS walk stop=\S+ frames=230 visible=gather lastGiRow=-1 ", log), \
    "the walk stayed in one lattice cell: no GI row after the stop"
assert int(rows[1]["mirror_px"]) > 0 and int(rows[1]["glossy_px"]) > 0, "the panes and the mirror are counted"
moving = lambda r: int(r["glossy_max"]) > 0 or int(r["mirror_max"]) > 0
late = [i for i in range(1, len(rows)) if moving(rows[i])]
hold = (late[-1] + 1) if late else 1
fails = []
if hold >= len(rows): fails.append("(b) the glass never held inside the %d-frame window" % (len(rows) - 1))
print("(b) the hold: frame %d (0 codes from it on: %s)" % (hold, "yes" if hold < len(rows) else "NO"))
g17 = float(rows[17]["glossy_mean"])
win = [sum(float(rows[i]["glossy_mean"]) for i in range(a, min(a + 10, hold))) / max(1, min(10, hold - a))
       for a in range(17, hold, 10)]
print("(a) PENDING R1 (not judged): glossy mean at 17 %.3f; 10-frame window means 17 -> hold: %s"
      % (g17, " ".join("%.3f" % w for w in win)))
mirror = max(float(r["mirror_mean"]) for r in rows[1:max(hold, 2)])
print("(c) the mirror's mean change before the hold: max %.3f" % mirror)
if mirror > 1.0: fails.append("(c) the mirror's mean change before the hold <= 1 code: %.3f" % mirror)
m = re.search(r"path walk-vs-teleport \(at equal placements\) \| picture mean/p99/max codes, pixels >= 1: (.*?) \|", log)
assert m, "the path line"
cls = {k: (float(a), int(b)) for k, a, b in re.findall(r"(\w+)=([\d.]+)/(\d+)/\d+,\d+", m.group(1))}
print("(d) walk vs teleport at equal placements: glossy %s mirror %s" % (cls["glossy"], cls["mirror"]))
if cls["glossy"][0] > 0.5 or cls["glossy"][1] > 2: fails.append("(d) glossy mean <= 0.5 and p99 <= 2: %s" % (cls["glossy"],))
print("(d) the mirror's half is stage 5's (R2), not judged here: %s" % (cls["mirror"],))
try:
    from PIL import Image
except ImportError:
    Image = None
if Image:
    a = Image.open(os.path.join(wd, "data", "shots", "e-step.png")).convert("RGB").tobytes()
    b = Image.open(os.path.join(wd, "data", "shots", "e-ref.png")).convert("RGB").tobytes()
    d = [max(abs(a[i] - b[i]), abs(a[i + 1] - b[i + 1]), abs(a[i + 2] - b[i + 2])) for i in range(0, len(a), 3)]
    mean, over = sum(d) / len(d), sum(1 for v in d if v > 1)
    print("(e) the held small lamp step vs the converged new light: mean %.4f, %d px > 1 code, max %d" % (mean, over, max(d)))
    if mean > 0.05 or over > len(d) * 1e-4: fails.append("(e) mean <= 0.05 and <= 0.01 %% over a code: %.4f, %d px" % (mean, over))
else:
    fails.append("(e) PIL is missing: the lamp-step pictures cannot be compared")
print("(f) PENDING R1: the held picture against a 4,096-sample reference is not measured by this row yet")
for f in fails: print("FAIL:", f)
print("rtreflect.still_after_stop: %s" % ("FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
PY
