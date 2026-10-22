#!/bin/bash
# rtreflect.still_after_stop (PHOTON-2 stage 1, SPECS/PHOTON_2_SPEC.md rev 4's bars) — THE GLASS IS STILL AFTER A STOP.
# jahlab on photon-glass (the glossy pane pair at roughness 0.1 / 0.3, the pillars, the mirror, the declared mover), a
# 1.2 m walk INSIDE one c0 lattice cell (no GI row may fire after the stop: the reflection's own law, alone), the stop,
# 30 frames measured by the stillness door on the pixels the gather saw before the stop. THE LAW (physics: under still
# lights and geometry a reflection is a function of the world and the eye; once the eye stops, only noise falling under
# a code may change, and a still estimator rests and HOLDS):
#   (a) at frame 17 — the gather's settle, the hold — the glossy panes' per-frame MEAN change <= 0.05 codes and their
#       share of pixels changing by a code or more <= the plaster's share on its last moving frame (16);
#   (b) from frame 19 on the glossy panes and the mirror change 0 codes (the hold at 17; the screen march reflects the
#       previous frame's colour, one frame of lag; one of margin);
#   (c) before the hold, the mirror's per-frame mean change <= 1 code with the mover in the room (its estimator fixed:
#       PHOTON-2 1c).
# THE BASE (main 8423ec631, measured 2026-10-22 at 1080p, spikes/photon-2/stage0): glossy mean 0.22 at frame 17 and 0.16
# for ever (69k pixels every frame); the mirror 1.24 codes every frame; never 0.
# usage: still_after_stop.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
cat > "$WD/run.lab" <<'S'
stillness teleport:0.3,1.8,3.4,0.3,0.5,-3/hold:200/walk:1.5,1.8,3.4,1.5,0.5,-3:30 30 out=still
quit
S
"$LAB" --headless --no-stdin --size 960x540 --data-root "$WD/data" --env photon-glass --script "$WD/run.lab" > "$WD/lab.log" 2>&1
rc=$?
[ $rc -eq 0 ] || { echo "FAIL: jahlab exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR' "$WD/lab.log" | head; tail -5 "$WD/lab.log"; exit 1; }
grep '^LAB STILLNESS' "$WD/lab.log"
python3 -I - "$WD" <<'PY'
import csv, os, sys
wd = sys.argv[1]
rows = [r for r in csv.DictReader(open(os.path.join(wd, "data", "stillness", "still.tsv")), delimiter="\t") if r["window"] == "walk"]
assert len(rows) == 31, "the window's 31 frames: %d" % len(rows)
log = open(os.path.join(wd, "lab.log")).read()
assert " visible=gather lastGiRow=-1 " in log, "the walk stayed in one lattice cell: no GI row after the stop"
share = lambda r, c: int(r[c + "_changed"]) / max(1, int(r[c + "_px"]))
g17, p16 = rows[17], rows[16]
print("frame 17: glossy mean %.3f share %.4f; plaster share at 16 %.4f" % (float(g17["glossy_mean"]), share(g17, "glossy"), share(p16, "rough")))
assert float(g17["glossy_mean"]) <= 0.05, "(a) the panes' mean change at frame 17 <= 0.05 codes: %s" % g17["glossy_mean"]
assert share(g17, "glossy") <= share(p16, "rough"), "(a) the panes' changing share at 17 <= the plaster's at 16"
late = [(int(r["frame"]), int(r["glossy_max"]), int(r["mirror_max"])) for r in rows[19:] if int(r["glossy_max"]) or int(r["mirror_max"])]
assert not late, "(b) the glass holds from frame 19: %s" % late[:5]
mirror = max(float(r["mirror_mean"]) for r in rows[1:17])
print("mirror mean before the hold: max %.3f" % mirror)
assert mirror <= 1.0, "(c) the mirror's mean change before the hold <= 1 code: %.3f" % mirror
assert int(rows[1]["mirror_px"]) > 0 and int(rows[1]["glossy_px"]) > 0, "the panes and the mirror are counted"
print("rtreflect.still_after_stop: PASS")
PY
