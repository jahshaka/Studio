#!/usr/bin/env bash
#
# scripting.e2e.shipped_assets — plan item 15c, both halves.
#
#   1. THE DOCUMENT HALF (scripts/e2e_shipped_assets.js, --headless): the
#      default ground's tile, the default particle image, a material preset's
#      maps and a sky preset's faces save as asset GUIDS, are pinned library
#      textures rendered from the store, survive save -> reopen, and are ONE
#      row per library whatever the project.
#   2. THE DISK HALF (here): nothing copied a shipped file anywhere. Until 15c
#      createDefaultScene copied Tile.png into every project folder (and, for
#      the startup placeholder, into the working directory — the `run/Tile.png`
#      in every suite home), the particle add copied "Glowing Particle.jpg",
#      and the sky presets copied "<sky>_<face>.<ext>" six at a time. The
#      store names an object by its sha256, so a file on disk carrying one of
#      those NAMES can only be such a copy.
#
#   3. THE RIGGED-MODEL ARM: the one real consumer the deleted by-name lookup
#      had. A rigged model's own skeletal clips carry no guid in its import
#      blob (and none in a placed instance saved since — the writer's by-name
#      fallback used to supply one). When the file it was imported from is
#      gone, the clips were found again ONLY by asking the catalog for a row
#      called like the file. They are found now through the guid of the mesh
#      rows their own subtree carries (SceneReader::ownModelGuidFor): import
#      from a scratch copy, delete the copy, place the model, and its clips
#      must be real — before and after a save/reopen.
#
# $1 = the Jahshaka binary, $2 = the script, $3 = a rigged model with clips
set -u
BIN="$1"
SCRIPT="$2"
RIG="$3"
fail=0
check() { if [ "$1" = "0" ]; then echo "ok:   $2"; else echo "FAIL: $2"; fail=1; fi }

: "${JAHSHAKA_DATA_ROOT:?the suite must run with its own data root}"

# The home survives between runs (ctest reuses it): a copy an OLD binary left
# would otherwise fail this run. Start from nothing — but ONLY inside the
# suite's own scratch home, because this deletes the data root and empties the
# working directory. Anything else is refused outright.
case "$HOME" in
    */e2e-home-shipped_assets) ;;
    *) echo "FAIL: refusing to run outside the suite's scratch HOME ($HOME)"; exit 1 ;;
esac
case "$JAHSHAKA_DATA_ROOT:$PWD" in
    "$HOME"/*:"$HOME"/run) ;;
    *) echo "FAIL: the data root and the working directory must be inside $HOME"; exit 1 ;;
esac
rm -rf "$JAHSHAKA_DATA_ROOT"
find "$PWD" -mindepth 1 -maxdepth 1 -exec rm -rf {} + 2>/dev/null

"$BIN" --headless --script "$SCRIPT" > run.log 2>&1
rc=$?
cat run.log
check $rc "the document half passes (exit $rc)"
grep -q "shipped_assets: ALL OK" run.log
check $? "the script reached its end"

# ---- THE RIGGED-MODEL ARM ----------------------------------------------------
mkdir -p staged && cp "$RIG" staged/rig.glb
cat > rig_import.js <<JS
var p = project.create("Shipped rig " + Date.now());
var g = assets.import("$PWD/staged/rig.glb");
if (!g || assets.addToProject(g) !== g) throw new Error("rig import/add failed");
if (project.save() !== true) throw new Error("save failed");
console.log("RIG P=" + p + " G=" + g);
JS
"$BIN" --headless --script rig_import.js > rig_import.log 2>&1
check $? "the rig imports from a scratch copy"
P="$(grep -o 'RIG P=[^ ]*' rig_import.log | cut -d= -f2)"
G="$(grep -o ' G=[^ ]*' rig_import.log | cut -d= -f2)"
rm -rf staged          # THE SOURCE FILE IS GONE from here on
cat > rig_place.js <<JS
function assert(c, m) { if (!c) throw new Error("assert failed: " + m); console.log("ok: " + m); }
function skeletal(root) {
    var nodes = scene.nodes({ subtree: root }), n = 0;
    for (var i = 0; i < nodes.length; i++) {
        var clips = anim.list(nodes[i].id);
        for (var k = 0; k < clips.length; k++) if (clips[k].skeletal) n++;
    }
    return n;
}
assert(project.open("$P") === true, "reopen the rig's project");
var root = assets.addToScene("$G", { position: { x: 0, y: 0, z: 0 } });
assert(skeletal(root) === 2, "a rig placed after its source file is gone has both clips (" + skeletal(root) + ")");
assert(project.save() === true && project.close() === true && project.open("$P") === true, "save / close / reopen");
// Node ids are guids and survive the round trip.
assert(skeletal(root) === 2, "and still has them after the reopen (" + skeletal(root) + ")");
console.log("rig arm: ALL OK");
JS
"$BIN" --headless --script rig_place.js > rig_place.log 2>&1
rc=$?
cat rig_place.log | grep -E "^ok:|assert failed|ALL OK"
check $rc "the rigged-model arm passes (exit $rc)"
grep -q "rig arm: ALL OK" rig_place.log
check $? "the rigged-model arm reached its end"

# ---- THE DISK HALF ---------------------------------------------------------
copies="$(find "$HOME" \( -iname 'tile.png' -o -name 'Glowing Particle.jpg' \
                          -o -name '*_front.jpg' -o -name '*_front.png' \
                          -o -name '*_bottom.jpg' -o -name '*_bottom.png' \) \
               -not -path '*/exported-raw/*' -print 2>/dev/null)"
[ -z "$copies" ]
check $? "no shipped file was COPIED anywhere under HOME (found: ${copies:-none})"

# The working directory is the old placeholder's "project folder" (it was
# QDir::currentPath()): no image may appear in it. (Logs may — they are not
# this suite's business.)
strays="$(find "$PWD" -maxdepth 1 -type f \( -iname '*.png' -o -iname '*.jpg' -o -iname '*.jpeg' \) -print)"
[ -z "$strays" ]
check $? "no image was written into the working directory (found: ${strays:-none})"

# The projects this run created exist, and hold no image at all: every texture
# a scene uses lives in the store and is pinned, never beside the scene.
projects="$JAHSHAKA_DATA_ROOT/Projects"
[ -d "$projects" ]
check $? "the run's projects live under the data root ($projects)"
images="$(find "$projects" -type f \( -iname '*.png' -o -iname '*.jpg' -o -iname '*.jpeg' \) -print 2>/dev/null)"
[ -z "$images" ]
check $? "no project folder holds an image (found: ${images:-none})"

exit $fail
