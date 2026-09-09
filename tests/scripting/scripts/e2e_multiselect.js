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

// ---- 4b. SELECT ALL (Ctrl+A, §8.7) ----------------------------------------
// Every node except the World root, in document pre-order, topmost first — and
// not an undo entry. The KEY half (a focused text field keeps the chord) is
// app.input_keys; this is the capability the key runs.
var pushesSelAll = editor.undoState().pushes;
var all = editor.selectAll();
assert(all.length === scene.nodes().length - 1,
       "selectAll takes every node but one (" + all.length + " of " +
       scene.nodes().length + " document nodes)");
assert(all.indexOf(scene.root()) === -1, "and the one left out is the World root (D6)");
assert(J(editor.selectionSet()) === J(all), "the returned set IS the selection");
assert(editor.selection() === all[0], "the topmost node is the primary");
// ...and "topmost" is the document's own order, which is what the outliner
// draws: scene.nodes() walks pre-order from the root, so dropping the root
// leaves exactly the set selectAll returns, in exactly its order.
var docOrder = scene.nodes().map(function (x) { return x.id; })
                    .filter(function (id) { return id !== scene.root(); });
assert(J(all) === J(docOrder),
       "the set IS the document's pre-order, root removed: " + J(all) + " vs " + J(docOrder));
assert(editor.undoState().pushes === pushesSelAll, "selectAll pushes no undo step (D10)");
// It replaces rather than adds, and it is idempotent.
editor.select(n[3]);
var again = editor.selectAll();
assert(J(again) === J(all), "selectAll from a different selection gives the same set");

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

// ---- 6. the PRIMARY's outline colour (D4 b) --------------------------------
// The VERB half of the primary-outline row: the persisted values, the clamps,
// the derivation and the refusals. The PIXEL half — that the primary's band
// actually takes this colour and the secondaries' do not — is
// app.multiselect_outline, which needs a GPU; everything below runs headless.
var o = editor.outline();
assert(typeof o.width === "number" && o.width >= 1, "outline().width is a number: " + o.width);
assert(o.color.length === 7 && o.color[0] === "#", "outline().color is a hex colour: " + o.color);
assert(o.primaryColorStored === false,
       "a fresh profile has no stored primary colour (" + o.primaryColorStored + ")");
assert(o.primaryColor !== o.color,
       "and the DERIVED primary differs from the outline colour: " +
       o.color + " -> " + o.primaryColor);

// The derivation FOLLOWS the outline colour: a user who never touches the
// primary row still gets a primary that is lighter than THEIR colour.
// ODD channel values on purpose: halving (255 + c) is exact only for odd c, so
// the expected value below is a whole number and not a rounding coin-toss.
var dark = editor.setOutline({ color: "#113355" });
assert(dark.color === "#113355", "color round-trips: " + dark.color);
assert(dark.primaryColorStored === false, "setting the base colour stores no primary");
assert(dark.primaryColor !== "#113355" && dark.primaryColor !== o.primaryColor,
       "the derived primary moved with it: " + dark.primaryColor);
// Halfway to white, per channel: 0x11 -> 0x88, 0x33 -> 0x99, 0x55 -> 0xaa.
assert(dark.primaryColor === "#8899aa",
       "and it is the base lifted halfway to white (" + dark.primaryColor + ")");

var chosen = editor.setOutline({ primaryColor: "#00ff00" });
assert(chosen.primaryColor === "#00ff00" && chosen.primaryColorStored === true,
       "an explicit primary colour is stored and wins: " + J(chosen));
assert(chosen.color === "#113355", "and does not disturb the base colour");
var cleared = editor.setOutline({ primaryColor: null });
assert(cleared.primaryColorStored === false,
       "null clears the stored choice back to derived: " + J(cleared));
assert(cleared.primaryColor === dark.primaryColor,
       "and the derived value comes back unchanged: " + cleared.primaryColor);

var w = editor.setOutline({ width: 500 });
assert(w.width === 30, "width is CLAMPED, not refused (500 -> " + w.width + ")");
editor.setOutline({ width: 3, color: "#3498db", primaryColor: null });

var threw = false;
try { editor.setOutline({ color: "not-a-colour" }); } catch (e) { threw = true; }
assert(threw, "an unparseable colour is REFUSED, not silently defaulted");
threw = false;
try { editor.setOutline({ nope: 1 }); } catch (e) { threw = true; }
assert(threw, "an unknown key is refused");
console.log("scripting.e2e.multiselect: ALL PASS");
