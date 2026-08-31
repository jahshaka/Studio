// scripting.e2e.bake_pipeline — the Materials Evaluator bake pipeline end to
// end through the verbs (MATERIALS_EVALUATOR_SPEC sections 5-6): create a
// graph, build a math+uniform surface via graph.*, classify with bakeInfo,
// bake to hash-cached PNGs under <project>/BakedMaps/<guid>/, apply with
// toMaterial (a final-bake trigger) and read the maps back off the scene
// material, save (bakes into the stored definition), and recover with
// materials.regenerate.
//
// .jaf export note: the exporter zips the whole project working directory
// recursively (src/shell/mainwindow.cpp exportSceneAsZip), so asserting the
// baked maps land INSIDE the project folder is asserting they ride the zip.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var projectGuid = project.create("Bake Pipeline " + Date.now());
assert(projectGuid.length > 10, "project.create");
var folder = project.current().folder;

// ---- build the graph through the verbs ----
var shaderGuid = materials.createGraph("BakePipelineFx");
assert(shaderGuid.length > 10, "materials.createGraph");

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
assert(baked.maps.roughnessMap && baked.maps.roughnessMap.indexOf("BakedMaps/") === 0,
       "bake emits a project-relative roughnessMap under BakedMaps/");
assert(baked.maps.roughnessMap.indexOf(shaderGuid) !== -1, "the map lands in the shader's own cache dir");
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
assert(("" + mat.roughnessMap).indexOf(folder) === 0,
       "scene material's roughnessMap is INSIDE the project folder (rides the .jaf zip)");
assert(("" + mat.roughnessMap).indexOf("BakedMaps/" + shaderGuid) !== -1,
       "scene material's roughnessMap points into BakedMaps/<shaderGuid>/");

// ---- save: the stored definition bakes too ----
assert(graph.save() === true, "graph.save (final-bake trigger)");

// ---- regenerate: the cache-recovery verb ----
assert(materials.regenerate(shaderGuid) === true, "materials.regenerate");
var mat2 = material.get(cube);
assert(("" + mat2.roughnessMap).indexOf("BakedMaps/" + shaderGuid) !== -1,
       "regenerate refreshed the applied material from the fresh bake");

project.close();
console.log("bake pipeline e2e passed");
