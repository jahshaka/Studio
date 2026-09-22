#!/usr/bin/env bash
#
# app.reset_library — RESET THE LIBRARY, AND COME BACK AS A FIRST LAUNCH
# (owner review R10.2; services/libraryreset.h).
#
# THE DEFECT THIS GATES. "Clear Database" called `Database::wipeDatabase()` —
# which DROPs the tables and nothing else — and then quit and spawned
# `arguments()[0]` again. The asset store's objects and sidecars, every project
# folder on disk and every abandoned staging temp survived a "clear", so the
# library reopened onto a store full of orphans; and because nothing created
# the dropped tables again, the next boot's schema check ran against an empty
# database file and said so three times in the log.
#
# The assertions, in the order they are made:
#   1. a FRESH data root's census (the first-launch numbers) is recorded;
#   2. a second root is POPULATED — a project with a folder, an imported
#      texture with its stored object and sidecar, a saved scene — and the
#      files are really there;
#   3. an abandoned staging temp is planted in the store, the shape
#      `FileWrite::stagingTempPath` leaves behind — AND a foreign file, a
#      foreign folder and a foreign folder under `Projects/`, because the store
#      root is a path the USER chose (it can be ~/Documents or the top of a USB
#      stick) and the projects root comes from a free-form folder picker: a
#      reset may remove only what this app put there. All three must survive;
#   4. `app.resetLibrary()` reports what it removed, the CENSUS AFTERWARDS
#      EQUALS THE FRESH ONE, the catalog is usable (a project and an import
#      work on it), and a second and third call are clean no-ops;
#   5. on disk: objects/ and sidecar/ are empty, the projects root is empty,
#      the store's identity was written again (store.json) and the store ROOT
#      and the database file still exist — a reset is not a deletion of the
#      places, only of the contents;
#   6. THE NEXT BOOT IS CLEAN. This is the half the owner saw on push #52: the
#      old path dropped the tables and never created them again, so the
#      restarted app ran its schema check against an empty database file and
#      logged "There was an error fetching db metadata", "080SchemaUpdate query
#      failed to execute!" and "updateMetadataVersion query failed to execute!
#      Parameter count mismatch". A boot on the reset root must log none of the
#      three — and neither must a boot on a genuinely fresh one, which is the
#      reference;
#   7. THE SEED THE RESET STARTS stamps its maps as members (SEED-STAMP-1): a
#      reset ends by running the first-run seed a LAUNCH runs — the async
#      seeder — and until that lane it left thirty-one preset maps in the
#      library as the user's own tiles.
#
# Document verbs only -> --headless, no display.
#
# $1 = the Jahshaka binary   $2 = tests/scripting/fixtures/tiny.png
set -u

BIN="$1"
FIXTURE="$2"
fail=0
check() { if [ "$1" = "0" ]; then echo "ok:   $2"; else echo "FAIL: $2"; fail=1; fi }

# The working directory survives between runs (ctest fixes it), so everything
# this script makes is cleared FIRST — a test whose verdict depends on what the
# previous run left behind is not a gate (app.data_root learned this by
# failing).
rm -rf root-fresh root-live root-seed
rm -f fresh.log fresh-boot.log populate.log reset.log afterboot.log resetseed.log populate.run.js reset.run.js

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT_FRESH="$PWD/root-fresh"
ROOT="$PWD/root-live"

# ---- 1. the FIRST-LAUNCH census -------------------------------------------
"$BIN" --headless --data-root "$ROOT_FRESH" --script "$HERE/census.js" > fresh.log 2>&1
check $? "a fresh data root opens (the first-launch census)"
FRESH_CENSUS="$(grep '^CENSUS_' fresh.log | sort)"
test -n "$FRESH_CENSUS"
check $? "…and it reported one ($(echo "$FRESH_CENSUS" | tr '\n' ' '))"

# ---- 2. populate a second root --------------------------------------------
printf 'var FIXTURE_PNG = "%s";\n' "$FIXTURE" > populate.run.js
cat "$HERE/populate.js" >> populate.run.js
"$BIN" --headless --data-root "$ROOT" --script populate.run.js > populate.log 2>&1
check $? "the populate run exits 0"
PROJECT_FOLDER="$(grep '^PROJECT_FOLDER=' populate.log | head -1 | cut -d= -f2-)"
test -n "$PROJECT_FOLDER" && test -d "$PROJECT_FOLDER"
check $? "a project folder exists on disk ($PROJECT_FOLDER)"
OBJECTS_BEFORE="$(find "$ROOT/AssetStore/objects" -type f 2>/dev/null | wc -l)"
[ "$OBJECTS_BEFORE" -ge 1 ]
check $? "the asset store holds $OBJECTS_BEFORE object(s)"
SIDECARS_BEFORE="$(find "$ROOT/AssetStore/sidecar" -type f 2>/dev/null | wc -l)"
[ "$SIDECARS_BEFORE" -ge 1 ]
check $? "…and $SIDECARS_BEFORE sidecar(s)"

