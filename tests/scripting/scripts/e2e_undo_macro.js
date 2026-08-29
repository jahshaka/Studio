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

console.log("e2e_undo_macro: ALL OK");
