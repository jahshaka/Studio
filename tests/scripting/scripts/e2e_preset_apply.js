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
//   4b. EVERY PRESET IS A GRAPH, AND SELECTING ONE SHOWS IT (PRESET-UNIFY-1,
//      the owner 2026-09-20). One shipped list, no duplicates; a preset opens
//      READ-ONLY and opening one seeds nothing; the CUSTOMISED copy carries
//      the graph, which is what makes it something the node editor can open
//      and edit.
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

// ---- 0. ONE LIST, NO DUPLICATES (PRESET-UNIFY-1) -------------------------
//
// Twenty, because the two graph templates that had no tray tile (Painted
// Metal, Grass 2) are presets now and the fifteen that were BOTH a template
// and a preset are one thing.
var presets = materials.presets();
assert(presets.length === 20,
       "twenty shipped presets — got " + presets.length);
var seenNames = {}, seenGuids = {};
presets.forEach(function (p) {
    if (!p.guid || p.guid.length < 10) throw new Error("preset '" + p.name + "' has no guid");
    if (seenNames[p.name]) throw new Error("two presets called '" + p.name + "'");
    if (seenGuids[p.guid]) throw new Error("two presets on guid " + p.guid);
    seenNames[p.name] = seenGuids[p.guid] = true;
});
assert(true, "…each with a guid, and no name or guid appears twice");
// THE OLD FAMILY IS GONE: the drawer used to show "Gold" beside "Gold PBR".
["Gold", "Brick", "Silver", "Glass", "Default", "Basic", "Texture", "Checker Board",
 "Grass", "Leather", "Stone", "Wood", "Marble tile", "Patchy grass", "sand",
 "Painted metal", "Grass2"].forEach(function (old) {
    if (seenNames[old]) throw new Error("the graph template '" + old + "' is still a second preset");
});
assert(true, "…and not one of the seventeen graph templates survives as a second tile");

// ---- 1. THE FIRST-RUN SEED, then three applies that add nothing ----------
//
// The app seeds every shipped preset at launch (services/materialpresetseeder.h
// — the map bytes on a worker, the rows one per event-loop turn), so the
// library reaches a steady state of TWENTY bundles, one per preset, with no
// gesture at all. `materials.seedPresets()` is that same seed on demand: it
// makes the count deterministic here instead of racing the launch.
// ---- 1b. LOOKING AT A PRESET WRITES NOTHING (PRESET-UNIFY-1) -------------
//
// Before the seed, so "nothing exists yet" is a fact and not a hope. Asked of
// BRICK, which has THREE MAPS — the first version of this arm asked it of
// Gold, which has none, and so proved nothing: opening a TEXTURED preset went
// through the content import, wrote three library Texture rows, PINNED them
// into the open project and waited for the device once per file, on the
// thread that draws, because somebody looked at a material.
assert(materialRows().length === 0, "nothing is seeded yet");
var texturesAtRest = assets.list({ scope: "store", type: "texture" }).length;
var pinsAtRest = assets.list({ scope: "project" }).length;
var waitsAtRest = assets.storeStatus().deviceWaits;

var peek = materials.loadGraph("Brick PBR");
assert(peek.nodes >= 4, "a TEXTURED preset's graph opens (" + peek.nodes + " nodes)");
assert(peek.master === "PbrMaterial", "…on the one master");
assert(peek.readOnly === true, "…and it says READ-ONLY, before any edit is attempted");
assert(peek.texturesResolved === 3,
       "…with its three images bound (" + peek.texturesResolved + ")");
assert(peek.texturesImported === 0, "…and NONE of them imported");
assert(materialRows().length === 0,
       "…no material row (" + materialRows().length + ")");
assert(assets.list({ scope: "store", type: "texture" }).length === texturesAtRest,
       "…no texture row either");
assert(assets.list({ scope: "project" }).length === pinsAtRest,
       "…nothing pinned into the open project");
assert(assets.storeStatus().deviceWaits === waitsAtRest,
       "…AND NOT ONE WAIT FOR THE DEVICE (" + waitsAtRest + " -> "
       + assets.storeStatus().deviceWaits + ")");

