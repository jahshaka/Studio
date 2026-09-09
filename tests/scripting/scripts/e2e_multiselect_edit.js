// scripting.e2e.multiselect_edit — DELETE / DUPLICATE / COPY / PASTE ON A SET
// (EDITOR_MULTISELECT_SPEC §2.5, gate table §5), through the verbs.
//
// The properties asserted here, none of which a single-node verb can have:
//
//   1. D5 REDUCTION — a member whose ancestor is also selected is SKIPPED. A
//      parent and its child both selected and then deleted is one delete of the
//      parent, not a delete of a node that is already gone; the same reduction
//      keeps duplicate and copy from producing two copies of the child.
//   2. PLACEMENT — a duplicate lands right after its own original even when
//      several siblings are duplicated at once (the descending-sibling order),
//      and a paste lands right after the PRIMARY (D7 a) with FRESH GUIDS.
//   3. THE SELECTION THAT RESULTS — copies/pastes become the new selection,
//      primary first; a delete ends with nothing selected.
//   4. THE CLIPBOARD IS IN-APP (D9 a) and is not part of the document.
//
// WHAT IS NOT ASSERTED HERE, and why: that each of these is ONE undo step. A
// --script run is itself one open macro and QUndoStack refuses to undo into an
// open macro (editor.undoState().macroOpen says so — the same honest split
// scripting.e2e.anim documents), so editor.undo() from inside this file can
// never reach the steps it just made. What it CAN prove is that the commands
// were recorded, via undoState().pushes. The atomicity is gated where a user
// actually meets it: app.multiselect_keys presses Delete and then Ctrl+Z as
// REAL KEYS, with no script macro anywhere, and asserts every member comes back.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }
function allNodes() { return scene.nodes(); }
function exists(id) {
    return allNodes().some(function (n) { return n.id === id; });
}
/// The root's children, in document order (scene.nodes walks depth-first).
function rootChildren() {
    var root = scene.root();
    return allNodes().filter(function (n) { return n.parent === root; })
                     .map(function (n) { return n.id; });
}
function childrenOf(id) {
    return allNodes().filter(function (n) { return n.parent === id; })
                     .map(function (n) { return n.id; });
}

var guid = project.create("MultiselectEdit " + Date.now());
assert(guid.length > 10, "project created");

// ---- 1. deleteSelection: D5 reduction --------------------------------------
var a = scene.addEmpty();
var aChild = scene.addPrimitive("cube");
assert(node.reparent(aChild, a), "aChild parented under a");
var b = scene.addPrimitive("sphere");

var pushesBefore = editor.undoState().pushes;
editor.select([a, aChild, b]);
assert(editor.selectionSet().length === 3, "three nodes selected (a parent, its child, a sibling)");
var res = editor.deleteSelection();
console.log("deleteSelection -> " + J(res));
assert(res.deleted.length === 2, "only the two ROOTS were deleted (D5): " + J(res.deleted));
assert(res.deleted.indexOf(aChild) === -1,
       "the selected child went with its parent, not as a delete of its own");
assert(!exists(a) && !exists(b) && !exists(aChild), "all three are gone from the document");
assert(editor.selectionSet().length === 0, "the selection is empty after a delete");
assert(editor.undoState().pushes === pushesBefore + 2,
       "exactly two delete commands were recorded (" + pushesBefore + " -> " +
       editor.undoState().pushes + ")");
assert(editor.undoState().macroOpen === true,
       "the script's own macro is open, which is WHY undo cannot be exercised from here");

// D6, end to end: the World root can never travel in a set, so a delete of
// "this object and the world" is a delete of the object. (The service ALSO
// reports the root as skipped if one ever reaches it — defence in depth for a
// caller that is not the verb layer.)
var c = scene.addPrimitive("cone");
editor.select([c, scene.root()]);
assert(J(editor.selectionSet()) === J([c]),
       "the root is dropped from a set at the verb (D6): " + J(editor.selectionSet()));
var res2 = editor.deleteSelection();
console.log("deleteSelection of {object, root} -> " + J(res2));
assert(res2.deleted.length === 1 && res2.deleted[0] === c, "the ordinary node was still deleted");
assert(exists(scene.root()), "the World root survived");

