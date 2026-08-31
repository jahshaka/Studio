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

var cube = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: 0 } });
assert(cube.length > 10, "scene.addPrimitive(cube)");
var sphere = scene.addPrimitive("sphere", { position: { x: 2, y: 1, z: 0 } });
assert(sphere.length > 10, "scene.addPrimitive(sphere)");
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