# ---- 3. an abandoned staging temp -----------------------------------------
# `<final>.tmp-<pid>-<serial>`: what a killed import leaves in the store. The
# reset counts it apart from the objects so the two numbers mean what they say.
STAGING_DIR="$(find "$ROOT/AssetStore/objects" -mindepth 1 -maxdepth 1 -type d | head -1)"
[ -n "$STAGING_DIR" ] || STAGING_DIR="$ROOT/AssetStore/objects"
mkdir -p "$STAGING_DIR"
: > "$STAGING_DIR/abandoned.png.tmp-999999-1"
test -f "$STAGING_DIR/abandoned.png.tmp-999999-1"
check $? "an abandoned staging temp is planted in the store"

# THE USER'S OWN THINGS, in the two directories a reset walks. Named so that
# nothing about them looks like this app's: not a guid, not a store folder.
echo "the owner's notes" > "$ROOT/AssetStore/my-notes.txt"
mkdir -p "$ROOT/AssetStore/Holiday Photos"
echo "a photo" > "$ROOT/AssetStore/Holiday Photos/beach.jpg"
mkdir -p "$ROOT/Projects/Tax Returns"
echo "2025" > "$ROOT/Projects/Tax Returns/2025.txt"
test -f "$ROOT/AssetStore/my-notes.txt" && test -f "$ROOT/Projects/Tax Returns/2025.txt"
check $? "a foreign file and two foreign folders are planted (store root, projects root)"

# ---- 4. THE RESET ----------------------------------------------------------
printf 'var RESET_FIXTURE_PNG = "%s";\n' "$FIXTURE" > reset.run.js
cat "$HERE/reset.js" >> reset.run.js
"$BIN" --headless --data-root "$ROOT" --script reset.run.js > reset.log 2>&1
check $? "the reset run exits 0"
grep -q "^ALL PASS" reset.log
check $? "every in-process assertion passed (see reset.log)"
grep -q "^REMOVED_STAGING=1" reset.log
check $? "the staging temp was counted and removed"

RESET_CENSUS="$(grep '^CENSUS_' reset.log | sort)"
[ "$RESET_CENSUS" = "$FRESH_CENSUS" ]
check $? "THE LIBRARY IS EXACTLY A FIRST LAUNCH (census matches the fresh root)"
if [ "$RESET_CENSUS" != "$FRESH_CENSUS" ]; then
    echo "      fresh: $(echo "$FRESH_CENSUS" | tr '\n' ' ')"
    echo "      reset: $(echo "$RESET_CENSUS" | tr '\n' ' ')"
fi

# ---- 5. and on disk --------------------------------------------------------
# The LAST call in reset.js is a reset of a library with nothing of the USER'S in
# it, so everything below is measured on a library that is exactly a first launch.
#
# "EXACTLY A FIRST LAUNCH" IS NOT "EMPTY" (ATOM P2). A first launch seeds the
# shipped geometry — the twelve primitives, the Ground every new scene stands on
# and the Teapot the samples name — as baked library assets, so its store holds a
# source and a bake per row. The comparison is therefore against THE FRESH ROOT
# this script measured in step 1, which is the claim the suite is actually making;
# zero was only ever a spelling of "the same as a first launch", and it stopped
# being one the day the app started shipping geometry it could not draw without.
# WHAT A FIRST LAUNCH LEAVES, counted from the app's own line rather than pinned
# to a number here: it logs "primitives: baked <n> shipped meshes" when it seeds,
# and each seed row stores TWO objects — the shipped mesh it was baked from and the
# bake. So this moves with src/data/primitives.h and still says something exact.
# (A fresh root ALSO holds the material presets' maps, which a reset's async seeder
# has not finished writing when the script exits, so the fresh root's total is not
# the comparison to make — the census above is.)
SEEDS="$(cat ./*.log 2>/dev/null | grep -o 'baked [0-9]* shipped' | grep -o '[0-9][0-9]*' \
         | tail -1)"
[ -n "$SEEDS" ] || SEEDS=0
OBJECTS_AFTER="$(find "$ROOT/AssetStore/objects" -type f 2>/dev/null | wc -l)"
[ "$SEEDS" -gt 0 ] && [ "$OBJECTS_AFTER" = "$((SEEDS * 2))" ]
check $? "the store's objects/ holds nothing but the shipped seed ($OBJECTS_AFTER file(s) = 2 x $SEEDS)"
SIDECARS_AFTER="$(find "$ROOT/AssetStore/sidecar" -type f 2>/dev/null | wc -l)"
[ "$SIDECARS_AFTER" = "$((SEEDS * 2))" ]
check $? "…and its sidecar/ the same ($SIDECARS_AFTER)"
# No PROJECT folders (a guid-named directory); the user's own folder is still
# there and is counted by the assertions below, not by this one.
PROJECTS_AFTER="$(ls -A "$ROOT/Projects" 2>/dev/null \
                  | grep -cE '^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$')"
