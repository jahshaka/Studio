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
// A SECOND EDIT ADOPTS the copy rather than making another (arm 2b) — asked
// here of the VERB; the same rule through the Materials PAGE (the preset tile
// carries the master's guid, so the second double-click opens the master
// again) is scripting.e2e.material_tabs 6c, which is where the page is.
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
// EVERY material row the open project can see: the library's AND the
// project's own. A project's copy is THAT PROJECT'S row (ASSETS-SCOPE-1) — it
// is never in the store listing (section 5b) — so these two count and name the
// rows the copy-on-write MINTS through the union and the by-guid read.
function materialRows() {
    var seen = {};
    var rows = assets.list({ scope: "store", type: "material" })
                   .concat(assets.list({ scope: "project", type: "material" }));
    return rows.filter(function (a) {
        if (seen[a.guid]) return false;
        seen[a.guid] = true;
        return true;
    });
}
function nameOf(guid) {
    var meta = assets.metadata(guid);
    return meta && meta.name ? meta.name : "";
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

// ---- 2b. A SECOND EDIT ADOPTS THE COPY, IT DOES NOT MAKE ANOTHER --------
//
// A preset TILE carries the MASTER's guid wherever it is shown — the Presets
// drawer, the tray, a drag payload — so the second double-click, and a second
// `materials.edit(<master>)`, arrive naming the master again. Without the
// project-copy check they made a SECOND "Wood PBR": two rows pinned, the new
// command finding no master pin to move and no mesh to re-point, and the
// answer adopting whichever row the catalog listed first — so the edit could
// land on a copy nothing wears.
var rowsBeforeSecond = materialRows().length;
var second = materials.edit(WOOD);              // BY THE MASTER'S GUID, again
assert(second.copied === false, "a second materials.edit(master) copies nothing");
assert(second.guid === copy, "…it answers the copy this project already has");
assert(second.master === WOOD, "…still naming the master behind it");
assert(materialRows().length === rowsBeforeSecond, "…and mints no row");
assert(projectMaterials().length === 1 && projectMaterials()[0] === copy,
       "…the project still holds exactly ONE material");
// …and by the preset's NAME, which is the other spelling every door accepts.
var byName = materials.edit("Wood PBR");
assert(byName.copied === false && byName.guid === copy,
       "…and so does the same call by name");
assert(materialRows().length === rowsBeforeSecond, "…still no new row");

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

// ---- 3b. AND THE PRESET'S TILE NOW MEANS THE PROJECT'S COPY -------------
//
// Dropping the shipped tile again must not put a SECOND "Wood PBR" in the
// tray beside the user's own, and must not paint the mesh with a picture they
// have already changed.
var cubeA2 = scene.addPrimitive("Cube");
assert(material.apply(cubeA2, "Wood PBR") === true, "the preset applied again, by name");
assert(projectMaterials().length === 1 && projectMaterials()[0] === copy,
       "the project still holds ONE material — its own copy, not the master back again");
assert(Math.abs(material.get(cubeA2).roughness - 0.13) < 1e-3,
       "…and the new cube wears the EDITED picture (" + material.get(cubeA2).roughness + ")");

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

// ---- 5b. PRESET-FOLD-1: a project's copy is that project's ---------------
//
// Two projects now hold a copy of Wood PBR. The LIBRARY listing — the Assets
// page's grid, assets.list({scope:'store'}) — shows the preset ONCE: a copy is
// its project's own row (ASSETS-SCOPE-1) and is shown only in its own
// project's views. And B cannot pin A's copy: that would make it B's copy too
// (projectCopyOf answers the first copy pinned).
function woodIn(rows) { return rows.filter(function (a) { return a.name === "Wood PBR"; }); }
var libWood = woodIn(assets.list({ scope: "store" }));
assert(libWood.length === 1 && libWood[0].guid === WOOD,
       "the library list shows ONE 'Wood PBR' — the master (" + JSON.stringify(libWood) + ")");
var storeGuids = assets.list({ scope: "store", members: true }).map(function (a) { return a.guid; });
assert(storeGuids.indexOf(copy) < 0 && storeGuids.indexOf(editB.guid) < 0,
       "…and neither project's copy is a library row at all, members shown or not");
assert(nameOf(copy) === "Wood PBR" && nameOf(editB.guid) === "Wood PBR",
       "…while both still resolve by guid");
var refusedPin = "";
try { assets.addToProject(copy); } catch (e) { refusedPin = e.message; }
console.log("   refusal: " + refusedPin);
assert(/own copy of the shipped preset/.test(refusedPin),
       "B's add of A's copy is REFUSED, with the reason");
assert(assets.pins(copy).length === 1 && projectMaterials().indexOf(copy) < 0,
       "…A's copy is still pinned by A alone, and not by B");
assert(materials.edit(WOOD).guid === editB.guid,
       "…and 'Wood PBR' in B still means B's own copy");
var projC = project.create("Preset Edit C " + Date.now());
assert(assets.addToProject(WOOD) === WOOD, "a third project's add from the library gets the MASTER");
assert(projectMaterials().length === 1 && projectMaterials()[0] === WOOD,
       "…and pins the master, not anybody's copy");
assert(project.open(projB) === true, "back to B for the sections below");

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