// The same for a preset with no maps at all, which is where this started.
var gold = materials.loadGraph("Gold PBR");
assert(gold.readOnly === true && materialRows().length === 0,
       "…and Gold PBR too");

// ---- 1c. THE READ-ONLY GRAPH REFUSES EDITS (fix round) -------------------
//
// It used to TAKE them: the canvas accepted nodes, wires, drags and typed
// values, the save quietly returned, and Customise then built the copy from
// the SHIPPED definition — so the work went into a window that showed it and
// into nothing else.
var presetNodes = graph.nodes().length;
["addNode", "setBlendMode", "save"].forEach(function (verb) {
    var refused = false;
    try {
        if (verb === "addNode") graph.addNode("float");
        else if (verb === "setBlendMode") graph.setBlendMode("Translucent");
        else refused = (graph.save() !== true);
    } catch (e) { refused = true; }
    if (!refused) throw new Error("graph." + verb + " was NOT refused on a read-only preset");
});
assert(true, "graph.addNode / setBlendMode / save are refused on a read-only preset");
assert(graph.nodes().length === presetNodes,
       "…and the graph is untouched (" + graph.nodes().length + " nodes)");

assert(materials.seedPresets() === 20, "the first-run seed: twenty bundles");
var seeded = materialRows();
assert(seeded.length === 20,
       "…and that is the whole library's material list (" + seeded.length + ")");
assert(materials.seedPresets() === 20, "seeding again is idempotent");
assert(materialRows().length === 20, "…and mints nothing the second time");

var cube = scene.addPrimitive("Cube");
assert(!!cube, "a cube to paint");

assert(material.apply(cube, "Gold PBR") === true, "apply 1: Gold PBR");
assert(materialRows().length === 20,
       "the apply MINTED NOTHING (" + materialRows().length + " rows)");
assert(materialRows().filter(function (r) { return r.guid === GOLD; }).length === 1,
       "…it used the preset's own reserved guid");

assert(material.apply(cube, "Gold PBR") === true, "apply 2");
assert(material.apply(cube, GOLD) === true, "apply 3 (by guid this time)");
assert(materialRows().length === 20,
       "THREE APPLIES, ZERO NEW ROWS (" + materialRows().length + ")");

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
var rowsBefore = materialRows().length;
var proj2 = project.create("Preset Apply Second " + Date.now());
var cube2 = scene.addPrimitive("Cube");
assert(material.apply(cube2, "Brick PBR") === true, "the second project applies Brick PBR");
assert(materialRows().length === rowsBefore,
       "still one row per PRESET, not per project (" + materialRows().length + ")");
assert(assets.list({ scope: "store", type: "texture" }).length === texturesBefore,
       "and not one texture was imported a second time");
assert(assets.list({ scope: "project", type: "material" }).length === 1,
       "the second project pins the same bundle");

// ---- 3. read-only in fact -------------------------------------------------
var texturesNow = assets.list({ scope: "store", type: "texture" }).length;
var refused = false;
try { materials.addTexture(BRICK, texturesBefore > 0
                                      ? assets.list({ scope: "store", type: "texture" })[0].guid
                                      : GOLD,
                           { slot: "emissiveMap" }); }
catch (e) { refused = true; console.log("   refusal: " + e.message); }
assert(refused, "a WRITE to a shipped preset is refused");
assert(assets.list({ scope: "store", type: "texture" }).length === texturesNow,
       "…BEFORE it imports anything for an edit that cannot land");

// …and the refusal is the WRITER's, not the UI's: regenerate goes straight to
// MaterialBundle::write, which knows the reserved guid by name.
var regenRefused = false;
try { regenRefused = (materials.regenerate(BRICK) !== true); }
catch (e) { regenRefused = true; console.log("   refusal: " + e.message); }
assert(regenRefused, "…and so is a re-bake, at the definition writer");

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