[ "$PROJECTS_AFTER" = "0" ]
check $? "…and the projects root holds no project folders ($PROJECTS_AFTER)"
test ! -d "$PROJECT_FOLDER"
check $? "…the populated project's own folder is gone"
test -d "$ROOT/AssetStore"
check $? "the store ROOT still exists (a reset is not a deletion of the place)"
test -f "$ROOT/AssetStore/my-notes.txt"
check $? "THE USER'S OWN FILE in the store root survived the reset"
test -f "$ROOT/AssetStore/Holiday Photos/beach.jpg"
check $? "…and their own folder in it, with its contents"
test -f "$ROOT/Projects/Tax Returns/2025.txt"
check $? "…and their own folder under Projects/ (only guid-named ones are ours)"
test -f "$ROOT/AssetStore/store.json"
check $? "…with a store identity written again (store.json)"
ls "$ROOT"/*.db >/dev/null 2>&1
check $? "…and the library database file is still there"

# ---- 6. THE NEXT BOOT IS CLEAN --------------------------------------------
# The schema upgrader runs from main() BEFORE the tables would be created, so a
# database whose tables were dropped and not re-created fails it — loudly, and
# then keeps going. The reset root must boot exactly as silently as a fresh one.
"$BIN" --headless --data-root "$ROOT" --script "$HERE/census.js" > afterboot.log 2>&1
check $? "a boot on the reset library exits 0"
"$BIN" --headless --data-root "$ROOT_FRESH" --script "$HERE/census.js" > fresh-boot.log 2>&1
check $? "…and so does one on the fresh root (the reference)"
for phrase in "error fetching db metadata" \
              "080SchemaUpdate query failed" \
              "updateMetadataVersion query failed"; do
    ! grep -qi "$phrase" fresh-boot.log
    check $? "the FRESH root's boot says nothing about '$phrase'"
    ! grep -qi "$phrase" afterboot.log
    check $? "…and neither does the RESET one ('$phrase')"
done
AFTER_BOOT_CENSUS="$(grep '^CENSUS_' afterboot.log | sort)"
[ "$AFTER_BOOT_CENSUS" = "$FRESH_CENSUS" ]
check $? "…and the reset library still censuses as a first launch after a boot"

# ---- 7. THE SEED THE RESET STARTS STAMPS ITS MAPS -------------------------
# `app.resetLibrary()` ends by running the same first-run seed a launch does —
# the ASYNC seeder, which is the route a person takes and the one whose maps
# used to come back as the user's own tiles (SEED-STAMP-1). Its own data root,
# so nothing it seeds moves the censuses compared above, and its own run
# because JAHSHAKA_SEED_PRESETS must be set for a driven session to take that
# seed at all.
ROOT_SEED="$PWD/root-seed"
rm -rf root-seed
JAHSHAKA_SEED_PRESETS=1 "$BIN" --headless --data-root "$ROOT_SEED" \
    --script "$HERE/reset_seed.js" > resetseed.log 2>&1
check $? "the reset-seed run exits 0"
grep -q "^ALL PASS" resetseed.log
check $? "the reset's OWN seed stamps every preset map as a member (see resetseed.log)"

# ONE PICTURE, ONE SPELLING (PATHCLEAN-1, SEED-SMALL-1). The seeder logs the
# files its worker HASHED beside the imports it then made, and those two numbers
# must be equal: a gap is the path-spelling defect returning —
# `AssetIOBase::getAbsolutePath` used to hand back
# '…/materials/../../shadergraph/wood.jpg' where the graph reader's own
# cleanPath produced '…/shadergraph/wood.jpg', so three of the shipped pictures
# were hashed twice under two names (34 files for 31 imports). The seed's
# by-content filter hid the symptom; the equality gates the cause. Read off the
# LAUNCH seed's line, the first of the two this run writes.
SEED_LINE="$(grep -m1 'map file(s) prepared' resetseed.log)"
test -n "$SEED_LINE"
check $? "the seed reported what it prepared ($SEED_LINE)"
PREPARED="$(printf '%s' "$SEED_LINE" | sed -n 's/.*seed: \([0-9]*\) map file(s) prepared.*/\1/p')"
IMPORTED="$(printf '%s' "$SEED_LINE" | sed -n 's/.*prepared, \([0-9]*\) imported.*/\1/p')"
[ -n "$PREPARED" ] && [ "$PREPARED" = "$IMPORTED" ]
check $? "ONE PICTURE, ONE SPELLING: the seed hashed $PREPARED file(s) and imported $IMPORTED"

if [ $fail -eq 0 ]; then echo "ALL PASS"; else echo "FAILURES"; fi
exit $fail
