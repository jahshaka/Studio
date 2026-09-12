// scripting.e2e.sun_light — THE SUN (SPECS/SUN_AND_LIGHT_DEFAULTS_SPEC.md,
// owner decisions Q1/Q1d/Q1e) and the sky's steering of it.
//
// THE RULE this suite pins, end to end through the verbs the panels call:
//   * the FIRST directional light in a scene IS the sun; further ones are
//     secondary and are ordered by Forward Shading Priority, which is assigned
//     automatically (0, then 1, then 2 ...) and can be changed by hand;
//   * a scene needs NO sun at all — two of the eight shipped samples are lit by
//     lamps alone — so "no directional light" answers cleanly everywhere and
//     never warns;
//   * only the sun casts a shadow among directionals (one directional slot);
//   * the SKY'S STEERING is a separate switch from the sun itself. It used to
//     be the same field, so choosing which light was the sun and letting the
//     sky aim it could not be told apart — world.sunLight is the first
//     question, world.sky({drivesSun}) the second.
//
// Shadows-on-by-default is asserted here too, at the level only an end-to-end
// run can reach: a light born through the verb casts, and it still casts after
// a save and a reopen.

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

// ---- registry: the verbs exist and are documented ----
var world_ = api.verbs().filter(function (m) { return m.module === "world"; })[0];
var names = world_.verbs.map(function (v) { return v.name; });
assert(names.indexOf("sunLight") >= 0, "world.sunLight is registered");
assert(names.indexOf("sun") >= 0, "world.sun is registered");
assert(names.indexOf("setSunLight") >= 0, "world.setSunLight alias is registered");