// A COPY IS A GRAPH MATERIAL, so its slots come from the graph — the slot
// door says so by name and imports the picture anyway rather than losing it.
var slotRouted = false;
try { materials.addTexture(named, presetMemberGuids[0], { slot: "emissiveMap" }); }
catch (e) { slotRouted = /comes from the graph/.test(e.message); console.log("   " + e.message); }
assert(slotRouted,
       "a slot edit on the COPY is routed to its graph (it has one now), not refused as read-only");

// ---- 4b. THE COPY CARRIES THE GRAPH (PRESET-UNIFY-1) ---------------------
//
// "a custom preset is a new material based on the preset it was customised
// from". Before this the copy carried the preset's VALUES only, so the node
// editor opened on an empty canvas — which is what the owner saw.
var presetGraph = materials.loadGraph(BRICK);
assert(presetGraph.nodes >= 3, "the preset's own graph reads (" + presetGraph.nodes + " nodes)");
assert(presetGraph.readOnly === true, "…read-only");
var copyGraph = materials.loadGraph(named);
assert(copyGraph.nodes === presetGraph.nodes,
       "the COPY carries the same graph (" + copyGraph.nodes + " nodes)");
assert(graph.settings().name === "My Bricks",
       "…under ITS OWN name, not the preset's (graph settings say '"
       + graph.settings().name + "')");
assert(copyGraph.master === "PbrMaterial", "…on the one master");
assert(copyGraph.readOnly === false,
       "…and it is NOT read-only: the copy is the user's to edit");

// CUSTOMISE AND SAVE CHANGES NOTHING ABOUT THE PICTURE. This is the assertion
// the whole design rests on: a preset's VALUES and its GRAPH describe one
// material, so re-deriving the copy from its graph gives back what the preset
// applies — including the rows no graph can produce (Brick's roughness remap
// bounds and its 4x tiling), which a save carries forward instead of deleting.
var presetCube = scene.addPrimitive("Cube");
var copyCube = scene.addPrimitive("Cube");
assert(material.apply(presetCube, BRICK) === true, "the preset on one cube");
var beforeSave = material.get(presetCube);
materials.loadGraph(named);
assert(graph.save() === true, "the copy re-saved from its graph, untouched");
assert(material.apply(copyCube, named) === true, "the re-saved copy on another");
var afterSave = material.get(copyCube);
["baseColor", "metallic", "roughness", "roughnessLowerBound", "roughnessUpperBound",
 "textureScale", "textureScaleV", "normalFactor", "alpha", "alphaMode",
 "baseColorMap", "roughnessMap", "normalMap"].forEach(function (key) {
    var a = beforeSave[key], b = afterSave[key];
    if (typeof a === "number" ? Math.abs(a - b) > 1e-3 : String(a) !== String(b))
        throw new Error("customise + save moved '" + key + "': " + a + " -> " + b);
});
assert(true, "…every row of the picture is the preset's, after a re-save from the graph");

// AND THE COPY TAKES A REAL GRAPH EDIT, which is what Customise is for. The
// preset's own graph refuses the same save, by the reserved guid.
materials.loadGraph(named);
var master = graph.nodes().filter(function (n) { return n.master; })[0];
assert(!!master, "the copy's graph has a master");
var rough = graph.addNode("float");
assert(!!rough, "a float node on the copy's graph");
assert(graph.setValue(rough, 0.11) === true, "…set to 0.11");
assert(graph.connect(rough, 0, master.id, "Roughness") === true, "…wired to Roughness");
assert(graph.save() === true, "the COPY saves");
var afterEdit = graph.evaluate().values.roughness;
assert(Math.abs(afterEdit - 0.11) < 1e-3,
       "…and the graph now folds to that roughness (" + afterEdit + ")");
// THE PRESET REFUSES THE SAME SAVE.
materials.loadGraph(BRICK);
var presetSaveRefused = false;
try { presetSaveRefused = (graph.save() !== true); }
catch (e) { presetSaveRefused = true; console.log("   " + e.message); }
assert(presetSaveRefused, "…while the PRESET's own graph cannot be saved over");

