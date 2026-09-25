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

// The Photon dial, through the verb that records one (world.photon -> applyPhoton).
var beforeTier = pushes();
assert(world.photon({ tier: "high" }).tier === "high", "world.photon(tier) accepted");
assert(pushes() === beforeTier + 1, "a Photon tier switch is ONE step");

// THE WORLD VERBS (smoke L10 item 5, closing the gap debt L6 recorded here):
// world.sunDisc / gravity / fog / gi / sky write the same fields the World
// panels write, and they now record through the same commands — one call, ONE
// step, whatever mix of plain fields and quality-registry rows it touched.
// (Undoing one is proven where a run boundary exists: mcp.e2e, "world verbs".)
function oneStep(label, call) {
    var before = pushes();
    assert(call(), label + " accepted");
    assert(pushes() === before + 1, label + " recorded exactly ONE step (" +
           before + " -> " + pushes() + ")");
}
// The SKY LIGHT's strength is a light-node row, so it goes through
// SetNodePropertyCommand like every other light's (SKY_LIGHT_SPEC.md §2), and
// the world-level sun-disc switch through the world-row path.
var skyLightId = world.skyLight().light;
oneStep("node.setProperty(skyLight, intensity)",
        function () { return node.setProperty(skyLightId, "intensity", 1.7); });
oneStep("world.sunDisc", function () { return world.sunDisc({ visible: false }).visible === false; });
oneStep("world.gravity", function () { return world.gravity(-4.5); });
oneStep("world.fog (four keys)", function () {
    return world.fog({ enabled: true, color: "#808080", density: 0.02, heightLevel: 1.5 });
});
// Registry rows (bounces PINS) and plain fields (updateBudget) in one call.
oneStep("world.gi (a pinned row + plain fields)", function () {
    return world.gi({ bounces: 2, updateBudget: 3 });
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

// ---- WHAT THE STACK OWES THE LIBRARY (CLOSE-1) -----------------------------
//
// A delete command finalises its asset row when it DIES, and it used to do that
// with one transaction and one fdatasync each — closing the owner's project
// froze the UI thread for 33,156 ms inside QUndoStack::clear(). The command now
// queues the row and one flush applies the batch (the timing half is the
// commands.undo_clear_cost suite). `editor.undoState().pendingAssetDeletes` is
// that queue, and the rule it reports is the one a user can feel: a delete that
// can still be UNDONE owes nothing at all.
var owed = scene.addPrimitive("cube");
var owedId = (typeof owed === "string") ? owed : owed.id;
assert(editor.undoState().pendingAssetDeletes === 0,
       "nothing is owed to the library before a delete");
assert(node.remove(owedId), "the primitive is deleted");
assert(editor.undoState().pendingAssetDeletes === 0,
       "a delete that is still UNDOABLE queues nothing (its command is alive)");

// ---- CLOSE-2: ONE GESTURE, ONE COMMIT — AND THE PROJECT BOUNDARY -----------
//
// A script run is one undo macro; it is now also ONE database transaction, so
// the library rows a run writes cost one commit instead of one fdatasync each
// (the owner's scripted-sphere runs). `dbBatchDepth` is that scope and
// `dbCommits` is the process's durable write commits, so the cost of an action
// is assertable from a script instead of timed.
var batchState = editor.undoState();
assert(batchState.dbBatchDepth === 1, "a script run holds ONE database batch");

var commitsBefore = batchState.dbCommits;
var many = scene.addPrimitive("cube", { count: 20 });
assert(many.length === 20, "twenty primitives added in one call");
assert(editor.undoState().dbCommits === commitsBefore,
       "...and their twenty library rows cost ZERO commits (they ride the run's batch)");

// THE PROJECT BOUNDARY. UndoService::clear() is a no-op while the run's macro
// is open, so a scripted project.close() used to leave the WHOLE undo stack
// alive across the close — commands holding nodes and asset guids of a
// document that no longer existed, reachable by Ctrl+Z the moment the run
// ended. The verb ends the run's entry first: the edits so far become one undo
// step of the project being closed, and they die with it.
assert(editor.undoState().pushes > 0, "the run has recorded steps before the close");
// A deleted primitive whose command is still ALIVE on the stack: its library
// row is finalised by the command's DESTRUCTOR, so the row surviving the close
// is the visible proof that the stack survived it too.
var doomed = scene.addPrimitive("cube");
assert(node.remove(doomed), "a primitive deleted, its command alive on the stack");
assert(assets.metadata(doomed).guid === doomed,
       "...and its library row is still there while the command lives");
assert(project.close(), "project closed from inside the run");
var afterClose = editor.undoState();
assert(afterClose.count === 0, "the undo stack is EMPTY after a scripted close");
assert(afterClose.macroOpen === true,
       "...and a fresh run entry is open for the rest of the script");
assert(afterClose.dbBatchDepth === 1, "...with a fresh database batch");
assert(afterClose.pendingAssetDeletes === 0,
       "...and the library work the dead commands owed was flushed, not stranded");
editor.undo();   // nothing to reach, and nothing to crash on
assert(editor.undoState().count === 0, "undo after the close finds nothing and survives");

// The run continues in a NEW project, recording into the fresh entry.
var secondGuid = project.create("Undo Macro Test B " + Date.now());
assert(secondGuid.length > 10, "a second project created after the close");
var afterOpen = editor.undoState();
assert(afterOpen.macroOpen === true, "the run's entry is open again in the new project");
assert(afterOpen.dbBatchDepth === 1, "...and so is the database batch");
var stale = false;
try { assets.metadata(doomed); } catch (e) { stale = true; }
assert(stale, "the closed project's deleted row went with it — no command outlived the close");
var reborn = scene.addPrimitive("cube");
assert(typeof reborn === "string" && reborn.length > 10,
       "and the run can still edit: a primitive added in the second project");

console.log("e2e_undo_macro: ALL OK");
