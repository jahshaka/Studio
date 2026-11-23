#!/usr/bin/env bash
#
# library.scale — A LIBRARY OF 10,000 ASSETS AND 500 PROJECTS RUNS AS MANY STATEMENTS AS ONE OF 1,000 AND 50
# (LARGE-LIBRARY-1 stage 2; the owner: a large library must list as fast as a small one). Headless (NULL), no card.
#
#   1. The generator (test_library_statements --generate, tests/support/libraryfixture.h) writes root A (1,000 assets,
#      50 projects) and root B (10,000, 500) — its time printed apart, never a law — and both get the PREVIOUS
#      index set (this build's covering and bake indexes dropped), as a library an earlier build wrote.
#   2. THE FIRST SESSION per root: the shipped primitives' seed and the boot's ONE-TIME INDEX GATE (main.cpp:
#      Database::prepareIndexes on a worker before the shell opens the library). Asserted: the gate ran (its RESULT,
#      app.bootReport().indexGate: statements built > 0, its ms printed), the session ended 0. Its boot is printed, never barred: a library's first session is not
#      the boot the owner's sentence is about.
#   3. THE SECOND SESSION per root — A RETURNING USER'S: the boot, open the generated project (the tray's listing is
#      inside it), assets.list, assets.list({members}), assets.selected, the first entry to Materials, the Desktop,
#      desktop.gridStats, project.create — each read around by app.databaseReads with JAH_DB_STATEMENT_TRACE=1.
#      LAWS:
#      - per step, the boot included, TRACED statements(B) == statements(A);
#      - boot, open, create and the Desktop: bytes(B) - bytes(A) <= 50 B an added asset (450,000 B);
#      - no thumbnail select is ever a listing's (every one keyed by guid), in either session;
#      - the gate costs nothing on a prepared library (indexGate.built 0), no index is built on the opening thread
#        (indexAtOpen 0), the bake sweep rebuilds nothing (sweep 'planned', staleRebuilds 0: the script JOINS the
#        plan - app.bootReport({joinSweep}) returns when the sweep's worker has published a terminal state, its
#        bound the worker's own end - and any other state is a FAIL by its name; the generator's models carry their
#        real bakes) - every one a RESULT the session's script prints from app.bootReport(), never a log line;
#      - THE SWEEP OFF IS READ AS OFF: root A's first session runs with JAHSHAKA_BAKE_SWEEP=0 and its join answers
#        'off' at once (never a hang, never a 0);
#      - gridStats: outOfStep 0, lastBuildMaxSliceTiles <= sliceTiles, lastBuildDecodes 0, and tiles + pendingTiles
#        == P: the grid builds in slices that run only when a verb turns the event loop, and a script holds the loop
#        for its whole run (scriptrunner.cpp), so how far the build got is not the script's to settle — every
#        project is a tile or waiting for one, exactly once;
#      - every step SUCCEEDED (no lastError; open, list and create read the library): a step failing the same way
#        at both sizes would pass the equality;
#      - THE FIRST ENTRY TO MATERIALS FILLS THE CUSTOM DRAWER ONCE: the first session saved the open project's tab
#        set with a Materials-home material open (one made by materials.create), so the entry restores it (EffectsPage::showEvent) — the drawer's
#        listing runs once, never from the show path and again from the space's entry;
#      - THE MEMBERS PANEL LISTS ONCE: the first entry to Materials (a restored tab) and a material opened from the
#        custom drawer each list the Members panel's home pictures ONCE (fetchAssetsInHome.type2 == 1: its
#        materialmembers::unused) - an open used to show the same document three times and re-ask the panel twice
#        more (measured on the parent's page: 5 at the entry, 3 at an open from the drawer; NOQT-DATABASE-1);
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