// ---- 4c. THE FOUR WAYS A SAVE USED TO CHANGE WHAT NOBODY TOUCHED --------
//
// The first version of the carry-forward kept every row the graph did not
// produce, which meant it kept rows the graph had DELIBERATELY not produced.
// Each of these is that defect, as a picture-row check after a save.
function rowsOf(guid) {
    var probe = scene.addPrimitive("Cube");
    material.apply(probe, guid);
    return material.get(probe);
}
var glassCopy = materials.createFromPreset("Glass PBR", { name: "Glass Copy" });
assert(Math.abs(rowsOf(glassCopy).alphaMode - 3) < 0.5,
       "(a) the Glass copy IS glass to begin with (alphaMode "
       + rowsOf(glassCopy).alphaMode + ")");
materials.loadGraph(glassCopy);
assert(graph.settings().blendMode === "Glass",
       "…and its GRAPH says so, which is what makes it survive a save ('"
       + graph.settings().blendMode + "')");
assert(graph.save() === true, "…re-saved untouched");
assert(Math.abs(rowsOf(glassCopy).alphaMode - 3) < 0.5, "…still glass");

materials.loadGraph(glassCopy);
assert(graph.setBlendMode("Opaque") === true, "(a) now set Blend Mode -> Opaque");
assert(graph.save() === true, "…and save");
assert(Math.abs(rowsOf(glassCopy).alphaMode) < 0.5,
       "…IT IS OPAQUE (alphaMode " + rowsOf(glassCopy).alphaMode + ", was glass for ever)");

// (b) the same for a mode a graph has always been able to say.
materials.loadGraph(glassCopy);
graph.setBlendMode("Translucent"); graph.save();
assert(Math.abs(rowsOf(glassCopy).alphaMode - 2) < 0.5, "(b) translucent lands");
materials.loadGraph(glassCopy);
graph.setBlendMode("Opaque"); graph.save();
assert(Math.abs(rowsOf(glassCopy).alphaMode) < 0.5, "(b) …and clears again");

// (c) disconnecting the Alpha socket clears the alpha it was carrying.
materials.loadGraph(glassCopy);
var glassMaster = graph.nodes().filter(function (n) { return n.master; })[0];
assert(Math.abs(graph.evaluate().values.alpha - 0.3) < 1e-3,
       "(c) the Glass copy's graph folds alpha 0.3");
assert(graph.disconnect({ to: glassMaster.id, toSocket: "Alpha" }) === true,
       "…disconnect Alpha");
assert(graph.save() === true, "…and save");
assert(Math.abs(rowsOf(glassCopy).alpha - 1) < 1e-3,
       "…the material is OPAQUE-alpha again (" + rowsOf(glassCopy).alpha
       + ", the old 0.3 used to come back)");

// (d) THE OWNER'S OWN GESTURE (R19): change the UV tiling, then REMOVE the
// node, and the tiling goes with it instead of the last saved one coming back.
//
// The first version of this arm proved nothing and is worth naming: it wrote
// `graph.setValue(uv, {x:10, y:10}) === true || true` — a tautology, and the
// UV node reads `tileX`/`tileY`, so it set identity — then connected into an
// OCCUPIED socket (which left the preset's own wire in the graph), printed
// the tiling instead of asserting it, and finished by checking for the very
// number the preset already had.
var tiled = materials.createFromPreset("Brick PBR", { name: "Tiled Bricks" });
materials.loadGraph(tiled);
var tiledUv = graph.nodes().filter(function (n) { return n.type === "uv"; })[0];
assert(!!tiledUv, "(d) the copy carries the preset's own UV node");
assert(graph.setValue(tiledUv.id, { tileX: 10, tileY: 10 }) === true, "…set to 10 x 10");
assert(graph.save() === true, "…saved");
var tiled10 = rowsOf(tiled);
console.log("   (d) after the edit: textureScale=" + tiled10.textureScale
            + " textureScaleV=" + tiled10.textureScaleV);
assert(Math.abs(tiled10.textureScale - 10) < 1e-3 && Math.abs(tiled10.textureScaleV - 10) < 1e-3,
       "…and BOTH axes read 10 (" + tiled10.textureScale + ", " + tiled10.textureScaleV + ")");

