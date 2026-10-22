#!/bin/bash
# lab.dolly_matches_probe (LAB-1 stage 3): scripts/lab/dolly.sh on the kit-bashed box set at two stops — the dolly's
# table equals the engine's own readouts at each stop, +- 0: the table's cut triangles are the engine's AtomDrawStatus
# (Scene::atomDrawStatus, the status line jahlab writes at the stop), and the cut probe's drawn triangles — the table's
# hidden-share denominator, summed over the objects the probe read — equal that same cut (a settled stop: the probe's
# frame and the status's counters describe one cut). Runs on the rig's display, under the card's lock (re-entrant).
# usage: dolly_matches_probe.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../.." && pwd)
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
python3 -I "$HERE/kitbash_obj.py" "$WD/kitbash.obj" || exit 1
cat > "$WD/stops.txt" <<'S'
camera 2.85,3,5,2.85,1.35,0   # near
camera 2.85,40,300,2.85,1.35,0  # far
S
JAHLAB="$LAB" "$TREE/scripts/lab/dolly.sh" --size 960x540 --data-root "$WD/data" "$WD/kitbash.obj" "$WD/stops.txt" "$WD/out"
rc=$?
[ $rc -eq 0 ] || { echo "FAIL: dolly.sh exited $rc"; tail -20 "$WD/out/dolly.log"; exit 1; }
python3 -I - "$WD/out" <<'PY'
import csv, json, os, sys
out = sys.argv[1]
table = list(csv.DictReader(open(os.path.join(out, "table.tsv")), delimiter="\t"))
status = [json.loads(l) for l in open(os.path.join(out, "status.jsonl"))]
assert len(table) == 2 == len(status), "two stops: table %d, status %d" % (len(table), len(status))
for t, s in zip(table, status):
    a, p = s["atom"], s["probe"]
    assert a["on"] and a["cutValid"], "stop %s: no cut read" % t["stop"]
    checks = [("cutTriangles", int(t["cutTriangles"]), a["cutTriangles"]),
              ("cutClusters", int(t["cutClusters"]), a["cutClusters"]),
              ("occludedObjects", int(t["occludedObjects"]), a["occluded"]),
              ("probeTriangles", int(t["probeTriangles"]), p["drawnTriangles"]),
              ("probe == the engine's cut", p["drawnTriangles"], a["cutTriangles"])]
    for what, x, y in checks:
        assert x == y, "stop %s %s: %s != %s" % (t["name"], what, x, y)
    assert a["cutTriangles"] > 0, "stop %s: nothing drawn" % t["name"]
    print("stop %s (%s): cut %d tris = probe %d, %d clusters, %d occluded, hidden %s" % (
        t["stop"], t["name"], a["cutTriangles"], p["drawnTriangles"], a["cutClusters"], a["occluded"], t["hiddenTriangles"]))
print("lab.dolly_matches_probe: PASS")
PY
