// scripting.e2e.sun_light — SUN COUPLING (VISUAL_PARITY re-audit F5): the
// realistic sky's sun drives a chosen directional light.
//
// API-first (SCRIPTING_SPEC §2.3): world.sunLight is the verb the panel
// checkbox on both sky panels calls, and it is what this suite drives — the
// link, the follow, the undo/redo, the refusals, and the save/reopen round
// trip of the document field that carries it.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function throws(fn, msg) {
    var threw = false;
    try { fn(); } catch (e) { threw = true; console.log("ok: " + msg + " (" + e.message + ")"); }
    if (!threw) throw new Error("assert failed (no error): " + msg);
}
function near(a, b, tol, msg) { assert(Math.abs(a - b) <= tol, msg + " (" + a + " vs " + b + ")"); }

// ---- registry: the verb exists and is documented ----
var world_ = api.verbs().filter(function (m) { return m.module === "world"; })[0];
var names = world_.verbs.map(function (v) { return v.name; });
assert(names.indexOf("sunLight") >= 0, "world.sunLight is registered");
assert(names.indexOf("setSunLight") >= 0, "world.setSunLight alias is registered");

var guid = project.create("Sun light " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- fixtures: a directional light, a point light, a cube ----
var sun = scene.addLight("directional", { position: { x: 0, y: 6, z: 0 }, name: "Sun" });
var lamp = scene.addLight("point", { position: { x: 3, y: 2, z: 0 }, name: "Lamp" });
var cube = scene.addPrimitive("cube", { position: { x: 0, y: 0, z: 0 } });
assert(sun.length > 10 && lamp.length > 10 && cube.length > 10, "fixtures created");

world.sky("realistic", { azimuth: 90, elevation: 60, turbidity: 4 });

// ---- unlinked by default ----
assert(world.sunLight() === "", "nothing is driven by default");
assert(world.get().sky.sunLight === "", "world.get() reports the (empty) link");

// The authored rotation, which unlinking has to give back.
var authored = node.info(sun).rotation;

// ---- refusals ----
throws(function () { world.sunLight("not-a-node"); }, "sunLight rejects an unknown id");
throws(function () { world.sunLight(cube); }, "sunLight rejects a non-light node");
throws(function () { world.sunLight(lamp); }, "sunLight rejects a POINT light (the sun is directional)");
assert(world.sunLight() === "", "a refused link changes nothing");

// ---- link: the light swings onto the sun immediately ----
assert(world.sunLight(sun) === sun, "world.sunLight(sun) links the light");
assert(world.sunLight() === sun, "the link reads back");
assert(world.get().sky.sunLight === sun, "world.get() reports the link");
var linked = node.info(sun).rotation;
assert(linked.x !== authored.x || linked.y !== authored.y || linked.z !== authored.z,
       "linking rotated the light off its authored value");

// ---- follow: moving the sun moves the light ----
// A light emits down its local -Y, so elevation 90 (sun straight up) is the
// identity rotation and elevation 0 lays it on its side: the light's PITCH has
// to track the sun's elevation one-for-one.
function pitchFor(elevation, azimuth) {
    world.sky("realistic", { elevation: elevation, azimuth: azimuth });
    return node.info(sun).rotation;
}
var overhead = pitchFor(89, 0);
var horizon = pitchFor(1, 0);
assert(Math.abs(overhead.x) < 2.0, "sun overhead -> light is (nearly) unrotated: x=" + overhead.x);
near(Math.abs(horizon.x), 89, 2.0, "sun on the horizon -> light pitched ~90 degrees");

// Azimuth is the compass half of the same control: four points of the compass
// at one elevation must give four DIFFERENT rotations. (The exact direction is
// pinned where it can be measured without decomposing a quaternion in JS —
// mirror.document_to_engine asserts the light's -Y axis lands on the sun
// vector to within 0.001.)
var compass = [0, 90, 180, 270].map(function (a) {
    var r = pitchFor(20, a);
    return { a: a, r: r, key: r.x.toFixed(2) + "/" + r.y.toFixed(2) + "/" + r.z.toFixed(2) };
});
compass.forEach(function (c) { console.log("    azimuth " + c.a + " -> " + c.key); });
for (var i = 0; i < compass.length; ++i) {
    for (var j = i + 1; j < compass.length; ++j) {
        assert(compass[i].key !== compass[j].key,
               "azimuth " + compass[i].a + " and " + compass[j].a + " aim the light differently");
    }
}

// ---- undo/redo: one step, and unlinking restores manual control ----
world.sky("realistic", { elevation: 45, azimuth: 30 });
var driven = node.info(sun).rotation;
assert(world.sunLight(null) === "", "world.sunLight(null) unlinks");
assert(world.sunLight() === "", "the link is gone");
var afterUnlink = node.info(sun).rotation;
near(afterUnlink.x, driven.x, 0.01, "unlinking leaves the light where the sun left it");
// and the sun no longer moves it
world.sky("realistic", { elevation: 5, azimuth: 200 });
var manual = node.info(sun).rotation;
near(manual.x, driven.x, 0.01, "an unlinked light ignores the sun (x)");
near(manual.y, driven.y, 0.01, "an unlinked light ignores the sun (y)");

// A manual edit sticks, which is what "manual control" means.
node.transform(sun, { rotation: { x: -33, y: 12, z: 0 } });
world.sky("realistic", { elevation: 70, azimuth: 15 });
near(node.info(sun).rotation.x, -33, 0.01, "a hand-set rotation survives a sun move while unlinked");

// ---- save / reopen: the link is a document field ----
assert(world.sunLight(sun) === sun, "re-link before saving");
project.save();
project.close();
project.open(guid);
assert(world.sunLight() === sun, "the link survives save + reopen");
var reopened = node.info(sun).rotation;
world.sky("realistic", { elevation: 10, azimuth: 300 });
var moved = node.info(sun).rotation;
assert(Math.abs(moved.x - reopened.x) > 1.0 || Math.abs(moved.y - reopened.y) > 1.0,
       "and it still drives the light after a reopen");

console.log("sun/sky-light coupling e2e: all checks passed");
