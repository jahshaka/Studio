#!/usr/bin/env bash
#
# library.scale — A LIBRARY OF 10,000 ASSETS AND 500 PROJECTS RUNS AS MANY STATEMENTS AS ONE OF 1,000 AND 50
# (LARGE-LIBRARY-1 stage 2; the owner: a large library must list as fast as a small one). Headless (NULL), no card.
#
#   1. The generator (test_library_statements --generate, tests/support/libraryfixture.h) writes root A (1,000 assets,
#      50 projects) and root B (10,000, 500) — its time printed apart, never a law — and both get the PREVIOUS
#      index set (this build's covering and bake indexes dropped), as a library an earlier build wrote.
#   2. THE FIRST SESSION per root: the shipped primitives' seed and the boot's ONE-TIME INDEX GATE (main.cpp:
#      Database::prepareIndexes on a worker before the shell opens the library). Asserted: the gate ran (its line,
#      its ms printed), the session ended 0. Its boot is printed, never barred: a library's first session is not
#      the boot the owner's sentence is about.
#   3. THE SECOND SESSION per root — A RETURNING USER'S: the boot, open the generated project (the tray's listing is
#      inside it), assets.list, assets.list({members}), assets.selected, the first entry to Materials, the Desktop,
#      desktop.gridStats, project.create — each read around by app.databaseReads with JAH_DB_STATEMENT_TRACE=1.
#      LAWS:
#      - per step, the boot included, TRACED statements(B) == statements(A);
#      - boot, open, create and the Desktop: bytes(B) - bytes(A) <= 50 B an added asset (450,000 B);
#      - no thumbnail select is ever a listing's (every one keyed by guid), in either session;
#      - the gate costs nothing on a prepared library (no gate line), no DEFECT is said, the bake sweep rebuilds
#        nothing (the generator's models carry their real bakes);
#      - gridStats: outOfStep 0, lastBuildMaxSliceTiles <= sliceTiles, lastBuildDecodes 0, and tiles + pendingTiles
#        == P: the grid builds in slices that run only when a verb turns the event loop, and a script holds the loop
#        for its whole run (scriptrunner.cpp), so how far the build got is not the script's to settle — every
#        project is a tile or waiting for one, exactly once;
#      - NEGATIVE CONTROL: a step made N+1 on purpose grows with the library (the comparer sees growth).
#      PRINTED, never barred: the logged counts by name (the workers' tile reads among them: ruled call 3 — only the
#      default connection is traced), the ms, the heartbeat's worst gap, and RESIDUE (B): the first entry to
#      Materials, whose listing selects each material's definition behind its rendered tile.
#   4. Root A: one assets.importFile of a real PNG against a generated texture, column by column (call 5).
# databaseReads unavailable = SKIP 77. The roots (~600 MB) are removed at the end.
#
# usage: library_scale.sh <jahshaka> --headless <generator> <script.js> <library_columns.py> <png>   cwd = the fresh home's run dir
set -u
APP="$1"; MODEFLAG="$2"; GEN="$3"; JS="$4"; COLUMNS="$5"; PNG="$6"
[ "$MODEFLAG" = "--headless" ] || { echo "FAIL: usage: library_scale.sh <app> --headless <generator> <js> <columns.py> <png>"; exit 2; }
fail=0
check() { if [ "$1" = "0" ]; then echo "ok:   $2"; else echo "FAIL: $2"; fail=$((fail + 1)); fi }
cleanup() { rm -rf "$PWD/root-a" "$PWD/root-b"; }
trap cleanup EXIT

declare -A ASSETS=([a]=1000 [b]=10000) PROJECTS=([a]=50 [b]=500)
for r in a b; do
    rm -rf "root-$r"; mkdir -p "root-$r"
    "$GEN" --generate "$PWD/root-$r/JahLibrary.db" "$PWD/root-$r/AssetStore" "${ASSETS[$r]}" "${PROJECTS[$r]}" > "gen-$r.log" 2>&1
    check $? "the generator wrote root $r (${ASSETS[$r]} assets, ${PROJECTS[$r]} projects)"
    grep -a "^generate: ok" "gen-$r.log" | cut -c1-140 | sed 's/^/   /'
    # THE PREVIOUS INDEX SET: a library an earlier build wrote (the boot's gate builds what it lacks).
    python3 -I - "$PWD/root-$r/JahLibrary.db" <<'PY'