// ---- 2. duplicateSelection -------------------------------------------------
var d1 = scene.addPrimitive("cube");
var d2 = scene.addPrimitive("sphere");
var d1Kid = scene.addPrimitive("cone");
assert(node.reparent(d1Kid, d1), "d1 has a child");

editor.select([d1, d2]);
var pushesDup = editor.undoState().pushes;
var copies = editor.duplicateSelection();
console.log("duplicateSelection -> " + J(copies));
assert(copies.length === 2, "two copies");
assert(copies.indexOf(d1) === -1 && copies.indexOf(d2) === -1, "the copies are new nodes");
assert(J(editor.selectionSet().slice().sort()) === J(copies.slice().sort()),
       "the copies are the new selection: " + J(editor.selectionSet()));
assert(editor.selection() === copies[0], "the primary's copy is the new primary");
assert(editor.undoState().pushes === pushesDup + 2, "two duplicate commands were recorded");

var kids = rootChildren();
assert(kids.indexOf(copies[0]) >= 0 && kids.indexOf(copies[1]) >= 0, "both copies are root children");
for (var i = 0; i < copies.length; ++i) {
    var pos = kids.indexOf(copies[i]);
    var prev = kids[pos - 1];
    assert(prev === d1 || prev === d2,
           "a copy sits right after ONE OF THE ORIGINALS, not at the end (" + J(kids) + ")");
}
// The subtree came with it, and the child is a copy too.
var copyOfD1 = childrenOf(copies[0]).length > 0 ? copies[0] : copies[1];
assert(childrenOf(copyOfD1).length === 1, "the duplicated subtree carries its child");
assert(childrenOf(copyOfD1)[0] !== d1Kid, "and that child is a COPY, with its own guid");

// ---- 3. copy + paste -------------------------------------------------------
editor.select([d1, d2]);
assert(editor.copy() === 2, "copy() stored two fragments");
var clip = editor.clipboard();
assert(clip.length === 2, "the clipboard reads back two entries");
assert(clip[0].format === "jahshaka.scene", "a clipboard entry is a scene fragment: " + J(clip[0].format));
assert(clip[0].version >= 2, "and it names the format version");
assert(clip[0].node && clip[0].node.guid, "and carries the node object");

// D5 again: copying a parent AND its child stores ONE fragment.
editor.select([d1, d1Kid]);
assert(editor.copy() === 1, "copying a parent and its own child stores one fragment (D5)");

// Paste beside the PRIMARY: same parent, sibling index + 1.
editor.select([d1, d2]);
editor.copy();
editor.select(d2);
var pushesPaste = editor.undoState().pushes;
var pasted = editor.paste();
console.log("paste -> " + J(pasted));
assert(pasted.length === 2, "two nodes pasted");
assert(pasted.indexOf(d1) === -1 && pasted.indexOf(d2) === -1, "fresh guids, not the originals");
assert(editor.undoState().pushes === pushesPaste + 2, "two paste commands were recorded");
assert(J(editor.selectionSet().slice().sort()) === J(pasted.slice().sort()),
       "the pasted nodes are the new selection");
var kids2 = rootChildren();
assert(kids2.indexOf(pasted[0]) === kids2.indexOf(d2) + 1,
       "the first pasted node landed right after the primary (" + J(kids2) + ")");
var pastedWithKid = childrenOf(pasted[0]).length > 0 ? pasted[0] : pasted[1];
assert(childrenOf(pastedWithKid).length === 1, "the pasted subtree carries its child");
assert(childrenOf(pastedWithKid)[0] !== d1Kid, "with a fresh guid of its own");

// ---- 4. the clipboard is in-app, not the document --------------------------
editor.selectNone();
// Copying nothing is a REFUSED verb (it raises), not a silent success — and a
// refusal must not clear what was copied a moment ago.
var refused = false;
try { editor.copy(); } catch (e) { refused = true; }
assert(refused, "copying with nothing selected is refused, not silently accepted");
assert(editor.clipboard().length === 2,
       "and leaves the previous clipboard alone — Ctrl+C on empty space must not lose it");
var atRoot = editor.paste();
assert(atRoot.length === 2, "paste with no selection still pastes");
assert(rootChildren().indexOf(atRoot[0]) >= 0, "at the scene root");

console.log("scripting.e2e.multiselect_edit: ALL PASS");
