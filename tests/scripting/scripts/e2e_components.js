// scripting.e2e.components — THE PARTS OF A GROUPED NODE (owner review R14,
// COMPONENTS-1), the verb half.
//
// An imported model arrives as ONE node with its meshes hanging off it, and
// those children are `attached`, which is exactly what the outliner uses to
// decide not to draw them. That rule keeps a two-hundred-part model from
// burying the scene and it left the parts unreachable. node.components(id) is
// the way back in: the node's descendants, flattened, in document order, each
// with the depth it sits at and its own visibility, lock and selection state.
//
// What this pins:
//   * the list is the DESCENDANTS, never the node itself, and it is empty for
//     a leaf (which is how both callers decide the section does not apply);
//   * DOCUMENT ORDER with DEPTH — a parent before its children, siblings by
//     index, depth 1 for a direct child;
//   * `attached` is NOT a filter: a model's parts (which the outliner hides)
//     and a hand-made group's children read the same;
//   * `visible` and `locked` are the parts' OWN flags, and they follow the
//     document the moment it changes;
//   * `selected` follows the editor selection, including a multi-selection,
//     so the panel and a script agree on what is highlighted;
//   * the list follows the structure: a part reparented away leaves it, a
//     grandchild arrives at depth 2;
//   * a stale id refuses rather than throwing half a listing.
//
// Document verbs only -> --headless.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function names(list) {
    return list.map(function (c) { return c.name; }).join(",");
}
function depths(list) {
    return list.map(function (c) { return c.depth; }).join(",");
}
function byName(list, name) {
    var hit = list.filter(function (c) { return c.name === name; });
    assert(hit.length === 1, "the list has exactly one '" + name + "'");
    return hit[0];
}

project.create("Components " + Date.now());

// ---- an imported model's shape, built by hand -----------------------------
// A wrapper node with three mesh children marked `attached` — what
// irisgl/import/meshbake.cpp produces for a model (the root not attached, every
// part attached).
var group = scene.addEmpty({ position: { x: 0, y: 0, z: 0 } });
node.rename(group, "Robot");
var partNames = ["Head", "Torso", "Leg"];
var parts = [];
for (var i = 0; i < partNames.length; i++) {
    var p = scene.addPrimitive("cube", { position: { x: i, y: 0, z: 0 }, parent: group });
    node.rename(p, partNames[i]);
    node.setAttached(p, true);
    parts.push(p);
}

// ---- the list -------------------------------------------------------------
var list = node.components(group);
assert(list.length === 3, "a group with three parts lists three components");
assert(names(list) === "Head,Torso,Leg",
       "...in DOCUMENT ORDER, siblings by index [" + names(list) + "]");
assert(depths(list) === "1,1,1", "...each a direct child, depth 1");
assert(list.map(function (c) { return c.id; }).join(",") === parts.join(","),
       "...carrying the parts' own ids");
assert(list.every(function (c) { return c.type === "mesh"; }),
       "...and their node type, which is what the section's icon is drawn from");
assert(list.every(function (c) { return c.id !== group; }),
       "THE NODE ITSELF IS NEVER IN ITS OWN LIST");

// The outliner deliberately does NOT draw these rows — that is the gap this
// verb exists to close, so the two answers must differ.
var outliner = editor.outlinerRows().map(function (r) { return r.name; });
assert(outliner.indexOf("Robot") >= 0, "the outliner draws the model's own row");
assert(outliner.indexOf("Head") < 0,
       "...and NOT its parts (they are attached) - which is why components() exists");

// ---- a leaf has no components --------------------------------------------
assert(node.components(parts[0]).length === 0,
       "a part with no children of its own lists nothing");
var lone = scene.addPrimitive("cube", { position: { x: 0, y: 5, z: 0 } });
assert(node.components(lone).length === 0, "a lone object has no components");

// ---- `attached` is not a filter ------------------------------------------
var handmade = scene.addEmpty({ position: { x: 0, y: 9, z: 0 } });
node.rename(handmade, "Handmade");
var kid = scene.addPrimitive("sphere", { position: { x: 0, y: 9, z: 0 }, parent: handmade });
node.rename(kid, "Ball");
assert(node.attached(kid) === false, "a hand-made group's child is not `attached`");
assert(names(node.components(handmade)) === "Ball",
       "...and it is listed all the same - a group is a group to a person");

// ---- depth follows the hierarchy -----------------------------------------
var bolt = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: 0 }, parent: parts[0] });
node.rename(bolt, "Bolt");
node.setAttached(bolt, true);
list = node.components(group);
assert(names(list) === "Head,Bolt,Torso,Leg",
       "a grandchild lists directly after its parent [" + names(list) + "]");
assert(depths(list) === "1,2,1,1", "...at depth 2 [" + depths(list) + "]");
assert(node.components(parts[0]).length === 1,
       "...and the part's own list is just its one child");

// ---- the flags are the parts' OWN, and they are live ----------------------
assert(list.every(function (c) { return c.visible === true && c.locked === false; }),
       "every part starts visible and unlocked");
node.setProperty(parts[1], "visible", false);
node.setProperty(parts[2], "pickable", false);
list = node.components(group);
assert(byName(list, "Torso").visible === false, "hiding a part shows in its row");
assert(byName(list, "Leg").locked === true, "THE LOCK IS `pickable` - a locked part says so");
assert(byName(list, "Head").visible === true && byName(list, "Head").locked === false,
       "...and its siblings are untouched");
// The OWN flag, not the inherited one: hiding the group must not report every
// part as hidden (the section's eye is the row's own state, like the outliner's).
node.setProperty(group, "visible", false);
assert(byName(node.components(group), "Head").visible === true,
       "a hidden GROUP does not rewrite its parts' own flags");
node.setProperty(group, "visible", true);

// ---- `selected` follows the editor selection ------------------------------
// Adding an object selects it, so start from a clean slate deliberately.
editor.selectNone();
assert(node.components(group).every(function (c) { return c.selected === false; }),
       "with nothing selected, no row is marked");
editor.select(parts[1]);
list = node.components(group);
assert(byName(list, "Torso").selected === true, "selecting a part marks its row");
assert(list.filter(function (c) { return c.selected; }).length === 1,
       "...and only its row");
editor.selectAdd(parts[2]);
list = node.components(group);
assert(byName(list, "Torso").selected === true && byName(list, "Leg").selected === true,
       "a MULTI-selection marks every member (the section's Ctrl-click)");
editor.select(group);
assert(node.components(group).every(function (c) { return c.selected === false; }),
       "selecting the GROUP highlights no part");
editor.selectNone();

// ---- the list follows the structure --------------------------------------
node.reparent(parts[2], handmade);
assert(names(node.components(group)) === "Head,Bolt,Torso",
       "a part reparented away leaves the list");
assert(names(node.components(handmade)) === "Ball,Leg",
       "...and arrives in the new parent's");
node.remove(bolt);
assert(names(node.components(group)) === "Head,Torso", "a deleted part leaves the list");

// ---- a stale id refuses ---------------------------------------------------
var refused = false;
try { node.components("not-a-node"); } catch (e) { refused = true; }
assert(refused, "an unknown id is refused, not answered with an empty list");

console.log("scripting.e2e.components: PASS");