var guid = project.create("Sun " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- a fresh scene HAS a sun: the default scene's directional light --------
var startSun = world.sun();
assert(startSun.light !== "", "a new scene's directional light IS its sun");
assert(startSun.reason === "priority", "...chosen by priority, not pinned");
assert(startSun.priority === 0, "...at priority 0");
assert(startSun.castsShadows, "...and it casts shadows (the default)");
assert(startSun.secondaries.length === 0, "...with no secondary directionals");
assert(!startSun.skyDriven, "...and the sky is not steering it");
assert(startSun.nextPriority === 1, "the next directional added would take priority 1");

// SHADOWS ON BY DEFAULT, at the top level: every light in a brand-new scene
// casts. This is the owner decision, and the default scene's point light is
// exactly where it used to be contradicted.
scene.nodes().forEach(function (n) {
    var info = node.info(n.id);
    if (info.type !== "light") return;
    assert(node.property(n.id, "shadowMapType") !== 0,
           "the new scene's light '" + info.name + "' casts shadows out of the box");
});

// ---- fixtures -------------------------------------------------------------
var sun = scene.addLight("directional", { position: { x: 0, y: 6, z: 0 }, name: "Sun" });
var lamp = scene.addLight("point", { position: { x: 3, y: 2, z: 0 }, name: "Lamp" });
var cube = scene.addPrimitive("cube", { position: { x: 0, y: 0, z: 0 } });
assert(sun.length > 10 && lamp.length > 10 && cube.length > 10, "fixtures created");

// A LIGHT ADDED THROUGH THE VERB CASTS. (ShadowMap's constructor has said so
// for years; the editor's add path used to re-state it and the file reader used
// to disagree with it.)
assert(node.property(lamp, "shadowMapType") !== 0, "a point light added by verb casts shadows");
assert(node.property(sun, "shadowMapType") !== 0, "a directional light added by verb casts shadows");

// ---- the auto-slot --------------------------------------------------------
// The scene already had one directional (the default scene's), so the one just
// added is the SECOND and must have slotted into 1 by itself.
assert(node.property(sun, "forwardShadingPriority") === 1,
       "a second directional light auto-slots to priority 1");
var st = world.sun();
assert(st.light === startSun.light, "...and the sun does not move when one is added");
assert(st.secondaries.length === 1 && st.secondaries[0].light === sun,
       "...the new one is reported as a secondary");
assert(st.secondaries[0].priority === 1, "...at priority 1");

// world.shadowStatus reports the same two facts, for the panel.
var shadows = world.shadowStatus();
assert(shadows.sun === startSun.light, "world.shadowStatus().sun names the sun");
assert(shadows.secondaryDirectionals.length === 1 &&
       shadows.secondaryDirectionals[0] === sun,
       "world.shadowStatus().secondaryDirectionals names the other one");

// ---- the author changes the row ------------------------------------------
node.setProperty(sun, "forwardShadingPriority", 0);
node.setProperty(startSun.light, "forwardShadingPriority", 5);
var swapped = world.sun();
assert(swapped.light === sun, "lowering a light's priority makes IT the sun");
assert(swapped.secondaries.length === 1 && swapped.secondaries[0].light === startSun.light,
       "...and the old sun becomes the secondary");

// ---- reading and pinning --------------------------------------------------
assert(world.sunLight() === sun, "world.sunLight() reads the RESOLVED sun");
assert(world.sunLight(startSun.light) === startSun.light, "world.sunLight(id) pins a sun");
assert(world.sun().reason === "pinned", "...and the reason says so");
assert(world.sunLight() === startSun.light, "...the pin wins over the priority order");
assert(world.sunLight("auto") === sun, "world.sunLight('auto') drops the pin");
assert(world.sun().reason === "priority", "...back to the priority order");

// ---- refusals -------------------------------------------------------------
throws(function () { world.sunLight("not-a-node"); }, "sunLight rejects an unknown id");
throws(function () { world.sunLight(cube); }, "sunLight rejects a non-light node");
throws(function () { world.sunLight(lamp); }, "sunLight rejects a POINT light (only a directional can be the sun)");
assert(world.sunLight() === sun, "a refused pin changes nothing");

// ---- the sky's steering is a SEPARATE switch ------------------------------
world.sky("realistic", { azimuth: 90, elevation: 60, turbidity: 4 });
assert(world.get().sky.drivesSun === false, "the sky steers nothing by default");
var authored = node.info(sun).rotation;

world.sky("realistic", { drivesSun: true });
assert(world.get().sky.drivesSun === true, "world.sky({drivesSun:true}) turns the steering on");
assert(world.sun().skyDriven === true, "...and world.sun() reports it");
var steered = node.info(sun).rotation;
assert(steered.x !== authored.x || steered.y !== authored.y || steered.z !== authored.z,
       "turning the steering on aimed the sun");

// ---- follow: moving the sky's sun moves the SUN LIGHT ---------------------
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

var compass = [0, 90, 180, 270].map(function (a) {
    var r = pitchFor(20, a);
    return { a: a, r: r, key: r.x.toFixed(2) + "/" + r.y.toFixed(2) + "/" + r.z.toFixed(2) };
});
for (var i = 0; i < compass.length; ++i) {
    for (var j = i + 1; j < compass.length; ++j) {
        assert(compass[i].key !== compass[j].key,
               "azimuth " + compass[i].a + " and " + compass[j].a + " aim the light differently");
    }
}

// THE STEERING FOLLOWS THE SUN, not a remembered light: pinning a different sun
// hands the dials to the new one. (This is the thing one field could not do.)
world.sky("realistic", { elevation: 45, azimuth: 30 });
var otherBefore = node.info(startSun.light).rotation;
world.sunLight(startSun.light);
world.sky("realistic", { elevation: 20, azimuth: 200 });
var otherAfter = node.info(startSun.light).rotation;
assert(Math.abs(otherAfter.x - otherBefore.x) > 1.0 || Math.abs(otherAfter.y - otherBefore.y) > 1.0,
       "pinning a new sun hands it the sky's dials");
world.sunLight("auto");

// ---- turning the steering off gives manual control back -------------------
world.sky("realistic", { elevation: 45, azimuth: 30 });
var driven = node.info(sun).rotation;
world.sky("realistic", { drivesSun: false });
assert(world.get().sky.drivesSun === false, "the steering is off again");
var afterOff = node.info(sun).rotation;
near(afterOff.x, driven.x, 0.01, "the light stays where the sun left it");
world.sky("realistic", { elevation: 5, azimuth: 200 });
near(node.info(sun).rotation.x, driven.x, 0.01, "an unsteered light ignores the sky (x)");
node.transform(sun, { rotation: { x: -33, y: 12, z: 0 } });
world.sky("realistic", { elevation: 70, azimuth: 15 });
near(node.info(sun).rotation.x, -33, 0.01, "a hand-set rotation survives a sun move");

// Save before leaving: the reopen at the end asserts what this scene stored.
project.save();

// ---- A SCENE WITH NO DIRECTIONAL LIGHT IS NORMAL --------------------------
// Owner Q1c: an interior lit by lamps is an ordinary scene. Everything must
// answer cleanly, and NOTHING may warn.
var lampsOnly = project.create("Lamps only " + Date.now());
scene.nodes().forEach(function (n) {
    if (node.info(n.id).type === "light" && node.property(n.id, "lightType") === 1)
        node.remove(n.id);
});
var none = world.sun();
assert(none.light === "", "a scene with no directional light has no sun");
assert(none.reason === "none", "...and says 'none'");
assert(none.priority === -1, "...with no priority");
assert(none.secondaries.length === 0, "...and no secondaries");
assert(none.nextPriority === 0, "...and the next directional would take 0");
assert(world.sunLight() === "", "world.sunLight() answers empty, not an error");
assert(world.shadowStatus().sun === "", "world.shadowStatus().sun is empty too");
// The sky's steering refuses rather than pretending.
world.sky("realistic", { drivesSun: true });
assert(world.sun().skyDriven === true || world.sun().light === "",
       "asking the sky to steer a scene with no sun does not break anything");
// And it is NOT an issue: nothing to fix here.
var checked = editor.checkScene();
var sunIssues = checked.list.filter(function (i) { return i.kind === "sun.tie"; });
assert(sunIssues.length === 0, "a scene with no directional light raises NO issue");

// ---- save / reopen: the priority and the steering are document fields -----
project.open(guid);
var reopened = world.sun();
assert(reopened.light !== "", "the scene still has a sun after a reopen");
assert(node.property(sun, "forwardShadingPriority") === 0,
       "the Forward Shading Priority survived the save and reopen");
scene.nodes().forEach(function (n) {
    var info = node.info(n.id);
    if (info.type !== "light") return;
    assert(node.property(n.id, "shadowMapType") !== 0,
           "'" + info.name + "' still casts shadows after a reopen (no key, no surprise)");
});

console.log("the sun + sky steering e2e: all checks passed");
