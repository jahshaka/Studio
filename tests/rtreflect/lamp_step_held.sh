#!/bin/bash
# rtreflect.lamp_step_held (PHOTON-2 stage 1's round 2, rev 6's (e); split out of rtreflect.still_after_stop in round 4 so
# each row runs its law in under a minute) — A SMALL LAMP STEP AT REST IS HELD AT THE NEW LIGHT'S PICTURE.
# jahlab on photon-glass (the glossy pane pair at roughness 0.1 / 0.3, the pillars, the mirror, the declared mover -
# parked) at still_after_stop's stop pose; two sessions, each through the stillness door with a 600-frame window (the
# rest re-founded, PHOTON-7 stage 6 commit 8: four rest rays a texel a frame, no frame cap - a view holds when every texel
# has stopped by its standard error, about 100 frames on the glass):
#   (e) a lamp step SMALLER than the one-ray sigma (8 -> 8.16) applied at rest, HELD (the door says it held before its
#       shot), equals a session converged at the new light (held likewise): mean |change| <= 0.05 codes and at most
#       0.01 % of the pixels more than a code apart; each session's hold inside its window with NO CAPPED TEXEL (the
#       door's refl_rest_capped 0: every live texel's displayed standard error under a code) and 0 codes after it, and
#       NO RISE over the rest's own settle (16 frames past the rest's last start - the GI relights first, and its rows
#       break the reflection's rest - to the hold: tests/support/rise_law.py, as rtreflect.still_after_stop's (a)).
#       The step's own arrival (<= 16 frames) is rtreflect.history_detectors'.
# Every judgement is a FAIL line, never a traceback (a window that never holds included).
# usage: lamp_step_held.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
POSE=1.5,1.8,3.4,1.5,0.5,-3
WIN=600
# the step and its reference, each through the door at the stop's pose (a teleport to the pose it stands at: no move;
# the reference's 20 frames give the gather the history the door's mask counts)
printf 'camera %s\nframes 300\nlamp lamp intensity 8.16\nstillness teleport:%s/hold:1 %s out=estep\nshot e-step\nquit\n' \
    "$POSE" "$POSE" "$WIN" > "$WD/step.lab"
printf 'camera %s\nlamp lamp intensity 8.16\nstillness teleport:%s/hold:20 %s out=eref\nshot e-ref\nquit\n' \
    "$POSE" "$POSE" "$WIN" > "$WD/ref.lab"
for run in step ref; do
    "$LAB" --headless --no-stdin --size 960x540 --data-root "$WD/data" --env photon-glass --script "$WD/$run.lab" \
        > "$WD/$run.log" 2>&1
    rc=$?
    [ $rc -eq 0 ] || { echo "FAIL: jahlab ($run) exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR' "$WD/$run.log" | head; tail -5 "$WD/$run.log"; exit 1; }
done
HERE=$(cd "$(dirname "$0")" && pwd)
python3 -I - "$WD" "$WIN" "$HERE/../support" <<'PY'
import csv, os, sys
wd, win = sys.argv[1], int(sys.argv[2])
sys.path.insert(0, sys.argv[3])
import rise_law
fails = list(rise_law.selfcheck())
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
for name in ("estep", "eref"):
    r = window(name)
    if not r or len(r) != win + 1:
        fails.append("(e) the %s session's door window: %s" % (name, "no TSV" if not r else len(r)))
        continue
    if not int(r[1]["glossy_px"]) > 0:
        fails.append("(e) the %s session's door counted no glass" % name)
        continue
    h = holdOf(r)
    capped = int(r[h]["refl_rest_capped"]) if h < len(r) else -1
    print("(e) the %s session held at frame %d of %d before its shot (capped texels %d)" % (name, h, win, capped))
    if h >= len(r): fails.append("(e) the %s session never held before its shot" % name)
    elif capped != 0: fails.append("(e) the %s session's hold has %d capped texels (must be 0)" % (name, capped))
    end = h if h < len(r) else len(r)
    # THE REST'S OWN SETTLE: from its last start + 16 frames (the GI relights after the lamp's change first - its rows
    # break the reflection's rest, and its own settle is not the rest's) to the hold
    restStart = max([i for i in range(1, end) if int(r[i]["refl_rest_frames"]) <= 1] or [1])
    if end > restStart + 26:
        g = [float(x["glossy_mean"]) for x in r]
        _, _, sigma, rising = rise_law.judge(g, restStart + 16, end, max(1e-4, 1.0 / max(1, int(r[1]["glossy_px"]))))
        print("(e) the %s session's settle: the rise law's noise %.5f, rises %d" % (name, sigma, len(rising)))
        if rising: fails.append("(e) the %s session rises before its hold: %s" % (name, " ".join("%s:%d:%.4f->%.4f(%.4f)" % x for x in rising[:6])))
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
for f in fails: print("FAIL:", f)
print("rtreflect.lamp_step_held: %s" % ("FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
PY
