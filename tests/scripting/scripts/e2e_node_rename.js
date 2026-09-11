// scripting.e2e.node_rename — node.rename(id, name) (plan item 15, lane L11).
//
// The verb is the one edit a typed outliner name and a scripted one share
// (SceneEditService::renameNode): names are unique among SIBLINGS by the copy
// rule services/nodenaming.h already applies to Duplicate and Paste — "Cube"
// taken becomes "Cube2", never "Cube Copy" — and each rename is ONE undo step.
//
// WHAT IS NOT ASSERTED HERE: that undo puts the old name back. A --script run
// is one open macro and QUndoStack refuses to undo into it (the split
// scripting.e2e.multiselect_edit documents), so this file proves the step is
// RECORDED (undoState().pushes) and mcp.e2e proves it is UNDONE — across two
// MCP run_script calls, where the first run's macro is closed.
//
// Document verbs only -> --headless (offscreen QPA, NULL render system).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function refuses(fn, msg) {
    var threw = false;
    try { fn(); } catch (e) { threw = true; }
    assert(threw, msg);
}
function nameOf(id) { return node.info(id).name; }
function pushes() { return editor.undoState().pushes; }

var guid = project.create("NodeRename " + Date.now());
assert(guid.length > 10, "project created");

// ---- 1. a plain rename: the name it asked for, one recorded step ----------
var a = scene.addPrimitive("cube");
var b = scene.addPrimitive("sphere");
var before = pushes();
assert(node.rename(a, "Hero") === "Hero", "a free name is given as asked");
assert(nameOf(a) === "Hero", "and the document carries it");
assert(pushes() === before + 1, "one rename = exactly ONE undo step");

// ---- 2. a COLLISION among siblings: the copy rule ---------------------------
assert(node.rename(b, "Hero") === "Hero2", "a sibling's name is uniquified (Hero -> Hero2)");
assert(nameOf(b) === "Hero2", "the document carries the uniquified name");
var c = scene.addPrimitive("torus");
assert(node.rename(c, "Hero") === "Hero3", "the next free suffix (Hero3)");
assert(node.rename(c, "Hero2") === "Hero3",
       "asking for a taken suffixed name counts on from it (Hero2 -> Hero3, its own name)");

// ---- 3. renaming to its OWN name changes nothing and records nothing ---------
var same = pushes();
assert(node.rename(a, "Hero") === "Hero", "a node keeps its own name (not Hero4)");
assert(pushes() === same, "and a no-op rename records NO undo step");

// ---- 4. uniqueness is per PARENT, as the outliner shows it ------------------
var group = scene.addEmpty();
var child = scene.addPrimitive("cone");
assert(node.reparent(child, group), "a child under another parent");
assert(node.rename(child, "Hero") === "Hero", "a different parent's child may be called Hero");

// ---- 5. trimming, and the refusals ------------------------------------------
assert(node.rename(child, "  Sidekick  ") === "Sidekick", "surrounding spaces are trimmed");
refuses(function () { node.rename(child, "   "); }, "a blank name is refused");
assert(nameOf(child) === "Sidekick", "and a refused rename leaves the name alone");
refuses(function () { node.rename(scene.root(), "Universe"); }, "the world root is refused");
refuses(function () { node.rename("no-such-node", "X"); }, "an unknown id is refused");

// ---- 6. the copy rule is ONE rule: a duplicate of "Hero" skips the taken names
var dup = node.duplicate(a);
assert(nameOf(dup) === "Hero4", "Duplicate follows the same sibling rule (Hero..Hero3 taken -> Hero4)");

console.log("node_rename: all assertions passed");
