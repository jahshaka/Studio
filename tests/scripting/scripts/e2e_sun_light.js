// scripting.e2e.sun_light — THE SUN (SPECS/SUN_AND_LIGHT_DEFAULTS_SPEC.md,
// owner decisions Q1/Q1d/Q1e) and the sky it drives (SKY_LIGHT_SPEC.md §3, D15).
//
// THE RULE this suite pins, end to end through the verbs the panels call:
//   * the FIRST directional light in a scene IS the sun; further ones are
//     secondary and are ordered by Forward Shading Priority, which is assigned
//     automatically (0, then 1, then 2 ...) and can be changed by hand;
//   * a scene needs NO sun at all — two of the eight shipped samples are lit by
//     lamps alone — so "no directional light" answers cleanly everywhere and
//     never warns;
//   * only the sun casts a shadow among directionals (one directional slot);
//   * THE SUN DRIVES THE SKY, and never the other way round. The realistic
//     sky's sun position, its haze and the sun disc all come from this light's
//     rotation; the sky's own azimuth/elevation dials and the `drivesSun`
//     switch that let the sky push the light around are DELETED, and the verb
//     refuses them by name.
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
assert(typeof startSun.direction === "object", "...and reports the direction it travels");
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

// ---- THE SUN DRIVES THE SKY (D15), and the old dials are REFUSED ----------
world.sky("realistic", { density: 0.5 });
throws(function () { world.sky("realistic", { azimuth: 90 }); },
       "world.sky refuses 'azimuth' by name");
throws(function () { world.sky("realistic", { elevation: 60 }); },
       "world.sky refuses 'elevation' by name");
throws(function () { world.sky("realistic", { drivesSun: true }); },
       "world.sky refuses 'drivesSun' by name");
throws(function () { world.sky("realistic", { sunPosY: 450000 }); },
       "world.sky refuses the raw sun position too");
assert(world.get().sky.drivesSun === undefined, "world.get() no longer reports a steering");
assert(world.sun().skyDriven === undefined, "...nor does world.sun()");

// ROTATING THE LIGHT MOVES THE SKY. The sky is a CPU bake keyed on the sun's
// direction, so the proof is in pixels: with the sun ahead of the camera the
// picture is brighter than with the sun behind it, and nothing but the light's
// rotation changed between the two shots.
world.sunDisc({ visible: false });          // measure the SKY, not the disc on it
// LOOK AT THE SKY, and at a band well above the horizon: the default camera
// frames the ground, and the ground is lit by the very light being rotated —
// which would make this a statement about shading rather than about the bake.
editor.setCamera({ position: { x: 0, y: 2, z: 0 }, lookAt: { x: 0, y: 14, z: -6 } });
function skyBand(tag, rx, ry, rz) {
    node.transform(sun, { rotation: { x: rx, y: ry, z: rz } });
    // The realistic bake is DEBOUNCED at 150 ms, so a frame count alone can
    // measure the PREVIOUS sun twice. Two long runs with a screenshot between
    // them (which costs real time) clear it.
    editor.frame(60, 1 / 60);
    editor.screenshot("sun_light_settle_" + tag + ".png", 64, 64);
    editor.frame(60, 1 / 60);
    var s = editor.screenshot("sun_light_" + tag + ".png", 128, 128,
                              [[0.5, 0.2], [0.2, 0.3], [0.8, 0.3]], "plain");
    var sum = 0;
    for (var i = 0; i < s.probes.length; i++) sum += s.probes[i].r + s.probes[i].g + s.probes[i].b;
    console.log("sky band [" + tag + "] " + Math.round(sum));
    return sum;
}
var aheadLum = skyBand("ahead", -20, 0, 0);     // the sun low, in front of the camera
var behindLum = skyBand("behind", -20, 180, 0); // ...and turned right around
console.log("sky with the sun ahead " + Math.round(aheadLum) +
            " vs behind " + Math.round(behindLum));
assert(Math.abs(aheadLum - behindLum) > 8,
       "rotating the SUN LIGHT re-bakes the realistic sky (D15)");

// A HAND-SET ROTATION IS NEVER OVERWRITTEN. The old coupling rewrote it from
// the sky every frame; nothing does now.
node.transform(sun, { rotation: { x: -33, y: 12, z: 0 } });
world.sky("realistic", { density: 0.9 });
editor.frame(10, 1 / 60);
near(node.info(sun).rotation.x, -33, 0.01, "a sky edit never moves the sun light");
world.sunDisc({ visible: true });

// ---- THE SUN DISC is a WORLD setting (owner pick 4 / ledger 193a) ----------
var disc = world.sunDisc();
assert(disc.visible === true, "the sun disc is on by default");
assert(disc.inProbes === false, "...and out of the reflection probes by default");
assert(world.sunDisc({ visible: false }).visible === false, "world.sunDisc turns it off");
assert(world.sunDisc({ inProbes: true }).inProbes === true, "...and can put it in the probes");
world.sunDisc({ visible: true, inProbes: false });
assert(node.property(sun, "sunAngle") > 0, "the sun's ANGULAR SIZE is a row on the light");
assert(node.setProperty(sun, "sunAngle", 2.5) === true, "...and it is settable");
near(node.property(sun, "sunAngle"), 2.5, 0.01, "...and the set stuck");
node.setProperty(sun, "sunAngle", 0.53);

// ---- THE SKY LIGHT (D14): ambient is a light --------------------------------
assert(names.indexOf("skyLight") >= 0, "world.skyLight is registered");
assert(names.indexOf("sunDisc") >= 0, "world.sunDisc is registered");
assert(names.indexOf("ambient") < 0, "world.ambient is GONE");
assert(names.indexOf("ambientFromSky") < 0, "world.ambientFromSky is GONE");
var sky = world.skyLight();
assert(sky.light !== "", "a new scene ships with a Sky Light");
assert(sky.reason === "first", "...and it is THE skylight");
near(sky.intensity, 1.0, 0.001, "...at intensity 1.0");
assert(sky.count === 1, "...and it is the only one");

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
// A REALISTIC SKY WITH NO SUN is legal: the model bakes its own night and
// nothing warns (the analytic sky has no sun-less daylight).
world.sky("realistic", { density: 0.4 });
assert(world.sun().light === "", "a realistic sky in a sunless scene still has no sun");
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

// ---- API-FIRST: the row a model has to be able to FIND -------------------
// node.property/node.setProperty have carried forwardShadingPriority since it
// landed, but node.properties(id) never LISTED it — so the one row that decides
// which directional is the sun was unreachable to anything discovering the
// surface rather than being told about it (MIRROR_SCALE lane, 2026-09-13).
var listed = node.properties(sun).filter(function (p) {
    return p.name === "forwardShadingPriority";
});
assert(listed.length === 1, "forwardShadingPriority is LISTED by node.properties()");
assert(listed[0].value === node.property(sun, "forwardShadingPriority"),
       "...and the listed value is the one node.property() reports");
assert(node.setProperty(sun, "forwardShadingPriority", 2) === true,
       "...and it is settable through the same reflected name");
assert(node.property(sun, "forwardShadingPriority") === 2, "...and the set stuck");
assert(node.setProperty(sun, "forwardShadingPriority", -7) === true,
       "a negative slot is accepted by the verb");
assert(node.property(sun, "forwardShadingPriority") === 0, "...and CLAMPED to 0, not stored");
assert(node.setProperty(sun, "forwardShadingPriority", 0) === true, "back to the sun slot");

console.log("the sun + sky steering e2e: all checks passed");