materials.loadGraph(tiled);
tiledUv = graph.nodes().filter(function (n) { return n.type === "uv"; })[0];
assert(graph.removeNode(tiledUv.id) === true, "…now REMOVE the UV node");
assert(graph.save() === true, "…and save again");
var tiledNone = rowsOf(tiled);
console.log("   (d) after the removal: textureScale=" + tiledNone.textureScale
            + " textureScaleV=" + tiledNone.textureScaleV);
assert(Math.abs(tiledNone.textureScale - 1) < 1e-3 && Math.abs(tiledNone.textureScaleV - 1) < 1e-3,
       "…the tiling is IDENTITY, not the 10 that was there a moment ago ("
       + tiledNone.textureScale + ", " + tiledNone.textureScaleV + ")");

// (e) AN INPUT TAKES ONE WIRE, AND A NEW ONE REPLACES THE OLD. The old
// ConnectionModel used to be left in the graph — still serialized, still
// pointing at a socket that no longer believed in it.
materials.loadGraph(tiled);
var replMaster = graph.nodes().filter(function (n) { return n.master; })[0];
var wiresBefore = graph.connections().filter(function (c) {
    return c.to === replMaster.id && c.toSocket === "Roughness";
}).length;
assert(wiresBefore === 1, "(e) Roughness has exactly one wire to begin with");
var newRough = graph.addNode("float");
assert(graph.setValue(newRough, 0.4) === true, "…a float of 0.4");
assert(graph.connect(newRough, 0, replMaster.id, "Roughness") === true,
       "…connected into the OCCUPIED Roughness socket");
var wiresAfter = graph.connections().filter(function (c) {
    return c.to === replMaster.id && c.toSocket === "Roughness";
}).length;
assert(wiresAfter === 1,
       "…and it REPLACED the old wire rather than leaving it behind ("
       + wiresBefore + " -> " + wiresAfter + ")");
assert(graph.save() === true, "…saved");
assert(Math.abs(rowsOf(tiled).roughness - 0.4) < 1e-3,
       "…the material takes the new wire's value (" + rowsOf(tiled).roughness + ")");

// (f) THE MODE ITSELF ROUND-TRIPS, both ways. A graph can NAME Refractive
// (alphaMode 6) but has no socket for what refraction is MADE of —
// refractionStrength, ior, the fresnel rows — so those ride the previous
// definition whenever the mode is Refractive and are dropped for every other
// mode, exactly like the roughness remap bounds. WHAT THIS ARM DOES NOT
// ASSERT, said plainly rather than faked: no verb writes a library material's
// scalar rows, so there is no way from a script to plant a DISTINCTIVE
// refractionStrength and watch it survive — every reachable starting value is
// the constructor's 0.35, which proves nothing. The carry itself is covered by
// the code and by this arm's neighbours; what is asserted here is the half a
// script can see.
var refr = materials.createFromPreset("Glass PBR", { name: "Refractive Copy" });
materials.loadGraph(refr);
assert(graph.setBlendMode("Refractive") === true, "(f) a copy set to Refractive");
assert(graph.save() === true, "…saved");
var refrRows = rowsOf(refr);
console.log("   (f) alphaMode=" + refrRows.alphaMode
            + " refractionStrength=" + refrRows.refractionStrength);
assert(Math.abs(refrRows.alphaMode - 6) < 0.5,
       "…the material IS refractive (alphaMode " + refrRows.alphaMode + ")");
assert(refrRows.refractionStrength > 0,
       "…with a refraction strength, not a zeroed row (" + refrRows.refractionStrength + ")");
materials.loadGraph(refr);
graph.setBlendMode("Opaque"); graph.save();
assert(Math.abs(rowsOf(refr).alphaMode) < 0.5, "…and it comes back out again");

