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

// ROTATING THE LIGHT MOVES THE SKY. The sky is the engine's analytic model now
// (SKY-GPU), keyed on the sun direction we push it, so the proof is in pixels:
// with the sun ahead of the camera the picture differs from the same picture
// with the sun behind it, and nothing but the light's rotation changed.
//
// A LOW SUN, and that is a re-baselining this lane owes an explanation: the sky
// is only ASYMMETRIC in azimuth near the horizon. High up, both models are
// close to a dome that looks the same whichever way you turn — the old bake's
// forward-scattering lobe carried the difference at the 20-degree sun this case
// used to use, and the engine's does not. 70 degrees puts the sun low, where a
// sky has a bright side and a dark one.
world.sunDisc({ visible: false });          // measure the SKY, not the disc on it
// LOOK AT THE SKY, and at a band well above the horizon: the default camera
// frames the ground, and the ground is lit by the very light being rotated —
// which would make this a statement about shading rather than about the bake.
editor.setCamera({ position: { x: 0, y: 2, z: 0 }, lookAt: { x: 0, y: 14, z: -6 } });
function skyBand(tag, rx, ry, rz) {
    node.transform(sun, { rotation: { x: rx, y: ry, z: rz } });
    // NO DEBOUNCE ANY MORE: the sky is a shader, so the sun's direction is a
    // const-buffer write and the next frame already shows it. (This used to
    // spend two 60-frame runs and a throwaway screenshot clearing a 150 ms
    // bake debounce.)
    editor.frame(4, 1 / 60);
    var s = editor.screenshot("sun_light_" + tag + ".png", 128, 128,
                              [[0.5, 0.2], [0.2, 0.3], [0.8, 0.3]], "plain");
    var sum = 0;
    for (var i = 0; i < s.probes.length; i++) sum += s.probes[i].r + s.probes[i].g + s.probes[i].b;
    console.log("sky band [" + tag + "] " + Math.round(sum));
    return sum;
}
var aheadLum = skyBand("ahead", -70, 0, 0);     // the sun low, in front of the camera
var behindLum = skyBand("behind", -70, 180, 0); // ...and turned right around
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