run() {   # $1 = root, $2 = mode, $3 = open guid, $4 = png or empty, $5 = the bake sweep (1 on, 0 off; default on)
    { printf 'var MODE = "%s";\nvar OPEN = "%s";\nvar PNG = "%s";\n' "$2" "$3" "$4"; cat "$JS"; } > "$2-$1.js"
    JAH_DB_STATEMENT_TRACE=1 JAHSHAKA_BAKE_SWEEP="${5:-1}" "$APP" "$MODEFLAG" --data-root "$PWD/root-$1" --script "$PWD/$2-$1.js" \
        > "$2-$1.log" 2>&1
}
sweep() {   # $1 = a session's output: its "SCALE report" line's sweep state (the joined plan's terminal state)
    sed -n 's/^SCALE report .*\bsweep=\([a-zA-Z]*\).*/\1/p' "$1" | head -1
}
report() {   # $1 = a session's output, $2 = a field of its script's "SCALE report" line (app.bootReport(): a RESULT)
    sed -n "s/^SCALE report .*\b$2=\(-\{0,1\}[0-9][0-9]*\).*/\1/p" "$1" | head -1
}
for r in a b; do
    open=$OPEN_A; [ "$r" = b ] && open=$OPEN_B
    off=1; [ "$r" = a ] && off=0   # root A's first session: the sweep OFF (its join must answer 'off')
    run "$r" first "$open" "" "$off"
    check $? "root $r's FIRST session ended 0"
    if grep -aq "^SCALE unavailable" "first-$r.log"; then
        echo "SKIP: app.databaseReads is unavailable here ($(sed -n 's/^SCALE unavailable //p' first-$r.log | head -1))"; exit 77
    fi
    gate=$(report "first-$r.log" gateBuilt); gate_ms=$(report "first-$r.log" gateMs)
    [ "${gate:-0}" -gt 0 ]; check $? "root $r's first boot ran the one-time index gate before the shell: ${gate:-none} statement(s) in ${gate_ms:-none} ms"
    echo "   root $r first session: $(grep -a '^SCALE step=boot' "first-$r.log" | sed 's/^SCALE //') (printed, never barred)"
    grep -aq "^SCALE tabs saved .* lastError=null" "first-$r.log"
    check $? "root $r's first session saved the open project's tab set with a Materials-home material open in it"
    png=""; [ "$r" = a ] && png="$PNG"
    run "$r" law "$open" "$png"
    check $? "root $r's returning session ended 0"
    grep -aq "^SCALE drawerMaterial ok" "law-$r.log"
    check $? "root $r: the custom drawer shows the first session's second material (the open-from-drawer step's input)"
    grep -aq "^SCALE restored ok" "law-$r.log"
    check $? "root $r: the first entry restored the saved tab set, its material's tile in the drawer ($(sed -n 's/^SCALE restored //p' "law-$r.log" | cut -c1-80))"
    [ "$(report "law-$r.log" gateBuilt)" = 0 ]
    check $? "root $r's returning boot: the gate runs nothing on a prepared library (indexGate.built $(report "law-$r.log" gateBuilt); the first boot's above is its control)"
    [ "$(report "first-$r.log" indexAtOpen)" = 0 ] && [ "$(report "law-$r.log" indexAtOpen)" = 0 ]
    check $? "root $r: no index was built on the opening thread (indexAtOpen $(report "first-$r.log" indexAtOpen) and $(report "law-$r.log" indexAtOpen))"
    for m in first law; do
        s=$(sweep "$m-$r.log"); n=$(report "$m-$r.log" staleRebuilds)
        if [ "$r$m" = afirst ]; then
            [ "$s" = off ]; check $? "root a's first session (JAHSHAKA_BAKE_SWEEP=0): the joined sweep answers 'off' (${s:-no report line})"
        elif [ "$s" != planned ]; then
            check 1 "root $r's $m session: the sweep's plan ended '${s:-no report line}', not 'planned'"
        else
            [ "$n" = 0 ]; check $? "root $r's $m session: the bake sweep rebuilds nothing (planned, staleRebuilds $n; the generated models carry their real bakes)"
        fi
    done
done

# THE CONTROL OF THE SWEEP's ZERO (CODING_RULES § 6): root A with every recorded bake row dropped. The boot's seed rebuilds
# its own 17 primitives' bakes before the sweep starts (synchronously: shelllifecycle.cpp openLibrary), so what the sweep
# then plans is the generated models' bakes: staleRebuilds must read ABOVE zero, or the zeros above prove nothing.
python3 -I - "$PWD/root-a/JahLibrary.db" <<'PY'
import sqlite3, sys
c = sqlite3.connect(sys.argv[1])
c.execute("DELETE FROM asset_files WHERE role = 'bake'")
c.commit()
PY
run a control "$OPEN_A" ""
check $? "root a's CONTROL session (its bake rows dropped) ended 0"
s=$(sweep control-a.log); n=$(report control-a.log staleRebuilds)
[ "$s" = planned ] && [ "${n:-0}" -gt 0 ]
check $? "CONTROL: with root a's bake rows dropped the sweep plans ABOVE zero (sweep '${s:-no report line}', staleRebuilds ${n:-none})"

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
order = ["boot", "open", "list", "listMembers", "selected", "materials", "openFromDrawer", "desktop", "create"]
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
# EVERY STEP SUCCEEDED (a step that failed the same way at both sizes would pass the equality): no error, and the
# steps that must read the library read it.
for r, steps_ in (("a", a), ("b", b)):
    for s in order[1:]:
        if s not in steps_: continue
        ok(steps_[s].get("lastError") == "null" and (s not in ("open", "list", "create") or int(steps_[s]["statements"]) > 0),
           f"root {r} {s}: the step succeeded (lastError {steps_[s].get('lastError')}, {steps_[s]['statements']} statements)")
# THE FIRST ENTRY TO MATERIALS FILLS THE CUSTOM DRAWER ONCE, the saved tab set restored (EffectsPage::showEvent):
# the drawer's listing (fetchAssetsInHome) runs once in the step, never from the show path and again from the
# space's entry (residue (B)'s read paid twice).
for r, steps_ in (("a", a), ("b", b)):
    if "materials" not in steps_: continue
    names = dict(kv.split("=") for kv in steps_["materials"].get("byName", "").split(",") if "=" in kv)
    fills = names.get("fetchAssetsInHome.type1", "0")   # the drawer lists the home's MATERIALS (ModelTypes::Material = 1)
    ok(fills == "1", f"root {r}: the first entry to Materials with a saved tab set fills the custom drawer once ({fills})")
# THE MEMBERS PANEL LISTS ONCE an entry and an open (materialmembers::unused reads the home's pictures).
for r, steps_ in (("a", a), ("b", b)):
    for s, what in (("materials", "the first entry to Materials (a restored tab)"), ("openFromDrawer", "an open from the custom drawer")):
        if s not in steps_: continue
        names = dict(kv.split("=") for kv in steps_[s].get("byName", "").split(",") if "=" in kv)
        n_ = names.get("fetchAssetsInHome.type2", "0")
        ok(n_ == "1", f"root {r}: {what} lists the Members panel's home pictures once ({n_}; {steps_[s]['bytes']} B read)")
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
