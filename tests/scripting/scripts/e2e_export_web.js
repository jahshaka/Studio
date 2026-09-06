// scripting.e2e.export_web — WEB_EXPORT_AUDIT phase 0/1 end-to-end proof.
//
// Runs headless (--headless: document verbs only). Builds a scene through the
// API — the same shape as e2e_build_scene — then drives project.exportWeb and
// asserts the returned export summary: counts, KHR extensions, inlining. The
// deep structural validation of the GLB/HTML bytes lives in the C++ suite
// (export.web); this run proves the verb end to end in the real binary.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var guid = project.create("Web Export Test " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- publish.state() before anything was published (F14, 2026-09-06) ----
// The publish RECORD was invisible to scripts: "has this project been
// published, where, and is the export still there?" had no answer on the verb
// surface. It is READ-ONLY on purpose — project.exportWeb writes an export and
// deliberately does NOT write the record, because publishing is a deliberate
// act performed on the Publish page.
var pub0 = publish.state();
assert(pub0.state === "none", "a fresh project has never been published");
assert(pub0.dir === "" && pub0.index === "" && pub0.when === "",
       "…and every field is empty in that state: " + JSON.stringify(pub0));
assert(pub0.exists === false, "…exists is false");

var cube = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: 0 } });
assert(cube.length > 10, "scene.addPrimitive(cube)");
var sphere = scene.addPrimitive("sphere", { position: { x: 2, y: 1, z: 0 } });
assert(sphere.length > 10, "scene.addPrimitive(sphere)");
// A REFRACTIVE material (alphaMode 6) in the real binary: it exported as plain
// OPAQUE until PUBLISH_AUDIT #1, because the exporter's blend-mode switch
// stopped at 5. The proof is in r.extensions below — KHR_materials_ior is
// written by that arm and by nothing else.
var glassCube = scene.addPrimitive("cube", { position: { x: -2, y: 1, z: 0 } });
assert(glassCube.length > 10, "scene.addPrimitive(cube) for the refractive material");
assert(material.set(glassCube, { alphaMode: 6, alpha: 0.25, refractionStrength: 0.5 }),
       "material.set alphaMode=6 (refractive) + refractionStrength");
assert(material.get(glassCube).alphaMode === 6, "the material really is refractive");

var light = scene.addLight("point", { position: { x: 2, y: 3, z: 2 } });
assert(light.length > 10, "scene.addLight(point)");
var spot = scene.addLight("spot", { position: { x: 0, y: 4, z: 0 } });
assert(spot.length > 10, "scene.addLight(spot)");

var r = project.exportWeb();
assert(r && r.dir && r.dir.length > 0, "exportWeb returned a dir: " + r.dir);
assert(r.indexHtml.indexOf("index.html") >= 0, "indexHtml path: " + r.indexHtml);
assert(r.glb.indexOf("scene.glb") >= 0, "glb path: " + r.glb);
assert(r.glbSize > 1000, "glb has content (" + r.glbSize + " bytes)");
// the inlined index carries the 1.36MB three.js bundle + the GLB as base64
assert(r.inlined === true, "scene inlined under the size ceiling");
assert(r.indexSize > 1000000, "index.html embeds the viewer bundle (" + r.indexSize + " bytes)");
assert(r.nodes >= 4, "exported nodes: " + r.nodes);
assert(r.meshes >= 2, "exported meshes: " + r.meshes);
assert(r.materials >= 1, "exported materials: " + r.materials);
// the default scene project.create builds carries its own lights, so >= the 2 added
assert(r.lights >= 2, "exported lights: " + r.lights);
assert(r.extensions.indexOf("KHR_lights_punctual") >= 0,
       "KHR_lights_punctual present: [" + r.extensions.join(", ") + "]");
assert(r.extensions.indexOf("KHR_materials_transmission") >= 0,
       "refractive glass exported as transmission, not opaque");
assert(r.extensions.indexOf("KHR_materials_ior") >= 0,
       "refractive glass carries an index of refraction");

// ---- publish.state() after an export at the conventional path ----
// exportWeb with no argument writes <project>/exports/web — the SAME path the
// Publish page owns — so the record's backfill (a publish older than the
// record file, synthesized from index.html's timestamp) sees it. That is the
// half of the state machine a script can reach without the page.
var pub1 = publish.state();
assert(pub1.state === "present", "the export at the conventional path reads as published");
assert(pub1.exists === true, "…exists is true");
assert(pub1.dir.indexOf("exports") >= 0 && pub1.dir.indexOf("web") >= 0,
       "…dir is the per-project publish path: " + pub1.dir);
assert(pub1.index.indexOf("index.html") >= 0, "…index names the entry file: " + pub1.index);
assert(pub1.when.length >= 10, "…when is an ISO timestamp: " + pub1.when);

// export into an explicit directory too (the verb's dir parameter)
var r2 = project.exportWeb(r.dir + "-explicit");
assert(r2.dir.indexOf("-explicit") >= 0, "explicit dir honoured: " + r2.dir);

// previewWeb on a missing export fails catchably (never spawns a browser here)
var thrown = false;
try { project.previewWeb(r.dir + "-nonexistent"); } catch (e) {
    thrown = ("" + e).indexOf("exportWeb") >= 0;
}
assert(thrown, "previewWeb without an export throws a catchable, explanatory error");

console.log("e2e_export_web: ALL OK");
