// scripting.e2e.shipped_assets — plan item 15c: the files the app SHIPS reach
// a project through the CAS, never as copies found again by name.
//
// THE DEFECT CLASS. The default ground's tile, the default particle image, the
// material presets' maps and the six cube-sky presets all reached a project
// the pre-pipeline way: QFile::copy into the project folder (the working
// directory, for the startup placeholder) plus a bare catalog row with NO
// stored bytes, and SceneWriter / SceneReader / MaterialReader each kept a
// by-NAME fallback (Database::fetchAssetGUIDByName, projectFolder + row name)
// whose only job was to find those copies again. Nothing pinned them, so a
// project export left them behind (the Mirror Room and Showroom samples ship a
// fileless "Tile.png" row), every project minted its own row for the same
// bytes, and the only thing holding the round trip together was a file NAME.
//
// Every assertion below is about IDENTITY: the saved reference is a guid, the
// guid is a library texture PINNED by the project, the document renders the
// pinned STORE OBJECT, and the same shipped bytes are the same row in every
// project. The disk half (no copy anywhere under HOME, nothing beside the
// binary's working directory) is asserted by shipped_assets.sh after this run.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

function refuses(fn, msg) {
    var threw = false, text = "";
    try { fn(); } catch (e) { threw = true; text = "" + e; }
    assert(threw, msg + " (refused: " + text + ")");
    return text;
}

function isGuid(v) {
    return typeof v === "string" && /^\{?[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\}?$/.test(v);
}

// The saved form of one material slot on a node: what the scene FILE carries.
function savedSlot(id, slot) {
    var frag = node.serialize(id);
    var mat = frag.node.material;
    return (mat && mat.values) ? mat.values[slot] : undefined;
}

function pinnedBy(guid, projectGuid) {
    var pins = assets.pins(guid);
    for (var i = 0; i < pins.length; i++)
        if (pins[i].project === projectGuid) return true;
    return false;
}

var store = app.dataRoot().assetStore;
assert(store && store.length > 0, "the run has an asset store: " + store);

function inStore(path) {
    return typeof path === "string" && path.indexOf(store) === 0 && path.indexOf("/objects/") > 0;
}

// ---------------------------------------------------------------- 1. the tile
var projA = project.create("Shipped A " + Date.now());
assert(isGuid(projA), "project.create A");
var folderA = project.current().folder;
assert(folderA && folderA.length > 0, "project A has a folder: " + folderA);

var groundA = scene.find("Ground");
assert(groundA && groundA.length > 10, "the default scene has its Ground");
var tile = savedSlot(groundA, "baseColorMap");
assert(isGuid(tile), "the ground's saved baseColorMap is an asset GUID (" + tile + ")");
assert(pinnedBy(tile, projA),
       "the tile is PINNED by the project that uses it (a bare row had no pin)");
var tilePath = material.get(groundA).baseColorMap;
assert(inStore(tilePath), "the ground renders the pinned STORE OBJECT: " + tilePath);
assert(tilePath.indexOf(folderA) !== 0, "not a copy in the project folder");
// (Exported beside the data root, into a folder shipped_assets.sh excludes from
// its no-copies sweep: this is the one legitimate "Tile.png" on disk.)
var raw = assets.exportRaw(tile, app.dataRoot().root + "/exported-raw", { dependencies: false });
assert(raw.files && raw.files.length === 1,
       "the tile HAS stored bytes, so an export carries it (" + JSON.stringify(raw.files) + ")");

// -------------------------------------------------- 2. save -> reopen keeps it
assert(project.save() === true, "project.save");
assert(project.close() === true, "project.close");
assert(project.open(projA) === true, "project.open (REOPEN)");
groundA = scene.find("Ground");
assert(savedSlot(groundA, "baseColorMap") === tile, "the reopened ground still names the tile's guid");
assert(material.get(groundA).baseColorMap === tilePath,
       "and still renders the same pinned object after the round trip");

// ------------------------------- 3. ONE row per library, identified by content
var projB = project.create("Shipped B " + Date.now());
var groundB = scene.find("Ground");
assert(savedSlot(groundB, "baseColorMap") === tile,
       "a SECOND project's ground names the SAME library row (one row per content, not per project)");
assert(pinnedBy(tile, projA) && pinnedBy(tile, projB), "and both projects pin it");

// ---------------------------------------------- 4. the default particle image
var em1 = scene.addParticles("fire");
var em2 = scene.addParticles("smoke");
var img1 = node.serialize(em1).node.texture;
var img2 = node.serialize(em2).node.texture;
assert(isGuid(img1), "an emitter's default image saves as an asset GUID (" + img1 + ")");
assert(img2 === img1, "the second emitter reuses the same row (one per library, not per add)");
assert(pinnedBy(img1, projB), "the particle image is pinned by the project");
assert(img1 !== tile, "and it is its own asset, not the tile");

// ------------------------------------------------------ 5. a material preset
var cube = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: 0 } });
assert(material.apply(cube, "Leather PBR") === true, "material.apply(cube, 'Leather PBR')");
var leather = savedSlot(cube, "baseColorMap");
assert(isGuid(leather), "the preset's base map saves as an asset GUID (" + leather + ")");
assert(pinnedBy(leather, projB), "the preset's map is pinned by the project");
assert(inStore(material.get(cube).baseColorMap),
       "the preset renders its pinned store object, not the shipped file: " +
       material.get(cube).baseColorMap);
