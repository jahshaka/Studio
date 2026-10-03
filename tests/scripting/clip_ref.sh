#!/usr/bin/env bash
#
# scripting.e2e.clip_ref — A SKELETAL CLIP IS REFERENCED BY ITS ASSET GUID
# (CLIP-REF-1, the owner's smoke bug #4: two "missing models" and an avatar
# standing in her bind pose).
#
# Two defects, both arms below fail on the code before the lane:
#   A. a save re-derived the clip's guid from its PATH, and the reader kept the
#      persisted path (relative to the PROJECT folder, which need not live in
#      the data root). Once that path named another data root's store, a save
#      wrote the clip without its guid and the next open lost the model.
#   B. a placed model or a spawned avatar sourced its OWN clips from the file
#      it was imported from, which the bake store refuses — the own clip came
#      back null at every placement while that file still existed.
#
# ARM 1 (process 1, data root R1, projects OUTSIDE the data root, the staged
#   source files still on disk): import + spawn an avatar with embedded clips,
#   load an extra clip, place a model with its own clip -> zero issues, every
#   clip resolved; save -> switch project -> reopen, twice.
# ARM 2 (process 2): the data root COPIED to R2 and opened with --data-root
#   (the projects stay where they were, so any path they persisted points at
#   R1's store): zero issues, every clip resolved, after open and after
#   save -> switch -> reopen, twice.
#
# $1 = the Jahshaka binary, $2 = a rig with embedded clips (rig2.glb),
# $3 = an animation file for it (rig2_walk_anim.glb), $4 = a model with its own
# clip (mixamo_tpose.fbx)
set -u
BIN="$1"; RIG="$2"; WALK="$3"; MODEL="$4"
fail=0
check() { if [ "$1" = "0" ]; then echo "ok:   $2"; else echo "FAIL: $2"; fail=1; fi }

: "${JAHSHAKA_DATA_ROOT:?the suite must run with its own data root}"
case "$HOME" in
    */e2e-home-clip_ref) ;;
    *) echo "FAIL: refusing to run outside the suite's scratch HOME ($HOME)"; exit 1 ;;
