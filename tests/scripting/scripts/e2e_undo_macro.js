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

// THE WORLD VERBS (smoke L10 item 5, closing the gap debt L6 recorded here):
// world.ambient / gravity / fog / gi / sky write the same fields the World
// panels write, and they now record through the same commands — one call, ONE
// step, whatever mix of plain fields and quality-registry rows it touched.
// (Undoing one is proven where a run boundary exists: mcp.e2e, "world verbs".)
function oneStep(label, call) {
    var before = pushes();
    assert(call(), label + " accepted");
    assert(pushes() === before + 1, label + " recorded exactly ONE step (" +
           before + " -> " + pushes() + ")");
}
oneStep("world.ambient", function () { return world.ambient("#336699"); });
oneStep("world.gravity", function () { return world.gravity(-4.5); });
oneStep("world.fog (four keys)", function () {
    return world.fog({ enabled: true, color: "#808080", density: 0.02, heightLevel: 1.5 });
});
// Registry rows (bounces PINS) and plain fields (updateBudget, bounds) in one call.
oneStep("world.gi (a pinned row + plain fields)", function () {
    return world.gi({ bounces: 2, updateBudget: 3, boundsMin: { x: -4, y: 0, z: -4 } });
});
oneStep("world.gi (tier + an explicit knob)", function () {
    return world.gi({ tier: "medium", quality: "high" });
});
oneStep("world.sky (gradient)", function () {
    return world.sky("gradient", { top: "#1a2b3c", bottom: "#ffeedd" });
});

// A call that changes nothing records nothing (the panel's before == after rule).
var beforeSame = pushes();
assert(world.gravity(-4.5), "world.gravity to the value it already has");
assert(pushes() === beforeSame, "...records no step");

// A REFUSED call records nothing AND changes nothing: world.fog used to write
// `enabled` before it discovered the colour was unreadable.
var fogBefore = world.get().fog;
var beforeRefused = pushes();
var refusedFog = false;
try { world.fog({ enabled: !fogBefore.enabled, color: "not a colour" }); } catch (e) { refusedFog = true; }
assert(refusedFog, "world.fog with an unreadable colour is refused");
assert(pushes() === beforeRefused, "...records no step");
assert(world.get().fog.enabled === fogBefore.enabled, "...and left fog.enabled untouched");
var refusedGi = false;
var budgetBefore = world.get().gi.updateBudget;
try { world.gi({ updateBudget: budgetBefore + 1, quality: "bogus" }); } catch (e) { refusedGi = true; }
assert(refusedGi, "world.gi with an unknown quality is refused");
assert(pushes() === beforeRefused, "...records no step");
assert(world.get().gi.updateBudget === budgetBefore, "...and rolled back the budget it had written");

console.log("e2e_undo_macro: ALL OK");