assert(isGuid(savedSlot(cube, "normalMap")) && isGuid(savedSlot(cube, "roughnessMap")),
       "every map the preset carries is a guid (normal + roughness too)");

// ------------------------------------------------------------ 6. sky presets
var names = world.skyPresets();
assert(names.length === 6 && names.indexOf("Cove") >= 0,
       "world.skyPresets lists the six shipped skies: " + JSON.stringify(names));
var faces = world.skyPreset("cove");
var slots = ["front", "back", "left", "right", "top", "bottom"];
for (var i = 0; i < slots.length; i++) {
    assert(isGuid(faces[slots[i]]), "sky preset face " + slots[i] + " is a guid");
    assert(pinnedBy(faces[slots[i]], projB), "and pinned by the project");
}
var sky = world.get().sky;
assert(sky.type === "Cubemap" || ("" + sky.type).toLowerCase() === "cubemap",
       "the sky is now a cubemap (" + sky.type + ")");
assert(sky.data.front === faces.front && sky.data.bottom === faces.bottom,
       "and the scene's sky block names the pinned faces");
refuses(function () { world.skyPreset("no-such-sky"); }, "an unknown preset name is refused");
// The by-NAME spelling world.sky used to accept is gone with the lookup.
refuses(function () { world.sky("cubemap", { front: "cove_front.jpg" }); },
        "world.sky refuses a FILE NAME for a face (a name is not an identity)");

// Everything above survives a save and a reopen, by guid.
assert(project.save() === true, "project B save");
assert(project.close() === true, "project B close");
assert(project.open(projB) === true, "project B reopen");
assert(savedSlot(scene.find("Cube"), "baseColorMap") === leather, "the preset map survived the reopen");
assert(inStore(material.get(scene.find("Cube")).baseColorMap), "and still renders from the store");
assert(world.get().sky.data.front === faces.front, "the sky's faces survived the reopen");
var em = scene.nodes().filter(function (n) { return n.type === "particles"; });
assert(em.length === 2, "both emitters reopened");
assert(node.serialize(em[0].id).node.texture === img1, "the particle image survived the reopen");

// A third project re-applying the same sky reuses the same six rows.
var projC = project.create("Shipped C " + Date.now());
var facesC = world.skyPreset("Cove");
assert(facesC.front === faces.front && facesC.top === faces.top,
       "a third project's sky preset names the same library rows");
assert(savedSlot(scene.find("Ground"), "baseColorMap") === tile, "and its ground the same tile");

console.log("shipped_assets: ALL OK");