// ---- 4d. A NEW MATERIAL CANNOT TAKE A PRESET'S NAME ----------------------
//
// A preset is reached BY NAME, so a user's material called "Gold PBR" is not
// a duplicate label: it is a material nothing can ever address by name, while
// the name goes on resolving to the preset.
var nameRefused = false;
try { materials.create("Gold PBR", { graph: true }); }
catch (e) { nameRefused = true; console.log("   " + e.message); }
assert(nameRefused, "materials.create refuses a shipped preset's name");
var renameRefusedToPreset = false;
try { renameRefusedToPreset = (assets.rename(copy1, "Silver PBR") !== true); }
catch (e) { renameRefusedToPreset = true; }
assert(renameRefusedToPreset, "…and so does a RENAME into one");
assert(materials.createFromPreset("Gold PBR") !== "",
       "…while Customise, which names the copy '<Preset>-N', is how you get one");
// …and a name that differs from a preset's only in CASE is a preset's name:
// `MaterialPresets::find` matches case-insensitively, so the namer must too.
var lowerCopy = materials.createFromPreset("Gold PBR", { name: "gold pbr" });
var lowerName = assets.list({ scope: "store" }).filter(function (a) {
    return a.guid === lowerCopy;
})[0].name;
assert(lowerName.toLowerCase() !== "gold pbr",
       "…and a lowercase 'gold pbr' is bumped too (got '" + lowerName + "')");

// EVERY preset has one, which is the whole rule — a tile that cannot answer
// "show me this material" has no business in the drawer.
presets.forEach(function (p) {
    var g = materials.loadGraph(p.guid);
    if (!(g.nodes >= 2) || g.master !== "PbrMaterial")
        throw new Error("preset '" + p.name + "' has no graph");
    if (g.readOnly !== true)
        throw new Error("preset '" + p.name + "' does not report read-only");
});
assert(true, "…and all twenty presets open as read-only PBR graphs");

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
assert(afterNew === pushesBefore + 3,
       "an apply of a NEW bundle records THREE commands in its one macro — the pin, the "
       + "material and the use edge (" + pushesBefore + " -> " + afterNew + ")");

var cube4 = scene.addPrimitive("Cube");
var pushesHeld = editor.undoState().pushes;
assert(material.apply(cube4, "Silver PBR") === true, "apply it again, on another mesh");
assert(editor.undoState().pushes === pushesHeld + 2,
       "a bundle the project already holds records the material and the use edge, no pin");
assert(editor.undoState().macroOpen === true,
       "the run's own macro is open, which is WHY the undo itself is commands.pin_asset's");

// ---- 6. THE ROW IS READ-ONLY TOO, not just its definition (F4) -----------
var renameRefused = false;
try { assets.rename(GOLD, "Fred"); } catch (e) { renameRefused = true; console.log("   " + e.message); }
assert(renameRefused, "a shipped preset cannot be RENAMED (one guid, two names, for ever)");
assert(assets.list({ scope: "store" }).filter(function (a) { return a.guid === GOLD; })[0].name
           === "Gold PBR",
       "…and the row still carries the preset's name");
assert(assets.setTags(GOLD, ["kitchen"]).length === 1,
       "…while TAGGING one is still the user's own business");
assert(assets.rename(copy1, "My Gold") === true, "the COPY renames, as any material does");

// ---- 7. AN APPLY THAT CANNOT HAPPEN IMPORTS NOTHING (F6) ------------------
//
// The tray passes whatever is selected, so a preset double-clicked with a
// LIGHT selected used to import the preset's maps, mint its row and pay for
// all of it before the apply returned false. The mesh test comes first now.
var light = scene.addLight("point");
var texturesBeforeLight = assets.list({ scope: "store", type: "texture" }).length;
var rowsBeforeLight = materialRows().length;
var lightRefused = false;
// The VERB refuses by precondition (it throws with the node named); the TRAY
// does not — it hands SceneEditService whatever is selected — which is why
// the mesh test also lives above the seed in the service. Either door, the
// answer is the same: nothing is written down.
try { material.apply(light, "Marble PBR"); } catch (e) { lightRefused = true; }
assert(lightRefused, "applying to a LIGHT is refused");
assert(assets.list({ scope: "store", type: "texture" }).length === texturesBeforeLight
           && materialRows().length === rowsBeforeLight,
       "…and imported nothing, minted nothing");

console.log("preset_apply: all assertions passed");