import sqlite3, sys
c = sqlite3.connect(sys.argv[1])
for n in ("idx_assets_library", "idx_assets_collection_listed", "idx_assets_material_copies", "idx_asset_files_bake"):
    c.execute("DROP INDEX IF EXISTS " + n)
c.execute("CREATE INDEX IF NOT EXISTS idx_assets_collection ON assets (collection)")
c.commit()
PY
done
OPEN_A=$(sed -n 's/.*open project \([0-9a-f-]*\).*/\1/p' gen-a.log | head -1)
OPEN_B=$(sed -n 's/.*open project \([0-9a-f-]*\).*/\1/p' gen-b.log | head -1)
TEXTURE_A=$(sed -n 's/^generate: texture \([0-9a-f-]*\) .*/\1/p' gen-a.log | head -1)

run() {   # $1 = root, $2 = mode, $3 = open guid, $4 = png or empty
    { printf 'var MODE = "%s";\nvar OPEN = "%s";\nvar PNG = "%s";\n' "$2" "$3" "$4"; cat "$JS"; } > "$2-$1.js"
    JAH_DB_STATEMENT_TRACE=1 "$APP" "$MODEFLAG" --data-root "$PWD/root-$1" --script "$PWD/$2-$1.js" > "$2-$1.log" 2>&1
}
for r in a b; do
    open=$OPEN_A; [ "$r" = b ] && open=$OPEN_B
    run "$r" first "$open" ""
    check $? "root $r's FIRST session ended 0"
    if grep -aq "^SCALE unavailable" "first-$r.log"; then
        echo "SKIP: app.databaseReads is unavailable here ($(sed -n 's/^SCALE unavailable //p' first-$r.log | head -1))"; exit 77
    fi
    gate=$(grep -a "index statement(s) built before the shell opened it" "first-$r.log" | head -1)
    [ -n "$gate" ]; check $? "root $r's first boot ran the one-time index gate before the shell: ${gate#*library: }"
    echo "   root $r first session: $(grep -a '^SCALE step=boot' "first-$r.log" | sed 's/^SCALE //') (printed, never barred)"
    png=""; [ "$r" = a ] && png="$PNG"
    run "$r" law "$open" "$png"
    check $? "root $r's returning session ended 0"
    ! grep -aq "index statement(s) built before the shell opened it" "law-$r.log"
    check $? "root $r's returning boot: the gate runs nothing on a prepared library"
    ! grep -aq "library: DEFECT" "first-$r.log" "law-$r.log"
    check $? "root $r: no index was built on the opening thread (no DEFECT)"
    ! grep -aq "mesh bake: rebuilding" "first-$r.log" "law-$r.log"
    check $? "root $r: the bake sweep rebuilds nothing (the generated models carry their real bakes)"
done

python3 -I - law-a.log law-b.log "${ASSETS[a]}" "${ASSETS[b]}" "${PROJECTS[a]}" "${PROJECTS[b]}" > compare.txt <<'PY'
import json, re, sys
la, lb = sys.argv[1], sys.argv[2]
added = int(sys.argv[4]) - int(sys.argv[3])
P = {"a": int(sys.argv[5]), "b": int(sys.argv[6])}
def steps(path):
    out, grid, beat = {}, None, None
    for line in open(path, errors="replace"):
        if line.startswith("SCALE step="):
            f = dict(kv.split("=", 1) for kv in line[6:].strip().split(" ") if "=" in kv)
            out[f["step"]] = f
        elif line.startswith("SCALE grid "): grid = json.loads(line[len("SCALE grid "):])
        elif line.startswith("SCALE heartbeat "): beat = json.loads(line[len("SCALE heartbeat "):])
    return out, grid, beat
(a, ga, ha), (b, gb, hb) = steps(la), steps(lb)
order = ["boot", "open", "list", "listMembers", "selected", "materials", "desktop", "create"]
bounded = {"boot", "open", "create", "desktop"}
bar = 50 * added
def ok(cond, text): print(("ok:   " if cond else "FAIL: ") + text)
print(f"   {'step':12} {'statements A/B':>16} {'bytes A':>11} {'bytes B':>11} {'B-A':>11} {'logged A/B':>11} {'ms A/B':>11}")
for s in order:
    if s not in a or s not in b: ok(False, f"step {s} printed in both sessions"); continue
    x, y = a[s], b[s]
    d = int(y["bytes"]) - int(x["bytes"])
    print(f"   {s:12} {x['statements'] + '/' + y['statements']:>16} {x['bytes']:>11} {y['bytes']:>11} {d:>11} "
          f"{x.get('logged', '-') + '/' + y.get('logged', '-'):>11} {x.get('ms', '-') + '/' + y.get('ms', '-'):>11}")
