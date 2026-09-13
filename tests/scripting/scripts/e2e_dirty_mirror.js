// scripting.e2e.dirty_mirror — WHAT THE MIRROR LOOKS AT, THROUGH THE REAL APP.
//
// SPECS/DIRTY_SET_MIRROR_SPEC.md (owner option A). The mirror used to ask every
// object in the scene "did you change?" once a frame — on the render review's
// 8,404-node lattice that was ~14 ms of a 16 ms budget with nothing moving.
// Now each object says so when it changes and the mirror handles the list.
//
// The C++ suites prove the two halves in isolation (document.dirty_list that
// the list behaves, mirror.dirty_equals_full that what it carries is ENOUGH —
// the whole walk run behind it has nothing to do). This one proves it in the
// SHIPPED PATH: the editor viewport's own mirror, driven by the same verbs a
// user's actions go through, with the engine up.
//
//   * a still scene visits ZERO objects, for as many frames as you like;
//   * moving one object is a list of one;
//   * hiding a group is its subtree and nothing else;
//   * editing a material reaches the object that draws it without the object
//     having changed at all;
//   * deleting an object is an EVENT, not a sweep;
//   * and `verifierCatches` — the always-on background re-read that catches a
//     change nobody reported — stays at ZERO throughout.
//
// Engine UP, not --headless: the counters are a measurement of the real mirror.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

project.create("Dirty Mirror " + Date.now());

var ground = scene.addPrimitive("ground");
var group = scene.addEmpty({ position: { x: 0, y: 0, z: 0 } });
node.setProperty(group, "name", "Props");
var kids = [];
for (var i = 0; i < 12; ++i) {
    var k = scene.addPrimitive("cube", { position: { x: i - 6, y: 1, z: 0 } });
    node.reparent(k, group);
    kids.push(k);
}
var lone = scene.addPrimitive("sphere", { position: { x: 0, y: 1, z: 4 } });
editor.frame(6);

var s = editor.mirrorStats();
assert(s.available === true, "mirrorStats is live (the editor viewport has a mirror)");
console.log("mirrorStats after the build = " + JSON.stringify(s));

// ---- A STILL SCENE LOOKS AT NOTHING --------------------------------------
editor.frame(30);
s = editor.mirrorStats();
assert(s.walkMode === "dirty", "a settled scene syncs in DIRTY mode [" + s.walkMode + "]");
assert(s.dirtyNodes === 0, "a still frame: the document reported NO changes [" + s.dirtyNodes + "]");
assert(s.nodesVisited === 0, "...so the mirror visited NO objects [" + s.nodesVisited + "]");
assert(s.materialBuilds === 0, "...and rebuilt no material description");
assert(s.evictedNodes === 0, "...and released nothing");
var pushesAtRest = s.pushes;
editor.frame(60);
s = editor.mirrorStats();
assert(s.nodesVisited === 0, "sixty more still frames: still zero");
assert(s.pushes === pushesAtRest,
       "...and the renderer was written to exactly zero times [" + (s.pushes - pushesAtRest) + "]");
assert(s.verifierVisits > 0,
       "the background verifier IS running (it re-reads a few objects a frame) [" +
       s.verifierVisits + "]");
assert(s.verifierCatches === 0, "...and has found nothing behind");

// ---- ONE OBJECT MOVED IS A LIST OF ONE -----------------------------------
node.transform(lone, { position: { x: 0, y: 2, z: 4 } });
editor.frame(1);
s = editor.mirrorStats();
assert(s.dirtyNodes === 1, "moving one object marks ONE [" + s.dirtyNodes + "]");
assert(s.nodesVisited === 1, "...and the mirror visits ONE [" + s.nodesVisited + "]");
editor.frame(5);
assert(editor.mirrorStats().nodesVisited === 0, "...and goes quiet again straight after");

// ---- A FLAG, AND A NAME --------------------------------------------------
node.setProperty(lone, "castShadow", false);
editor.frame(1);
assert(editor.mirrorStats().nodesVisited === 1, "changing one flag visits one object");
editor.frame(4);

// ---- HIDING A GROUP IS ITS SUBTREE, AND NOTHING ELSE ---------------------
//
// The F6 contract: since the engine's own visibility walk stopped descending
// into document children, the mirror is the SOLE pusher of effective
// visibility — a descendant it does not visit stays drawn AND stays in the
// bounce light. So "the subtree" is a floor, not a ceiling.
node.setProperty(group, "visible", false);
editor.frame(1);
s = editor.mirrorStats();
console.log("hiding the group visited " + s.nodesVisited + " of 13 (group + 12 cubes)");
assert(s.nodesVisited === 13,
       "hiding a group visits the group AND every child [" + s.nodesVisited + "]");
editor.frame(4);
node.setProperty(group, "visible", true);
editor.frame(1);
assert(editor.mirrorStats().nodesVisited === 13, "...and showing it again does the same");
editor.frame(4);

// ---- A MATERIAL EDIT MOVES NO OBJECT, AND STILL REACHES ONE --------------
//
// The whole reason materials carry their own change signal: the panel writes a
// colour and the thing it paints never changed.
material.set(kids[0], { baseColor: "#20c040", roughness: 0.25 });
editor.frame(1);
s = editor.mirrorStats();
console.log("after a material edit: dirty " + s.dirtyNodes + " visited " + s.nodesVisited +
            " builds " + s.materialBuilds);
assert(s.nodesVisited >= 1, "editing a material reaches the object drawing it");
assert(s.materialBuilds >= 1, "...and rebuilds exactly that description");
editor.frame(5);
assert(editor.mirrorStats().materialBuilds === 0, "...once, then nothing");

// ---- A DELETION IS AN EVENT ----------------------------------------------
node.remove(kids[11]);
editor.frame(1);
s = editor.mirrorStats();
assert(s.evictedNodes === 1,
       "deleting an object releases exactly one entry, as an EVENT [" + s.evictedNodes + "]");
editor.frame(5);
assert(editor.mirrorStats().evictedNodes === 0, "...and the frames after it release nothing");

// ---- NOTHING WAS EVER MISSED ---------------------------------------------
//
// The background verifier has been re-reading objects this whole time. If any
// of the edits above had failed to report itself, the verifier would have
// pushed it — and counted itself doing so.
editor.frame(120);
s = editor.mirrorStats();
console.log("final mirrorStats = " + JSON.stringify(s));
assert(s.verifierCatches === 0,
       "across every edit in this run the change list missed NOTHING [" +
       s.verifierCatches + "]");
assert(s.nodesVisited === 0, "and the scene is quiet again");
assert(s.walkMode === "dirty", "...in dirty mode");
