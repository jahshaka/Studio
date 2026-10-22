#!/bin/bash
# rtreflect.still_after_stop (PHOTON-2 stage 1, its fix round and its round 2: SPECS/PHOTON_2_SPEC.md rev 6's bars, R1) —
# THE GLASS IS STILL AFTER A STOP. jahlab on photon-glass (the glossy pane pair at roughness 0.1 / 0.3, the pillars, the
# mirror, the declared mover - parked), a 1.2 m walk INSIDE one c0 lattice cell (no GI row may fire after the stop: the
# reflection's own law, alone), the stop, then the path's teleport back to the stop at EQUAL GI placements (the door's
# placement pin); and a lamp step smaller than the one-ray sigma at rest against a session converged at the new light.
# THE LAW (physics: under still lights and geometry a reflection is a function of the world and the eye): once the eye
# stops, each texel hands its moving history over to its own rest mean (eased out over 2N = 102 samples,
# jah_reflect_rest.glsl), converges PER TEXEL until its answer has changed under a code for 8 frames, then STOPS
# tracing; the view HOLDS - bit-still - when no texel is left or at 512 rest frames (K_max):
#   (a) from frame 17 the glossy per-frame mean change <= 0.05 codes and DECREASING over 10-frame windows to the hold;
#   (b) from the HOLD frame on the glossy panes and the mirror change 0 codes, the hold at frame <= 512;
#   (c) before the hold the mirror's per-frame mean change <= 1 code with the mover parked (its fixed estimator);
#   (d) walk vs teleport at EQUAL placements: glossy mean <= 0.5 codes and p99 <= 2 (the MIRROR's half is stage 5's,
#       R2: printed, not judged);
#   (e) a lamp step SMALLER than the one-ray sigma (8 -> 8.16) HELD at rest (the session's door says it held before
#       its shot) equals a session converged at the new light (held likewise): mean |change| <= 0.05 codes and at most
#       0.01 % of the pixels more than a code apart. The step's own arrival (<= 16 frames) is
#       rtreflect.history_detectors'.
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
WIN=530
cat > "$WD/run.lab" <<S
stillness teleport:0.3,1.8,3.4,0.3,0.5,-3/hold:200/walk:$POSE:30 $WIN path out=still
quit
S
# the step and its reference, each through the door at the stop's pose (a teleport to the pose it stands at: no move;
# the reference's 20 frames give the gather the history the door's mask counts)
printf 'camera %s\nframes 300\nlamp lamp intensity 8.16\nstillness teleport:%s/hold:1 %s out=estep\nshot e-step\nquit\n' \
    "$POSE" "$POSE" "$WIN" > "$WD/step.lab"
printf 'camera %s\nlamp lamp intensity 8.16\nstillness teleport:%s/hold:20 %s out=eref\nshot e-ref\nquit\n' \
    "$POSE" "$POSE" "$WIN" > "$WD/ref.lab"
for run in run step ref; do
    "$LAB" --headless --no-stdin --size 960x540 --data-root "$WD/data" --env photon-glass --script "$WD/$run.lab" \
        > "$WD/$run.log" 2>&1
    rc=$?
    [ $rc -eq 0 ] || { echo "FAIL: jahlab ($run) exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR' "$WD/$run.log" | head; tail -5 "$WD/$run.log"; exit 1; }
done
grep '^LAB STILLNESS' "$WD/run.log" | cut -c1-400
python3 -I - "$WD" "$WIN" <<'PY'
import csv, os, re, sys
wd, win = sys.argv[1], int(sys.argv[2])
fails = []
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
    if not held: fails.append("(b) the glass never held inside the %d-frame window" % (len(rows) - 1))
    elif hold > 512: fails.append("(b) the hold at frame <= 512: %d" % hold)
    end = hold if held else len(rows)
    starts = list(range(17, end, 10))
    win10 = [sum(float(rows[i]["glossy_mean"]) for i in range(a, min(a + 10, end))) / min(10, end - a) for a in starts]
    print("(a) glossy mean change, 10-frame windows from 17 to the hold: %s" % " ".join("%.4f" % w for w in win10))
    over = [(a, w) for a, w in zip(starts, win10) if w > 0.05]
    if over: fails.append("(a) <= 0.05 codes from frame 17: %s" % " ".join("%d:%.4f" % x for x in over))
    rising = [(starts[i + 1], win10[i], win10[i + 1]) for i in range(len(win10) - 1) if win10[i + 1] > win10[i]]
    if rising: fails.append("(a) decreasing to the hold: rises at %s" % " ".join("%d:%.4f->%.4f" % x for x in rising))
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
for name in ("estep", "eref"):
    r = window(name)
    if not r or len(r) != win + 1:
        fails.append("(e) the %s session's door window: %s" % (name, "no TSV" if not r else len(r)))
        continue
    if not int(r[1]["glossy_px"]) > 0:
        fails.append("(e) the %s session's door counted no glass" % name)
        continue
    h = holdOf(r)
    print("(e) the %s session held at frame %d of %d before its shot" % (name, h, win))
    if h >= len(r): fails.append("(e) the %s session never held before its shot" % name)
try:
    from PIL import Image
except ImportError:
    Image = None
if not Image:
    fails.append("(e) PIL is missing: the lamp-step pictures cannot be compared")
elif all(os.path.exists(os.path.join(wd, "data", "shots", n + ".png")) for n in ("e-step", "e-ref")):
    a = Image.open(os.path.join(wd, "data", "shots", "e-step.png")).convert("RGB").tobytes()
    b = Image.open(os.path.join(wd, "data", "shots", "e-ref.png")).convert("RGB").tobytes()
    d = [max(abs(a[i] - b[i]), abs(a[i + 1] - b[i + 1]), abs(a[i + 2] - b[i + 2])) for i in range(0, len(a), 3)]
    mean, over = sum(d) / len(d), sum(1 for v in d if v > 1)
    print("(e) the held small lamp step vs the converged new light: mean %.4f, %d px > 1 code, max %d" % (mean, over, max(d)))
    if mean > 0.05 or over > len(d) * 1e-4: fails.append("(e) mean <= 0.05 and <= 0.01 %% over a code: %.4f, %d px" % (mean, over))
else:
    fails.append("(e) the step's or the reference's shot is missing")
print("(f) the held picture against the 4,096-sample reference: a measurement (the arm reflect.restReference), not this row")
for f in fails: print("FAIL:", f)
print("rtreflect.still_after_stop: %s" % ("FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
PY