for s in order:
    if s not in a or s not in b: continue
    ok(a[s]["statements"] == b[s]["statements"],
       f"{s}: the same TRACED statements at {sys.argv[3]} / {P['a']} as at {sys.argv[4]} / {P['b']} ({a[s]['statements']} and {b[s]['statements']})")
    if s in bounded:
        d = int(b[s]["bytes"]) - int(a[s]["bytes"])
        ok(d <= bar, f"{s}: bytes(B) - bytes(A) = {d} <= {bar} (50 B an added asset)")
    if s != "boot":
        ok(a[s].get("nonGuidThumbnailSelects") == "0" and b[s].get("nonGuidThumbnailSelects") == "0",
           f"{s}: every thumbnail select keyed by guid (listings select none)")
for s in ("open", "materials", "desktop", "create"):
    if s in a and s in b: print(f"   by name, {s}: A {a[s].get('byName', '')}\n   by name, {s}: B {b[s].get('byName', '')}")
if "materials" in a and "materials" in b:
    print(f"   residue (B): the first entry to Materials reads {a['materials']['bytes']} / {b['materials']['bytes']} B "
          f"(fetchAssetsInHome selects each material's definition behind its rendered tile: the owner's question)")
n = "negativeControl"
if n in a and n in b:
    grew = int(b[n]["statements"]) - int(a[n]["statements"])
    ok(grew >= (added * 63 // 100) // 100, f"NEGATIVE CONTROL: a step made N+1 on purpose grows by {grew} statements (the comparer sees growth)")
for r, g in (("a", ga), ("b", gb)):
    if not g: ok(False, f"root {r}: desktop.gridStats printed"); continue
    ok(g.get("outOfStep") == 0 and g.get("lastBuildDecodes") == 0 and g.get("lastBuildMaxSliceTiles", 1 << 30) <= g.get("sliceTiles", 0)
       and g.get("tiles", -1) + g.get("pendingTiles", -1) == P[r],
       f"root {r}: gridStats outOfStep {g.get('outOfStep')}, lastBuildDecodes {g.get('lastBuildDecodes')}, lastBuildMaxSliceTiles "
       f"{g.get('lastBuildMaxSliceTiles')} <= sliceTiles {g.get('sliceTiles')}, tiles {g.get('tiles')} + pending {g.get('pendingTiles')} == {P[r]}")
for r, h in (("a", ha), ("b", hb)):
    if h: print(f"   root {r} heartbeat: worst gap {h.get('maxGapMs')} ms over {h.get('ticks')} ticks (printed, never barred)")
PY
grep -a "^FAIL" compare.txt > /dev/null && cfail=$(grep -ac "^FAIL" compare.txt) || cfail=0
cat compare.txt
fail=$((fail + cfail))

IMPORTED=$(sed -n 's/^SCALE import \([0-9a-f-]*\) .*/\1/p' law-a.log | head -1)
[ -n "$IMPORTED" ] && [ -n "$TEXTURE_A" ]
check $? "root A: a real PNG imported (${IMPORTED:-none}) beside a generated texture (${TEXTURE_A:-none})"
if [ -n "$IMPORTED" ] && [ -n "$TEXTURE_A" ]; then
    python3 -I "$COLUMNS" "$PWD/root-a/JahLibrary.db" "$PWD/root-a/JahLibrary.db" "texture=$IMPORTED:$TEXTURE_A" > columns.txt 2>&1
    check $? "the generated texture's row has a real import's columns, class by class ($(tail -1 columns.txt))"
    grep -a "DIFF" columns.txt | head -10 | sed 's/^/   /'
fi
if [ $fail -ne 0 ]; then
    for f in first-a first-b law-a law-b; do echo "---- $f.log"; grep -aE '^SCALE|assert|Error|FAIL' "$f.log" | cut -c1-300 | tail -8; done
    echo "library.scale: FAILED ($fail)"; exit 1
fi
echo "library.scale: PASSED"
