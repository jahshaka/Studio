// scripting.e2e.preset_edit — A PRESET A PROJECT HOLDS IS THE PROJECT'S TO
// EDIT, end to end on the real binary (PRESET-EDIT-1; the owner, joint,
// 2026-09-21: "when I go to Materials and edit the project materials they are
// locked? Only the MASTER materials should be locked; if they are added to a
// project they should be editable already").
//
// What each arm answers:
//
//   1. NOTHING IS MINTED UNTIL AN EDIT. Applying a preset pins the shipped
//      bundle, exactly as it always did (scripting.e2e.preset_apply owns that
//      rule); `materials.masterOf` says what the material IS.
//   2. THE FIRST EDIT COPIES ON WRITE. `materials.edit` mints the project's
//      own bundle from the preset — KEEPING ITS NAME, sharing its member
//      textures — moves the project's pin onto it and answers the new guid.
//      A second call is a no-op: the copy is an ordinary material.
//   3. THE EDIT LANDS ON THE COPY AND REACHES THE SCENE. The mesh that wore
//      the preset wears the copy, and a graph edit saved into the copy changes
//      what that mesh looks like — which is the thing that could not happen
//      at all before this lane.
//   4. THE MASTER IS UNTOUCHED, and so is every other project: a second
//      project that pins the same preset still pins the PRESET, and its own
//      mesh still wears the shipped picture.
//   5. THE LIBRARY VIEW IS THE SAME MOVE. Editing a master with a project open
//      copies it into THAT project (each project gets its own copy, on its own
//      guid, under the same name).
//   6. A GRAPH SAVE ON A PRESET COPIES TOO — the implicit door, which is the
//      one the Materials page's autosave takes.
//   7. WITH NO PROJECT OPEN IT IS REFUSED, with the reason. That is the only
//      lock left on a preset, and the library master keeps it.
//
// UNDO is not assertable from inside a run (a run is ONE open macro, so
// editor.undo() cannot reach the step). What undoing it DOES is
// commands.preset_copy, against the same command with the real database.
//
// Document verbs only -> --headless.

function assert(cond, msg) {
    if (!cond) throw new Error("FAILED: " + msg);
    console.log("ok: " + msg);
}
function materialRows() {
    return assets.list({ scope: "store", type: "material" });
}
function nameOf(guid) {
    var hit = assets.list({ scope: "store" }).filter(function (a) { return a.guid === guid; });
    return hit.length ? hit[0].name : "";
}
function projectMaterials() {
    return assets.list({ scope: "project", type: "material" }).map(function (a) { return a.guid; });
}

var WOOD = "00000000-0000-0000-0000-000000002022";   // Wood PBR — three maps

// ---- 1. a preset applied is a preset PINNED, and nothing else ------------
var projA = project.create("Preset Edit A " + Date.now());
assert(projA.length > 10, "project A");
assert(materials.seedPresets() === 20, "the twenty shipped bundles are seeded");
var rowsAfterSeed = materialRows().length;

var cubeA = scene.addPrimitive("Cube");
assert(material.apply(cubeA, "Wood PBR") === true, "Wood PBR applied to a cube");
assert(materialRows().length === rowsAfterSeed, "the apply minted nothing");
assert(projectMaterials().length === 1 && projectMaterials()[0] === WOOD,
       "the project holds the PRESET itself, by its reserved guid");
assert(materials.masterOf("Wood PBR") === WOOD,
       "materials.masterOf of a preset is the preset");
var shippedRoughness = material.get(cubeA).roughness;
var shippedBaseMap = material.get(cubeA).baseColorMap;
assert(shippedBaseMap && shippedBaseMap.length > 0, "…and the cube wears its picture");

// ---- 2. the first edit copies on write ----------------------------------
var presetMembers = materials.members(WOOD).map(function (m) { return m.guid; }).sort();
assert(presetMembers.length === 3, "the preset has its three maps as members");

var edit = materials.edit("Wood PBR");
assert(edit.copied === true, "materials.edit COPIED it (the first edit)");
assert(edit.master === WOOD, "…from the shipped master");
assert(edit.guid && edit.guid !== WOOD, "…onto a guid of its own");
var copy = edit.guid;
assert(nameOf(copy) === "Wood PBR",
       "…UNDER THE PRESET'S OWN NAME — the user sees one material (got '"
       + nameOf(copy) + "')");
assert(materialRows().length === rowsAfterSeed + 1, "…one new row, and one only");
assert(materials.masterOf(copy) === WOOD, "materials.masterOf(copy) names the master");

// THE PIN MOVED.
var held = projectMaterials();
assert(held.length === 1 && held[0] === copy,
       "the project holds the COPY and has let go of the master (" + held.join(", ") + ")");