// ---- FOLLOWS ATMOSPHERE (SUN_FOLLOWS_ATMOSPHERE, lane ENGINE-7 item 6) -----
//
// With the Realistic sky the sun's DIRECT light is tinted by what the air does
// to it at the sun's own elevation: the colour the user picked is the NOON
// colour, and a low sun arrives reddened and dimmed — Unreal's Sun Sky does the
// same. Off is the old behaviour (the picked colour at every elevation), and on
// any other sky the toggle is inert, because a picture of a sky knows nothing
// about air.
//
// MEASURED ON A LIT SURFACE, never on the sky: the sky is drawn by the same
// model either way, so only a surface can show what reached the ground. The sky
// is turned right down (power 0.02) so that what the probe reads is the SUN and
// not the sunset's own glow reflected off the surface.
{
    var atmoGuid = project.create("Sun Atmosphere " + Date.now());
    assert(atmoGuid.length > 10, "a scene for the atmosphere tint");
    var asun = world.sun().light;
    assert(asun !== "", "...with a sun");
    assert(node.property(asun, "followsAtmosphere") === true,
           "Follows Atmosphere is ON by default (the owner's decision)");
    world.skyLight({ intensity: 0 });           // the sun alone lights the wall
    world.sky("realistic", { power: 0.02 });
    world.sunDisc({ visible: false });
    var wall = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: 0 } });
    editor.select("");                          // no gizmo over the probes
    editor.frame(60, 1 / 60);                   // the primitive's mesh arrives
    editor.setCamera({ position: { x: 0, y: 1, z: 4 }, lookAt: { x: 0, y: 1, z: 0 } });
    node.setProperty(asun, "intensity", 0.6);

    function litFace(tag) {
        editor.frame(8, 1 / 60);
        var s = editor.screenshot("sun_atmo_" + tag + ".png", 320, 240,
                                  [{ x: 0.45, y: 0.5 }, { x: 0.55, y: 0.5 },
                                   { x: 0.5, y: 0.45 }], "plain");
        var r = 0, g = 0, b = 0;
        for (var i = 0; i < s.probes.length; i++) {
            r += s.probes[i].r; g += s.probes[i].g; b += s.probes[i].b;
        }
        r /= 3; g /= 3; b /= 3;
        console.log("lit face [" + tag + "] r=" + Math.round(r) + " g=" + Math.round(g) +
                    " b=" + Math.round(b) + "  r/b " + (r / Math.max(1, b)).toFixed(2));
        return { r: r, g: g, b: b, rb: r / Math.max(1, b) };
    }

    // A SUNSET: the sun about 5 degrees above the horizon, behind the camera.
    node.transform(asun, { rotation: { x: -85, y: 180, z: 0 } });
    var sunsetOn = litFace("sunset_on");
    assert(node.setProperty(asun, "followsAtmosphere", false) === true,
           "the toggle is settable through the property surface");
    assert(node.property(asun, "followsAtmosphere") === false, "...and it stuck");
    var sunsetOff = litFace("sunset_off");
    assert(sunsetOn.rb > 1.3,
           "a 5-degree sun REDDENS what it lights when it follows the atmosphere (r/b " +
           sunsetOn.rb.toFixed(2) + ")");
    assert(sunsetOff.rb < 1.3,
           "...and does not when it does not (r/b " + sunsetOff.rb.toFixed(2) + ")");
    assert(sunsetOn.b < sunsetOff.b * 0.8 && sunsetOn.g < sunsetOff.g,
           "...and it DIMS it, blue first (b " + Math.round(sunsetOn.b) + " vs " +
           Math.round(sunsetOff.b) + ")");

    // NOON IS THE COLOUR THE USER PICKED: straight down, the tint is 1,1,1, so
    // the two states are the same picture.
    node.transform(asun, { rotation: { x: 0, y: 0, z: 0 } });
    editor.setCamera({ position: { x: 0, y: 5, z: 0.02 }, lookAt: { x: 0, y: 1, z: 0 } });
    var noonOff = litFace("noon_off");
    assert(node.setProperty(asun, "followsAtmosphere", true) === true, "following again");
    var noonOn = litFace("noon_on");
    assert(Math.abs(noonOn.r - noonOff.r) <= 2 && Math.abs(noonOn.g - noonOff.g) <= 2 &&
           Math.abs(noonOn.b - noonOff.b) <= 2,
           "at NOON the two states are the same picture: the picked colour IS the noon colour");

    // INERT ON ANY OTHER SKY: a colour sky has no atmosphere to ask.
    world.sky("color", { color: "#404060" });
    node.transform(asun, { rotation: { x: -85, y: 180, z: 0 } });
    editor.setCamera({ position: { x: 0, y: 1, z: 4 }, lookAt: { x: 0, y: 1, z: 0 } });
    // A NEW SKY MOVES THE EXPOSURE, and the eye adapts over frames rather than
    // instantly (the lesson cameras.exposure is named for: count frames, and
    // read only once the picture holds still). The COLOUR is what this case is
    // about, so it is the colour that is compared — the tint is a hue, and an
    // inert tint cannot move one.
    editor.frame(90, 1 / 60);
    var plainOn = litFace("colorsky_on");
    node.setProperty(asun, "followsAtmosphere", false);
    editor.frame(30, 1 / 60);
    var plainOff = litFace("colorsky_off");
    assert(Math.abs(plainOn.rb - plainOff.rb) < 0.05,
           "on a COLOUR sky the toggle tints nothing — it is inert (r/b " +
           plainOn.rb.toFixed(2) + " vs " + plainOff.rb.toFixed(2) + ")");
    assert(Math.abs(plainOn.r - plainOn.g) < 4 && Math.abs(plainOff.r - plainOff.g) < 4,
           "...and neither picture is red-shifted at all");

    // IT SURVIVES A SAVE AND A REOPEN, and the default is written by ABSENCE:
    // a document that never heard of this row reads TRUE.
    node.setProperty(asun, "followsAtmosphere", false);
    project.save();
    project.open(atmoGuid);
    assert(node.property(world.sun().light, "followsAtmosphere") === false,
           "the switch survives a save and a reopen");
    node.setProperty(world.sun().light, "followsAtmosphere", true);
    project.save();
    project.open(atmoGuid);
    assert(node.property(world.sun().light, "followsAtmosphere") === true,
           "...and so does switching it back on");
}

console.log("the sun + sky steering e2e: all checks passed");
