// scripting.e2e.bake_pipeline — the Materials Evaluator bake pipeline end to
// end through the verbs (MATERIALS_EVALUATOR_SPEC sections 5-6): create a
// graph, build a math+uniform surface via graph.*, classify with bakeInfo,
// bake to hash-cached PNGs, apply with toMaterial (a final-bake trigger) and
// read the maps back off the scene material, save (which is the DEFINITION
// write and stores the maps as MEMBERS), and recover with
// materials.regenerate.
//
// WHERE A BAKED MAP LIVES CHANGED (MATERIAL_BUNDLE_SPEC phase 1). It used to
// be a loose PNG under `<projectFolder>/BakedMaps/<guid>/` named in the
// definition by a project-RELATIVE path, and this suite asserted exactly that
// — "the maps ride the .jaf zip because the zip is the project folder". A
// baked map is a MEMBER TEXTURE in the content-addressed store now: named by
// guid, pinned like any other member, carried by an archive through the
// manifest rather than by being inside a directory, and resolvable on a
// machine that has no such project folder at all. graph.bake is a diagnostic
// that writes into the store's own disposable derived cache and needs no
// project; graph.save is what makes the members.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var projectGuid = project.create("Bake Pipeline " + Date.now());
assert(projectGuid.length > 10, "project.create");
var folder = project.current().folder;

// ---- build the graph through the verbs ----
var shaderGuid = materials.create("BakePipelineFx", { graph: true });
assert(shaderGuid.length > 10, "materials.create");

var masterId = null;
graph.nodes().forEach(function (n) { if (n.master) masterId = n.id; });
assert(masterId !== null, "the new graph has a master node");

var uv = graph.addNode("texCoords");
var split = graph.addNode("splitvector");
assert(graph.connect(uv, 0, split, 0), "texCoords -> splitvector");
assert(graph.connect(split, 0, masterId, "Roughness"), "U -> Roughness (varying)");

var col = graph.addNode("color");
assert(graph.setValue(col, { r: 1.0, g: 0.1, b: 0.1, a: 1.0 }), "color set red");
assert(graph.connect(col, 0, masterId, "Base Color"), "color -> Base Color (uniform)");

// ---- classification ----
var info = graph.bakeInfo().perSocket;
assert(info["Roughness"] === "baked", "bakeInfo: Roughness is 'baked'");
assert(info["Base Color"] === "uniform", "bakeInfo: Base Color is 'uniform'");
assert(info["Emissive"] === "unconnected", "bakeInfo: Emissive is 'unconnected'");

// ---- evaluate still folds the uniform side ----
var ev = graph.evaluate();
assert(ev.values.baseColor && ev.values.baseColor.r > 0.9, "evaluate folds the base colour");

// ---- bake ----
var baked = graph.bake({ resolution: 64 });
// THE MAP MUST NAME A FILE THAT EXISTS. "length > 0" is not an assertion:
// with the baker's `relativePrefix` empty this reported a BARE FILENAME that
// resolved against nothing, and an applied baked map rendered as no texture
// at all — and the weakened check passed the whole way. The honest test with
// the verbs we have is to put the reported path back through the ONE content
// import: it hashes and stores the bytes, so it can only succeed if the file
// is really there.
var bakedPath = "" + baked.maps.roughnessMap;
assert(bakedPath.length > 0, "bake emits a roughnessMap");
assert(bakedPath.indexOf(folder) !== 0,
       "and NOT inside the project folder any more (nothing of a material lives outside the store)");
assert(bakedPath.charAt(0) === "/", "it NAMES its file, absolutely: " + bakedPath);
assert(bakedPath.indexOf("roughnessMap") !== -1, "named for the slot it bakes, plus its content hash");
var provedGuid = assets.importFile(bakedPath);
assert(provedGuid && provedGuid.length > 10,
       "and THE FILE EXISTS — the content import read its bytes");
assert(baked.values.roughness === 1, "roughness factor lands 1.0 beside the map");
assert(baked.unsupported.length === 0, "nothing unsupported");
assert(typeof baked.msElapsed === "number", "bake reports msElapsed (" + baked.msElapsed + " ms)");

// cache hit: identical re-bake names the same file
var baked2 = graph.bake({ resolution: 64 });
assert(baked2.maps.roughnessMap === baked.maps.roughnessMap, "second bake is a cache hit (same hash name)");

// ---- toMaterial: the scene material carries the baked map ----
var cube = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: 0 } });
assert(graph.toMaterial(cube) === true, "graph.toMaterial applies to the cube");
var mat = material.get(cube);
assert(("" + mat.roughnessMap).length > 0, "the scene material carries the baked roughnessMap");

// ---- save: THE DEFINITION WRITE, and the bake becomes a MEMBER ----
assert(graph.save() === true, "graph.save (the definition write, a final-bake trigger)");
var members = materials.members(shaderGuid);
assert(members.length >= 1, "the material has members after the save");
var bakedMember = null;
members.forEach(function (m) { if (m.slot === "roughnessMap") bakedMember = m; });
assert(bakedMember !== null, "the baked roughnessMap is a MEMBER of the material");
assert(bakedMember.baked === true, "and it is recorded as baked");
assert(("" + bakedMember.guid).length > 10, "named by GUID, never by a path");

// ---- regenerate: the cache-recovery verb ----
assert(materials.regenerate(shaderGuid) === true, "materials.regenerate");
var membersAgain = materials.members(shaderGuid);
var again = null;
membersAgain.forEach(function (m) { if (m.slot === "roughnessMap") again = m; });
assert(again !== null && again.guid === bakedMember.guid,
       "a re-bake moves the member row's bytes, never its guid (a node's copied values stay valid)");

project.close();
console.log("bake pipeline e2e passed");