// THE MEMBERS ARE THE MASTER'S OWN ROWS — one object, shared.
var copyMembers = materials.members(copy).map(function (m) { return m.guid; }).sort();
assert(JSON.stringify(copyMembers) === JSON.stringify(presetMembers),
       "the copy names the SAME three pictures, not copies of them");

// A SECOND CALL IS A NO-OP: the copy is an ordinary material now.
var again = materials.edit(copy);
assert(again.copied === false && again.guid === copy,
       "a second materials.edit copies nothing and answers the same guid");
assert(materialRows().length === rowsAfterSeed + 1, "…and mints nothing");
// …and so is a call on an ordinary material that was never a preset.
var plain = materials.create("Plain " + Date.now());
var plainEdit = materials.edit(plain);
assert(plainEdit.copied === false && plainEdit.guid === plain && plainEdit.master === "",
       "an ordinary material is answered unchanged, with no master");

// ---- 3. the edit lands on the copy, and reaches the scene ---------------
//
// The mesh follows the COPY: the re-dress walks the use edges, so a picture
// that changes on the cube is the edge having moved with the pin.
materials.loadGraph(copy);
var master = graph.nodes().filter(function (n) { return n.master; })[0];
assert(!!master, "the copy's graph carries the preset's master node");
var rough = graph.addNode("float");
assert(graph.setValue(rough, 0.13) === true, "a float node set to 0.13");
assert(graph.connect(rough, 0, master.id, "Roughness") === true, "…wired to Roughness");
assert(graph.save() === true, "the COPY saves (it is an ordinary bundle)");
assert(Math.abs(material.get(cubeA).roughness - 0.13) < 1e-3,
       "THE CUBE FOLLOWS THE COPY: its roughness is the edited one ("
       + material.get(cubeA).roughness + ")");
assert(materialRows().length === rowsAfterSeed + 2,
       "…and the save minted nothing beyond the plain material above");

// ---- 4. the master is untouched, and so is everybody else ---------------
var projB = project.create("Preset Edit B " + Date.now());
var cubeB = scene.addPrimitive("Cube");
assert(material.apply(cubeB, "Wood PBR") === true, "project B applies the same preset");
assert(projectMaterials().length === 1 && projectMaterials()[0] === WOOD,
       "B pins the PRESET, not A's copy");
assert(Math.abs(material.get(cubeB).roughness - shippedRoughness) < 1e-3,
       "…and B's cube wears the SHIPPED picture (roughness "
       + material.get(cubeB).roughness + ", A's edit was 0.13)");
assert(materials.members(WOOD).length === 3, "the master's own definition never moved");

// ---- 5. the library view is the same move, into THIS project ------------
var editB = materials.edit(WOOD);
assert(editB.copied === true && editB.guid !== copy,
       "B's first edit makes B ITS OWN copy, not A's");
assert(nameOf(editB.guid) === "Wood PBR", "…under the same name again");
assert(projectMaterials()[0] === editB.guid, "…and B's pin moves onto it");
assert(materials.masterOf(editB.guid) === WOOD, "…with the same master behind it");
// A's copy is where it was.
assert(nameOf(copy) === "Wood PBR" && materials.masterOf(copy) === WOOD,
       "A's copy is untouched by B's");

// ---- 6. a graph save on a PRESET copies too (the implicit door) ---------
//
// This is the route the Materials page's 1.5 s autosave takes: the user edits
// the preset's graph on the canvas and the save is what lands. Asked of a
// preset B has never touched.
var GLASS = "00000000-0000-0000-0000-000000002028";  // Glass PBR
var beforeGlass = materialRows().length;
materials.loadGraph(GLASS);
assert(graph.setBlendMode("Opaque") === true,
       "a preset's graph TAKES an edit with a project open (it used to refuse)");
assert(graph.save() === true, "…and the save lands");
assert(materialRows().length === beforeGlass + 1, "…having made the project's own copy");
var glassCopy = projectMaterials().filter(function (g) {
    return materials.masterOf(g) === GLASS;
})[0];
assert(!!glassCopy && glassCopy !== GLASS, "…which is what the project now pins");
assert(nameOf(glassCopy) === "Glass PBR", "…under the preset's name");
assert(materials.members(GLASS).length === materials.members(glassCopy).length,
       "…sharing the master's members");

// ---- 7. with no project open there is nowhere to copy to ----------------
assert(project.close() === true, "the project is closed");
var refused = false;
try { materials.edit(WOOD); }
catch (e) { refused = /Open a project/.test(e.message); console.log("   refusal: " + e.message); }
assert(refused, "editing a master with NO project open is refused, with the reason");
var refusedSave = false;
materials.loadGraph(WOOD);
try { refusedSave = (graph.setBlendMode("Opaque") !== true); }
catch (e) { refusedSave = true; console.log("   refusal: " + e.message); }
assert(refusedSave, "…and so is a graph edit on it: the library's master stays locked");

console.log("preset_edit: all assertions passed");
