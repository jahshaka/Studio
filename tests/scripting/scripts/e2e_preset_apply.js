// scripting.e2e.preset_apply — MATERIAL_BUNDLE_SPEC phase 3, end to end on
// the real binary: A PRESET IS A READ-ONLY LIBRARY BUNDLE, AND APPLYING ONE
// PINS IT.
//
// What each arm answers:
//
//   1. NO ROW PER APPLY (the audit's F5; the spec's "three applies -> zero
//      new rows"). A preset used to mint a project Material row on every
//      apply — the owner's library held "Gold PBR" three times over, under a
//      "Presets" folder, with a matgen.material file written beside each.
//      Applying one now SEEDS the bundle the first time anybody uses it and
//      then pins it: the second and third applies add nothing at all.
//   2. THE BUNDLE IS THE PRESET. Its definition names its maps as MEMBER
//      textures by guid — no file path can reach a definition (F3) — and the
//      picture on the mesh is the one the preset always gave.
//   3. READ-ONLY IN FACT. The definition writer refuses a preset by name, so
//      every verb that would edit one is refused too; the UI merely agrees.
//   4. R18 — CUSTOMISE. materials.createFromPreset mints an EDITABLE copy
//      named "<Preset>-1", the suffix bumped against the names already
//      there, sharing the preset's member textures (one object, used by
//      two). It is an ordinary bundle: editing it is allowed.
//   5. THE PIN IS AN UNDO STEP (F5's other half). What can be asserted from
//      inside a script run is that the step was RECORDED — a run is ONE open
//      macro, so editor.undo() cannot reach it. What undoing it DOES is
//      commands.pin_asset, against the same command with the real database.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var GOLD  = "00000000-0000-0000-0000-000000002024";   // Gold PBR: colours only
var BRICK = "00000000-0000-0000-0000-000000002014";   // Brick PBR: three maps

function materialRows() {
    return assets.list({ scope: "store", type: "material" });
}
function names(rows) {
    return rows.map(function (r) { return r.name; }).sort();
}

var proj = project.create("Preset Apply " + Date.now());
assert(proj.length > 10, "project.create");

// Nothing is seeded before it is used: a drawer lists presets from the
// shipped list and their reserved guids, so the library is still empty here.
assert(materialRows().length === 0, "no preset row exists before any preset is used");
assert(materials.presets().length === 18,
       "eighteen shipped presets (the fourteen pre-PBR files are deleted) — got "
       + materials.presets().length);
materials.presets().forEach(function (p) {
    if (!p.guid || p.guid.length < 10) throw new Error("preset '" + p.name + "' has no guid");
});

// ---- 1. three applies, one row -------------------------------------------
var cube = scene.addPrimitive("Cube");
assert(!!cube, "a cube to paint");

assert(material.apply(cube, "Gold PBR") === true, "apply 1: Gold PBR");
var afterFirst = materialRows();
assert(afterFirst.length === 1, "the first apply SEEDED one bundle (" + afterFirst.length + ")");
assert(afterFirst[0].guid === GOLD, "…under the preset's own reserved guid");

assert(material.apply(cube, "Gold PBR") === true, "apply 2");
assert(material.apply(cube, GOLD) === true, "apply 3 (by guid this time)");
assert(materialRows().length === 1,
       "THREE APPLIES, ZERO NEW ROWS (" + JSON.stringify(names(materialRows())) + ")");

// …and none of the project bookkeeping the old tail minted.
var projectRows = assets.list({ scope: "project", type: "material" });
assert(projectRows.length === 1 && projectRows[0].guid === GOLD,
       "the project holds exactly the preset bundle, by its own guid");
assert(assets.list({ scope: "project" }).filter(function (a) {
           return a.name === "Presets";
       }).length === 0,
       "no 'Presets' folder is minted in the project");

// ---- 2. the bundle IS the preset -----------------------------------------
var brickCube = scene.addPrimitive("Cube");
assert(material.apply(brickCube, "Brick PBR") === true, "apply Brick PBR (three maps)");
var brickMembers = materials.members(BRICK);
assert(brickMembers.length === 3,
       "the preset's three maps are MEMBERS of the bundle (" + brickMembers.length + ")");
brickMembers.forEach(function (m) {
    assert(m.guid.indexOf("/") === -1 && m.guid.indexOf("\\") === -1,
           "member '" + m.name + "' is named by guid, never by path (F3)");
    assert(m.slot && m.slot.length > 0, "member '" + m.name + "' fills a named slot (" + m.slot + ")");
    assert(m.pinned === true, "member '" + m.name + "' is pinned into the project with the bundle");
});
// The picture actually arrived: the mesh wears the preset's base colour.
var painted = material.get(brickCube);
assert(painted.baseColorMap && painted.baseColorMap.length > 0,
       "the mesh's material carries the base colour map");

