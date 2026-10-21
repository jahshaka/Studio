#!/bin/bash
# lab.boots_and_shoots (LAB-1 stage 1): `jahlab --headless --script` on the kit-bashed box set (tests/meshbake's
# kitBash() written to an OBJ): it loads, BAKES into the lab's data root, shows the Triangles view, renders 30 frames,
# shoots a PNG and dumps the engine's status; the Atom cut drew (cutTriangles > 0) and its pixels are on screen (the
# Density view's visible Atom pixels > 0, the shot's false colours). A SECOND RUN of the same script skips the parse and
# the bake (the cache's law: "CACHE HIT"), and its status equals the first run's cut.
# usage: boots_and_shoots.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
HERE=$(cd "$(dirname "$0")" && pwd)
rm -rf "$WD"; mkdir -p "$WD"
python3 -I "$HERE/kitbash_obj.py" "$WD/kitbash.obj" || { echo "FAIL: the box set"; exit 1; }
cat > "$WD/s.txt" <<'S'
view triangles
frames 30
shot a
view density
frames 10
status s.json
quit
S
fail=0
for run in 1 2; do
    "$LAB" --headless --no-stdin --size 960x540 --data-root "$WD/data" --script "$WD/s.txt" "$WD/kitbash.obj" \
        > "$WD/run$run.log" 2>&1
    rc=$?
    grep -E '^LAB (LOAD|FIRST|SHOT|END)' "$WD/run$run.log"
    [ $rc -eq 0 ] || { echo "FAIL: run $run exited $rc"; tail -20 "$WD/run$run.log"; exit 1; }
done
grep -q '^LAB LOAD kitbash.obj: BAKED' "$WD/run1.log" || { echo "FAIL: the first run did not bake"; fail=1; }
grep -q '^LAB LOAD kitbash.obj: CACHE HIT' "$WD/run2.log" || { echo "FAIL: the second run did not hit the bake cache"; fail=1; }
python3 -I - "$WD" <<'PY' || fail=1
import json, os, re, sys
wd = sys.argv[1]
rows = [json.loads(l) for l in open(os.path.join(wd, "data", "s.json")) if l.strip()]
assert len(rows) == 2, "two status lines (one per run), got %d" % len(rows)
for i, r in enumerate(rows):
    a = r["atom"]
    assert a["on"] and a["cutValid"], "run %d: the Atom split is not live / no cut read" % (i + 1)
    assert a["cutTriangles"] > 0, "run %d: cutTriangles %s" % (i + 1, a["cutTriangles"])
    assert a["densityValid"] and a["densityPixels"] > 0, "run %d: no visible Atom pixels" % (i + 1)
    print("run %d: cutTriangles %d, clusters %d, Atom pixels %d of %d objects" % (i + 1, a["cutTriangles"], a["cutClusters"], a["densityPixels"], a["densityObjects"]))
assert rows[0]["atom"]["cutTriangles"] == rows[1]["atom"]["cutTriangles"], "the cached open cut differently"
for n in (1, 2):
    m = [l for l in open(os.path.join(wd, "run%d.log" % n)) if l.startswith("LAB SHOT ")]
    assert m, "run %d: no shot" % n
    c = int(re.search(r"colours (\d+)", m[0]).group(1))
    assert c > 100, "run %d: the Triangles view shows %d colours (its false colours are missing)" % (n, c)
assert os.path.getsize(os.path.join(wd, "data", "shots", "a.png")) > 0
print("ok")
PY
[ $fail -eq 0 ] && echo "lab.boots_and_shoots: PASS" || echo "lab.boots_and_shoots: FAIL"
exit $fail
