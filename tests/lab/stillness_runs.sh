#!/bin/bash
# lab.stillness_runs (PHOTON-2 stage 0): THE STILLNESS DOOR RUNS — `jahlab --env photon-glass` (the reflective fixture:
# the glossy pane pair, the pillars, the mirror, the declared mover) through the `stillness` verb with the path check,
# then the `move` and `lamp` verbs. A fixture row: the door's SHAPE and its invariants, never a stillness bar (the bars
# are the later stages' rows). It asserts: the verb prints a STILLNESS line per window and the path line; the TSV holds
# a header and every window frame (2 x 21 rows); the walk window's mask is the gather's own history count and counts
# pixels of every class the pose shows (rough, glossy, mirror, mover); the reflection count histogram came back for the
# walk window; the move's GI rows were recorded; the teleport window counts every pixel; the scripted move and the lamp
# step are accepted. On the rig's display, at 960x540 (the door's cost is per pixel).
# usage: stillness_runs.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
cat > "$WD/run.lab" <<'S'
stillness teleport:0.3,1.8,3.4,0.3,0.5,-3/hold:40/walk:1.5,1.8,3.4,1.5,0.5,-3:10 20 path out=row
move 1.0,1.8,3.4,1.0,0.5,-3 4
move 0.3,1.8,3.4,0.3,0.5,-3 0
lamp lamp intensity 2.5
lamp lamp position 0,5.5,0
frames 2
quit
S
"$LAB" --headless --no-stdin --size 960x540 --data-root "$WD/data" --env photon-glass --script "$WD/run.lab" > "$WD/lab.log" 2>&1
rc=$?
[ $rc -eq 0 ] || { echo "FAIL: jahlab exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR' "$WD/lab.log" | head; tail -5 "$WD/lab.log"; exit 1; }
grep '^LAB STILLNESS' "$WD/lab.log"
python3 -I - "$WD" <<'PY'
import csv, os, re, sys
wd = sys.argv[1]
log = open(os.path.join(wd, "lab.log")).read()
lines = [l for l in log.splitlines() if l.startswith("LAB STILLNESS ")]
walk = [l for l in lines if l.startswith("LAB STILLNESS walk ")]
tele = [l for l in lines if l.startswith("LAB STILLNESS teleport ")]
path = [l for l in lines if l.startswith("LAB STILLNESS path ")]
assert len(walk) == 1 and len(tele) == 1 and len(path) == 1, "a line per window and the path line: %d/%d/%d" % (len(walk), len(tele), len(path))
assert " visible=gather " in walk[0], "the walk window's mask is the gather's history count: " + walk[0][:160]
assert " visible=all " in tele[0], "the teleport window counts every pixel"
assert "| the move (" in walk[0] and "vct." in walk[0].split("| the move (")[1], "the move's GI rows were recorded"
assert re.search(r"c0=-?\d/-?\d/-?\d", path[0]), "the path line reads the cascades: " + path[0][-200:]
rows = list(csv.DictReader(open(os.path.join(wd, "data", "stillness", "row.tsv")), delimiter="\t"))
assert len(rows) == 42, "every window frame is a row: %d" % len(rows)
w = [r for r in rows if r["window"] == "walk"]
assert [int(r["frame"]) for r in w] == list(range(21)), "the walk window frames 0..20"
for c in ("rough", "glossy", "mirror", "mover"):
    assert int(w[1][c + "_px"]) > 0, "the walk window counts %s pixels" % c
assert int(w[1]["all_px"]) >= sum(int(w[1][c + "_px"]) for c in ("rough", "glossy", "mirror", "foliage", "mover")), "all >= the classes"
assert any(int(r["glossy_refl_gt33"]) >= 0 for r in w[1:]), "the reflection count histogram came back"
t = [r for r in rows if r["window"] == "teleport"]
assert int(t[1]["all_px"]) == 960 * 540, "the teleport window counts every pixel: %s" % t[1]["all_px"]
for verb in ("move 1.0,1.8,3.4,1.0,0.5,-3 4", "move 0.3,1.8,3.4,0.3,0.5,-3 0", "lamp lamp intensity 2.5",
             "lamp lamp position 0,5.5,0"):
    assert ("LAB OK (script) " + verb) in log, "the verb ran: " + verb
for f in ("row-walk.png", "row-teleport.png", "row-walk-heat.png", "row-classes.png"):
    assert os.path.getsize(os.path.join(wd, "data", "stillness", f)) > 0, f
print("lab.stillness_runs: PASS")
PY