// A SECOND PROJECT REUSES THE SAME BUNDLE — the preset is library content,
// and the store is keyed on content, so nothing is imported twice.
var texturesBefore = assets.list({ scope: "store", type: "texture" }).length;
var proj2 = project.create("Preset Apply Second " + Date.now());
var cube2 = scene.addPrimitive("Cube");
assert(material.apply(cube2, "Brick PBR") === true, "the second project applies Brick PBR");
assert(materialRows().length === 2,
       "still one row per PRESET, not per project (" + JSON.stringify(names(materialRows())) + ")");
assert(assets.list({ scope: "store", type: "texture" }).length === texturesBefore,
       "and not one texture was imported a second time");
assert(assets.list({ scope: "project", type: "material" }).length === 1,
       "the second project pins the same bundle");

// ---- 3. read-only in fact -------------------------------------------------
var refused = false;
try { materials.addTexture(BRICK, texturesBefore > 0
                                      ? assets.list({ scope: "store", type: "texture" })[0].guid
                                      : GOLD,
                           { slot: "emissiveMap" }); }
catch (e) { refused = true; console.log("   refusal: " + e.message); }
assert(refused, "a WRITE to a shipped preset is refused — by the writer, not by the UI");

var stillThree = materials.members(BRICK).length;
assert(stillThree === 3, "…and the preset's definition did not move (" + stillThree + " members)");

// ---- 4. R18 — Customise ---------------------------------------------------
var copy1 = materials.createFromPreset("Gold PBR");
assert(copy1 && copy1.length > 10, "materials.createFromPreset -> a guid");
assert(copy1 !== GOLD, "…a NEW guid: the copy is not the preset");
var copy1Name = assets.list({ scope: "store" }).filter(function (a) {
    return a.guid === copy1;
})[0].name;
assert(copy1Name === "Gold PBR-1", "…named 'Gold PBR-1' (got '" + copy1Name + "')");

var copy2 = materials.createFromPreset(GOLD);
var copy2Name = assets.list({ scope: "store" }).filter(function (a) {
    return a.guid === copy2;
})[0].name;
assert(copy2Name === "Gold PBR-2", "…and the suffix bumps against the rows already there ('"
       + copy2Name + "')");

var named = materials.createFromPreset("Brick PBR", { name: "My Bricks" });
var namedName = assets.list({ scope: "store" }).filter(function (a) {
    return a.guid === named;
})[0].name;
assert(namedName === "My Bricks", "…and {name} wins when it is given");

// THE COPY SHARES THE PRESET'S MEMBERS (one object, used by two) and IS
// editable — which is the whole point of Customise.
var copyMembers = materials.members(named);
assert(copyMembers.length === 3, "the copy names the preset's three maps (" + copyMembers.length + ")");
var presetMemberGuids = materials.members(BRICK).map(function (m) { return m.guid; }).sort();
assert(JSON.stringify(copyMembers.map(function (m) { return m.guid; }).sort())
           === JSON.stringify(presetMemberGuids),
       "…the SAME rows, not copies of them");
assert(copyMembers[0].usedBy >= 2, "…so the shared picture reads 'used by 2' or more");

var edited = materials.addTexture(named, presetMemberGuids[0], { slot: "emissiveMap" });
assert(edited === presetMemberGuids[0], "the COPY accepts an edit the preset refused");
assert(materials.members(named).length === 3,
       "…(the same three rows: the emissive slot now names one of them too)");

// ---- 5. the pin is an undo step -------------------------------------------
//
// A script run is ONE open macro, so editor.undo() from here reaches the step
// BEFORE the run and can never reach this apply — what undoing the pin DOES
// is commands.pin_asset. What belongs here is that the step was RECORDED, and
// that a SECOND apply of an already-held bundle records no pin step at all.
var cube3 = scene.addPrimitive("Cube");
var pushesBefore = editor.undoState().pushes;
assert(material.apply(cube3, "Silver PBR") === true, "apply a preset the project does not hold");
var afterNew = editor.undoState().pushes;
assert(afterNew === pushesBefore + 2,
       "an apply of a NEW bundle records TWO steps — the pin and the material ("
       + pushesBefore + " -> " + afterNew + ")");

var cube4 = scene.addPrimitive("Cube");
var pushesHeld = editor.undoState().pushes;
assert(material.apply(cube4, "Silver PBR") === true, "apply it again, on another mesh");
assert(editor.undoState().pushes === pushesHeld + 1,
       "a bundle the project already holds records ONLY the material change");
assert(editor.undoState().macroOpen === true,
       "the run's own macro is open, which is WHY the undo itself is commands.pin_asset's");

console.log("preset_apply: all assertions passed");
