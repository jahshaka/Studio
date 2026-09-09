// scripting.e2e.multiselect — THE SELECTION IS A SET (EDITOR_MULTISELECT_SPEC
// §2.1/§2.7), driven through the verbs in the real app.
//
// What is asserted here is the SEMANTICS the owner asked for, in the layer
// every surface goes through:
//
//   * a set with a PRIMARY: editor.selection() keeps answering one id (every
//     single-target verb and the properties panel read it) while
//     editor.selectionSet() answers the whole thing, primary first;
//   * Ctrl+click's rule: selectToggle adds, toggles off, and removing the
//     primary promotes the TOPMOST remaining member (D2) — not the most
//     recently added, which is what every other DCC does and what the owner's
//     Shift rule would then disagree with;
//   * Shift+click's rule (D1 b, the owner's): the range runs from the TOPMOST
//     SELECTED row to the clicked one, inclusive, and REPLACES the selection;
//   * the World root is never a member of a multi (D6);
//   * selection is not undoable (D10): none of these verbs pushes an undo step.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }

var guid = project.create("Multiselect " + Date.now());
assert(guid.length > 10, "project created");

// Six primitives at the root, in a known order: n[0] is the topmost row.
var n = [];
var kinds = ["cube", "sphere", "cone", "cylinder", "torus", "capsule"];
for (var i = 0; i < kinds.length; ++i) n.push(scene.addPrimitive(kinds[i]));
assert(n.length === 6 && n[5].length > 10, "six primitives added");

// ---- 1. the primary and the set -------------------------------------------
assert(editor.select(n[1]), "select(id) still takes one node");
assert(editor.selection() === n[1], "editor.selection() is that node");
assert(J(editor.selectionSet()) === J([n[1]]), "the set is that node alone");

assert(editor.select([n[2], n[0], n[4]]), "select([...]) takes a set");
assert(editor.selection() === n[2], "the FIRST id of the array is the primary");
assert(J(editor.selectionSet()) === J([n[2], n[0], n[4]]),
       "the set is primary-first, then document pre-order: " + J(editor.selectionSet()));

// ---- 2. Ctrl+click: add, toggle off, promote -------------------------------
assert(editor.selectNone(), "selectNone clears");
assert(!editor.selection(), "nothing is selected");
assert(J(editor.selectionSet()) === J([]), "the set is empty");

assert(editor.select(n[1]), "select row 1");
assert(editor.selectAdd(n[3]), "selectAdd adds a second node");
assert(editor.selection() === n[3], "the last added node is the primary");
assert(editor.selectAdd([n[5]]), "selectAdd takes an array too");
assert(J(editor.selectionSet()) === J([n[5], n[1], n[3]]),
       "the set holds all three, primary first: " + J(editor.selectionSet()));

assert(editor.selectToggle(n[5]) === false, "toggling the primary removes it");
assert(editor.selection() === n[1],
       "the TOPMOST remaining member is promoted (D2), not the most recent: " + editor.selection());
assert(editor.selectToggle(n[5]) === true, "toggling it back adds it");
assert(editor.selection() === n[5], "the toggled-in node is the primary again");

// ---- 3. Shift+click: the owner's range ------------------------------------
// Rows 1 and 3 selected, Shift-click row 5: the range runs from the TOPMOST
// selected row (1) to the clicked one (5) and REPLACES — Qt/Unreal/Blender
// would have given {3..5}.
assert(editor.select([n[1]]), "select row 1");
assert(editor.selectAdd(n[3]), "ctrl-add row 3");
// The verb takes the two ENDS explicitly — deciding that the first one is the
// topmost SELECTED row is the tree's job (ui.tree_multiselect proves that half).
var range = editor.selectRange(n[1], n[5]);
assert(range.length === 5, "the range is inclusive on both ends (" + range.length + " rows)");
assert(editor.selection() === n[5], "the clicked end becomes the primary");
var members = editor.selectionSet().slice().sort();
assert(J(members) === J([n[1], n[2], n[3], n[4], n[5]].slice().sort()),
       "the range spans rows 1..5 from the topmost selected row: " + J(editor.selectionSet()));

// A range that runs UPWARDS is the same set (min..max), which is the one case
// every system agrees on.
var up = editor.selectRange(n[4], n[2]);
assert(up.length === 3, "an upward range is [min..max] too (" + up.length + ")");

// ---- 4. the World root is never in a multi (D6) ----------------------------
var rootId = scene.root();
assert(editor.select(rootId), "the root can be selected alone");
assert(editor.selection() === rootId, "and it is the selection");
assert(editor.select([rootId, n[0]]),
       "a set containing the root is accepted");
assert(editor.selectionSet().indexOf(rootId) === -1,
       "but the root is dropped from it: " + J(editor.selectionSet()));
editor.select([n[0], n[1]]);
assert(editor.selectAdd(rootId), "selectAdd on the root is accepted");
assert(J(editor.selectionSet()) === J([rootId]),
       "and REPLACES the set — the root can never be a member (D6): " + J(editor.selectionSet()));
editor.select(n[0]);
assert(editor.selectToggle(rootId) === true, "ctrl-clicking the root is a plain click");
assert(J(editor.selectionSet()) === J([rootId]), "which replaces the selection with the root");

// ---- 5. selection is not undoable (D10) -----------------------------------
var before = editor.undoState().pushes;
editor.select([n[0], n[1]]);
editor.selectAdd(n[2]);
editor.selectToggle(n[2]);
editor.selectRange(n[0], n[3]);
editor.selectNone();
assert(editor.undoState().pushes === before,
       "no selection verb pushes an undo step (" + before + " -> " +
       editor.undoState().pushes + ")");

console.log("scripting.e2e.multiselect: ALL PASS");
