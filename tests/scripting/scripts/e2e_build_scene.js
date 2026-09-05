// scripting.e2e.build_scene — SCRIPTING_SPEC phase 1 end-to-end proof.
//
// Runs inside the real app (./Jahshaka --script) with the engine viewport up
// and HOME redirected to a scratch dir (fresh DB, fresh projects folder).
// Builds a scene through the API, asserts document state, steps deterministic
// frames and pixel-asserts a screenshot. Throwing exits non-zero.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function near(a, b) { return Math.abs(a - b) < 1e-3; }

// ---- registry sanity: every verb documented and classified ----
var mods = api.verbs();
assert(mods.length >= 5, "registry enumerates " + mods.length + " modules");
var verbCount = 0;
for (var i = 0; i < mods.length; i++) {
    var m = mods[i];
    for (var j = 0; j < m.verbs.length; j++) {
        var v = m.verbs[j];
        if (!v.doc || v.doc.length === 0) throw new Error(m.module + "." + v.name + " has no doc");
        if (["document", "engine", "window"].indexOf(v.needs) < 0) throw new Error(m.module + "." + v.name + " has bad needs: " + v.needs);
        verbCount++;
    }
}
assert(verbCount >= 30, "all " + verbCount + " verbs documented and classified");
assert(api.version.length > 0, "api.version = " + api.version);

// ---- project lifecycle ----
// unique name: the scratch HOME (and its DB) persists across ctest reruns
var guid = project.create("Scripted Test " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);
var cur = project.current();
assert(cur && cur.guid === guid, "project.current matches");
var listed = project.list().filter(function (p) { return p.guid === guid; });
assert(listed.length === 1, "project.list sees it");

// ---- build a scene ----
var baseline = scene.nodes().length;
var cube = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: 0 } });
assert(cube.length > 10, "scene.addPrimitive(cube)");
var light = scene.addLight("point", { position: { x: 2, y: 3, z: 2 } });
assert(light.length > 10, "scene.addLight(point)");
assert(scene.nodes().length === baseline + 2, "two nodes added");

node.transform(cube, { rotation: { y: 45 }, scale: { x: 2, y: 2, z: 2 } });
var info = node.info(cube);
assert(info.type === "mesh", "cube is a mesh node");
assert(near(info.position.y, 1), "cube position honoured");
assert(near(info.scale.x, 2), "cube scale transformed");
assert(near(info.rotation.y, 45), "cube rotation transformed");

// reflected properties (lights first, per the spec)
node.setProperty(light, "intensity", 3.5);
assert(near(node.property(light, "intensity"), 3.5), "light intensity via setProperty");
node.setProperty(light, "lightColor", "#ff0000");
assert(node.property(light, "lightColor") === "#ff0000", "light color via setProperty");
node.setProperty(cube, "position", { x: 0, y: 2, z: 0 });
assert(near(node.property(cube, "position").y, 2), "position via reflection");

// hierarchy
var group = scene.addEmpty();
node.reparent(cube, group);
assert(node.info(cube).parent === group, "cube reparented under the empty");
var thrown = false;
try { node.reparent(group, cube); } catch (e) { thrown = true; }
assert(thrown, "reparent cycle refused with a catchable error");

// duplicate + remove
var copy = node.duplicate(cube);
assert(copy.length > 10 && copy !== cube, "node.duplicate");
assert(node.remove(copy), "node.remove");
assert(scene.find("nonexistent-node") === null || scene.find("nonexistent-node") === undefined, "find misses cleanly");

// selection round-trip
editor.select(cube);
assert(editor.selection() === cube, "selection round-trip");
editor.select(null);
assert(editor.selection() === null || editor.selection() === undefined, "deselect");

// ---- save (blob-only path must not no-op) ----
assert(project.save() === true, "project.save");

// ---- deterministic frames + pixel assertions ----
editor.frame(3);
var s1 = editor.screenshot("shot1.png", 128, 128);
assert(s1.width === 128 && s1.height === 128, "screenshot size");
var s2 = editor.screenshot("shot2.png", 128, 128);
assert(s1.center.r === s2.center.r && s1.center.g === s2.center.g && s1.center.b === s2.center.b,
       "two screenshots agree (deterministic frames)");
// clear colour is (0.10, 0.11, 0.14) -> ~(26, 28, 36); the default scene's
// ground plane must cover the centre.
var differs = Math.abs(s1.center.r - 26) > 2 || Math.abs(s1.center.g - 28) > 2 || Math.abs(s1.center.b - 36) > 2;
assert(differs, "centre pixel (" + s1.center.r + "," + s1.center.g + "," + s1.center.b + ") is not the clear colour");

// ---- SCENE_STATIC: the never-moves hint (SCENEGRAPH_SPEC 6) --------------
// An empty carries no renderable, so the graph can always switch it; that is
// the case this pins. A drawn MESH may refuse (its renderable was created
// dynamic) and the verb says so rather than lying — asserted only for the flag
// it actually ends up with.
var stat = scene.addEmpty({ position: { x: 3, y: 0, z: 0 } });
assert(node.isStatic(stat) === false, "a new node is dynamic");
assert(node.setStatic(stat, true) === true, "node.setStatic(true) on an empty");
assert(node.isStatic(stat) === true, "...and it reads back as static");
// A static node still MOVES; it just costs more. The transform must land.
node.transform(stat, { position: { x: 7, y: 1, z: 0 } });
assert(Math.abs(node.transform(stat).position.x - 7) < 1e-3,
       "a static node still accepts a transform");
assert(node.setStatic(stat, false) === true, "node.setStatic(false) switches back");
assert(node.isStatic(stat) === false, "...and it reads back as dynamic");
assert(node.remove(stat), "node.remove(static probe)");

// ---- error surface: engine guard errors are catchable, bad ids throw ----
thrown = false;
try { node.info("no-such-guid"); } catch (e) { thrown = true; assert(("" + e).indexOf("no-such-guid") >= 0, "bad id error names the id"); }
assert(thrown, "bad node id throws");

console.log("e2e_build_scene: ALL OK (" + verbCount + " verbs registered)");
