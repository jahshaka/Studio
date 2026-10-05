#!/usr/bin/env bash
#
# app.library_keep — THE USER'S STORAGES SURVIVE WHAT THEY SHOULD (ASSETS-HOME-1).
#
#   1. one data root is POPULATED with a row in every home (keep_populate.js):
#      imports in Assets, an avatar in the Avatar storage, a New Material and a
#      material created from a preset in the Materials storage, a material saved
#      to Assets, a project with its own material;
#   2. CLEAR DATABASE, four arms on COPIES of it — no box, Assets, Materials,
#      both (keep_check.js): exactly the unticked storages come back, whole;
#   3. A FORMAT BUMP on another copy (keep_bump.js): the boot runs with
#      JAHSHAKA_TEST_LIBRARY_GENERATION one above this build's generation, and
#      every storage row is rebuilt from its sidecar in its home with its
#      members, edges and definitions, its bakes re-derived; a second boot at
#      that generation finds the library current.
#
# Document verbs only -> --headless, no display.
# $1 = binary  $2 = tiny.png  $3 = a rigged model  $4 = a clip  $5 = a model
set -u
BIN="$1"; PNG="$2"; RIG="$3"; CLIP="$4"; MODEL="$5"
fail=0
check() { if [ "$1" = "0" ]; then echo "ok:   $2"; else echo "FAIL: $2"; fail=1; fi }
HERE="$(cd "$(dirname "$0")" && pwd)"

rm -rf keep-src keep-arm-* keep-bump ./*.run.js ./*.log

# ---- 1. populate ------------------------------------------------------------
printf 'var KEEP_PNG = "%s"; var KEEP_RIG = "%s"; var KEEP_CLIP = "%s"; var KEEP_MODEL = "%s";\n' \
       "$PNG" "$RIG" "$CLIP" "$MODEL" > populate.run.js
cat "$HERE/keep_populate.js" >> populate.run.js
"$BIN" --headless --data-root "$PWD/keep-src" --script populate.run.js > populate.log 2>&1
check $? "the populate run exits 0"
grep -q "^ALL PASS" populate.log
check $? "every home holds what its door put there (see populate.log)"
GUIDS="$(grep -E '^(TEXTURE|MODEL|RIG|CLIP|AVATAR|MAT|FROMPRESET|SAVED|PROJMAT)=' populate.log \
         | sed -E 's/^([A-Z]+)=(.*)$/var \1 = "\2";/' | tr '\n' ' ')"
test -n "$GUIDS"
check $? "it printed the guids"

# ---- 2. Clear Database, four arms --------------------------------------------
arm() {
    local name="$1" assets="$2" mats="$3"
    cp -a keep-src "keep-arm-$name"
    printf 'var CLEAR_ASSETS = %s; var CLEAR_MATERIALS = %s; %s\n' "$assets" "$mats" "$GUIDS" \
           > "arm-$name.run.js"
    cat "$HERE/keep_check.js" >> "arm-$name.run.js"
    "$BIN" --headless --data-root "$PWD/keep-arm-$name" --script "arm-$name.run.js" \
           > "arm-$name.log" 2>&1
    check $? "Clear Database [$name]: the run exits 0"
    grep -q "^ALL PASS" "arm-$name.log"
    check $? "Clear Database [$name]: exactly the unticked storages are kept (see arm-$name.log)"
}
arm none false false
arm assets true false
arm materials false true
arm both true true

# ---- 3. a format bump ---------------------------------------------------------
cp -a keep-src keep-bump
GEN="$(grep -o 'kUserVersion = [0-9]*' "$HERE/../../src/data/database/casschema.h" | grep -o '[0-9]*$')"
NEXT=$((GEN + 1))
printf '%s\n' "$GUIDS" > bump.run.js
cat "$HERE/keep_bump.js" >> bump.run.js
JAHSHAKA_TEST_LIBRARY_GENERATION=$NEXT "$BIN" --headless --data-root "$PWD/keep-bump" \
    --script bump.run.js > bump.log 2>&1
check $? "the bumped boot ($GEN -> $NEXT) exits 0"
grep -q "^ALL PASS" bump.log
check $? "A FORMAT BUMP KEEPS THE STORAGES: every row rebuilt in its home, bakes re-derived (see bump.log)"
grep -qE "rebuilding [1-9][0-9]* stale bake" bump.log
check $? "…the bump DROPPED the kept rows' bakes and the background rebuild re-derived them"
printf 'var g = app.libraryGeneration(); if (g.outcome !== "current") throw new Error(g.outcome); console.log("ALL PASS");\n' \
       > again.run.js
JAHSHAKA_TEST_LIBRARY_GENERATION=$NEXT "$BIN" --headless --data-root "$PWD/keep-bump" \
    --script again.run.js > again.log 2>&1
grep -q "^ALL PASS" again.log
check $? "…and the next boot at $NEXT finds the library current"

exit $fail