esac
case "$JAHSHAKA_DATA_ROOT:$PWD" in
    "$HOME"/*:"$HOME"/run) ;;
    *) echo "FAIL: the data root and the working directory must be inside $HOME"; exit 1 ;;
esac
R1="$JAHSHAKA_DATA_ROOT"
R2="$HOME/relocated/Jahshaka"
LOC="$HOME/Documents/Jahshaka"
rm -rf "$R1" "$HOME/relocated" "$HOME/Documents" "$HOME/cwd"
find "$PWD" -mindepth 1 -maxdepth 1 -exec rm -rf {} + 2>/dev/null
mkdir -p staged "$LOC"
cp "$RIG" staged/Hero.glb && cp "$WALK" staged/HeroWalk.glb && cp "$MODEL" staged/Prop.fbx

# The shared checks: every clip of a subtree resolved (skeletal), no issue.
cat > common.js <<'JS'
function assert(c, m) { if (!c) throw new Error("assert failed: " + m); console.log("ok: " + m); }
function skeletal(root) {
    var nodes = scene.nodes({ subtree: root }), n = 0, all = 0;
    for (var i = 0; i < nodes.length; i++) {
        var clips = anim.list(nodes[i].id);
        for (var k = 0; k < clips.length; k++) { all++; if (clips[k].skeletal) n++; }
    }
    return { skeletal: n, all: all };
}
function state(av, pl, what) {
    var issues = editor.issues();
    console.log(what + " issues: " + JSON.stringify(issues.map(function (i) { return i.id + " — " + i.message; })));
    var a = skeletal(av), p = skeletal(pl);
    assert(issues.length === 0, what + ": ZERO issues (" + issues.length + ")");
    assert(a.skeletal === 3 && a.all === 3,
           what + ": the avatar has its two own clips + the loaded one, all resolved (" + JSON.stringify(a) + ")");
    assert(p.skeletal === 1 && p.all === 1,
           what + ": the placed model has its own clip, resolved (" + JSON.stringify(p) + ")");
}
function roundTrips(p1, p2, av, pl, what) {
    for (var round = 1; round <= 2; round++) {
        assert(project.save() === true, what + " round " + round + ": save");
        assert(project.open(p2) === true && project.save() === true,
               what + " round " + round + ": switch to the other project (and save it)");
        assert(project.open(p1) === true, what + " round " + round + ": reopen");
        state(av, pl, what + " round " + round + " reopened");
    }
}
JS

cat > arm1.js <<JS
var p1 = project.create("clip ref one", { location: "$LOC" });
var r = avatar.importAvatar("$PWD/staged/Hero.glb", { scope: "project" });
assert(r && r.asset, "the avatar imports (" + JSON.stringify(r) + ")");
var av = avatar.spawn(r.asset, { position: { x: 0, y: 0, z: 0 } });
var loaded = avatar.loadClip(av, "$PWD/staged/HeroWalk.glb");
assert(loaded && loaded.added >= 1, "the extra clip loads onto the spawned avatar");
var placed = assets.importAndPlace("$PWD/staged/Prop.fbx", { position: { x: 3, y: 0, z: 0 } });
var pl = placed.nodeId;
state(av, pl, "fresh");
assert(project.save() === true, "save the first project");
var p2 = project.create("clip ref two", { location: "$LOC" });
assert(project.open(p1) === true, "back to the first project");
state(av, pl, "first reopen");
roundTrips(p1, p2, av, pl, "R1");
console.log("CLIPREF P1=" + p1 + " P2=" + p2 + " AV=" + av + " PL=" + pl);
console.log("arm 1: ALL OK");
JS
cat common.js arm1.js > arm1.run.js
# THE PROCESS RUNS FROM A DIRECTORY AS DEEP UNDER \$HOME AS A PROJECT FOLDER
# ($LOC/Projects/<guid>). Before the lane, an import blob recorded the model's
# own clips by the import path RELATIVE to the open project's folder, and a
# placement resolved that string against the process's working directory: at
# equal depths it named the staged file again — present, outside the store,
# refused by the bake store — and the own clip came back null (defect B; the
# owner's box hit exactly that). At any other depth the path missed and an
# own-model fallback hid the bug. This makes the failing case the tested one.
mkdir -p "$HOME/cwd/a/b/c"
RUN="$PWD"
( cd "$HOME/cwd/a/b/c" && "$BIN" --headless --script "$RUN/arm1.run.js" ) > arm1.log 2>&1
rc=$?
grep -E "^ok:|assert failed|issues:|ALL OK" arm1.log
check $rc "arm 1 (import, spawn, place, save/switch/reopen twice) passes (exit $rc)"
grep -q "arm 1: ALL OK" arm1.log
check $? "arm 1 reached its end"

P1="$(grep -o 'CLIPREF P1=[^ ]*' arm1.log | cut -d= -f2)"
P2="$(grep -o ' P2=[^ ]*' arm1.log | cut -d= -f2)"
AV="$(grep -o ' AV=[^ ]*' arm1.log | cut -d= -f2)"
PL="$(grep -o ' PL=[^ ]*' arm1.log | cut -d= -f2)"

[ -n "$P1" ] && [ -n "$P2" ] || { echo "FAIL: arm 1 named no projects; arm 2 cannot run"; exit 1; }

# No path is persisted as a clip's reference.
mkdir -p "$(dirname "$R2")" && cp -a "$R1" "$R2"
check $? "the data root is copied to $R2"

cat > arm2.js <<JS
assert(project.open("$P1") === true, "the relocated data root opens the first project");
state("$AV", "$PL", "relocated open");
roundTrips("$P1", "$P2", "$AV", "$PL", "R2");
console.log("arm 2: ALL OK");
JS
cat common.js arm2.js > arm2.run.js
JAHSHAKA_DATA_ROOT="$R2" "$BIN" --headless --data-root "$R2" --script arm2.run.js > arm2.log 2>&1
rc=$?
grep -E "^ok:|assert failed|issues:|ALL OK" arm2.log
check $rc "arm 2 (relocated data root, save/switch/reopen twice) passes (exit $rc)"
grep -q "arm 2: ALL OK" arm2.log
check $? "arm 2 reached its end"

exit $fail
