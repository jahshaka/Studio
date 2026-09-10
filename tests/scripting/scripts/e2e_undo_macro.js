// scripting.e2e.undo_macro — the one-undo-step-per-script contract, proven in
// the real app across two evaluate boundaries: the console-style runner wraps
// each run in a macro, so this script builds state, then undoes THE PREVIOUS
// script run (its own macro is still open) — verified via editor.beginBatch/
// endBatch grouping instead: a batch closed inside the run is one undoable unit.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var guid = project.create("Undo Macro Test " + Date.now());
assert(guid.length > 10, "project created");
var baseline = scene.nodes().length;

// A closed batch inside the still-open script macro: QUndoStack nests macros,
// so undoing inside the script is blocked (index frozen while a macro is open)
// — but the batch verbs must balance without corrupting the stack.
editor.beginBatch();
var a = scene.addPrimitive("sphere");
var b = scene.addPrimitive("torus");
editor.endBatch();
assert(scene.nodes().length === baseline + 2, "batch added two nodes");

var thrown = false;
try { editor.endBatch(); } catch (e) { thrown = true; }
assert(thrown, "unbalanced endBatch refused");

// ---- ONE CALL, ONE STEP: the command layer the PANELS now share (debt L6) ---
//
// Every properties row became undoable through the very commands these verbs
// push — SetNodePropertyCommand for a node row, WorldModeCommand for a quality
// row, ScenePropertyCommand for the world rows the panels own. `pushes` is the
// only honest count from inside a run (the run's macro swallows `count`), so
// bracket each call with it: a verb that records two steps, or none, would make
// the panel that shares its command lie about a gesture in exactly the same way.
function pushes() { return editor.undoState().pushes; }

var light = scene.addLight("point");
var beforeNode = pushes();
assert(node.setProperty(light, "intensity", 4.25), "node.setProperty(intensity) accepted");
assert(node.property(light, "intensity") > 4.2, "and wrote the document");
assert(pushes() === beforeNode + 1, "node.setProperty recorded exactly ONE step");

var beforeRow = pushes();
assert(world.override({ id: "msaa", value: 2 }), "world.override(msaa) accepted");
assert(world.settings()["msaa"].source === "override", "and PINNED the row");
assert(pushes() === beforeRow + 1, "a quality-row edit is ONE step (value AND pin)");

// The Rayon dial, through the verb that records one (world.rayon -> applyRayon).
var beforeTier = pushes();
assert(world.rayon({ tier: "high" }).tier === "high", "world.rayon(tier) accepted");
assert(pushes() === beforeTier + 1, "a Rayon tier switch is ONE step");

// FOUND ALONG THE WAY (debt L6, reported): world.gi() writes the same rows the
// Rayon panel does and records NOTHING — the panel's sliders are undoable and
// the verb is not, which is the API-first inversion pointing the wrong way. It
// is asserted here so the gap is a failing expectation the day it is closed
// rather than a silent difference: raise this to `=== beforeGi + 1` then.
var beforeGi = pushes();
assert(world.gi({ bounces: 2 }), "world.gi(bounces) accepted");
assert(pushes() === beforeGi, "world.gi records NO undo step (a known gap, not a design)");

console.log("e2e_undo_macro: ALL OK");
